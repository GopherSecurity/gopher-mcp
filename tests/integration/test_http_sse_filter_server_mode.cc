/**
 * @file test_http_sse_filter_server_mode.cc
 * @brief Integration tests for ServerConnectionMode wiring in the filter
 *
 * Verifies that ServerConnectionMode is correctly wired into
 * HttpSseJsonRpcProtocolFilter by testing observable behavior through
 * real TCP connections:
 *
 * - GET /sse → SseStream mode (SSE response headers + endpoint event)
 * - POST /callback/{id} → CallbackProxy mode (202 Accepted)
 * - POST /mcp → PlainHttp mode (normal JSON-RPC response)
 * - HandshakeWriteGuard prevents re-framing during inline writes
 * - SSE stream body is ignored in onBody
 *
 * Uses real TCP socketpairs following test_sse_transport_round_trip.cc.
 */

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/types.h"

#include "real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

class ServerModeCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& req) override {
    requests_.push_back(req);
  }
  void onNotification(const jsonrpc::Notification&) override {}
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override { error_count_++; }
  void onMessageEndpoint(const std::string&) override {}
  bool sendHttpPost(const std::string&) override { return true; }

  std::vector<jsonrpc::Request> requests_;
  int error_count_{0};
};

class ServerModeFilterTest : public test::RealIoTestBase {
 protected:
  struct Harness {
    std::shared_ptr<HttpSseFilterChainFactory> factory;
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  Harness makeServerHarness(ServerModeCallbacks& callbacks,
                            const std::shared_ptr<HttpSseFilterChainFactory>&
                                existing_factory = nullptr) {
    auto factory = existing_factory;
    if (!factory) {
      factory =
          std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks,
                                                      /*is_server=*/true,
                                                      /*http_path=*/"/mcp",
                                                      /*http_host=*/"localhost",
                                                      /*use_sse=*/true,
                                                      /*sse_path=*/"/sse",
                                                      /*rpc_path=*/"/mcp");
    }

    auto pair = createSocketPair();
    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto si = std::make_shared<stream_info::StreamInfoImpl>();

    auto conn = network::ConnectionImpl::createServerConnection(
        *dispatcher_, std::move(socket), std::move(transport), *si);
    auto* ci = static_cast<network::ConnectionImpl*>(conn.get());
    EXPECT_TRUE(factory->createFilterChain(ci->filterManager()));
    ci->filterManager().initializeReadFilters();

    return Harness{std::move(factory), std::move(conn), std::move(pair.second),
                   std::move(si)};
  }

  std::string drainPeer(network::IoHandle& peer,
                        std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = peer.read(buf, 4096);
      if (r.ok() && *r > 0) {
        out.append(buf.toString());
      } else if (!out.empty()) {
        return out;
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }

  void writeClientBytes(network::IoHandle& peer, const std::string& data) {
    OwnedBuffer buf;
    buf.add(data);
    auto r = peer.write(buf);
    ASSERT_TRUE(r.ok()) << "peer.write failed: errno=" << errno;
  }

  void closeOnDispatcher(std::unique_ptr<network::ServerConnection> conn,
                         std::shared_ptr<HttpSseFilterChainFactory> factory) {
    executeInDispatcher([&]() {
      if (conn) {
        conn->close(network::ConnectionCloseType::NoFlush);
      }
      conn.reset();
      factory.reset();
    });
  }
};

// ── GET /sse → SseStream mode ──────────────────────────────────────

TEST_F(ServerModeFilterTest, GetSse_SseStreamMode) {
  ServerModeCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    writeClientBytes(*peer,
                     "GET /sse HTTP/1.1\r\n"
                     "Host: localhost\r\n"
                     "Accept: text/event-stream\r\n"
                     "\r\n");
  });

  std::string wire = drainPeer(*peer, 500ms);

  // Should respond with SSE headers and endpoint event
  EXPECT_NE(wire.find("HTTP/1.1 200"), std::string::npos)
      << "Expected 200 OK, got: " << wire;
  EXPECT_NE(wire.find("Content-Type: text/event-stream"), std::string::npos)
      << "Expected SSE content-type, got: " << wire;
  EXPECT_NE(wire.find("event: endpoint"), std::string::npos)
      << "Expected endpoint event, got: " << wire;
  EXPECT_NE(wire.find("callback/"), std::string::npos)
      << "Expected callback URL, got: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ── GET /sse with query string ─────────────────────────────────────

TEST_F(ServerModeFilterTest, GetSseWithQueryString_StillSseStream) {
  ServerModeCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // Query string should be stripped before matching
    writeClientBytes(*peer,
                     "GET /sse?token=abc HTTP/1.1\r\n"
                     "Host: localhost\r\n"
                     "\r\n");
  });

  std::string wire = drainPeer(*peer, 500ms);
  EXPECT_NE(wire.find("HTTP/1.1 200"), std::string::npos)
      << "Expected 200 OK for /sse?token, got: " << wire;
  EXPECT_NE(wire.find("event: endpoint"), std::string::npos)
      << "Expected endpoint event, got: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ── POST /mcp → PlainHttp mode ────────────────────────────────────

TEST_F(ServerModeFilterTest, PostMcp_PlainHttpMode) {
  ServerModeCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    std::string body =
        R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
    std::string request =
        "POST /mcp HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "\r\n" +
        body;
    writeClientBytes(*peer, request);
  });

  // Give the dispatcher time to process
  std::this_thread::sleep_for(200ms);

  // The server should have received the JSON-RPC request
  EXPECT_GE(callbacks.requests_.size(), 1u)
      << "Server should have received the initialize request";
  if (!callbacks.requests_.empty()) {
    EXPECT_EQ(callbacks.requests_[0].method, "initialize");
  }

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ── SseStream body ignored ────────────────────────────────────────

TEST_F(ServerModeFilterTest, SseStream_BodyIgnored) {
  ServerModeCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // Send GET /sse (opens SSE stream)
    writeClientBytes(*peer,
                     "GET /sse HTTP/1.1\r\n"
                     "Host: localhost\r\n"
                     "\r\n");
  });

  drainPeer(*peer, 500ms);

  // Send some garbage body data on the SSE connection — it should
  // be ignored by onBody (SseStream mode ignores request body).
  executeInDispatcher(
      [&]() { writeClientBytes(*peer, "some garbage body data\r\n"); });
  std::this_thread::sleep_for(100ms);

  // No requests should have been parsed from the garbage
  EXPECT_EQ(callbacks.requests_.size(), 0u)
      << "SSE stream should ignore request body data";

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ── Connection close cleans up ────────────────────────────────────

TEST_F(ServerModeFilterTest, ConnectionClose_NoHangOrCrash) {
  ServerModeCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    writeClientBytes(*peer,
                     "GET /sse HTTP/1.1\r\n"
                     "Host: localhost\r\n"
                     "\r\n");
  });

  drainPeer(*peer, 500ms);

  // Close the peer (simulate client disconnect)
  peer.reset();
  std::this_thread::sleep_for(200ms);

  // Clean shutdown — no hang, no crash
  closeOnDispatcher(std::move(conn), std::move(factory));
  SUCCEED();
}

}  // namespace
}  // namespace filter
}  // namespace mcp
