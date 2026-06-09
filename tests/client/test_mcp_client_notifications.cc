/**
 * @file test_mcp_client_notifications.cc
 * @brief Unit tests for client-side notification handler registration.
 *
 * Verifies McpClient::registerNotificationHandler / handleNotification, the
 * mechanism that lets applications observe server-initiated notifications such
 * as resource updates (notifications/resources/updated).
 *
 * These tests drive handleNotification() directly (the dispatcher-thread entry
 * point invoked by the protocol filter), so no socket or live connection is
 * required — the dispatch logic is exercised in isolation.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/types.h"

namespace mcp {
namespace client {
namespace {

// Test peer exposing the protected dispatcher-thread entry point so the
// dispatch logic can be exercised directly, without standing up a connection.
// This is the same path the JSON-RPC protocol filter drives on the wire.
class TestClient : public McpClient {
 public:
  using McpClient::McpClient;
  void deliverNotification(const jsonrpc::Notification& notification) {
    handleNotification(notification);
  }
};

class McpClientNotificationTest : public ::testing::Test {
 protected:
  McpClientConfig config_;
};

// A registered handler fires for a matching notification method and receives
// the notification (including its params) intact.
TEST_F(McpClientNotificationTest,
       RegisteredHandlerReceivesMatchingNotification) {
  TestClient client(config_);

  int call_count = 0;
  std::string seen_uri;
  client.registerNotificationHandler(
      "notifications/resources/updated", [&](const jsonrpc::Notification& n) {
        ++call_count;
        ASSERT_TRUE(n.params.has_value());
        auto it = n.params.value().find("uri");
        ASSERT_NE(it, n.params.value().end());
        ASSERT_TRUE(holds_alternative<std::string>(it->second));
        seen_uri = get<std::string>(it->second);
      });

  Metadata params;
  params["uri"] = std::string("file:///tmp/watched.txt");
  jsonrpc::Notification notification("notifications/resources/updated", params);

  client.deliverNotification(notification);

  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(seen_uri, "file:///tmp/watched.txt");
}

// A notification with no registered handler is silently ignored (notifications
// are never answered) and must not affect other handlers.
TEST_F(McpClientNotificationTest, UnregisteredMethodIsIgnored) {
  TestClient client(config_);

  bool called = false;
  client.registerNotificationHandler(
      "notifications/resources/updated",
      [&](const jsonrpc::Notification&) { called = true; });

  jsonrpc::Notification other("notifications/tools/list_changed");
  EXPECT_NO_THROW(client.deliverNotification(other));
  EXPECT_FALSE(called);
}

// Registering a second handler for the same method replaces the first.
TEST_F(McpClientNotificationTest, ReRegisteringReplacesHandler) {
  TestClient client(config_);

  bool first_called = false;
  bool second_called = false;
  client.registerNotificationHandler(
      "notifications/resources/updated",
      [&](const jsonrpc::Notification&) { first_called = true; });
  client.registerNotificationHandler(
      "notifications/resources/updated",
      [&](const jsonrpc::Notification&) { second_called = true; });

  client.deliverNotification(
      jsonrpc::Notification("notifications/resources/updated"));

  EXPECT_FALSE(first_called);
  EXPECT_TRUE(second_called);
}

// An exception thrown by a handler is swallowed (a misbehaving callback must
// not tear down the dispatcher) and counted as a client error.
TEST_F(McpClientNotificationTest, HandlerExceptionIsContainedAndCounted) {
  TestClient client(config_);

  const uint64_t errors_before = client.getClientStats().errors_total;
  client.registerNotificationHandler(
      "notifications/resources/updated", [](const jsonrpc::Notification&) {
        throw std::runtime_error("handler failure");
      });

  EXPECT_NO_THROW(client.deliverNotification(
      jsonrpc::Notification("notifications/resources/updated")));
  EXPECT_EQ(client.getClientStats().errors_total, errors_before + 1);
}

// Distinct methods route to their own handlers independently.
TEST_F(McpClientNotificationTest, DistinctMethodsRouteIndependently) {
  TestClient client(config_);

  int updated_calls = 0;
  int list_changed_calls = 0;
  client.registerNotificationHandler(
      "notifications/resources/updated",
      [&](const jsonrpc::Notification&) { ++updated_calls; });
  client.registerNotificationHandler(
      "notifications/resources/list_changed",
      [&](const jsonrpc::Notification&) { ++list_changed_calls; });

  client.deliverNotification(
      jsonrpc::Notification("notifications/resources/updated"));
  client.deliverNotification(
      jsonrpc::Notification("notifications/resources/list_changed"));
  client.deliverNotification(
      jsonrpc::Notification("notifications/resources/updated"));

  EXPECT_EQ(updated_calls, 2);
  EXPECT_EQ(list_changed_calls, 1);
}

}  // namespace
}  // namespace client
}  // namespace mcp
