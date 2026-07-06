/**
 * Integration test: server-initiated notification delivery over HTTP+SSE.
 *
 * End-to-end acceptance for issue #240 — "client can't obtain
 * notifications from server even though it has registered a callback".
 * Everything runs over real sockets on loopback; nothing is mocked.
 *
 * The full chain under test:
 *
 *   1. McpClient connects with an /sse URL: GET opens the SSE stream, the
 *      server answers with an "endpoint" event carrying a *relative*
 *      callback URL, and the client resolves it against the server
 *      address (endpoint-resolution fix) so its POSTs have somewhere to
 *      go.
 *   2. initialize and resources/subscribe each ride a one-shot POST
 *      connection; the server keys the session on the SSE stream id
 *      announced by the transport filter (transport-session fix), so the
 *      subscription made on POST #2 lands in the same session initialize
 *      created — not in a session that died with its POST connection.
 *   3. McpServer::notifyResourceUpdate builds
 *      notifications/resources/updated and fans it out to subscribed
 *      sessions (delivery fix), routing through the SSE session registry
 *      onto the client's SSE stream (routing fix).
 *   4. The client's SSE stream parses the event into a JSON-RPC
 *      notification and dispatches it to the handler registered via
 *      registerNotificationHandler.
 *
 *   A second test covers broadcastNotification reaching an HTTP+SSE
 *   client on its own stream, including when called from a non-dispatcher
 *   application thread (the deadlock-free hop).
 */

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

// Pick a loopback port the kernel believes is free by briefly binding
// ephemeral-port 0. Same accepted TOCTOU trade-off as the other
// integration tests in this directory.
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

class ServerNotificationDeliveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = pickEphemeralPort();

    server::McpServerConfig server_config;
    server_config.server_name = "notification-delivery-test-server";
    server_config.server_version = "0.0.1";
    server_config.supported_transports = {TransportType::HttpSse};
    server_config.num_workers = 1;

    server_ = server::createMcpServer(server_config);
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
    // Client first so the server observes RemoteClose on a live
    // dispatcher; then drain the server before joining its thread.
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  // Connect-probe until the listener accepts (McpServer::listen gives no
  // readiness signal).
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

  // Bring up a connected + initialized client over the HTTP+SSE
  // transport (the /sse path selects it in negotiateTransport).
  void connectAndInitializeClient() {
    client::McpClientConfig client_config;
    client_config.client_name = "notification-delivery-test-client";
    client_config.client_version = "0.0.1";
    client_config.num_workers = 1;
    // Tight timeouts: a broken leg of the chain should fail the test,
    // not hang it.
    client_config.request_timeout = 5000ms;
    client_config.protocol_initialization_timeout = 5000ms;
    client_config.protocol_connection_timeout = 5000ms;

    client_ = client::createMcpClient(client_config);
    ASSERT_NE(client_, nullptr);

    const std::string uri =
        "http://127.0.0.1:" + std::to_string(port_) + "/sse";
    auto connect_result = client_->connect(uri);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connect_result))
        << "McpClient::connect failed against real server";

    auto init_future = client_->initializeProtocol();
    ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready)
        << "initialize never round-tripped over HTTP+SSE";
    ASSERT_NO_THROW(init_future.get());
  }

  uint16_t port_{0};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
  std::unique_ptr<client::McpClient> client_;
};

// The issue #240 scenario: subscribe to a resource, server announces an
// update, the registered client callback fires with the right URI.
TEST_F(ServerNotificationDeliveryTest, ResourceUpdateReachesSubscriber) {
  const std::string kUri = "test://resource/watched";

  // Registered before connect so no delivery window is open before the
  // handler exists. Promise is set at most once — the server sends one
  // update; a second delivery would throw and fail the test.
  std::promise<std::string> delivered;
  auto delivered_future = delivered.get_future();
  connectAndInitializeClient();
  client_->registerNotificationHandler(
      "notifications/resources/updated",
      [&delivered](const jsonrpc::Notification& notification) {
        std::string uri;
        if (notification.params.has_value()) {
          auto it = notification.params->find("uri");
          if (it != notification.params->end() &&
              holds_alternative<std::string>(it->second)) {
            uri = get<std::string>(it->second);
          }
        }
        delivered.set_value(uri);
      });

  // Subscribe rides its own one-shot POST connection; the ack coming
  // back proves the request reached the server's session.
  auto subscribe_future = client_->subscribeResource(kUri);
  ASSERT_EQ(subscribe_future.wait_for(5s), std::future_status::ready)
      << "resources/subscribe never acknowledged";
  auto subscribe_result = subscribe_future.get();
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(subscribe_result))
      << "resources/subscribe failed";

  // Application-side trigger. Called from the test thread (not the
  // server dispatcher) — delivery must hop threads internally.
  server_->notifyResourceUpdate(kUri);

  ASSERT_EQ(delivered_future.wait_for(5s), std::future_status::ready)
      << "notifications/resources/updated never reached the client handler";
  EXPECT_EQ(delivered_future.get(), kUri);
}

// broadcastNotification must reach an HTTP+SSE client through its own SSE
// stream, without requiring a resource subscription.
TEST_F(ServerNotificationDeliveryTest, BroadcastReachesHttpSseClient) {
  std::promise<std::string> delivered;
  auto delivered_future = delivered.get_future();
  connectAndInitializeClient();
  client_->registerNotificationHandler(
      "test/broadcast", [&delivered](const jsonrpc::Notification& n) {
        std::string payload;
        if (n.params.has_value()) {
          auto it = n.params->find("payload");
          if (it != n.params->end() &&
              holds_alternative<std::string>(it->second)) {
            payload = get<std::string>(it->second);
          }
        }
        delivered.set_value(payload);
      });

  Metadata params;
  params["payload"] = std::string("hello-subscribers");
  jsonrpc::Notification notification("test/broadcast", params);

  // Off-dispatcher caller: exercises the post-to-dispatcher hop.
  server_->broadcastNotification(notification);

  ASSERT_EQ(delivered_future.wait_for(5s), std::future_status::ready)
      << "broadcast notification never reached the client handler";
  EXPECT_EQ(delivered_future.get(), "hello-subscribers");
}

}  // namespace
}  // namespace mcp
