/**
 * Unit tests: the context-free legacy dispatch path must not leak
 * sessions.
 *
 * A message arriving through the context-free hooks carries no origin
 * connection, so the session lookup has no key. Creating a session per
 * message would register one unretrievable (null-keyed) session per
 * message until max_sessions is exhausted — at which point EVERY
 * transport on the server starts failing with "Max sessions reached".
 * The server instead keeps exactly one shared session for the
 * context-free path, which also preserves a legacy client's state
 * across its messages.
 */

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

// Widen access to the protected context-free hooks — the test drives the
// exact entry an un-migrated external producer would use.
class LegacyDispatchTestServer : public McpServer {
 public:
  explicit LegacyDispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onNotification;
  using McpServer::onRequest;
};

jsonrpc::Request makePing(int64_t id) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(id);
  request.method = "ping";
  return request;
}

TEST(LegacyDispatchSession, ContextFreeMessagesShareOneSession) {
  McpServerConfig config;
  config.server_name = "legacy-dispatch-test";
  config.server_version = "0.0.1";
  LegacyDispatchTestServer server(config);

  const auto& stats = server.getServerStats();
  ASSERT_EQ(stats.sessions_total.load(), 0u);

  // Ten context-free requests and notifications: exactly ONE session may
  // be created, and it must be reused for every subsequent message.
  for (int64_t i = 1; i <= 5; ++i) {
    server.onRequest(makePing(i));

    jsonrpc::Notification notification;
    notification.jsonrpc = "2.0";
    notification.method = "ping";
    server.onNotification(notification);
  }

  EXPECT_EQ(stats.sessions_total.load(), 1u)
      << "context-free dispatches must share one session, not leak one "
         "per message";
  EXPECT_EQ(stats.sessions_active.load(), 1u);
}

TEST(LegacyDispatchSession, ContextFreeSessionDoesNotStarveMaxSessions) {
  McpServerConfig config;
  config.server_name = "legacy-dispatch-capacity-test";
  config.server_version = "0.0.1";
  config.max_sessions = 3;
  LegacyDispatchTestServer server(config);

  // Far more context-free messages than max_sessions: with the leak,
  // request #4 onward would fail with "Max sessions reached"; with the
  // shared session the capacity is never approached.
  for (int64_t i = 1; i <= 10; ++i) {
    server.onRequest(makePing(i));
  }

  const auto& stats = server.getServerStats();
  EXPECT_EQ(stats.sessions_total.load(), 1u);
  EXPECT_LT(stats.sessions_active.load(),
            static_cast<uint64_t>(config.max_sessions))
      << "legacy dispatches must not consume the session capacity other "
         "transports depend on";
}

}  // namespace
}  // namespace server
}  // namespace mcp
