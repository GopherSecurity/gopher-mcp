/**
 * @file test_streamable_http_resume.cc
 * @brief Wire-level tests for coming back to a stream that was lost
 *
 * An event id is a promise: a client that reads one may return with it and
 * ask for everything after. What matters here is that the promise is kept
 * exactly — not almost. So these tests count what arrives rather than
 * merely looking for it, and every one of them says what must *not* come
 * back: another stream's events, an event the client already had, or
 * anything at all once a stream has been given up on.
 *
 * Real TCP socketpairs, following test_streamable_http_get_stream.cc.
 */

#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

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

/** Answers requests, and can answer one with a stream it keeps hold of. */
class ResumeTestCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests.push_back(request);
  }

  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    requests.push_back(request);

    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));

    if (!stream_answers || request.method == "initialize") {
      context.sendResponse(response);
      return;
    }

    // Kept past the dispatch on purpose: a request whose answer is still
    // being produced is the one a client can lose halfway through.
    held = context.beginResponseStream();
    if (!held) {
      context.sendResponse(response);
      return;
    }
    for (size_t i = 0; i < progress_before_answer; ++i) {
      jsonrpc::Notification progress;
      progress.jsonrpc = "2.0";
      progress.method = "notifications/progress";
      held->sendNotification(progress);
    }
    if (answer_now) {
      held->sendResponse(response);
    } else {
      pending_answer = response;
    }
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    notifications.push_back(notification);
  }

  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext&) override {
    notifications.push_back(notification);
  }

  void onResponse(const jsonrpc::Response& response) override {
    responses.push_back(response);
  }

  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  bool stream_answers{false};
  size_t progress_before_answer{0};
  bool answer_now{true};
  ResponseStreamPtr held;
  jsonrpc::Response pending_answer;

  std::vector<jsonrpc::Request> requests;
  std::vector<jsonrpc::Notification> notifications;
  std::vector<jsonrpc::Response> responses;
};

/** How the server under test is configured. */
struct ServerOptions {
  bool resumable = true;
  size_t replay_events = 256;
  size_t queue_limit = 256;
  std::chrono::milliseconds retention{60000};
  std::chrono::milliseconds session_timeout{300000};
};

/** One client: a connection and the socket the test reads from. */
struct Client {
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<stream_info::StreamInfo> info;
};

class StreamableHttpResumeTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    executeInDispatcher([&]() {
      callbacks_.held.reset();
      for (auto& client : clients_) {
        if (client->conn) {
          client->conn->close(network::ConnectionCloseType::NoFlush);
        }
        client->conn.reset();
      }
      clients_.clear();
      factory_.reset();
    });
    test::RealIoTestBase::TearDown();
  }

  void startServer(ServerOptions options = ServerOptions()) {
    options_ = options;
    executeInDispatcher([&]() {
      factory_ =
          std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks_,
                                                      /*is_server=*/true,
                                                      /*http_path=*/"/mcp",
                                                      /*http_host=*/"localhost",
                                                      /*use_sse=*/true,
                                                      /*sse_path=*/"/sse",
                                                      /*rpc_path=*/"/mcp");
      transport::StreamableHttpConfig config;
      config.enable_resumability = options_.resumable;
      config.replay_buffer_events = options_.replay_events;
      config.closed_stream_retention = options_.retention;
      config.session_timeout = options_.session_timeout;
      factory_->setSessionConfig(config);
      factory_->setSecurityConfig(config);
    });
  }

  Client* connect() {
    std::unique_ptr<Client> client(new Client());
    Client* raw = client.get();
    executeInDispatcher([&]() {
      auto pair = createSocketPair();
      auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      auto transport = std::make_unique<network::RawBufferTransportSocket>();
      raw->info = std::make_shared<stream_info::StreamInfoImpl>();

      raw->conn = network::ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket), std::move(transport), *raw->info);
      auto* impl = static_cast<network::ConnectionImpl*>(raw->conn.get());
      ASSERT_TRUE(factory_->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();
      raw->peer = std::move(pair.second);
    });
    clients_.push_back(std::move(client));
    return raw;
  }

  /** The client goes away without saying anything, as clients do. */
  void disconnect(Client& client) {
    executeInDispatcher([&]() {
      if (client.conn) {
        client.conn->close(network::ConnectionCloseType::NoFlush);
        client.conn.reset();
      }
    });
    std::this_thread::sleep_for(100ms);
  }

  void send(Client& client,
            const std::string& method,
            const std::string& body,
            const std::string& extra_headers = std::string()) {
    const std::string request =
        method +
        " /mcp HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n" +
        extra_headers + "Content-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body;
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(request);
      auto result = client.peer->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
  }

  /** Read whatever has arrived, giving it a moment to. */
  std::string read(Client& client, std::chrono::milliseconds budget = 400ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = client.peer->read(buffer, 4096);
      if (result.ok() && *result > 0) {
        out.append(buffer.toString());
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }

  /** Read until something arrives, or the budget runs out. */
  std::string readSomething(Client& client,
                            std::chrono::milliseconds budget = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = client.peer->read(buffer, 4096);
      if (result.ok() && *result > 0) {
        return buffer.toString();
      }
      std::this_thread::sleep_for(5ms);
    }
    return std::string();
  }

  /** Everything a stream has to say for a while. */
  std::string drain(Client& client) {
    return readSomething(client) + read(client);
  }

  std::string initialize(Client& client) {
    send(client, "POST", kInitialize);
    const std::string response = readSomething(client);
    const std::string name = "\r\nMcp-Session-Id: ";
    const size_t at = response.find(name);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t start = at + name.size();
    return response.substr(start, response.find("\r\n", start) - start);
  }

  void push(const std::string& id, const std::string& payload) {
    executeInDispatcher([&]() {
      auto* sessions = factory_->sessionManager();
      ASSERT_NE(sessions, nullptr);
      auto* session = sessions->find(id);
      ASSERT_NE(session, nullptr);
      sessions->routeUnsolicited(*session, payload);
    });
  }

  static int statusOf(const std::string& response) {
    if (response.compare(0, 9, "HTTP/1.1 ") != 0) {
      return 0;
    }
    return std::atoi(response.c_str() + 9);
  }

  /** The event ids a stream sent, in the order it sent them. */
  static std::vector<std::string> eventIds(const std::string& bytes) {
    std::vector<std::string> ids;
    size_t at = 0;
    while ((at = bytes.find("id: ", at)) != std::string::npos) {
      // Only at the start of a line: "id: " inside a JSON payload is not
      // an id field, and counting one would make this test agree with a
      // server that never sent any.
      const bool at_line_start = at == 0 || bytes[at - 1] == '\n';
      const size_t start = at + 4;
      at = start;
      if (!at_line_start) {
        continue;
      }
      size_t end = bytes.find('\n', start);
      if (end == std::string::npos) {
        end = bytes.size();
      }
      std::string id = bytes.substr(start, end - start);
      while (!id.empty() && (id.back() == '\r' || id.back() == ' ')) {
        id.pop_back();
      }
      ids.push_back(id);
    }
    return ids;
  }

  /** How many times something appears, which is what "once" needs. */
  static size_t occurrences(const std::string& haystack,
                            const std::string& needle) {
    size_t count = 0;
    size_t at = 0;
    while ((at = haystack.find(needle, at)) != std::string::npos) {
      ++count;
      at += needle.size();
    }
    return count;
  }

  std::string sseHeaders(const std::string& id,
                         const std::string& last_event_id = std::string()) {
    std::string headers =
        "Accept: text/event-stream\r\nMcp-Session-Id: " + id + "\r\n";
    if (!last_event_id.empty()) {
      headers += "Last-Event-ID: " + last_event_id + "\r\n";
    }
    return headers;
  }

  static constexpr const char* kInitialize =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  static constexpr const char* kSlowCall =
      "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\"}";

  ResumeTestCallbacks callbacks_;
  ServerOptions options_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::vector<std::unique_ptr<Client>> clients_;
};

constexpr const char* StreamableHttpResumeTest::kInitialize;
constexpr const char* StreamableHttpResumeTest::kSlowCall;

// ── The ids themselves ─────────────────────────────────────────────────────

TEST_F(StreamableHttpResumeTest, EveryEventSaysWhereInItsStreamItSits) {
  startServer();
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);

  push(id, "{\"method\":\"notifications/one\"}");
  push(id, "{\"method\":\"notifications/two\"}");
  const std::vector<std::string> ids = eventIds(drain(*client));

  ASSERT_EQ(ids.size(), 2u);
  const size_t split = ids[0].rfind(':');
  ASSERT_NE(split, std::string::npos) << ids[0];
  // Named after the stream, then numbered within it: the first half is
  // what a client comes back holding, the second is where it got to.
  EXPECT_EQ(ids[0].substr(0, split), ids[1].substr(0, split));
  EXPECT_EQ(ids[0].substr(split), ":1");
  EXPECT_EQ(ids[1].substr(split), ":2");
}

TEST_F(StreamableHttpResumeTest, NoTwoStreamsOfASessionShareAName) {
  startServer();
  Client* first = connect();
  const std::string id = initialize(*first);
  ASSERT_FALSE(id.empty());

  send(*first, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*first)), 200);
  push(id, "{\"method\":\"notifications/first\"}");
  const std::vector<std::string> first_ids = eventIds(drain(*first));
  ASSERT_EQ(first_ids.size(), 1u);

  // The newest stream is where the next message goes, so this one gets it.
  Client* second = connect();
  send(*second, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*second)), 200);
  push(id, "{\"method\":\"notifications/second\"}");
  const std::vector<std::string> second_ids = eventIds(drain(*second));
  ASSERT_EQ(second_ids.size(), 1u);

  // Both are the first event of their own stream, so only the name keeps
  // them apart — and it has to, or a resume would find the wrong buffer.
  EXPECT_NE(first_ids[0], second_ids[0]);
}

// ── Coming back ────────────────────────────────────────────────────────────

TEST_F(StreamableHttpResumeTest, AResumedStreamIsGivenExactlyWhatItMissed) {
  startServer();
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);

  for (int i = 1; i <= 5; ++i) {
    push(id, "{\"method\":\"notifications/" + std::to_string(i) + "\"}");
  }
  const std::vector<std::string> sent = eventIds(drain(*client));
  ASSERT_EQ(sent.size(), 5u);

  disconnect(*client);

  // Back on a new connection, saying it got as far as the second.
  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, sent[1]));
  const std::string body = drain(*returning);
  ASSERT_EQ(statusOf(body), 200) << body;

  EXPECT_EQ(occurrences(body, "notifications/1"), 0u) << body;
  EXPECT_EQ(occurrences(body, "notifications/2"), 0u)
      << "the event the client said it had was sent again";
  EXPECT_EQ(occurrences(body, "notifications/3"), 1u) << body;
  EXPECT_EQ(occurrences(body, "notifications/4"), 1u) << body;
  EXPECT_EQ(occurrences(body, "notifications/5"), 1u) << body;

  // Replayed under the ids they were first sent with, so the client's
  // place in that stream goes on meaning the same thing.
  const std::vector<std::string> replayed = eventIds(body);
  ASSERT_EQ(replayed.size(), 3u);
  EXPECT_EQ(replayed[0], sent[2]);
  EXPECT_EQ(replayed[2], sent[4]);

  // And it is a live stream afterwards, not just a recording.
  push(id, "{\"method\":\"notifications/later\"}");
  EXPECT_NE(drain(*returning).find("notifications/later"), std::string::npos);
}

TEST_F(StreamableHttpResumeTest, OnlyTheNamedStreamIsReplayed) {
  ServerOptions options;
  startServer(options);
  callbacks_.stream_answers = true;
  callbacks_.progress_before_answer = 2;

  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);
  push(id, "{\"method\":\"notifications/standalone\"}");
  const std::vector<std::string> standalone = eventIds(drain(*client));
  ASSERT_EQ(standalone.size(), 1u);

  // A request answered on a stream of its own, on another connection.
  Client* asking = connect();
  send(*asking, "POST", kSlowCall, "Mcp-Session-Id: " + id + "\r\n");
  const std::string answer = drain(*asking);
  ASSERT_EQ(statusOf(answer), 200) << answer;
  const std::vector<std::string> answering = eventIds(answer);
  ASSERT_FALSE(answering.empty()) << answer;
  EXPECT_NE(answering[0].substr(0, answering[0].rfind(':')),
            standalone[0].substr(0, standalone[0].rfind(':')))
      << "an answering stream and a standalone one shared a name";

  disconnect(*client);
  push(id, "{\"method\":\"notifications/afterwards\"}");

  // Resuming the standalone stream must not turn up the answer to
  // somebody's request, which is nobody else's business.
  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, standalone[0]));
  const std::string body = drain(*returning);
  ASSERT_EQ(statusOf(body), 200) << body;
  EXPECT_EQ(occurrences(body, "notifications/progress"), 0u)
      << "another stream's events were replayed: " << body;
  EXPECT_EQ(occurrences(body, "\"id\":7"), 0u)
      << "another request's answer was replayed: " << body;
  EXPECT_EQ(occurrences(body, "notifications/afterwards"), 1u) << body;
}

TEST_F(StreamableHttpResumeTest, ASeveredAnswerIsFinishedOnTheStreamThatAsks) {
  startServer();
  callbacks_.stream_answers = true;
  callbacks_.progress_before_answer = 2;
  callbacks_.answer_now = false;

  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  Client* asking = connect();
  send(*asking, "POST", kSlowCall, "Mcp-Session-Id: " + id + "\r\n");
  const std::string started = drain(*asking);
  ASSERT_EQ(statusOf(started), 200) << started;
  const std::vector<std::string> progress = eventIds(started);
  ASSERT_EQ(progress.size(), 2u) << started;

  // The client goes while the handler is still working. The work carries
  // on: a disconnect is not a cancellation.
  disconnect(*asking);

  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, progress[0]));
  const std::string replayed = drain(*returning);
  ASSERT_EQ(statusOf(replayed), 200) << replayed;
  EXPECT_EQ(occurrences(replayed, "notifications/progress"), 1u)
      << "what the client already had came back too: " << replayed;

  // Whatever the handler produces from here is owed to this stream now.
  executeInDispatcher([&]() {
    ASSERT_TRUE(callbacks_.held != nullptr);
    jsonrpc::Notification more;
    more.jsonrpc = "2.0";
    more.method = "notifications/nearly";
    callbacks_.held->sendNotification(more);
    callbacks_.held->sendResponse(callbacks_.pending_answer);
  });

  const std::string finished = drain(*returning);
  EXPECT_EQ(occurrences(finished, "notifications/nearly"), 1u) << finished;
  EXPECT_EQ(occurrences(finished, "\"id\":7"), 1u)
      << "the answer never reached the client that came back: " << finished;
}

// ── When there is nothing to give back ─────────────────────────────────────

TEST_F(StreamableHttpResumeTest, ACursorPastTheBufferGetsAFreshStream) {
  ServerOptions options;
  options.replay_events = 2;
  startServer(options);

  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);
  for (int i = 1; i <= 5; ++i) {
    push(id, "{\"method\":\"notifications/" + std::to_string(i) + "\"}");
  }
  const std::vector<std::string> sent = eventIds(drain(*client));
  ASSERT_EQ(sent.size(), 5u);
  disconnect(*client);

  // The first event is long gone from a buffer that keeps two. Replaying
  // from the top instead would hand back things the client already had.
  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, sent[0]));
  const std::string body = drain(*returning);

  EXPECT_EQ(statusOf(body), 200) << "resuming is optional, so this is no error";
  EXPECT_TRUE(eventIds(body).empty()) << body;
}

TEST_F(StreamableHttpResumeTest, AnIdThisServerCouldNotHaveIssuedIsHarmless) {
  startServer();
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, "not-an-id-at-all"));
  EXPECT_EQ(statusOf(readSomething(*returning)), 200);

  Client* invented = connect();
  send(*invented, "GET", "", sseHeaders(id, "deadbeef:9"));
  EXPECT_EQ(statusOf(readSomething(*invented)), 200);
}

TEST_F(StreamableHttpResumeTest, AStreamGivenUpOnIsNoLongerResumable) {
  ServerOptions options;
  options.retention = 150ms;
  startServer(options);

  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);
  push(id, "{\"method\":\"notifications/early\"}");
  const std::vector<std::string> sent = eventIds(drain(*client));
  ASSERT_EQ(sent.size(), 1u);

  disconnect(*client);
  // Long enough for the window to pass and a sweep to notice.
  std::this_thread::sleep_for(700ms);

  executeInDispatcher([&]() {
    auto* sessions = factory_->sessionManager();
    ASSERT_NE(sessions, nullptr);
    auto* session = sessions->find(id);
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(session->streams.empty()) << "a stream nobody came back for";
    EXPECT_TRUE(session->stream_index.empty());
    EXPECT_EQ(sessions->accounting()->events.load(), 0u);
  });

  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, sent[0]));
  const std::string body = drain(*returning);
  EXPECT_EQ(statusOf(body), 200) << body;
  EXPECT_EQ(occurrences(body, "notifications/early"), 0u) << body;
}

TEST_F(StreamableHttpResumeTest, WithoutResumabilityNoPromiseIsMade) {
  ServerOptions options;
  options.resumable = false;
  startServer(options);

  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);
  push(id, "{\"method\":\"notifications/one\"}");
  const std::string body = drain(*client);

  ASSERT_NE(body.find("notifications/one"), std::string::npos) << body;
  EXPECT_TRUE(eventIds(body).empty())
      << "an id was offered with nothing behind it: " << body;

  disconnect(*client);

  // And an id from somewhere is not believed either.
  Client* returning = connect();
  send(*returning, "GET", "", sseHeaders(id, "abcdef01:1"));
  const std::string resumed = drain(*returning);
  EXPECT_EQ(statusOf(resumed), 200) << resumed;
  EXPECT_EQ(occurrences(resumed, "notifications/one"), 0u) << resumed;
}

// ── What it costs ──────────────────────────────────────────────────────────

TEST_F(StreamableHttpResumeTest, WhatIsHeldStaysWithinItsBound) {
  ServerOptions options;
  options.replay_events = 4;
  startServer(options);

  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", sseHeaders(id));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);

  auto* sessions = factory_->sessionManager();
  ASSERT_NE(sessions, nullptr);

  for (int i = 0; i < 40; ++i) {
    push(id, "{\"method\":\"notifications/many\"}");
    // Checked as it goes rather than at the end: a bound that only holds
    // once the traffic stops is not a bound.
    EXPECT_LE(sessions->accounting()->events.load(), 4u) << "after " << i;
    EXPECT_LE(sessions->streamCount(), 1u) << "after " << i;
  }
  drain(*client);

  executeInDispatcher([&]() { sessions->remove(id); });
  EXPECT_EQ(sessions->accounting()->events.load(), 0u);
  EXPECT_EQ(sessions->accounting()->bytes.load(), 0u);
  EXPECT_EQ(sessions->streamCount(), 0u);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
