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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/streamable_http_filter.h"
#include "mcp/mcp_connection_manager.h"

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
  http::ResponseWriter::Observer* streamObserver() override { return nullptr; }
  bool streamEndsConnection() const override { return false; }

  std::string principal_value{"anonymous"};

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
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));

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

  // Set by the fixture so a test can see what the exchange recorded about
  // the peer while the message was still being dispatched.
  StreamableHttpFilter* filter{nullptr};
  transport::ExchangeClientContext client_at_request;

  bool answer_requests{true};
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
    filter_.reset(new StreamableHttpFilter(*dispatcher_, callbacks_, fallback_,
                                           *exchanges_, *host_, "/mcp"));
    codec_.reset(new HttpCodecFilter(*filter_, *dispatcher_,
                                     /*is_server=*/true));
    codec_->onNewConnection();
    callbacks_.filter = filter_.get();
  }

  void TearDown() override {
    codec_.reset();
    filter_.reset();
    exchanges_.reset();
    host_.reset();
    dispatcher_.reset();
    factory_.reset();
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

TEST_F(StreamableHttpFilterTest, TheSessionHeaderTravelsWithTheMessage) {
  feed(post("/mcp", kRequestBody, "Mcp-Session-Id: session-7\r\n"));

  EXPECT_EQ(callbacks_.session_at_request, "session-7");
}

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

TEST_F(StreamableHttpFilterTest, ANotificationIsNeverAnsweredWithAStream) {
  callbacks_.streaming = StreamingMode::Required;

  feed(post("/mcp", kNotificationBody, "Accept: text/event-stream\r\n"));

  // There is no request here to stream an answer to.
  EXPECT_EQ(wire_.find("HTTP/1.1 202 Accepted\r\n"), 0u) << wire_;
  EXPECT_EQ(wire_.find("text/event-stream"), std::string::npos) << wire_;
}

}  // namespace
}  // namespace filter
}  // namespace mcp
