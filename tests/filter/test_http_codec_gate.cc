/**
 * @file test_http_codec_gate.cc
 * @brief Tests for holding back request processing while a response streams
 *
 * HTTP/1.1 delivers responses in request order, so once a response starts
 * streaming, a request that arrives behind it cannot be answered until the
 * stream finishes. The codec's answer is to stop parsing rather than to
 * stop reading — reads stay armed so end-of-file still arrives promptly,
 * because for a streaming exchange the peer hanging up is the cancellation
 * signal.
 *
 * The case that matters most is a pipelined request arriving in the *same*
 * TCP segment as the one that opened the stream. A gate that only checked
 * on the next read would have parsed and dispatched it already.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/http_codec_filter.h"

namespace mcp {
namespace filter {
namespace {

// Records what the codec dispatched, and can shut the gate at a chosen
// point the way a real streaming response would.
class GateTestCallbacks : public HttpCodecFilter::MessageCallbacks,
                          public HttpCodecFilter::GateCallbacks {
 public:
  enum class PauseAt { Never, Headers, MessageComplete };

  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool) override {
    // The server codec surfaces the request target under "url".
    auto url = headers.find("url");
    paths.push_back(url != headers.end() ? url->second : std::string());
    if (pause_at == PauseAt::Headers && filter != nullptr) {
      filter->pauseRequestProcessing();
    }
  }

  void onBody(const std::string& data, bool) override {
    bodies.push_back(data);
  }

  void onMessageComplete() override {
    ++messages_complete;
    if (pause_at == PauseAt::MessageComplete && filter != nullptr) {
      filter->pauseRequestProcessing();
    }
  }

  void onError(const std::string& error) override { errors.push_back(error); }

  void onGatedInputOverflow() override { ++overflows; }
  void onGatedEof() override { ++eofs; }

  HttpCodecFilter* filter{nullptr};
  PauseAt pause_at{PauseAt::Never};
  std::vector<std::string> paths;
  std::vector<std::string> bodies;
  std::vector<std::string> errors;
  int messages_complete{0};
  int overflows{0};
  int eofs{0};
};

class HttpCodecGateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("gate_test");
    dispatcher_->run(event::RunType::NonBlock);
    filter_ = std::make_unique<HttpCodecFilter>(callbacks_, *dispatcher_,
                                                /*is_server=*/true);
    callbacks_.filter = filter_.get();
    filter_->setGateCallbacks(&callbacks_);
    filter_->onNewConnection();
  }

  void TearDown() override {
    filter_.reset();
    dispatcher_.reset();
  }

  static std::string get(const std::string& path) {
    return "GET " + path +
           " HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "\r\n";
  }

  void feed(const std::string& bytes, bool end_stream = false) {
    OwnedBuffer buffer;
    buffer.add(bytes);
    filter_->onData(buffer, end_stream);
    leftover_ = buffer.length();
  }

  GateTestCallbacks callbacks_;
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<HttpCodecFilter> filter_;
  size_t leftover_{0};
};

TEST_F(HttpCodecGateTest, GateIsOpenByDefault) {
  // Nothing changes for connections that never open a stream: pipelined
  // requests are parsed and dispatched as they always were.
  feed(get("/first") + get("/second"));

  EXPECT_FALSE(filter_->requestProcessingPaused());
  ASSERT_EQ(callbacks_.paths.size(), 2u);
  EXPECT_EQ(callbacks_.paths[0], "/first");
  EXPECT_EQ(callbacks_.paths[1], "/second");
}

TEST_F(HttpCodecGateTest, PipelinedRequestInTheSameSegmentIsHeldBack) {
  // Both requests arrive together. The first opens a stream, so the second
  // must not be dispatched — there is no way to answer it in order yet.
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream") + get("/pipelined"));

  EXPECT_TRUE(filter_->requestProcessingPaused());
  ASSERT_EQ(callbacks_.paths.size(), 1u);
  EXPECT_EQ(callbacks_.paths[0], "/stream");

  // Nothing may be left in the caller's buffer either: whoever runs after
  // the codec would otherwise read those bytes as something else entirely.
  EXPECT_EQ(leftover_, 0u);
}

TEST_F(HttpCodecGateTest, HeldRequestsAreAnsweredInOrderAfterResume) {
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream") + get("/second") + get("/third"));
  ASSERT_EQ(callbacks_.paths.size(), 1u);

  // Stop pausing, as a finished stream would, then open the gate.
  callbacks_.pause_at = GateTestCallbacks::PauseAt::Never;
  filter_->resumeRequestProcessing();

  EXPECT_FALSE(filter_->requestProcessingPaused());
  ASSERT_EQ(callbacks_.paths.size(), 3u);
  EXPECT_EQ(callbacks_.paths[1], "/second");
  EXPECT_EQ(callbacks_.paths[2], "/third");
}

TEST_F(HttpCodecGateTest, RequestsArrivingWhileGatedAreHeldThenAnswered) {
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream"));
  ASSERT_EQ(callbacks_.paths.size(), 1u);

  // A later read while the gate is still shut.
  feed(get("/later"));
  EXPECT_EQ(callbacks_.paths.size(), 1u);
  EXPECT_EQ(leftover_, 0u);

  callbacks_.pause_at = GateTestCallbacks::PauseAt::Never;
  filter_->resumeRequestProcessing();

  ASSERT_EQ(callbacks_.paths.size(), 2u);
  EXPECT_EQ(callbacks_.paths[1], "/later");
}

TEST_F(HttpCodecGateTest, PausingAtHeadersAlsoHoldsThePipelinedRequest) {
  // Same protection when the stream opens from the request headers rather
  // than at the end of the message.
  callbacks_.pause_at = GateTestCallbacks::PauseAt::Headers;
  feed(get("/stream") + get("/pipelined"));

  EXPECT_TRUE(filter_->requestProcessingPaused());
  EXPECT_EQ(callbacks_.paths.size(), 1u);
  EXPECT_EQ(leftover_, 0u);
}

TEST_F(HttpCodecGateTest, OverflowIsReportedOnceForTheOwnerToActOn) {
  // A mid-stream HTTP error cannot be sent — a response body is already
  // going out — so the only thing to do is tell the owner to close.
  filter_->setGatedInputLimit(64);
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream"));
  ASSERT_TRUE(filter_->requestProcessingPaused());

  feed(std::string(256, 'x'));
  EXPECT_EQ(callbacks_.overflows, 1);

  // Still one report, not one per read, so the owner closes once.
  feed(std::string(256, 'y'));
  EXPECT_EQ(callbacks_.overflows, 1);
}

TEST_F(HttpCodecGateTest, InputUnderTheLimitIsNotAnOverflow) {
  filter_->setGatedInputLimit(4096);
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream"));

  feed(get("/small"));
  EXPECT_EQ(callbacks_.overflows, 0);
}

TEST_F(HttpCodecGateTest, EndOfFileWhileGatedIsReportedImmediately) {
  // The peer hanging up is how a client cancels a response in flight. Held
  // back until the stream ended, that signal would arrive too late to act
  // on, which is why reads are never disabled to implement the gate.
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream"));
  ASSERT_TRUE(filter_->requestProcessingPaused());

  OwnedBuffer empty;
  filter_->onData(empty, /*end_stream=*/true);

  EXPECT_EQ(callbacks_.eofs, 1);
}

TEST_F(HttpCodecGateTest, ResumeWithoutHeldInputIsHarmless) {
  callbacks_.pause_at = GateTestCallbacks::PauseAt::MessageComplete;
  feed(get("/stream"));

  callbacks_.pause_at = GateTestCallbacks::PauseAt::Never;
  filter_->resumeRequestProcessing();
  filter_->resumeRequestProcessing();

  EXPECT_FALSE(filter_->requestProcessingPaused());
  EXPECT_EQ(callbacks_.paths.size(), 1u);
  EXPECT_TRUE(callbacks_.errors.empty());
}

}  // namespace
}  // namespace filter
}  // namespace mcp
