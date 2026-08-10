/**
 * What a Streamable HTTP client says about its session, and what it does
 * when the server says it has forgotten one.
 *
 * The peer here is scripted rather than a real McpServer, for two
 * reasons. The assertions are about what the server *received* — which
 * headers were on which request, how many initializes there were, in
 * what order — and a scripted peer records exactly that. And the
 * interesting cases are refusals: a session that has expired, one that
 * expires twice, a request refused for some other reason, a
 * notification answered with 202. Arranging those against a real server
 * means short timeouts and sleeps; arranging them here means writing the
 * status.
 *
 * Every count assertion is a count and not a search. A request sent
 * twice under two sessions and a request sent once look identical to an
 * assertion that merely finds one.
 */

#include <chrono>
#include <future>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/types.h"

#include "scripted_http_server.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;
using test::accepted;
using test::answer;
using test::handshakeAnswer;
using test::refuse;
using test::Reply;
using test::ScriptedServer;
using test::Seen;
using test::withBody;

class StreamableHttpClientSessionTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  /** Bring up a client pointed at the scripted peer and connect it. */
  void startClient(uint16_t port) {
    client::McpClientConfig config;
    config.client_name = "session-test-client";
    config.client_version = "0.0.1";
    config.num_workers = 1;
    config.request_timeout = 5000ms;
    config.protocol_initialization_timeout = 5000ms;
    config.protocol_connection_timeout = 5000ms;

    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);

    // A plain http:// URL whose path is neither /sse nor /events is what
    // McpClient negotiates Streamable HTTP for.
    const std::string uri = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    auto connected = client_->connect(uri);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
        << "could not connect to the scripted server";
  }

  void handshake() {
    auto init = client_->initializeProtocol();
    ASSERT_EQ(init.wait_for(5s), std::future_status::ready)
        << "initialize never came back";
    ASSERT_NO_THROW(init.get());
  }

  ScriptedServer server_;
  std::unique_ptr<client::McpClient> client_;
};

constexpr const char* kSessionOne = "session-one";
constexpr const char* kSessionTwo = "session-two";

// A client that has not been given a session cannot name one, and until
// the handshake has settled a revision it cannot declare one either.
TEST_F(StreamableHttpClientSessionTest, TheHandshakeSaysNothingItWasNotTold) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSessionOne, "2025-06-18"));
    }
    return Reply::write(accepted());
  });

  startClient(port);
  handshake();

  auto handshakes = server_.allOf("initialize");
  ASSERT_EQ(handshakes.size(), 1u);
  EXPECT_FALSE(handshakes[0].hasHeader("mcp-session-id"))
      << "named a session before being given one";
  EXPECT_FALSE(handshakes[0].hasHeader("mcp-protocol-version"))
      << "declared a revision before one was negotiated";
}

// And once it has been told, it says so on everything that follows.
TEST_F(StreamableHttpClientSessionTest, EveryRequestAfterNamesTheSession) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSessionOne, "2025-03-26"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());

  auto pings = server_.allOf("ping");
  ASSERT_EQ(pings.size(), 1u);
  EXPECT_EQ(pings[0].header("mcp-session-id"), kSessionOne);
  // The revision the server settled on, not the one the client asked
  // for — those differ here on purpose.
  EXPECT_EQ(pings[0].header("mcp-protocol-version"), "2025-03-26");
}

// The notification that completes the handshake is sent, and it names
// the session like everything else after initialize.
TEST_F(StreamableHttpClientSessionTest, TheHandshakeIsFollowedByItsNotice) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSessionOne, "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  ASSERT_TRUE(server_.waitForRequests(2));
  auto notices = server_.allOf("notifications/initialized");
  ASSERT_EQ(notices.size(), 1u) << "the server was never told we were ready";
  EXPECT_EQ(notices[0].header("mcp-session-id"), kSessionOne);

  // A 202 for that notification must not have consumed the place
  // belonging to the request behind it.
  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

// A server that keeps no sessions is never told about one, and the
// conversation works anyway.
TEST_F(StreamableHttpClientSessionTest, AServerKeepingNoSessionIsToldOfNone) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, std::string(), "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());

  for (const auto& seen : server_.seen()) {
    EXPECT_FALSE(seen.hasHeader("mcp-session-id"))
        << "claimed a session that was never minted, on " << seen.rpc_method;
  }
  // The revision is still declared: it was negotiated whether or not a
  // session came with it.
  auto pings = server_.allOf("ping");
  ASSERT_EQ(pings.size(), 1u);
  EXPECT_EQ(pings[0].header("mcp-protocol-version"), "2025-06-18");
}

// The one this is all for: the server forgets, says so, and the client
// starts again by itself — once, and sends the refused request once.
TEST_F(StreamableHttpClientSessionTest, AForgottenSessionIsStartedAgainOnce) {
  std::atomic<int> handshakes{0};
  const uint16_t port = server_.start([&handshakes](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      const int n = ++handshakes;
      return Reply::write(handshakeAnswer(
          seen, n == 1 ? kSessionOne : kSessionTwo, "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    // Anything sent under the session that has been forgotten.
    if (seen.header("mcp-session-id") == kSessionOne) {
      return Reply::write(
          refuse(404, "Not Found", "no such session; send initialize again"));
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready)
      << "the refused request was never answered";
  EXPECT_FALSE(ping.get().error.has_value())
      << "the request did not survive the new session";

  // Exactly one further handshake, and it carried nothing from the
  // session that was gone.
  auto all_handshakes = server_.allOf("initialize");
  ASSERT_EQ(all_handshakes.size(), 2u);
  EXPECT_FALSE(all_handshakes[1].hasHeader("mcp-session-id"))
      << "started a new session while still naming the old one";

  // Sent twice in total — once refused, once served — and not a third
  // time. The second one names the new session.
  auto pings = server_.allOf("ping");
  ASSERT_EQ(pings.size(), 2u);
  EXPECT_EQ(pings[0].header("mcp-session-id"), kSessionOne);
  EXPECT_EQ(pings[1].header("mcp-session-id"), kSessionTwo);
}

// A server that forgets every session it mints must not be able to keep
// a client going round.
TEST_F(StreamableHttpClientSessionTest, ARequestRefusedTwiceIsAnswered) {
  std::atomic<int> handshakes{0};
  const uint16_t port = server_.start([&handshakes](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      const int n = ++handshakes;
      return Reply::write(
          handshakeAnswer(seen, "session-" + std::to_string(n), "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(
        refuse(404, "Not Found", "no such session; send initialize again"));
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready)
      << "the request was left to sit out its deadline";
  EXPECT_TRUE(ping.get().error.has_value())
      << "a request nothing could serve was reported as served";

  // Two handshakes and two attempts, and then it stops. What ends it is
  // the client refusing to try again, not the deadline.
  EXPECT_EQ(server_.countOf("initialize"), 2u);
  EXPECT_EQ(server_.countOf("ping"), 2u);
}

// A refusal that is not about the session is answered where it is,
// rather than treated as something to recover from.
TEST_F(StreamableHttpClientSessionTest, ARefusalThatIsNotAboutTheSession) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSessionOne, "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(
        refuse(400, "Bad Request", "the origin is not allowed here"));
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready)
      << "a refused request was left to sit out its deadline";
  auto response = ping.get();
  ASSERT_TRUE(response.error.has_value());
  EXPECT_NE(response.error->message.find("the origin is not allowed here"),
            std::string::npos)
      << "the caller was not told what the server said: "
      << response.error->message;

  // Nothing about a bad request says the session is gone.
  EXPECT_EQ(server_.countOf("initialize"), 1u);
  EXPECT_EQ(server_.countOf("ping"), 1u);
}

// A client that is finished says so, rather than leaving the session to
// expire on its own.
TEST_F(StreamableHttpClientSessionTest, EndingTheClientEndsTheSession) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.method == "DELETE") {
      return Reply::write(
          withBody(200, "OK", "application/json", "{}", std::string()));
    }
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSessionOne, "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());

  client_->shutdown();
  client_.reset();

  ASSERT_TRUE(server_.waitForMethod("DELETE", 1))
      << "the session was abandoned rather than given back";
  ASSERT_EQ(server_.countOfMethod("DELETE"), 1u);
  for (const auto& seen : server_.seen()) {
    if (seen.method == "DELETE") {
      EXPECT_EQ(seen.header("mcp-session-id"), kSessionOne)
          << "asked to end a session without saying which";
      EXPECT_EQ(seen.path, "/mcp");
      EXPECT_TRUE(seen.body.empty())
          << "a request to end a session has no body";
    }
  }
}

}  // namespace
}  // namespace mcp
