/**
 * Unit tests for transport-session keyed MCP sessions.
 *
 * HTTP+SSE clients send every JSON-RPC request on a fresh one-shot POST
 * connection, so a session keyed on the connection pointer would be a new
 * session per request and per-session state (resource subscriptions,
 * client info) would silently evaporate. SessionManager therefore also
 * keys sessions on the durable transport session id (the SSE stream id
 * from the POST /callback/{id} path).
 *
 * Covers:
 * - get-or-create by transport id is stable across calls (the fix that
 *   lets a subscription made on POST #1 still exist on POST #2)
 * - transport-keyed sessions are independent of connection tracking, so a
 *   POST connection closing does not tear them down
 * - removal by transport id (SSE stream close) and via removeSession keep
 *   both indexes consistent
 * - the expiry sweep also cleans the transport index
 * - the at-capacity path applies to transport-keyed creation too
 */

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"

using namespace mcp;
using namespace mcp::server;

namespace {

class SessionTransportBindingTest : public ::testing::Test {
 protected:
  McpServerConfig config_;
  McpServerStats stats_;
};

// The core property behind issue #240: two requests arriving on different
// (short-lived) connections but carrying the same transport session id
// must resolve to the same session.
TEST_F(SessionTransportBindingTest, SameTransportIdSameSession) {
  SessionManager manager(config_, stats_);

  auto first = manager.getOrCreateSessionByTransportId("client_1");
  auto second = manager.getOrCreateSessionByTransportId("client_1");

  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first, second);
  EXPECT_EQ(stats_.sessions_total.load(), 1u);
}

TEST_F(SessionTransportBindingTest, DifferentTransportIdsDistinctSessions) {
  SessionManager manager(config_, stats_);

  auto first = manager.getOrCreateSessionByTransportId("client_1");
  auto second = manager.getOrCreateSessionByTransportId("client_2");

  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->getId(), second->getId());
}

// Transport-keyed sessions deliberately carry no connection: the POST
// connection a request arrived on is already closing, and tracking it
// would let its close event destroy the session.
TEST_F(SessionTransportBindingTest, TransportSessionHasNoConnection) {
  SessionManager manager(config_, stats_);

  auto session = manager.getOrCreateSessionByTransportId("client_1");

  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->getConnection(), nullptr);
  EXPECT_EQ(session->getTransportSessionId(), "client_1");
}

TEST_F(SessionTransportBindingTest, LookupWithoutCreation) {
  SessionManager manager(config_, stats_);

  EXPECT_EQ(manager.getSessionByTransportId("client_1"), nullptr);
  auto created = manager.getOrCreateSessionByTransportId("client_1");
  EXPECT_EQ(manager.getSessionByTransportId("client_1"), created);
}

// SSE stream close path: removal returns the session so the caller can
// release subscription state, and the id becomes free again.
TEST_F(SessionTransportBindingTest, RemoveByTransportId) {
  SessionManager manager(config_, stats_);

  auto session = manager.getOrCreateSessionByTransportId("client_1");
  ASSERT_NE(session, nullptr);

  auto removed = manager.removeSessionByTransportId("client_1");
  EXPECT_EQ(removed, session);
  EXPECT_EQ(manager.getSession(session->getId()), nullptr);
  EXPECT_EQ(manager.getSessionByTransportId("client_1"), nullptr);

  // Idempotent: second removal is a no-op.
  EXPECT_EQ(manager.removeSessionByTransportId("client_1"), nullptr);

  // A new stream reusing the id gets a fresh session, not the dead one.
  auto fresh = manager.getOrCreateSessionByTransportId("client_1");
  ASSERT_NE(fresh, nullptr);
  EXPECT_NE(fresh->getId(), session->getId());
}

// removeSession (by MCP session id) must keep the transport index in sync,
// otherwise the next getOrCreate would resurrect a removed session.
TEST_F(SessionTransportBindingTest, RemoveSessionClearsTransportIndex) {
  SessionManager manager(config_, stats_);

  auto session = manager.getOrCreateSessionByTransportId("client_1");
  ASSERT_NE(session, nullptr);

  manager.removeSession(session->getId());
  EXPECT_EQ(manager.getSessionByTransportId("client_1"), nullptr);
}

// The expiry sweep must clean the transport index too, or an expired id
// would keep resolving to a destroyed session.
TEST_F(SessionTransportBindingTest, ExpirySweepCleansTransportIndex) {
  config_.session_timeout = std::chrono::milliseconds(0);
  SessionManager manager(config_, stats_);

  auto session = manager.getOrCreateSessionByTransportId("client_1");
  ASSERT_NE(session, nullptr);

  manager.cleanupExpiredSessions();

  EXPECT_EQ(manager.getSessionByTransportId("client_1"), nullptr);
  EXPECT_EQ(manager.getSession(session->getId()), nullptr);
  EXPECT_EQ(stats_.sessions_expired.load(), 1u);
}

// The session limit covers transport-keyed sessions too, and rejection is
// prompt (shares the non-recursive-mutex-safe sweep with createSession).
TEST_F(SessionTransportBindingTest, AtCapacityReturnsNull) {
  config_.max_sessions = 1;
  SessionManager manager(config_, stats_);

  ASSERT_NE(manager.getOrCreateSessionByTransportId("client_1"), nullptr);
  EXPECT_EQ(manager.getOrCreateSessionByTransportId("client_2"), nullptr);
}

}  // namespace
