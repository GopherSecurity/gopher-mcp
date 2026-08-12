/**
 * @file test_streamable_http_filter.cc
 * @brief Tests for the MCP endpoint's request handling
 *
 * One HTTP request gets exactly one answer, whatever its body turned out to
 * contain. These tests drive a real HTTP codec with real request bytes and
 * read back what the filter decided to write, without a socket: the
 * interesting behaviour is the decision, and standing up a connection to
 * see it would only add a second thing that can fail.
 *
 * The 200 case is the exception worth explaining. An ordinary response is
 * written as a bare JSON body and framed by the codec on its way out
 * through the write path, which is not exercised here — so what these
 * tests see for a 200 is the unframed body. The framed bytes are asserted
 * at the wire level instead.
 */

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/streamable_http_filter.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/protocol/designated_params.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/protocol_versions.h"

namespace mcp {
namespace filter {
namespace {

/** A sink that appends to somewhere the test can still read afterwards. */
class RecordingSink : public transport::ExchangeSink {
 public:
  explicit RecordingSink(std::string& out) : out_(out) {}

  bool write(Buffer& data) override {
    const size_t length = data.length();
    if (length > 0) {
      out_.append(static_cast<const char*>(data.linearize(length)), length);
      data.drain(length);
    }
    return true;
  }

  bool alive() const override { return true; }

 private:
  std::string& out_;
};

class TestHost : public StreamableHttpFilter::Host {
 public:
  explicit TestHost(std::string& wire) : wire_(wire) {}

  transport::ExchangeSinkPtr makeSink() override {
    return transport::ExchangeSinkPtr(new RecordingSink(wire_));
  }
  network::Connection* connection() override { return nullptr; }
  bool requestIsHttp11() const override { return true; }
  const std::string& principal() const override { return principal_value; }
  http::ResponseWriter::HeaderList framedResponseHeaders() const override {
    return framed_headers;
  }
  http::ResponseWriter::Observer* streamObserver() override { return nullptr; }
  bool streamEndsConnection() const override { return false; }
  void holdInput(bool hold) override { input_held = hold; }

  std::string principal_value{"anonymous"};
  http::ResponseWriter::HeaderList framed_headers;
  bool input_held{false};

 private:
  std::string& wire_;
};

/** Everything the filter hands to the layer above it. */
class RecordingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests.push_back(request);
  }

  StreamingMode streamingFor(const jsonrpc::Request&) const override {
    return streaming;
  }

  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    requests.push_back(request);
    session_at_request = context.transportSessionId();
    if (filter != nullptr && filter->currentExchange()) {
      client_at_request = filter->currentExchange()->clientContext();
    }

    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    if (refuse_requests) {
      response.error = mcp::make_optional(
          Error(jsonrpc::INVALID_REQUEST, "unsupported protocol version"));
    } else if (result.isObject()) {
      response.result = mcp::make_optional(jsonrpc::ResponseResult(result));
    } else {
      response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
    }

    if (streaming == StreamingMode::None) {
      if (answer_requests) {
        context.sendResponse(response);
      }
      return;
    }

    // A streaming handler reports progress on the way to its answer, and
    // keeps its handle so the test can drive it after the dispatch returns.
    stream = context.beginResponseStream();
    if (!stream) {
      return;
    }
    for (size_t i = 0; i < progress_count; ++i) {
      jsonrpc::Notification progress;
      progress.jsonrpc = "2.0";
      progress.method = "notifications/progress";
      stream->sendNotification(progress);
    }
    if (answer_requests) {
      stream->sendResponse(response);
    }
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    notifications.push_back(notification);
  }

  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext& context) override {
    (void)context;
    notifications.push_back(notification);
  }

  void onResponse(const jsonrpc::Response& response) override {
    responses.push_back(response);
  }

  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  bool knowsMethod(const std::string& method) const override {
    // Empty means "no opinion", which is what a receiver that cannot say
    // answers — and it must not be read as refusing everything.
    return known_methods.empty() ||
           std::find(known_methods.begin(), known_methods.end(), method) !=
               known_methods.end();
  }

  std::vector<std::string> known_methods;

  // Set by the fixture so a test can see what the exchange recorded about
  // the peer while the message was still being dispatched.
  StreamableHttpFilter* filter{nullptr};
  transport::ExchangeClientContext client_at_request;

  bool answer_requests{true};
  // Answers with an error instead of a result, as a server refusing to
  // initialize a client it cannot serve would.
  bool refuse_requests{false};
  // What the answer carries, for tests that read something back out of it.
  json::JsonValue result;
  StreamingMode streaming{StreamingMode::None};
  size_t progress_count{0};
  // Kept past the dispatch on purpose: that is what the handle is for.
  ResponseStreamPtr stream;
  std::vector<jsonrpc::Request> requests;
  std::vector<jsonrpc::Notification> notifications;
  std::vector<jsonrpc::Response> responses;
  std::string session_at_request{"<never dispatched>"};
};

/** Stands in for the filter that serves everything this one does not. */
class RecordingFallback : public HttpCodecFilter::MessageCallbacks {
 public:
  void onHeaders(const std::map<std::string, std::string>&, bool) override {
    ++header_count;
  }
  void onBody(const std::string& data, bool) override { body += data; }
  void onMessageComplete() override { ++complete_count; }
  void onError(const std::string&) override {}

  size_t header_count{0};
  size_t complete_count{0};
  std::string body;
};

class StreamableHttpFilterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    factory_ = event::createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("streamable_filter_test");
    // Run non-blocking on this thread so this thread is the dispatcher
    // thread and the exchange's affinity assertions hold.
    dispatcher_->run(event::RunType::NonBlock);

    host_.reset(new TestHost(wire_));
    exchanges_.reset(new transport::ExchangeRegistry(*dispatcher_));
    buildFilter(StreamableHttpOptions());
  }

  void TearDown() override {
    codec_.reset();
    filter_.reset();
    sessions_.reset();
    exchanges_.reset();
    host_.reset();
    dispatcher_.reset();
    factory_.reset();
  }

  void buildFilter(const StreamableHttpOptions& options) {
    filter_.reset(new StreamableHttpFilter(*dispatcher_, callbacks_, fallback_,
                                           *exchanges_, *host_, "/mcp",
                                           options));
    codec_.reset(new HttpCodecFilter(*filter_, *dispatcher_,
                                     /*is_server=*/true));
    codec_->onNewConnection();
    callbacks_.filter = filter_.get();
  }

  /** Rebuild the filter as a server that keeps sessions. */
  void keepSessions(bool require_principal_match = true) {
    sessions_ =
        std::make_shared<transport::StreamableSessionManager>(*dispatcher_);
    sessions_options_ = StreamableHttpOptions();
    sessions_options_.sessions = sessions_.get();
    sessions_options_.require_principal_match = require_principal_match;
    buildFilter(sessions_options_);
  }

  StreamableHttpOptions sessions_options_;

  /** An endpoint that serves the revision with no handshake as well. */
  void serveModernEra() {
    StreamableHttpOptions options;
    options.enable_modern_era = true;
    options.protocol_versions = {protocol::kProtocolVersion20260728,
                                 protocol::kProtocolVersion20251125,
                                 protocol::kProtocolVersion20250618};
    buildFilter(options);
  }

  /** The headers a modern request must carry to be served at all. */
  static std::string modernHeaders(const std::string& method,
                                   const std::string& name = "") {
    std::string headers =
        std::string(protocol::modern::kProtocolVersionHeader) + ": " +
        protocol::kProtocolVersion20260728 + "\r\n" +
        protocol::modern::kMethodHeader + ": " + method + "\r\n";
    if (!name.empty()) {
      headers +=
          std::string(protocol::modern::kNameHeader) + ": " + name + "\r\n";
    }
    return headers;
  }

  /** The `_meta` a modern request carries, with or without a caller. */
  static std::string modernMeta(bool with_client_info = true) {
    std::string meta = std::string("\"_meta\":{\"") +
                       protocol::modern::kMetaProtocolVersion + "\":\"" +
                       protocol::kProtocolVersion20260728 + "\"";
    if (with_client_info) {
      meta += std::string(",\"") + protocol::modern::kMetaClientInfo +
              "\":{\"name\":\"ExampleClient\",\"version\":\"1.0.0\"}";
    }
    meta += std::string(",\"") + protocol::modern::kMetaClientCapabilities +
            "\":{\"roots\":{}}}";
    return meta;
  }

  /** The session id the last answer handed back, if it handed one back. */
  std::string sessionIdOnTheWire() const {
    const std::string name = "\r\nMcp-Session-Id: ";
    const size_t at = wire_.find(name);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t start = at + name.size();
    const size_t end = wire_.find("\r\n", start);
    return wire_.substr(start, end - start);
  }

  static std::string post(const std::string& path,
                          const std::string& body,
                          const std::string& extra_headers = "") {
    return "POST " + path +
           " HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n" +
           extra_headers + "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
  }

  static std::string get(const std::string& path,
                         const std::string& extra_headers = "") {
    return "GET " + path +
           " HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Accept: text/event-stream\r\n" +
           extra_headers + "Content-Length: 0\r\n\r\n";
  }

  static std::string del(const std::string& path,
                         const std::string& extra_headers = "") {
    return "DELETE " + path +
           " HTTP/1.1\r\n"
           "Host: localhost\r\n" +
           extra_headers + "Content-Length: 0\r\n\r\n";
  }

  /** Drive the dispatcher on this thread so timers actually fire. */
  void runFor(std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void feed(const std::string& bytes) {
    OwnedBuffer buffer;
    buffer.add(bytes);
    codec_->onData(buffer, false);
  }

  std::string wire_;
  RecordingCallbacks callbacks_;
  RecordingFallback fallback_;
  event::DispatcherFactoryPtr factory_;
  event::DispatcherPtr dispatcher_;
  std::unique_ptr<TestHost> host_;
  std::unique_ptr<transport::ExchangeRegistry> exchanges_;
  // Shared, because a session visit that hops threads holds the
  // manager for the length of the visit and can only do that if it is
  // something a reference can be held to.
  std::shared_ptr<transport::StreamableSessionManager> sessions_;
  std::unique_ptr<StreamableHttpFilter> filter_;
  std::unique_ptr<HttpCodecFilter> codec_;
};

const char kRequestBody[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
const char kNotificationBody[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";

// ── Requests ───────────────────────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, ARequestIsAnsweredWithTheHandlersResponse) {
  feed(post("/mcp", kRequestBody));

  ASSERT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(callbacks_.requests[0].method, "initialize");

  // Bare JSON: the codec frames it on the way out, which is not part of
  // this test.
  EXPECT_EQ(wire_.find("HTTP/1.1"), std::string::npos) << wire_;
  EXPECT_EQ(wire_.find('{'), 0u) << wire_;
  EXPECT_NE(wire_.find("\"id\":1"), std::string::npos) << wire_;
}

TEST_F(StreamableHttpFilterTest, AHandlerThatSaysNothingWritesNothing) {
  // A handler is entitled to answer later. Answering for it would put a
  // second response on a request it is still working on.
  callbacks_.answer_requests = false;

  feed(post("/mcp", kRequestBody));

  EXPECT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_TRUE(wire_.empty()) << wire_;
}

// What an offered session id means now depends on whether the server keeps
// any; both cases are under "Sessions" below.

TEST_F(StreamableHttpFilterTest, WithoutASessionHeaderTheBindingIsEmpty) {
  feed(post("/mcp", kRequestBody));

  EXPECT_EQ(callbacks_.session_at_request, "");
}

TEST_F(StreamableHttpFilterTest, WhatThePeerAcceptsIsRecordedOnTheExchange) {
  // Recorded, not acted on: the choice between a streamed and an unstreamed
  // response is made before the first byte and cannot be taken back, so
  // whoever makes it needs this in front of them.
  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));

  EXPECT_FALSE(callbacks_.client_at_request.accepts_json);
  EXPECT_TRUE(callbacks_.client_at_request.accepts_sse);
}

TEST_F(StreamableHttpFilterTest, AWildcardAcceptsEverything) {
  feed(post("/mcp", kRequestBody, "Accept: */*\r\n"));

  EXPECT_TRUE(callbacks_.client_at_request.accepts_json);
  EXPECT_TRUE(callbacks_.client_at_request.accepts_sse);
}

TEST_F(StreamableHttpFilterTest, NoAcceptHeaderMeansAnything) {
  feed(post("/mcp", kRequestBody));

  EXPECT_TRUE(callbacks_.client_at_request.accepts_json);
  EXPECT_TRUE(callbacks_.client_at_request.accepts_sse);
}

TEST_F(StreamableHttpFilterTest, TheProtocolVersionHeaderIsRecordedToo) {
  feed(post("/mcp", kRequestBody, "Mcp-Protocol-Version: 2025-06-18\r\n"));

  EXPECT_EQ(callbacks_.client_at_request.protocol_version, "2025-06-18");
}

TEST_F(StreamableHttpFilterTest, WhoTheRequestIsFromIsRecordedToo) {
  host_->principal_value = "alice";

  feed(post("/mcp", kRequestBody));

  // Carried on the exchange rather than left in the headers, because a
  // session is bound to the caller who created it and by the time that
  // matters the request's headers are gone.
  EXPECT_EQ(callbacks_.client_at_request.principal, "alice");
}

// ── Nothing to answer with ─────────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, ANotificationIsAcceptedWithAnEmptyBody) {
  feed(post("/mcp", kNotificationBody));

  ASSERT_EQ(callbacks_.notifications.size(), 1u);
  EXPECT_EQ(callbacks_.notifications[0].method, "notifications/initialized");

  EXPECT_EQ(wire_.find("HTTP/1.1 202 Accepted\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("\r\nContent-Length: 0\r\n"), std::string::npos)
      << wire_;
  ASSERT_GE(wire_.size(), 4u);
  EXPECT_EQ(wire_.substr(wire_.size() - 4), "\r\n\r\n")
      << "202 must carry no body: " << wire_;
}

TEST_F(StreamableHttpFilterTest, AResponseFromTheClientIsAlsoAccepted) {
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}"));

  EXPECT_EQ(callbacks_.responses.size(), 1u);
  EXPECT_EQ(wire_.find("HTTP/1.1 202 Accepted\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, ExactlyOneAnswerPerNotificationRequest) {
  // The older filter decided this per JSON-RPC message; here there is one
  // decision point per HTTP request, so there is nowhere for a second
  // answer to come from.
  feed(post("/mcp", kNotificationBody));

  size_t answers = 0;
  size_t at = wire_.find("HTTP/1.1");
  while (at != std::string::npos) {
    ++answers;
    at = wire_.find("HTTP/1.1", at + 1);
  }
  EXPECT_EQ(answers, 1u) << wire_;
}

// ── Bodies that cannot be served ───────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, AMalformedBodyIsRejectedWithAnIdLessError) {
  feed(post("/mcp", "not json at all"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("\r\nContent-Type: application/json\r\n"),
            std::string::npos)
      << wire_;
  EXPECT_NE(wire_.find("\"id\":null"), std::string::npos) << wire_;
  EXPECT_NE(wire_.find("\"code\":-32700"), std::string::npos) << wire_;
  EXPECT_TRUE(callbacks_.requests.empty());
}

TEST_F(StreamableHttpFilterTest, TwoMessagesInOneBodyRunNeitherOfThem) {
  // One HTTP response cannot answer two messages, so the pair is refused
  // before either reaches a handler.
  feed(post("/mcp", std::string(kRequestBody) + "\n" + kRequestBody));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  EXPECT_TRUE(callbacks_.requests.empty())
      << "the first message must not run before the second is rejected";
}

TEST_F(StreamableHttpFilterTest, ABatchIsRefusedAsAnInvalidRequest) {
  feed(post("/mcp", std::string("[") + kRequestBody + "]"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("\"code\":-32600"), std::string::npos) << wire_;
  EXPECT_TRUE(callbacks_.requests.empty());
}

TEST_F(StreamableHttpFilterTest, AnEmptyBodyIsRejected) {
  feed(post("/mcp", ""));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, AJsonDocumentThatIsNotAMessageIsRejected) {
  feed(post("/mcp", "{\"hello\":\"world\"}"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("\"id\":null"), std::string::npos) << wire_;
}

// ── What this filter does not serve ────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, AnotherPathGoesToTheFilterBehind) {
  feed(post("/rpc", kRequestBody));

  EXPECT_EQ(fallback_.body, kRequestBody);
  EXPECT_EQ(fallback_.complete_count, 1u);
  EXPECT_TRUE(callbacks_.requests.empty());
  EXPECT_TRUE(wire_.empty()) << wire_;
}

TEST_F(StreamableHttpFilterTest, AnotherMethodOnTheSamePathGoesBehindToo) {
  feed(
      "GET /mcp HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n");

  EXPECT_EQ(fallback_.header_count, 1u);
  EXPECT_EQ(fallback_.complete_count, 1u);
  EXPECT_TRUE(wire_.empty()) << wire_;
}

TEST_F(StreamableHttpFilterTest, HeadersAreSeenByTheFilterBehindRegardless) {
  // The filter behind keeps per-connection bookkeeping that every request
  // contributes to; hiding some of them would leave it describing half the
  // traffic.
  feed(post("/mcp", kRequestBody));

  EXPECT_EQ(fallback_.header_count, 1u);
  EXPECT_TRUE(fallback_.body.empty())
      << "a body this filter serves must not also reach the one behind it";
  EXPECT_EQ(fallback_.complete_count, 0u);
}

// ── Keep-alive ─────────────────────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, SequentialRequestsOnOneConnectionBothAnswer) {
  feed(post("/mcp", kRequestBody));
  const size_t after_first = wire_.size();
  ASSERT_GT(after_first, 0u);

  feed(post("/mcp", kNotificationBody));

  EXPECT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(callbacks_.notifications.size(), 1u);
  EXPECT_NE(wire_.find("HTTP/1.1 202 Accepted", after_first), std::string::npos)
      << wire_;
}

TEST_F(StreamableHttpFilterTest, ARequestAfterOneServedElsewhereStillWorks) {
  feed(post("/rpc", kRequestBody));
  ASSERT_TRUE(wire_.empty());

  feed(post("/mcp", kNotificationBody));

  EXPECT_EQ(wire_.find("HTTP/1.1 202 Accepted\r\n"), 0u) << wire_;
}

// ── Streamed answers ───────────────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, AStreamingHandlerAnswersWithAnEventStream) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 3;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 200 OK\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("Content-Type: text/event-stream"), std::string::npos)
      << wire_;
  EXPECT_NE(wire_.find("Transfer-Encoding: chunked"), std::string::npos)
      << "a stream with no chunk framing is an empty body: " << wire_;

  size_t progress = 0;
  for (size_t at = wire_.find("notifications/progress");
       at != std::string::npos;
       at = wire_.find("notifications/progress", at + 1)) {
    ++progress;
  }
  EXPECT_EQ(progress, 3u) << wire_;

  // The response is the last thing on the stream, and the terminating
  // chunk is what tells the client the body ended.
  const size_t response_at = wire_.find("\"result\"");
  ASSERT_NE(response_at, std::string::npos) << wire_;
  EXPECT_GT(response_at, wire_.rfind("notifications/progress")) << wire_;
  EXPECT_NE(wire_.rfind("0\r\n\r\n"), std::string::npos) << wire_;
}

TEST_F(StreamableHttpFilterTest, AStreamedEventCarriesNoIdYet) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 1;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));

  // Nothing can resume from an id yet, so none is promised.
  EXPECT_EQ(wire_.find("\nid: "), std::string::npos) << wire_;
}

TEST_F(StreamableHttpFilterTest, AHandlerThatOnlyAnswersStaysUnary) {
  // Asking for a stream and then never using it before the response costs
  // nothing: the fast path is still the fast path.
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 0;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1"), std::string::npos) << wire_;
  EXPECT_EQ(wire_.find('{'), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, ProgressIsDroppedForAClientThatCannotReadIt) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 3;

  feed(post("/mcp", kRequestBody, "Accept: application/json\r\n"));

  // Answerable without the progress, so it is answered — the progress is
  // simply not sent.
  EXPECT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(wire_.find("HTTP/1.1"), std::string::npos) << wire_;
  EXPECT_EQ(wire_.find("notifications/progress"), std::string::npos) << wire_;
  EXPECT_NE(wire_.find("\"result\""), std::string::npos) << wire_;
}

TEST_F(StreamableHttpFilterTest, ARequiredStreamIsRefusedBeforeTheHandlerRuns) {
  callbacks_.streaming = StreamingMode::Required;

  feed(post("/mcp", kRequestBody, "Accept: application/json\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 406 Not Acceptable\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("text/event-stream"), std::string::npos) << wire_;
  // Running it would leave it waiting on a question the client can never
  // be shown.
  EXPECT_TRUE(callbacks_.requests.empty());
}

TEST_F(StreamableHttpFilterTest, ARequiredStreamOpensBeforeTheHandlerRuns) {
  callbacks_.streaming = StreamingMode::Required;
  callbacks_.progress_count = 0;
  callbacks_.answer_requests = false;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));

  // The handler has said nothing at all, and the response headers are
  // already out: it can be asked something the moment it wants to be.
  EXPECT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(wire_.find("HTTP/1.1 200 OK\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("Content-Type: text/event-stream"), std::string::npos)
      << wire_;
}

TEST_F(StreamableHttpFilterTest, AHandlerKeepsItsStreamAfterTheDispatchEnds) {
  callbacks_.streaming = StreamingMode::Required;
  callbacks_.answer_requests = false;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));
  ASSERT_TRUE(callbacks_.stream);
  const size_t after_dispatch = wire_.size();

  // The dispatch is over and the handler is still producing.
  jsonrpc::Notification progress;
  progress.jsonrpc = "2.0";
  progress.method = "notifications/progress";
  ASSERT_FALSE(
      holds_alternative<Error>(callbacks_.stream->sendNotification(progress)));

  jsonrpc::Response response;
  response.jsonrpc = "2.0";
  response.id = RequestId(static_cast<int64_t>(1));
  response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
  ASSERT_FALSE(
      holds_alternative<Error>(callbacks_.stream->sendResponse(response)));

  const std::string tail = wire_.substr(after_dispatch);
  EXPECT_NE(tail.find("notifications/progress"), std::string::npos) << tail;
  EXPECT_NE(tail.find("\"result\""), std::string::npos) << tail;
  EXPECT_FALSE(callbacks_.stream->alive())
      << "the response was the last thing the stream had to say";
}

// ── The era with no handshake ──────────────────────────────────────────

// With no introduction, everything a server would have learned from one
// arrives on every request instead. What it says about itself has to
// reach the exchange, or nothing downstream can gate on it.
TEST_F(StreamableHttpFilterTest, AModernRequestSaysWhoIsCallingOnEveryOne) {
  serveModernEra();

  const std::string body = std::string(
                               "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                               "\"tools/list\",\"params\":{") +
                           modernMeta() + "}}";
  feed(post("/mcp", body, modernHeaders("tools/list")));

  ASSERT_EQ(callbacks_.requests.size(), 1u) << wire_;
  const auto& client = callbacks_.client_at_request;
  EXPECT_EQ(client.era, transport::ProtocolEra::Modern);
  EXPECT_EQ(client.protocol_version, protocol::kProtocolVersion20260728);

  ASSERT_TRUE(client.client_info.has_value())
      << "the caller named itself and the exchange did not hear it";
  EXPECT_NE(client.client_info->find("ExampleClient"), std::string::npos);
  ASSERT_TRUE(client.client_capabilities.has_value())
      << "what the caller can do never reached the exchange";
  EXPECT_NE(client.client_capabilities->find("roots"), std::string::npos);
}

// Naming yourself is optional, and a request that does not is served like
// any other. Only the version decides whether it can be served at all.
TEST_F(StreamableHttpFilterTest, AModernRequestNeedNotSayWhoIsCalling) {
  serveModernEra();

  const std::string body = std::string(
                               "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                               "\"tools/list\",\"params\":{") +
                           modernMeta(/*with_client_info=*/false) + "}}";
  feed(post("/mcp", body, modernHeaders("tools/list")));

  ASSERT_EQ(callbacks_.requests.size(), 1u)
      << "a request that did not name its caller was refused: " << wire_;
  const auto& client = callbacks_.client_at_request;
  EXPECT_EQ(client.era, transport::ProtocolEra::Modern);
  EXPECT_FALSE(client.client_info.has_value())
      << "a caller that said nothing about itself was reported as having";
  EXPECT_TRUE(client.client_capabilities.has_value());
}

// And a request from the older era is untouched by any of it.
TEST_F(StreamableHttpFilterTest, AClassicRequestIsStillClassic) {
  serveModernEra();

  feed(post("/mcp", kRequestBody,
            std::string("MCP-Protocol-Version: ") +
                protocol::kProtocolVersion20250618 + "\r\n"));

  ASSERT_EQ(callbacks_.requests.size(), 1u) << wire_;
  const auto& client = callbacks_.client_at_request;
  EXPECT_EQ(client.era, transport::ProtocolEra::Classic);
  EXPECT_FALSE(client.client_info.has_value());
  EXPECT_FALSE(client.client_capabilities.has_value());
}

// A body declaring the newest revision is a modern request whatever its
// headers say — so a modern request that forgot them is refused as one,
// rather than quietly served by the older rules it is not following.
TEST_F(StreamableHttpFilterTest,
       AModernBodyWithoutItsHeadersIsRefusedAsModern) {
  serveModernEra();

  const std::string body = std::string(
                               "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                               "\"tools/list\",\"params\":{") +
                           modernMeta() + "}}";
  feed(post("/mcp", body));

  EXPECT_TRUE(callbacks_.requests.empty())
      << "a request missing the headers it must carry was served";
  EXPECT_EQ(wire_.find("HTTP/1.1 400 "), 0u) << wire_;
  EXPECT_NE(wire_.find(std::to_string(protocol::modern::kHeaderMismatch)),
            std::string::npos)
      << wire_;
}

// The headers exist so that something between the two ends can route on
// them without parsing the body. That is only safe if the two agree, so a
// disagreement is a refusal rather than a preference — otherwise a router
// and a server act on different values for the same request.
TEST_F(StreamableHttpFilterTest, HeadersThatDisagreeWithTheBodyAreRefused) {
  serveModernEra();

  struct Row {
    const char* headers;
    const char* body_method;
    const char* why;
  };
  const Row rows[] = {
      {"MCP-Protocol-Version: 2025-11-25\r\nMcp-Method: tools/list\r\n",
       "tools/list", "the header names a different revision than the body"},
      {"MCP-Protocol-Version: 2026-07-28\r\n", "tools/list",
       "no Mcp-Method at all"},
      {"MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: tools/call\r\n",
       "tools/list", "Mcp-Method names a different method than the body"},
  };

  for (const auto& row : rows) {
    wire_.clear();
    callbacks_.requests.clear();
    const std::string body = std::string("{\"jsonrpc\":\"2.0\",\"id\":1,") +
                             "\"method\":\"" + row.body_method +
                             "\",\"params\":{" + modernMeta() + "}}";
    feed(post("/mcp", body, row.headers));

    EXPECT_TRUE(callbacks_.requests.empty()) << row.why << ": " << wire_;
    EXPECT_EQ(wire_.find("HTTP/1.1 400 "), 0u) << row.why << ": " << wire_;
    EXPECT_NE(wire_.find(std::to_string(protocol::modern::kHeaderMismatch)),
              std::string::npos)
        << row.why << ": " << wire_;
  }
}

// The three methods that name what they are about carry that name in a
// header too, and the server compares the decoded header against the body
// — so a name in another script travels encoded and still matches.
TEST_F(StreamableHttpFilterTest, TheNameHeaderIsComparedAgainstTheBody) {
  serveModernEra();

  const auto call = [this](const std::string& tool,
                           const std::string& name_header) {
    wire_.clear();
    callbacks_.requests.clear();
    const std::string body =
        std::string(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",") +
        "\"params\":{\"name\":\"" + tool + "\"," + modernMeta() + "}}";
    feed(post("/mcp", body, modernHeaders("tools/call", name_header)));
  };

  call("get_weather", "get_weather");
  EXPECT_EQ(callbacks_.requests.size(), 1u) << wire_;

  call("get_weather", "get_forecast");
  EXPECT_TRUE(callbacks_.requests.empty()) << "a mismatched name was served";
  EXPECT_EQ(wire_.find("HTTP/1.1 400 "), 0u) << wire_;

  call("get_weather", "");
  EXPECT_TRUE(callbacks_.requests.empty()) << "a missing name was served";

  // A tool named outside the header-safe set travels encoded, and the
  // server decodes before it compares.
  call("\xe5\xa4\xa9\xe6\xb0\x97", "=?base64?5aSp5rCX?=");
  EXPECT_EQ(callbacks_.requests.size(), 1u)
      << "an encoded name was not decoded before comparison: " << wire_;
}

// A version this server does not serve is refused in the shape its own
// era reads: a code of its own, and the list as data rather than prose,
// so a client can pick from it without parsing a sentence.
TEST_F(StreamableHttpFilterTest, AnUnservedRevisionIsRefusedWithTheList) {
  StreamableHttpOptions options;
  options.enable_modern_era = true;
  options.protocol_versions = {protocol::kProtocolVersion20260728,
                               protocol::kProtocolVersion20251125};
  buildFilter(options);

  const std::string body =
      std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",") +
      "\"params\":{\"_meta\":{\"" + protocol::modern::kMetaProtocolVersion +
      "\":\"2030-01-01\"}}}";
  feed(post("/mcp", body,
            "MCP-Protocol-Version: 2030-01-01\r\nMcp-Method: tools/list\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 "), 0u) << wire_;
  EXPECT_NE(
      wire_.find(std::to_string(protocol::modern::kUnsupportedProtocolVersion)),
      std::string::npos)
      << wire_;
  EXPECT_NE(wire_.find("\"supported\""), std::string::npos)
      << "the refusal did not say what is served: " << wire_;
  EXPECT_NE(wire_.find(protocol::kProtocolVersion20251125), std::string::npos)
      << wire_;
  EXPECT_NE(wire_.find("\"requested\":\"2030-01-01\""), std::string::npos)
      << wire_;
}

// A method this server does not have is a 404 carrying a JSON-RPC error,
// which is the only thing distinguishing a server that is there and has
// no such method from a URL that is not an endpoint at all.
TEST_F(StreamableHttpFilterTest, AMethodThisServerHasNotIsNotFound) {
  serveModernEra();
  callbacks_.known_methods = {"tools/list"};

  const std::string body = std::string(
                               "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                               "\"tools/invent\",\"params\":{") +
                           modernMeta() + "}}";
  feed(post("/mcp", body, modernHeaders("tools/invent")));

  EXPECT_TRUE(callbacks_.requests.empty()) << wire_;
  EXPECT_EQ(wire_.find("HTTP/1.1 404 "), 0u) << wire_;
  EXPECT_NE(wire_.find(std::to_string(protocol::modern::kMethodNotFound)),
            std::string::npos)
      << wire_;
}

// Every success in the newest revision says what kind of result it is,
// and an ordinary one is complete. Stamped by the transport rather than
// asked of each handler, since which era a request belongs to is not a
// handler's business.
TEST_F(StreamableHttpFilterTest, EveryModernAnswerSaysItIsComplete) {
  serveModernEra();

  const std::string body = std::string(
                               "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                               "\"tools/list\",\"params\":{") +
                           modernMeta() + "}}";
  feed(post("/mcp", body, modernHeaders("tools/list")));

  EXPECT_NE(wire_.find("\"resultType\":\"complete\""), std::string::npos)
      << wire_;
}

// And an older peer is handed no such thing: it has no field for it and
// would be given one it does not expect.
TEST_F(StreamableHttpFilterTest, AClassicAnswerSaysNoSuchThing) {
  serveModernEra();

  feed(post("/mcp", kRequestBody,
            std::string("MCP-Protocol-Version: ") +
                protocol::kProtocolVersion20250618 + "\r\n"));

  EXPECT_EQ(wire_.find("resultType"), std::string::npos) << wire_;
}

// The newest revision serves POST alone. A GET from such a caller is
// refused even where a stream is served, because what it may send is
// decided by its own era rather than by what the endpoint happens to
// offer everyone else.
TEST_F(StreamableHttpFilterTest, AModernCallerMaySendOnlyPost) {
  keepSessions();
  StreamableHttpOptions options = sessions_options_;
  options.enable_modern_era = true;
  options.protocol_versions = {protocol::kProtocolVersion20260728,
                               protocol::kProtocolVersion20250618};
  buildFilter(options);

  feed(get("/mcp", std::string("MCP-Protocol-Version: ") +
                       protocol::kProtocolVersion20260728 + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 405 "), 0u) << wire_;
  EXPECT_NE(wire_.find("Allow: POST\r\n"), std::string::npos)
      << "a caller was told about methods its revision does not have: "
      << wire_;
}

// While a caller from the older era, on the same server, is served the
// stream it asked for.
TEST_F(StreamableHttpFilterTest, AClassicCallerStillOpensAStream) {
  keepSessions();
  StreamableHttpOptions options = sessions_options_;
  options.enable_modern_era = true;
  options.protocol_versions = {protocol::kProtocolVersion20260728,
                               protocol::kProtocolVersion20250618};
  buildFilter(options);

  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 200 OK\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("text/event-stream"), std::string::npos) << wire_;
}

// A session id from an older client names nothing here, and must not be
// allowed to name something: this revision has no sessions, so one is
// neither honoured nor minted.
TEST_F(StreamableHttpFilterTest, ASessionIdMeansNothingToAModernRequest) {
  keepSessions();
  StreamableHttpOptions options = sessions_options_;
  options.enable_modern_era = true;
  options.protocol_versions = {protocol::kProtocolVersion20260728,
                               protocol::kProtocolVersion20250618};
  buildFilter(options);

  const std::string body =
      std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",") +
      "\"params\":{" + modernMeta() + "}}";
  feed(post("/mcp", body,
            modernHeaders("initialize") +
                "Mcp-Session-Id: a-session-from-somewhere-else\r\n"));

  EXPECT_TRUE(sessionIdOnTheWire().empty())
      << "a session was named to a revision that has none: " << wire_;
  EXPECT_EQ(callbacks_.session_at_request, "")
      << "an offered session id was believed";
}

/** A tool or two that designate an argument to be carried in a header. */
class TestDesignations : public protocol::modern::DesignatedParamLookup {
 public:
  bool paramsForTool(
      const std::string& tool_name,
      std::vector<protocol::modern::DesignatedParam>* out) const override {
    if (tool_name != "execute_sql") {
      return false;
    }
    protocol::modern::DesignatedParam region;
    region.header_name = "Region";
    region.path = {"region"};
    *out = {region};
    return true;
  }
};

// The mirroring is only worth anything while the header and the body
// agree, so both halves are held to: a header that disagrees is refused,
// and so is a body carrying a value whose header was left out. The second
// is the half that is easy to miss, and it is the one where an
// intermediary routing on the header saw nothing at all.
TEST_F(StreamableHttpFilterTest, ADesignatedArgumentMustMatchItsHeader) {
  TestDesignations designations;
  StreamableHttpOptions options;
  options.enable_modern_era = true;
  options.protocol_versions = {protocol::kProtocolVersion20260728};
  options.designated_params = &designations;
  buildFilter(options);

  const auto call = [this](const std::string& arguments,
                           const std::string& extra_headers) {
    wire_.clear();
    callbacks_.requests.clear();
    const std::string body =
        std::string(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",") +
        "\"params\":{\"name\":\"execute_sql\",\"arguments\":" + arguments +
        "," + modernMeta() + "}}";
    feed(post("/mcp", body,
              modernHeaders("tools/call", "execute_sql") + extra_headers));
  };

  call(R"({"region":"us-west1"})", "Mcp-Param-Region: us-west1\r\n");
  EXPECT_EQ(callbacks_.requests.size(), 1u)
      << "a call whose header matched its body was refused: " << wire_;

  call(R"({"region":"us-west1"})", "Mcp-Param-Region: us-east1\r\n");
  EXPECT_TRUE(callbacks_.requests.empty()) << "a mismatched header was served";
  EXPECT_EQ(wire_.find("HTTP/1.1 400 "), 0u) << wire_;

  call(R"({"region":"us-west1"})", "");
  EXPECT_TRUE(callbacks_.requests.empty())
      << "a value in the body with no header beside it was served, so "
         "anything routing on that header saw nothing";
  EXPECT_EQ(wire_.find("HTTP/1.1 400 "), 0u) << wire_;

  // An argument that was not given needs no header, and must not have
  // one: a header naming a value the call does not carry is one an
  // intermediary could act on and this server never would.
  call(R"({"query":"SELECT 1"})", "");
  EXPECT_EQ(callbacks_.requests.size(), 1u)
      << "a call that designated nothing was refused: " << wire_;

  call(R"({"query":"SELECT 1"})", "Mcp-Param-Region: us-west1\r\n");
  EXPECT_TRUE(callbacks_.requests.empty())
      << "a header was accepted for an argument the call never sent";
}

// A tool this server does not have is not this check's business: the
// layer that owns the tools answers that, and refusing here would refuse
// it first and for the wrong reason.
TEST_F(StreamableHttpFilterTest, AToolWithNoDesignationsIsLeftAlone) {
  TestDesignations designations;
  StreamableHttpOptions options;
  options.enable_modern_era = true;
  options.protocol_versions = {protocol::kProtocolVersion20260728};
  options.designated_params = &designations;
  buildFilter(options);

  const std::string body =
      std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",") +
      "\"params\":{\"name\":\"something_else\",\"arguments\":{\"region\":\"x\"}"
      "," +
      modernMeta() + "}}";
  feed(post("/mcp", body, modernHeaders("tools/call", "something_else")));

  EXPECT_EQ(callbacks_.requests.size(), 1u) << wire_;
}

// A server that needs something from the client mid-request asks on the
// stream the answer will arrive on, because that is where the client is
// already listening.
TEST_F(StreamableHttpFilterTest, AQuestionForTheClientGoesDownTheSameStream) {
  callbacks_.streaming = StreamingMode::Required;
  callbacks_.answer_requests = false;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));
  ASSERT_TRUE(callbacks_.stream);
  const size_t after_dispatch = wire_.size();

  jsonrpc::Request question;
  question.jsonrpc = "2.0";
  question.id = RequestId(std::string("ask-1"));
  question.method = "sampling/createMessage";
  ASSERT_FALSE(
      holds_alternative<Error>(callbacks_.stream->sendRequest(question)));

  const std::string tail = wire_.substr(after_dispatch);
  EXPECT_NE(tail.find("sampling/createMessage"), std::string::npos) << tail;
  EXPECT_NE(tail.find("ask-1"), std::string::npos)
      << "the question carries the id its answer will come back under: "
      << tail;
  EXPECT_TRUE(callbacks_.stream->alive())
      << "asking a question is not the end of the answer";
}

// Progress a client cannot read is dropped and the request carries on.
// A question is not: the handler is waiting for an answer that would now
// never arrive, so it has to be told.
TEST_F(StreamableHttpFilterTest, AQuestionWithNowhereToGoIsReported) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.answer_requests = false;

  feed(post("/mcp", kRequestBody, "Accept: application/json\r\n"));
  ASSERT_TRUE(callbacks_.stream);

  jsonrpc::Notification progress;
  progress.jsonrpc = "2.0";
  progress.method = "notifications/progress";
  EXPECT_FALSE(
      holds_alternative<Error>(callbacks_.stream->sendNotification(progress)))
      << "progress a client cannot read is not a failed request";

  jsonrpc::Request question;
  question.jsonrpc = "2.0";
  question.id = RequestId(std::string("ask-1"));
  question.method = "sampling/createMessage";
  EXPECT_TRUE(
      holds_alternative<Error>(callbacks_.stream->sendRequest(question)))
      << "a question that can never be shown was reported as asked";
}

TEST_F(StreamableHttpFilterTest, ANotificationIsNeverAnsweredWithAStream) {
  callbacks_.streaming = StreamingMode::Required;

  feed(post("/mcp", kNotificationBody, "Accept: text/event-stream\r\n"));

  // There is no request here to stream an answer to.
  EXPECT_EQ(wire_.find("HTTP/1.1 202 Accepted\r\n"), 0u) << wire_;
  EXPECT_EQ(wire_.find("text/event-stream"), std::string::npos) << wire_;
}

// ── Sessions ───────────────────────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, InitializeComesBackWithASessionToUse) {
  keepSessions();

  feed(post("/mcp", kRequestBody));

  const std::string id = sessionIdOnTheWire();
  ASSERT_EQ(id.size(), 32u) << wire_;
  EXPECT_TRUE(sessions_->known(id));

  // The request that created the session is served under it, so whatever
  // was agreed at initialize is recorded against the identity the client
  // will actually come back with.
  EXPECT_EQ(callbacks_.session_at_request, id);
}

TEST_F(StreamableHttpFilterTest, NothingButInitializeCreatesASession) {
  keepSessions();

  feed(
      post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));

  // Anything else has to arrive with a session already, so there is nothing
  // here to mint one for and nothing to serve.
  EXPECT_TRUE(sessionIdOnTheWire().empty()) << wire_;
  EXPECT_EQ(sessions_->size(), 0u);
  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  EXPECT_EQ(callbacks_.session_at_request, "<never dispatched>");
}

TEST_F(StreamableHttpFilterTest, TwoClientsAreGivenDifferentSessions) {
  keepSessions();

  feed(post("/mcp", kRequestBody));
  const std::string first = sessionIdOnTheWire();
  wire_.clear();
  feed(post("/mcp", kRequestBody));
  const std::string second = sessionIdOnTheWire();

  ASSERT_FALSE(first.empty());
  ASSERT_FALSE(second.empty());
  EXPECT_NE(first, second);
  EXPECT_EQ(sessions_->size(), 2u);
}

TEST_F(StreamableHttpFilterTest, AnEchoedSessionIdReachesTheHandler) {
  keepSessions();

  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(callbacks_.session_at_request, id);
  // Only the answer that created the session announces it; repeating it on
  // every response would say nothing the client does not already know.
  EXPECT_TRUE(sessionIdOnTheWire().empty()) << wire_;
}

TEST_F(StreamableHttpFilterTest, ASessionInUseIsKeptAlive) {
  keepSessions();

  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  transport::SessionCtx* session = sessions_->find(id);
  ASSERT_NE(session, nullptr);
  const auto before = session->last_activity;
  session->last_activity -= std::chrono::seconds(60);

  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_GE(sessions_->find(id)->last_activity, before);
}

TEST_F(StreamableHttpFilterTest, TheAgreedRevisionIsRecordedOnTheSession) {
  keepSessions();
  callbacks_.result = json::JsonValue::object();
  callbacks_.result["protocolVersion"] = std::string("2025-06-18");

  feed(post("/mcp", kRequestBody));

  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());
  transport::SessionCtx* session = sessions_->find(id);
  ASSERT_NE(session, nullptr);
  // Read back off the answer the client was given, not negotiated a second
  // time here, which could differ from what the client was told.
  EXPECT_EQ(session->negotiated_protocol_version, "2025-06-18");
}

TEST_F(StreamableHttpFilterTest, ASessionRemembersWhoAskedForIt) {
  keepSessions();
  host_->principal_value = "alice";

  feed(post("/mcp", kRequestBody));

  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());
  ASSERT_NE(sessions_->find(id), nullptr);
  EXPECT_EQ(sessions_->find(id)->principal, "alice");
}

TEST_F(StreamableHttpFilterTest, ARefusedInitializeHandsBackNoSession) {
  keepSessions();
  callbacks_.refuse_requests = true;

  feed(post("/mcp", kRequestBody));

  // A client that was not initialized has nothing to continue, so an id
  // here would only be echoed back and refused on every later request.
  EXPECT_TRUE(sessionIdOnTheWire().empty()) << wire_;
  EXPECT_EQ(sessions_->size(), 0u);
}

TEST_F(StreamableHttpFilterTest, AStatelessServerMintsNothing) {
  feed(post("/mcp", kRequestBody));

  EXPECT_TRUE(sessionIdOnTheWire().empty()) << wire_;
  EXPECT_EQ(callbacks_.session_at_request, "");
}

TEST_F(StreamableHttpFilterTest, AStatelessServerDisregardsAnOfferedSession) {
  // Believing one would let a caller name any session it liked on a server
  // that keeps none of its own, and be handed whatever sits under it.
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: someone-elses-session\r\n"));

  EXPECT_EQ(callbacks_.session_at_request, "");
}

TEST_F(StreamableHttpFilterTest, ASessionThisServerNeverIssuedIsRefused) {
  keepSessions();

  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: 0123456789abcdef0123456789abcdef\r\n"));

  // 404 rather than 403: the status a client is told to start again on.
  EXPECT_EQ(wire_.find("HTTP/1.1 404 Not Found\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("send initialize again"), std::string::npos) << wire_;
  EXPECT_EQ(callbacks_.session_at_request, "<never dispatched>");
}

TEST_F(StreamableHttpFilterTest, AStaleSessionOnInitializeIsDroppedNotRefused) {
  keepSessions();

  // Introducing yourself is the way back from a session that is gone, so
  // refusing it would leave a client with no way back at all.
  feed(post("/mcp", kRequestBody,
            "Mcp-Session-Id: 0123456789abcdef0123456789abcdef\r\n"));

  const std::string id = sessionIdOnTheWire();
  ASSERT_EQ(id.size(), 32u) << wire_;
  EXPECT_NE(id, "0123456789abcdef0123456789abcdef");
  EXPECT_EQ(callbacks_.session_at_request, id);
}

TEST_F(StreamableHttpFilterTest, ASessionIsNotUsableByAnotherCaller) {
  keepSessions();
  host_->principal_value = "alice";
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  transport::SessionCtx* session = sessions_->find(id);
  ASSERT_NE(session, nullptr);
  const auto stamped = session->last_activity;

  wire_.clear();
  host_->principal_value = "mallory";
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 403 Forbidden\r\n"), 0u) << wire_;
  EXPECT_EQ(callbacks_.session_at_request, id)
      << "the refused request must not have reached a handler";
  // A caller who is not entitled to the session must not be able to keep
  // it alive either, or an unauthorized prod would postpone expiry forever.
  EXPECT_EQ(sessions_->find(id)->last_activity, stamped);
}

TEST_F(StreamableHttpFilterTest, WithoutPrincipalMatchingTheIdIsEnough) {
  keepSessions(/*require_principal_match=*/false);
  host_->principal_value = "alice";
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  host_->principal_value = "mallory";
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 403"), std::string::npos) << wire_;
  EXPECT_EQ(callbacks_.session_at_request, id);
}

TEST_F(StreamableHttpFilterTest, AStatelessServerRefusesNothing) {
  // None of these rules exist without sessions: there is nothing to
  // present, so nothing can be missing, unknown or someone else's.
  feed(
      post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400"), std::string::npos) << wire_;
  EXPECT_EQ(callbacks_.session_at_request, "");
  ASSERT_EQ(callbacks_.requests.size(), 1u);
}

TEST_F(StreamableHttpFilterTest, ANotificationNeedsASessionToo) {
  keepSessions();

  // The rule is about the session, not about what the body turned out to
  // carry: a notification from nobody in particular is still from nobody.
  feed(post("/mcp", kNotificationBody));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  EXPECT_TRUE(callbacks_.notifications.empty());
}

TEST_F(StreamableHttpFilterTest, NothingIsHeldBackWhileJudgingLocally) {
  keepSessions();

  feed(post("/mcp", kRequestBody));

  // The session is owned by this very dispatcher, so there was nothing to
  // wait for and no reason to stop reading the connection.
  EXPECT_FALSE(host_->input_held);
}

TEST_F(StreamableHttpFilterTest, AClientWithASessionIsNotGivenAnother) {
  keepSessions();

  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(post("/mcp", kRequestBody, "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_TRUE(sessionIdOnTheWire().empty()) << wire_;
  EXPECT_EQ(sessions_->size(), 1u);
  EXPECT_EQ(callbacks_.session_at_request, id);
}

TEST_F(StreamableHttpFilterTest, AStreamedInitializeAnnouncesItsSessionFirst) {
  keepSessions();
  callbacks_.streaming = StreamingMode::Required;

  feed(post("/mcp", kRequestBody, "Accept: text/event-stream\r\n"));

  const std::string id = sessionIdOnTheWire();
  ASSERT_EQ(id.size(), 32u) << wire_;
  // A stream puts its headers out before the handler says anything, so the
  // id has to be attached before the answer opens rather than after it.
  const size_t header_end = wire_.find("\r\n\r\n");
  ASSERT_NE(header_end, std::string::npos) << wire_;
  EXPECT_LT(wire_.find(id), header_end) << wire_;
}

TEST_F(StreamableHttpFilterTest, AClientCanEndTheSessionItWasGiven) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(del("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  // Nothing to say back: the thing the client was asking about is gone.
  EXPECT_EQ(wire_.find("HTTP/1.1 204 No Content\r\n"), 0u) << wire_;
  EXPECT_FALSE(sessions_->known(id));
  EXPECT_EQ(sessions_->size(), 0u);
}

TEST_F(StreamableHttpFilterTest, ASessionEndedTwiceIsNotFoundTheSecondTime) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());
  feed(del("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  wire_.clear();
  feed(del("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 404 Not Found\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, AnEndedSessionCannotBeUsedAgain) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());
  feed(del("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  wire_.clear();
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
            "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 404 Not Found\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, EndingASessionNeedsToNameOne) {
  keepSessions();

  feed(del("/mcp"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, ASessionCannotBeEndedByAnotherCaller) {
  keepSessions();
  host_->principal_value = "alice";
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  host_->principal_value = "mallory";
  feed(del("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 403 Forbidden\r\n"), 0u) << wire_;
  EXPECT_TRUE(sessions_->known(id))
      << "a caller who may not use the session may not end it either";
}

// ── Protocol revision ──────────────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, ARevisionThisServerCannotServeIsRefused) {
  StreamableHttpOptions options;
  options.protocol_versions = {"2025-11-25", "2025-06-18"};
  buildFilter(options);

  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}",
            "MCP-Protocol-Version: 1999-01-01\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
  // Named rather than merely refused: a peer told only "no" retries the
  // same request.
  EXPECT_NE(wire_.find("1999-01-01"), std::string::npos) << wire_;
  EXPECT_NE(wire_.find("2025-11-25"), std::string::npos) << wire_;
  EXPECT_TRUE(callbacks_.requests.empty());
}

TEST_F(StreamableHttpFilterTest, ARevisionThisServerServesIsKept) {
  StreamableHttpOptions options;
  options.protocol_versions = {"2025-11-25", "2025-06-18"};
  buildFilter(options);

  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}",
            "MCP-Protocol-Version: 2025-06-18\r\n"));

  ASSERT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(callbacks_.client_at_request.protocol_version, "2025-06-18");
}

TEST_F(StreamableHttpFilterTest, NoRevisionMeansTheOneThatDidNotNeedTheHeader) {
  StreamableHttpOptions options;
  options.protocol_versions = {"2025-11-25", "2025-06-18"};
  buildFilter(options);

  // The header only became mandatory after that revision, so its absence
  // identifies a peer speaking it rather than a peer that forgot.
  feed(
      post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));

  ASSERT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(callbacks_.client_at_request.protocol_version,
            protocol::kLegacyAssumedVersion);
}

TEST_F(StreamableHttpFilterTest, InitializeIsNotJudgedOnTheRevisionItAsksFor) {
  StreamableHttpOptions options;
  options.protocol_versions = {"2025-11-25"};
  buildFilter(options);

  // Which revision the two ends speak is what initialize settles; refusing
  // it on that header would refuse the conversation that decides.
  feed(post("/mcp", kRequestBody, "MCP-Protocol-Version: 1999-01-01\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400"), std::string::npos) << wire_;
  ASSERT_EQ(callbacks_.requests.size(), 1u);
}

TEST_F(StreamableHttpFilterTest, WithNoConfiguredListNothingIsRefused) {
  feed(post("/mcp", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}",
            "MCP-Protocol-Version: 1999-01-01\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400"), std::string::npos) << wire_;
  ASSERT_EQ(callbacks_.requests.size(), 1u);
}

// ── The standalone event stream ────────────────────────────────────────────

TEST_F(StreamableHttpFilterTest, AGetOpensTheSessionsEventStream) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 200 OK\r\n"), 0u) << wire_;
  EXPECT_NE(wire_.find("Content-Type: text/event-stream"), std::string::npos)
      << wire_;
  // The older transport announces a callback URL as its first event. This
  // one has no separate endpoint, and a client reading one would post its
  // requests somewhere that does not exist.
  EXPECT_EQ(wire_.find("event: endpoint"), std::string::npos) << wire_;

  transport::SessionCtx* session = sessions_->find(id);
  ASSERT_NE(session, nullptr);
  ASSERT_EQ(session->streams.size(), 1u);
  EXPECT_EQ(session->streams[0]->kind, transport::StreamCtx::Kind::Get);
  EXPECT_FALSE(session->streams[0]->id.empty());
}

TEST_F(StreamableHttpFilterTest, TwoStreamsOnOneSessionAreBothOpened) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  // Holding several at once is allowed, so a second is accepted rather
  // than refused.
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  transport::SessionCtx* session = sessions_->find(id);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->streams.size(), 2u);
  EXPECT_NE(session->streams[0]->id, session->streams[1]->id);
}

TEST_F(StreamableHttpFilterTest, AStreamBeyondTheCapIsRefused) {
  keepSessions();
  sessions_options_.max_get_streams_per_session = 1;
  buildFilter(sessions_options_);

  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  wire_.clear();
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 429 "), 0u) << wire_;
  EXPECT_EQ(sessions_->find(id)->streams.size(), 1u);
}

TEST_F(StreamableHttpFilterTest, AClientThatCannotReadAStreamIsToldSo) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(
      "GET /mcp HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Accept: application/json\r\n"
      "Mcp-Session-Id: " +
      id +
      "\r\n"
      "Content-Length: 0\r\n\r\n");

  EXPECT_EQ(wire_.find("HTTP/1.1 406 "), 0u) << wire_;
  EXPECT_TRUE(sessions_->find(id)->streams.empty());
}

// Saying nothing is not the same as saying yes. A GET here has exactly
// one kind of answer, and a client that never named it has not asked for
// one — where a POST that names nothing is only leaving the framing of
// an ordinary answer to the server, which is why that stays served.
TEST_F(StreamableHttpFilterTest, AClientThatSaidNothingIsNotGivenAStream) {
  keepSessions();
  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());

  wire_.clear();
  feed(
      "GET /mcp HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Mcp-Session-Id: " +
      id +
      "\r\n"
      "Content-Length: 0\r\n\r\n");

  EXPECT_EQ(wire_.find("HTTP/1.1 406 "), 0u) << wire_;
  EXPECT_TRUE(sessions_->find(id)->streams.empty())
      << "a stream was opened for a request that never asked for one";
}

// The same silence on a POST is left alone: there is nothing a client
// has to name to be sent an ordinary answer.
TEST_F(StreamableHttpFilterTest, APostThatSaidNothingIsStillAnswered) {
  keepSessions();

  feed(
      "POST /mcp HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      std::to_string(std::string(kRequestBody).size()) + "\r\n\r\n" +
      kRequestBody);

  EXPECT_EQ(wire_.find("HTTP/1.1 200 "), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, AStreamHasToNameASession) {
  keepSessions();

  feed(get("/mcp"));

  EXPECT_EQ(wire_.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, AStreamOnAnUnknownSessionIsNotFound) {
  keepSessions();

  feed(get("/mcp", "Mcp-Session-Id: 0123456789abcdef0123456789abcdef\r\n"));

  EXPECT_EQ(wire_.find("HTTP/1.1 404 Not Found\r\n"), 0u) << wire_;
}

TEST_F(StreamableHttpFilterTest, AStatelessServerHasNoStreamToOpen) {
  // No session to hang it on means nothing could ever be routed to it, so
  // the request goes to the layer behind rather than being answered here.
  feed(get("/mcp", "Mcp-Session-Id: whatever\r\n"));

  EXPECT_TRUE(wire_.empty()) << wire_;
  EXPECT_EQ(fallback_.header_count, 1u);
}

TEST_F(StreamableHttpFilterTest, AnIdleStreamKeepsSayingSomething) {
  keepSessions();
  sessions_options_.keepalive_interval = std::chrono::milliseconds(20);
  buildFilter(sessions_options_);

  feed(post("/mcp", kRequestBody));
  const std::string id = sessionIdOnTheWire();
  ASSERT_FALSE(id.empty());
  feed(get("/mcp", "Mcp-Session-Id: " + id + "\r\n"));

  wire_.clear();
  runFor(std::chrono::milliseconds(120));

  // A comment rather than an event: nothing is being said, which is the
  // point — it exists for whatever is sitting between the two ends.
  EXPECT_NE(wire_.find(": keep-alive"), std::string::npos) << wire_;
  EXPECT_EQ(wire_.find("data:"), std::string::npos) << wire_;
}

TEST_F(StreamableHttpFilterTest, NothingIsKeptAliveWhenNothingIsStreaming) {
  keepSessions();
  sessions_options_.keepalive_interval = std::chrono::milliseconds(20);
  buildFilter(sessions_options_);

  feed(post("/mcp", kRequestBody));
  wire_.clear();
  runFor(std::chrono::milliseconds(80));

  EXPECT_TRUE(wire_.empty()) << wire_;
}

}  // namespace
}  // namespace filter
}  // namespace mcp
