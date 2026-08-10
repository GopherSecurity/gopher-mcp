/**
 * What a Streamable HTTP client does with streams: an answer that
 * arrives as one, a stream it holds for the server to reach it on, and
 * what happens to either when it is cut.
 *
 * The peer is scripted, for the same reasons as the session tests: the
 * assertions here are about what the client sent and when — which
 * cursor it came back with, how many times it asked, whether it asked
 * again at all — and the interesting cases are the ones a real server
 * would only produce by accident.
 *
 * Where events are replayed, the tests count them. A stream delivered
 * twice and a stream delivered once are the same to an assertion that
 * only asks whether something arrived.
 */

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
using test::streamEnd;
using test::streamEvent;
using test::streamPrelude;

constexpr const char* kSession = "stream-session";

/** A notification the server pushes, as it goes on the wire. */
std::string notification(const std::string& method) {
  return "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\"}";
}

/** A request the server asks of the client. */
std::string serverRequest(int id, const std::string& method) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
         ",\"method\":\"" + method + "\"}";
}

/** A response arriving on a stream. */
std::string streamedAnswer(const std::string& rpc_id) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + rpc_id + ",\"result\":{}}";
}

/** Records what the application saw, in the order it saw it. */
class Arrivals {
 public:
  void record(const std::string& what) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_.push_back(what);
  }
  std::vector<std::string> order() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return order_;
  }
  size_t countOf(const std::string& what) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& seen : order_) {
      if (seen == what) {
        ++n;
      }
    }
    return n;
  }
  bool waitFor(size_t n, std::chrono::milliseconds budget = 5000ms) const {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (order_.size() >= n) {
          return true;
        }
      }
      std::this_thread::sleep_for(5ms);
    }
    return false;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> order_;
};

class StreamableHttpClientStreamTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  void startClient(uint16_t port, bool open_stream = true) {
    client::McpClientConfig config;
    config.client_name = "stream-test-client";
    config.client_version = "0.0.1";
    config.num_workers = 1;
    // These are about a client that has already chosen this transport,
    // not about how a client works out which one to choose. Saying so
    // keeps the search for that out of what they count.
    config.preferred_transport = TransportType::StreamableHttp;
    config.request_timeout = 5000ms;
    config.protocol_initialization_timeout = 5000ms;
    config.protocol_connection_timeout = 5000ms;
    config.streamable_http.open_server_stream = open_stream;
    // Compressed so that a test proving the client comes back does not
    // also have to prove it is patient.
    config.streamable_http.stream_reconnect_min = 30ms;
    config.streamable_http.stream_reconnect_max = 120ms;

    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);

    client_->registerNotificationHandler(
        "notifications/progress",
        [this](const jsonrpc::Notification&) { arrivals_.record("progress"); });
    client_->registerNotificationHandler(
        "notifications/pushed",
        [this](const jsonrpc::Notification&) { arrivals_.record("pushed"); });

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
  Arrivals arrivals_;
  std::unique_ptr<client::McpClient> client_;
};

// The point of a streamed answer: what happens on the way is delivered
// on the way, not collected up and handed over with the result.
TEST_F(StreamableHttpClientStreamTest, ProgressArrivesBeforeTheAnswer) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.method == "GET") {
      return Reply::stream(streamPrelude());
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    // The answer, with three notices on the way to it, all in one
    // stream that ends when the answer has been sent.
    return Reply::write(
        streamPrelude() +
        streamEvent("a:1", notification("notifications/progress")) +
        streamEvent("a:2", notification("notifications/progress")) +
        streamEvent("a:3", notification("notifications/progress")) +
        streamEvent("a:4", streamedAnswer(seen.rpc_id)) + streamEnd());
  });

  startClient(port);
  handshake();

  auto slow = client_->sendRequest("slow");
  ASSERT_EQ(slow.wait_for(5s), std::future_status::ready)
      << "the streamed answer never resolved the request";
  EXPECT_FALSE(slow.get().error.has_value());

  // By the time the result is in hand, everything that preceded it on
  // the stream has already been delivered — that is what "on the way"
  // means, and batching them behind the result would look identical to
  // an assertion that only checked they arrived.
  const auto order = arrivals_.order();
  ASSERT_EQ(order.size(), 3u) << "progress was not delivered as it arrived";
  EXPECT_EQ(order[0], "progress");
  EXPECT_EQ(order[1], "progress");
  EXPECT_EQ(order[2], "progress");
}

// A stream is held so the server can say something nobody asked for.
TEST_F(StreamableHttpClientStreamTest, APushOnTheStreamReachesTheClient) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.method == "GET") {
      return Reply::stream(streamPrelude());
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  ASSERT_TRUE(server_.waitForStream()) << "the client never opened a stream";
  auto gets = server_.allOfMethod("GET");
  ASSERT_FALSE(gets.empty());
  EXPECT_EQ(gets[0].header("mcp-session-id"), kSession)
      << "asked for a stream without saying which conversation it is for";
  EXPECT_NE(gets[0].header("accept").find("text/event-stream"),
            std::string::npos)
      << "asked for a stream without saying it wanted one";

  server_.pushToStream(
      streamEvent("s:1", notification("notifications/pushed")));

  ASSERT_TRUE(arrivals_.waitFor(1)) << "the push never arrived";
  EXPECT_EQ(arrivals_.countOf("pushed"), 1u);
}

// The server can ask the client something, and the answer comes back as
// a POST of its own.
TEST_F(StreamableHttpClientStreamTest, ARequestFromTheServerIsAnswered) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.method == "GET") {
      return Reply::stream(streamPrelude());
    }
    return Reply::write(accepted());
  });

  startClient(port);
  handshake();
  ASSERT_TRUE(server_.waitForStream());

  server_.pushToStream(streamEvent("s:1", serverRequest(77, "ping")));

  // The answer arrives as a POST, on the connection the client sends
  // everything else on, carrying the id it was asked under.
  const auto found = server_.waitFor([this]() {
    for (const auto& seen : server_.seen()) {
      if (seen.method == "POST" && seen.rpc_id == "77" &&
          seen.rpc_method.empty()) {
        return true;
      }
    }
    return false;
  });
  EXPECT_TRUE(found) << "the client never answered the server's request";
}

// The one this is all for: a stream that is cut is picked up where it
// stopped, and nothing arrives twice.
TEST_F(StreamableHttpClientStreamTest, ACutStreamIsPickedUpWhereItStopped) {
  std::atomic<int> streams{0};
  const uint16_t port = server_.start([&streams](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.method == "GET") {
      const int n = ++streams;
      if (n == 1) {
        return Reply::stream(streamPrelude());
      }
      // Whatever the client says it missed, it is given from there.
      return Reply::stream(
          streamPrelude() +
          streamEvent("s:3", notification("notifications/pushed")) +
          streamEvent("s:4", notification("notifications/pushed")) +
          streamEvent("s:5", notification("notifications/pushed")));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();
  ASSERT_TRUE(server_.waitForStream());

  server_.pushToStream(
      streamEvent("s:1", notification("notifications/pushed")) +
      streamEvent("s:2", notification("notifications/pushed")));
  ASSERT_TRUE(arrivals_.waitFor(2)) << "the first two never arrived";

  server_.cutStream();

  ASSERT_TRUE(arrivals_.waitFor(5)) << "the rest never arrived";
  // Five pushed, five delivered. A replay that started from the top of
  // the stream would deliver seven and still pass an assertion that
  // only asked whether the last one turned up.
  EXPECT_EQ(arrivals_.countOf("pushed"), 5u);
  EXPECT_EQ(arrivals_.order().size(), 5u);

  auto gets = server_.allOfMethod("GET");
  ASSERT_GE(gets.size(), 2u);
  EXPECT_FALSE(gets[0].hasHeader("last-event-id"))
      << "asked to carry on from somewhere before anything had been seen";
  EXPECT_EQ(gets[1].header("last-event-id"), "s:2")
      << "came back saying the wrong place";
}

// An answer cut off mid-stream is picked up on a stream carrying its
// cursor, and the request it belongs to still resolves.
TEST_F(StreamableHttpClientStreamTest, AnAnswerCutOffIsPickedUpOnAStream) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.method == "GET") {
      // Only a stream that says where to carry on from gets the rest of
      // the answer; a fresh one gets nothing, as it should.
      if (seen.header("last-event-id") == "a:1") {
        return Reply::stream(streamPrelude() +
                             streamEvent("a:2", streamedAnswer("2")));
      }
      return Reply::stream(streamPrelude());
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    // The answer starts arriving and the connection goes.
    return Reply::writeThenCut(
        streamPrelude() +
        streamEvent("a:1", notification("notifications/progress")));
  });

  startClient(port);
  handshake();
  ASSERT_TRUE(server_.waitForStream());

  auto slow = client_->sendRequest("slow");
  ASSERT_EQ(slow.wait_for(5s), std::future_status::ready)
      << "the interrupted answer was never picked up";
  EXPECT_FALSE(slow.get().error.has_value());

  // The progress that did arrive before the cut is not delivered again
  // by the replay — the client asked to carry on, not to start over.
  EXPECT_EQ(arrivals_.countOf("progress"), 1u);

  bool asked_from_the_cursor = false;
  for (const auto& seen : server_.allOfMethod("GET")) {
    if (seen.header("last-event-id") == "a:1") {
      asked_from_the_cursor = true;
    }
  }
  EXPECT_TRUE(asked_from_the_cursor)
      << "picked the answer up without saying where it stopped";
}

// A server that can never finish an answer must not be able to keep the
// request alive for as long as the client is up.
TEST_F(StreamableHttpClientStreamTest, AnAnswerNeverFinishedIsAnswered) {
  std::atomic<int> resume_streams{0};
  const uint16_t port =
      server_.start([&resume_streams](const Seen& seen) -> Reply {
        if (seen.rpc_method == "initialize") {
          return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
        }
        if (seen.method == "GET") {
          if (seen.hasHeader("last-event-id")) {
            ++resume_streams;
            // Cut again, every time.
            return Reply::writeThenCut(streamPrelude());
          }
          return Reply::stream(streamPrelude());
        }
        if (seen.rpc_id.empty()) {
          return Reply::write(accepted());
        }
        return Reply::writeThenCut(
            streamPrelude() +
            streamEvent("a:1", notification("notifications/progress")));
      });

  startClient(port);
  handshake();
  ASSERT_TRUE(server_.waitForStream());

  auto slow = client_->sendRequest("slow");
  ASSERT_EQ(slow.wait_for(5s), std::future_status::ready)
      << "the request was left to sit out its deadline";
  EXPECT_TRUE(slow.get().error.has_value())
      << "an answer that never arrived was reported as having arrived";

  // Asked for exactly as many times as it was allowed to be. What ends
  // it is the client deciding to stop, not the deadline.
  EXPECT_EQ(resume_streams.load(), 2);
}

// A server that will not hold a stream is not asked twice.
TEST_F(StreamableHttpClientStreamTest, AServerServingNoStreamIsNotAskedAgain) {
  const uint16_t port = server_.start([](const Seen& seen) -> Reply {
    if (seen.rpc_method == "initialize") {
      return Reply::write(handshakeAnswer(seen, kSession, "2025-06-18"));
    }
    if (seen.method == "GET") {
      return Reply::write(
          refuse(405, "Method Not Allowed", "this server serves no streams"));
    }
    if (seen.rpc_id.empty()) {
      return Reply::write(accepted());
    }
    return Reply::write(answer(seen, "{}"));
  });

  startClient(port);
  handshake();

  ASSERT_TRUE(server_.waitForMethod("GET", 1));

  // Long enough for several windows of the compressed backoff to have
  // passed, so a client that had not taken the refusal as final would
  // have asked again by now.
  std::this_thread::sleep_for(400ms);
  EXPECT_EQ(server_.countOfMethod("GET"), 1u)
      << "kept asking a server that had already said no";

  // And the conversation carries on without one.
  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

// The window before asking again grows and stops growing. Asserted on
// the schedule itself, so that proving a cap does not mean waiting for
// one.
TEST(StreamableHttpClientBackoffTest, TheWindowGrowsAndIsCapped) {
  client::RetryManager backoff(/*max_retries=*/0,
                               /*initial_delay=*/std::chrono::milliseconds(250),
                               /*backoff_multiplier=*/2.0,
                               /*max_delay=*/std::chrono::milliseconds(30000));

  // Jitter is ±20%, so consecutive windows are compared with that
  // allowed for: what matters is that each is around twice the last,
  // not that it is exactly.
  std::vector<int64_t> delays;
  for (size_t attempt = 0; attempt < 10; ++attempt) {
    delays.push_back(backoff.getRetryDelay(attempt).count());
  }

  EXPECT_GE(delays[0], 200);
  EXPECT_LE(delays[0], 300);
  for (size_t i = 1; i < delays.size(); ++i) {
    EXPECT_LE(delays[i], 30000)
        << "window " << i << " went past the cap: " << delays[i];
  }
  // Growing, allowing for the jitter that keeps clients apart: the
  // fourth window cannot be smaller than the first however the dice
  // fall.
  EXPECT_GT(delays[3], delays[0]);
  // And it does stop: by the tenth the cap is the only thing deciding.
  EXPECT_EQ(delays[9], 30000);
}

}  // namespace
}  // namespace mcp
