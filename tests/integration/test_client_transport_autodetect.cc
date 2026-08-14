/**
 * Working out what a server speaks, by asking it.
 *
 * The client used to read the transport off the URL — a path with
 * "/sse" in it meant one thing, anything else meant another. That is a
 * guess about a string. These tests are about the thing that replaced
 * it: a ladder that asks, stops when the answer is conclusive, and
 * falls back only when it is not.
 *
 * The peer is scripted rather than a real server because most of what
 * matters here is refusals and silences, and because the sharpest
 * assertions are about what the client did *not* do — a probe not sent,
 * a fallback not attempted. Only a peer that counts what arrives can
 * answer those.
 */

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/client/transport_probe.h"
#include "mcp/types.h"

#include "../client/scripted_http_server.h"

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
using test::streamEvent;
using test::streamPrelude;
using test::withBody;

constexpr const char* kSession = "ladder-session";

/** The answer a server speaking a newer protocol gives an introduction. */
std::string modernRefusal() {
  return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32020,"
         "\"message\":\"header mismatch\"}}";
}

/** What this project's own server answers for a path it does not serve. */
std::string plainNotFound() { return R"({"error":"not_found"})"; }

class TransportAutodetectTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  /**
   * A client pointed at the peer.
   *
   * @param preferred Leave as Stdio to have the transport worked out;
   *        naming an HTTP transport is somebody saying they already
   *        know, and must stop the asking entirely.
   */
  VoidResult connectClient(uint16_t port,
                           TransportType preferred = TransportType::Stdio,
                           const std::string& path = "/mcp",
                           bool modern_era = true) {
    client::McpClientConfig config;
    config.client_name = "ladder-test-client";
    config.client_version = "0.0.1";
    config.num_workers = 1;
    config.request_timeout = 5000ms;
    config.protocol_initialization_timeout = 5000ms;
    config.protocol_connection_timeout = 5000ms;
    config.preferred_transport = preferred;
    // Short, so that a test which proves the client gives up does not
    // also have to prove it is patient.
    config.streamable_http.fallback_probe_timeout = 700ms;
    config.streamable_http.enable_modern_era = modern_era;

    client_ = client::createMcpClient(config);
    EXPECT_NE(client_, nullptr);
    return client_->connect("http://127.0.0.1:" + std::to_string(port) + path);
  }

  ScriptedServer server_;
  std::unique_ptr<client::McpClient> client_;
};

// A server that answers the introduction is one that speaks this
// transport, and there is nothing further to ask it.
TEST_F(TransportAutodetectTest, AServerThatAnswersIsAskedNothingElse) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  auto connected = connectClient(port);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
      << "the client did not settle on the transport this server speaks";

  ASSERT_TRUE(server_.waitForRpc("initialize", 1));
  // The older transport is never tried, because there was no reason to.
  // A ladder that asked anyway would work and still be wrong.
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(server_.countOfMethod("GET"), 0u)
      << "fell back to an older transport after being answered";
}

// An introduction answered on a stream is answered. The reference
// implementation replies this way, and a client waiting for a stream to
// finish is waiting for the server to stop talking.
TEST_F(TransportAutodetectTest, AnAnswerOnAStreamIsStillAnAnswer) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      // Held open deliberately: no ending, no length, nothing to wait
      // for. Everything the ladder needs is in the headers.
      return Reply::stream(streamPrelude(kSession) +
                           streamEvent("i:1",
                                       "{\"jsonrpc\":\"2.0\",\"id\":0,"
                                       "\"result\":{}}"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  auto connected = connectClient(port);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(connected))
      << "an introduction answered on an open stream was read as no answer";

  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(server_.countOfMethod("GET"), 0u)
      << "fell back to an older transport after being answered on a stream";
}

// The rung that was standing empty until the newest revision existed.
// A server that answers the question only that revision asks is one this
// client cannot talk to, and the ladder stops rather than falling
// through — a server speaking only that revision would refuse the
// introduction below, and reading that refusal as "not this transport"
// would try the oldest one, fail there too, and report the wrong thing
// about the wrong attempt.
TEST_F(TransportAutodetectTest,
       AModernServerStopsTheLadderAndSaysWhatItServes) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "server/discover") {
      // The refusal a conformant server sends, with its own code and the
      // list a client is meant to pick from.
      return Reply::write(
          withBody(400, "Bad Request", "application/json",
                   "{\"jsonrpc\":\"2.0\",\"id\":\"discover-1\",\"error\":{"
                   "\"code\":-32022,"
                   "\"message\":\"Unsupported protocol "
                   "version\",\"data\":{\"supported\":"
                   "[\"2027-01-01\"],\"requested\":\"2026-07-28\"}}}",
                   std::string()));
    }
    return Reply::write(accepted());
  });

  auto connected = connectClient(port);
  ASSERT_FALSE(holds_alternative<std::nullptr_t>(connected))
      << "a server this client cannot speak to was reported as reachable";

  const auto* error = get_error<std::nullptr_t>(connected);
  ASSERT_NE(error, nullptr);
  EXPECT_NE(error->message.find("modern"), std::string::npos)
      << "the caller was not told why: " << error->message;
  EXPECT_NE(error->message.find("2027-01-01"), std::string::npos)
      << "the server said what it serves and the caller was not told: "
      << error->message;

  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(server_.countOf("initialize"), 0u)
      << "introduced itself to a server that has no introduction";
  EXPECT_EQ(server_.countOfMethod("GET"), 0u)
      << "fell through to the oldest transport after a modern answer";
}

// And a server that answers the discovery outright is just as modern,
// without having refused anything — and this client speaks that era, so
// it is talked to rather than reported unreachable. What makes it modern
// is what it did not need: no introduction, and no stream asked for.
TEST_F(TransportAutodetectTest, AServerThatAnswersTheDiscoveryIsModern) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "server/discover") {
      // Written out rather than built with answer(), which cannot echo a
      // string id — and the id this request carries is one, as the
      // revision's own example has it.
      return Reply::write(
          withBody(200, "OK", "application/json",
                   "{\"jsonrpc\":\"2.0\",\"id\":\"discover-1\",\"result\":{"
                   "\"resultType\":\"complete\",\"supportedVersions\":"
                   "[\"2026-07-28\"],\"capabilities\":{}}}",
                   std::string()));
    }
    return Reply::write(accepted());
  });

  auto connected = connectClient(port);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
      << "a server serving a revision this client speaks was not reached";

  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(server_.countOf("initialize"), 0u)
      << "introduced itself to a server that has no introduction";
  EXPECT_EQ(server_.countOfMethod("GET"), 0u)
      << "asked for a stream in an era that has none";
}

// A server of the newest era may serve older revisions beside it, and a
// client that cannot enter that era should meet it on one of those
// rather than be told there is nothing to talk about. This is what keeps
// a server turning the era on from cutting off every client that has not.
TEST_F(TransportAutodetectTest, AClientThatCannotEnterTheEraMeetsTheServer) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "server/discover") {
      return Reply::write(
          withBody(200, "OK", "application/json",
                   "{\"jsonrpc\":\"2.0\",\"id\":\"discover-1\",\"result\":{"
                   "\"resultType\":\"complete\",\"supportedVersions\":"
                   "[\"2026-07-28\",\"2025-06-18\"],\"capabilities\":{}}}",
                   std::string()));
    }
    if (seen.rpc_method == "initialize") {
      return Reply::write(withBody(
          200, "OK", "application/json",
          "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"protocolVersion\":"
          "\"2025-06-18\",\"capabilities\":{},\"serverInfo\":{\"name\":\"old\","
          "\"version\":\"1\"}}}",
          std::string()));
    }
    return Reply::write(accepted());
  });

  auto connected = connectClient(port, TransportType::Stdio, "/mcp",
                                 /*modern_era=*/false);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
      << "a client that declined the newest era was cut off from a server "
         "that also serves one it speaks";

  std::this_thread::sleep_for(300ms);
  EXPECT_GT(server_.countOf("initialize"), 0u)
      << "it neither entered the era nor introduced itself to the era "
         "below, so it reached nothing";
}

// Every server answers the discovery, including one that has never heard
// of the newest revision — it is mandatory, and this project serves it to
// callers of both eras. So answering is not evidence of an era, and a
// client that read it that way would refuse to talk to every server it
// could have talked to. What decides is the list.
TEST_F(TransportAutodetectTest, AnsweringTheDiscoveryIsNotEnoughToBeModern) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "server/discover") {
      return Reply::write(
          withBody(200, "OK", "application/json",
                   "{\"jsonrpc\":\"2.0\",\"id\":\"discover-1\",\"result\":{"
                   "\"supportedVersions\":[\"2025-11-25\",\"2025-06-18\"],"
                   "\"capabilities\":{}}}",
                   std::string()));
    }
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  auto connected = connectClient(port);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(connected))
      << "a server naming only older revisions was treated as one this "
         "client cannot talk to";
  EXPECT_GE(server_.countOf("initialize"), 1u)
      << "the ladder stopped instead of going on to introduce itself";
}

// A newer server's refusal stops the ladder. Falling through would fail
// for a reason that says nothing about why.
TEST_F(TransportAutodetectTest, ANewerServerStopsTheLadder) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.method == "POST") {
      return Reply::write(withBody(400, "Bad Request", "application/json",
                                   modernRefusal(), std::string()));
    }
    return Reply::write(accepted());
  });

  auto connected = connectClient(port);
  ASSERT_FALSE(holds_alternative<std::nullptr_t>(connected))
      << "a server this client cannot speak to was reported as reachable";

  const auto* error = get_error<std::nullptr_t>(connected);
  ASSERT_NE(error, nullptr);
  EXPECT_NE(error->message.find("modern"), std::string::npos)
      << "the caller was not told why: " << error->message;

  // And no fallback was attempted, which is the whole point of stopping.
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(server_.countOfMethod("GET"), 0u)
      << "tried an older transport against a server that speaks a newer one";
}

// A refusal that is not a newer server's is a reason to try the older
// transport, and this project's own answer for an unserved path is
// exactly that case.
TEST_F(TransportAutodetectTest, AnOrdinaryRefusalFallsThroughToTheOlderWay) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.method == "POST") {
      return Reply::write(withBody(404, "Not Found", "application/json",
                                   plainNotFound(), std::string()));
    }
    if (seen.method == "GET") {
      // The older transport announces where to post, which is the only
      // thing that proves it is what this server speaks.
      return Reply::stream(streamPrelude() +
                           streamEvent("", "callback/session-1", "endpoint"));
    }
    return Reply::write(accepted());
  });

  auto connected = connectClient(port);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(connected))
      << "did not fall through to the transport this server speaks";

  EXPECT_GE(server_.countOfMethod("POST"), 1u);
  EXPECT_GE(server_.countOfMethod("GET"), 1u)
      << "never tried the older transport after an ordinary refusal";
}

// A server that answers nothing is given up on, and the giving up says
// what was asked rather than only that it did not work.
TEST_F(TransportAutodetectTest, AServerSpeakingNothingIsGivenUpOn) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.method == "POST") {
      return Reply::write(withBody(404, "Not Found", "application/json",
                                   plainNotFound(), std::string()));
    }
    // A stream that opens and then says nothing at all: the case a
    // deadline exists for.
    return Reply::stream(streamPrelude());
  });

  const auto started = std::chrono::steady_clock::now();
  auto connected = connectClient(port);
  const auto took = std::chrono::steady_clock::now() - started;

  ASSERT_FALSE(holds_alternative<std::nullptr_t>(connected))
      << "a server that never answered was reported as reachable";

  const auto* error = get_error<std::nullptr_t>(connected);
  ASSERT_NE(error, nullptr);
  // One error naming both attempts, not the last thing that happened.
  EXPECT_NE(error->message.find("POST"), std::string::npos)
      << "did not say what the first attempt was told: " << error->message;
  EXPECT_NE(error->message.find("GET"), std::string::npos)
      << "did not say what the second attempt was told: " << error->message;

  // Bounded by the probe's own deadline rather than by whatever the
  // caller was prepared to wait.
  EXPECT_LT(took, 8s) << "gave up only when something else ran out";
}

// Somebody who has said which transport to use is not asked about it.
TEST_F(TransportAutodetectTest, AnAnsweredQuestionIsNotAsked) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    return Reply::write(accepted());
  });

  auto connected = connectClient(port, TransportType::StreamableHttp);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected));

  // Nothing at all until the application says something: no probe, and
  // in particular no introduction that the application did not ask for.
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(server_.countOf("initialize"), 0u)
      << "asked a question that had already been answered";
  EXPECT_EQ(server_.countOfMethod("GET"), 0u);
}

// The decision that carries a probe to a TLS server.
//
// Tested here rather than end to end: the way this was wrong — every
// probe built in plaintext whatever the URL said — is invisible from
// outside the probe, so a test that only watched a plaintext server
// could never have caught it. What is still untested is whether the TLS
// transport it now builds is configured correctly, which needs a TLS
// peer to answer.
TEST(TransportProbeTlsTest, AnHttpsUrlIsProbedOverTls) {
  EXPECT_TRUE(client::probeRequiresTls("https://example.com/mcp"));
  EXPECT_TRUE(client::probeRequiresTls("https://127.0.0.1:8443/mcp"));

  EXPECT_FALSE(client::probeRequiresTls("http://example.com/mcp"));
  EXPECT_FALSE(client::probeRequiresTls("http://127.0.0.1:8080/mcp"));
  // Neither a prefix of the scheme nor something merely containing it.
  EXPECT_FALSE(client::probeRequiresTls("https:/"));
  EXPECT_FALSE(client::probeRequiresTls(""));
  EXPECT_FALSE(client::probeRequiresTls("http://example.com/https://x"));
}

}  // namespace
}  // namespace mcp
