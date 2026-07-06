/**
 * Unit tests for SessionManager capacity handling.
 *
 * Regression coverage for a self-deadlock: the at-capacity path in
 * createSession() called the locking cleanupExpiredSessions() while
 * already holding the non-recursive session mutex, so the first time
 * max_sessions was reached the server hung instead of rejecting the
 * session. The tests assert prompt rejection when nothing is expired and
 * successful reclamation when expired sessions can be swept.
 */

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"

using namespace mcp;
using namespace mcp::server;

namespace {

class SessionManagerCapacityTest : public ::testing::Test {
 protected:
  McpServerConfig config_;
  McpServerStats stats_;
};

// With a generous timeout nothing is expired, so hitting max_sessions
// must return null promptly. Before the fix this test hung forever on
// the second createSession call.
TEST_F(SessionManagerCapacityTest, AtCapacityReturnsNullWithoutDeadlock) {
  config_.max_sessions = 1;
  SessionManager manager(config_, stats_);

  ASSERT_NE(manager.createSession(nullptr), nullptr);
  EXPECT_EQ(manager.createSession(nullptr), nullptr);
}

// With an instant timeout the at-capacity sweep can reclaim the expired
// session, so the create succeeds — proving the sweep actually runs
// inside the locked path rather than being skipped.
TEST_F(SessionManagerCapacityTest, AtCapacityReclaimsExpiredSessions) {
  config_.max_sessions = 1;
  config_.session_timeout = std::chrono::milliseconds(0);
  SessionManager manager(config_, stats_);

  auto first = manager.createSession(nullptr);
  ASSERT_NE(first, nullptr);

  auto second = manager.createSession(nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(second->getId(), first->getId());
  EXPECT_EQ(stats_.sessions_expired.load(), 1u);
}

// The public sweep still works standalone (wrapper over the shared body).
TEST_F(SessionManagerCapacityTest, PublicCleanupSweepsExpiredSessions) {
  config_.session_timeout = std::chrono::milliseconds(0);
  SessionManager manager(config_, stats_);

  auto session = manager.createSession(nullptr);
  ASSERT_NE(session, nullptr);

  manager.cleanupExpiredSessions();
  EXPECT_EQ(manager.getSession(session->getId()), nullptr);
}

}  // namespace
