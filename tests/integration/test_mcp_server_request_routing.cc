/**
 * Integration test: responses are routed by the request's own dispatch
 * context, not by ambient most-recently-accepted-connection state.
 *
 * Historical failure modes pinned here (all real with the ambient
 * current-connection scheme, all fixed by the per-message dispatch
 * context):
 *
 *   1. Cross-wired responses. The ambient pointer was stamped at accept
 *      time, so with two live connections (accept A, accept B) a request
 *      arriving on A was answered on B: client B received client A's
 *      response and A hung. The dispatch context pins the reply to the
 *      connection the request physically arrived on.
 *
 *   2. Silent drop after an unrelated close. Closing B nulled the ambient
 *      pointer even though A was alive and mid-flight, so A's next
 *      response was skipped without a log line. With the context, A's
 *      reply path is A's own connection regardless of what B does.
 *
 * The tests drive a real McpServer over real TCP sockets with plain HTTP
 * POSTs (no SSE handshake), because the plain-HTTP path is exactly the one
 * that used to depend on the ambient pointer for both session keying and
 * the response write.
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/network/address.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/socket_interface.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

// Bind ephemeral 0 to get a port the kernel thinks is free, then let go
// of it and hand the number to the server. Same mild TOCTOU as the other
// integration tests -- accepted on a loopback test bed.
uint16_t pickEphemeralPort() {
  auto& iface = network::socketInterface();

  auto fd_result =
      iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                   network::Address::IpVersion::v4);
  if (!fd_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: socket() failed");
  }

  auto handle = iface.ioHandleForFd(*fd_result, /*socket_v6only=*/false);
  handle->setBlocking(false);

  auto bind_addr = network::Address::parseInternetAddress("127.0.0.1", 0);
  auto bind_result = handle->bind(bind_addr);
  if (!bind_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: bind() failed");
  }

  auto local_addr_result = handle->localAddress();
  if (!local_addr_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: localAddress() failed");
  }

  const auto* ip =
      dynamic_cast<const network::Address::Ip*>(local_addr_result->get());
  if (ip == nullptr) {
    throw std::runtime_error("pickEphemeralPort: not an IP address");
  }
  uint16_t port = ip->port();
  handle->close();
  return port;
}

class McpServerRequestRoutingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = pickEphemeralPort();

    server::McpServerConfig config;
    config.server_name = "routing-test-server";
    config.server_version = "0.0.1";
    config.supported_transports = {TransportType::HttpSse};
    config.num_workers = 1;

    server_ = server::createMcpServer(config);
    ASSERT_NE(server_, nullptr);

    const std::string listen_address =
        "http://127.0.0.1:" + std::to_string(port_);
    auto listen_result = server_->listen(listen_address);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(listen_result))
        << "McpServer::listen failed";

    server_thread_ = std::thread([this]() { server_->run(); });

    ASSERT_TRUE(waitForListenerReady(port_, 5s))
        << "Server did not begin accepting on port " << port_;
  }

  void TearDown() override {
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  static bool waitForListenerReady(uint16_t port,
                                   std::chrono::milliseconds budget) {
    auto& iface = network::socketInterface();
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      auto fd_result =
          iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                       network::Address::IpVersion::v4);
      if (fd_result.ok()) {
        auto handle = iface.ioHandleForFd(*fd_result, false);
        handle->setBlocking(true);
        auto connect_result = handle->connect(addr);
        handle->close();
        if (connect_result.ok()) {
          return true;
        }
      }
      std::this_thread::sleep_for(25ms);
    }
    return false;
  }

  network::IoHandlePtr openClient() {
    auto& iface = network::socketInterface();
    auto fd_result =
        iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                     network::Address::IpVersion::v4);
    if (!fd_result.ok()) {
      return nullptr;
    }
    auto handle = iface.ioHandleForFd(*fd_result, /*socket_v6only=*/false);
    handle->setBlocking(true);
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port_);
    auto connect_result = handle->connect(addr);
    if (!connect_result.ok()) {
      handle->close();
      return nullptr;
    }
    return handle;
  }

  // POST a JSON-RPC body to the server's plain-HTTP RPC path on an
  // already-open client socket.
  static bool sendRpcPost(network::IoHandle& handle, const std::string& body) {
    std::string request =
        "POST /rpc HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "\r\n" +
        body;
    OwnedBuffer out;
    out.add(request);
    return handle.write(out).ok();
  }

  // Accumulate whatever arrives on the socket until it contains `needle`
  // or the budget elapses. Returns everything read either way.
  static std::string readUntilContains(network::IoHandle& handle,
                                       const std::string& needle,
                                       std::chrono::milliseconds budget) {
    handle.setBlocking(false);
    std::string received;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = handle.read(buf, /*max_length=*/8192);
      if (r.ok() && *r > 0) {
        received += buf.toString();
        if (received.find(needle) != std::string::npos) {
          return received;
        }
      } else if (r.ok() && *r == 0) {
        return received;  // EOF
      } else if (!r.wouldBlock()) {
        return received;  // hard error; caller's assertion reports it
      }
      std::this_thread::sleep_for(10ms);
    }
    return received;
  }

  uint16_t port_{0};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
};

// Two live connections; the request goes in on the FIRST-accepted one.
// Under the ambient scheme the reply went out on the most recently
// accepted connection (B); under the dispatch context it must come back
// on A, and B must stay silent.
TEST_F(McpServerRequestRoutingTest, ResponseReturnsOnOriginConnection) {
  auto client_a = openClient();
  ASSERT_NE(client_a, nullptr);
  auto client_b = openClient();
  ASSERT_NE(client_b, nullptr);

  // Give the dispatcher a beat to accept both, in order, so B is the
  // most recently accepted connection when A's request dispatches —
  // exactly the interleaving the ambient pointer got wrong.
  std::this_thread::sleep_for(100ms);

  ASSERT_TRUE(
      sendRpcPost(*client_a, R"({"jsonrpc":"2.0","id":41,"method":"ping"})"));

  std::string on_a = readUntilContains(*client_a, "\"id\":41", 3s);
  EXPECT_NE(on_a.find("\"id\":41"), std::string::npos)
      << "Ping response did not return on the connection that sent the "
         "request; got instead: "
      << on_a;

  // B must not have received A's response. A short read window is
  // enough: the response already proved the round trip completed.
  std::string on_b = readUntilContains(*client_b, "\"id\":41", 200ms);
  EXPECT_EQ(on_b.find("\"id\":41"), std::string::npos)
      << "Another connection received this request's response: " << on_b;

  // And the reverse direction: B's own request comes back on B.
  ASSERT_TRUE(
      sendRpcPost(*client_b, R"({"jsonrpc":"2.0","id":42,"method":"ping"})"));
  std::string on_b2 = readUntilContains(*client_b, "\"id\":42", 3s);
  EXPECT_NE(on_b2.find("\"id\":42"), std::string::npos)
      << "Second connection's response did not return to it; got: " << on_b2;

  client_a->close();
  client_b->close();
}

// Closing an unrelated connection must not affect another connection's
// request/response cycle. Under the ambient scheme, B's close nulled the
// shared pointer and A's response was silently skipped.
TEST_F(McpServerRequestRoutingTest, UnrelatedCloseDoesNotDropResponse) {
  auto client_a = openClient();
  ASSERT_NE(client_a, nullptr);
  auto client_b = openClient();
  ASSERT_NE(client_b, nullptr);

  // B is the most recently accepted connection; its close is the one the
  // ambient pointer used to track.
  std::this_thread::sleep_for(100ms);
  client_b->close();
  client_b.reset();
  std::this_thread::sleep_for(100ms);

  ASSERT_TRUE(
      sendRpcPost(*client_a, R"({"jsonrpc":"2.0","id":43,"method":"ping"})"));

  std::string on_a = readUntilContains(*client_a, "\"id\":43", 3s);
  EXPECT_NE(on_a.find("\"id\":43"), std::string::npos)
      << "Response was dropped after an unrelated connection closed; "
         "received: "
      << on_a;

  client_a->close();
}

// Several requests interleaved across two live connections, each keeping
// its own request/response pairing throughout — the steady-state version
// of the two tests above.
TEST_F(McpServerRequestRoutingTest, InterleavedRequestsKeepTheirConnections) {
  auto client_a = openClient();
  ASSERT_NE(client_a, nullptr);
  auto client_b = openClient();
  ASSERT_NE(client_b, nullptr);
  std::this_thread::sleep_for(100ms);

  for (int i = 0; i < 3; ++i) {
    const int id_a = 100 + i;
    const int id_b = 200 + i;

    ASSERT_TRUE(sendRpcPost(*client_a, R"({"jsonrpc":"2.0","id":)" +
                                           std::to_string(id_a) +
                                           R"(,"method":"ping"})"));
    ASSERT_TRUE(sendRpcPost(*client_b, R"({"jsonrpc":"2.0","id":)" +
                                           std::to_string(id_b) +
                                           R"(,"method":"ping"})"));

    std::string needle_a = "\"id\":" + std::to_string(id_a);
    std::string needle_b = "\"id\":" + std::to_string(id_b);

    std::string on_a = readUntilContains(*client_a, needle_a, 3s);
    EXPECT_NE(on_a.find(needle_a), std::string::npos)
        << "round " << i << ": A's response missing on A; got: " << on_a;
    EXPECT_EQ(on_a.find(needle_b), std::string::npos)
        << "round " << i << ": B's response leaked onto A: " << on_a;

    std::string on_b = readUntilContains(*client_b, needle_b, 3s);
    EXPECT_NE(on_b.find(needle_b), std::string::npos)
        << "round " << i << ": B's response missing on B; got: " << on_b;
    EXPECT_EQ(on_b.find(needle_a), std::string::npos)
        << "round " << i << ": A's response leaked onto B: " << on_b;
  }

  client_a->close();
  client_b->close();
}

}  // namespace
}  // namespace mcp
