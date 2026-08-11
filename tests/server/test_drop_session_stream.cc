/**
 * Closing the stream a session is holding.
 *
 * The interesting case — a real stream, closed, and a client that comes
 * back naming where it got to and is given what it missed — needs a
 * listener, a socket and a client, and is covered where all three exist:
 * against the official SDK's client in the interop suite, and against the
 * session manager directly in its own tests, which already pin that a
 * closed stream leaves the session and its replay buffer standing.
 *
 * What is left for here is the answer given when there is nothing to
 * close. That answer is worth pinning because the alternative to "no"
 * is not "yes" — it is reaching through a session manager that this
 * listener never built.
 */

#include <future>

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"

namespace mcp {
namespace server {
namespace {

using namespace std::chrono_literals;

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "drop-stream-test";
  config.server_version = "0.0.1";
  return config;
}

/** Runs the call and returns what it reported, or nothing if it never did. */
optional<bool> dropAndWait(McpServer& server, const std::string& session_id) {
  std::promise<bool> outcome;
  auto reached = outcome.get_future();
  server.dropSessionStream(
      session_id, [&outcome](bool dropped) { outcome.set_value(dropped); });
  if (reached.wait_for(2s) != std::future_status::ready) {
    return nullopt;
  }
  return mcp::make_optional(reached.get());
}

TEST(DropSessionStream, AServerServingNoStreamsDropsNothing) {
  McpServer server(testConfig());

  auto dropped = dropAndWait(server, "whatever");
  ASSERT_TRUE(dropped.has_value()) << "the caller was never told an outcome";
  EXPECT_FALSE(dropped.value());
}

TEST(DropSessionStream, AnUnknownSessionDropsNothing) {
  McpServer server(testConfig());

  auto dropped = dropAndWait(server, "a-session-that-never-existed");
  ASSERT_TRUE(dropped.has_value());
  EXPECT_FALSE(dropped.value());
}

// A caller with nothing to do afterwards may say so, and must not be
// made to invent a callback to be allowed to ask.
TEST(DropSessionStream, ACallerNeedNotWantAnAnswer) {
  McpServer server(testConfig());
  server.dropSessionStream("whatever");
}

}  // namespace
}  // namespace server
}  // namespace mcp
