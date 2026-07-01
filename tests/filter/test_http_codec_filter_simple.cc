/**
 * @file test_http_server_codec_filter_simple.cc
 * @brief Simple integration tests for HTTP server codec filter with state
 * machine
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "mcp/filter/http_codec_filter.h"
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

constexpr size_t kOversizedHttpBodyChunk = 16 * 1024 * 1024 + 1;
constexpr size_t kMaxHttpBodySize = 16 * 1024 * 1024;
constexpr char kBodyByte = 'x';

// Simple request callbacks implementation
class TestRequestCallbacks : public HttpCodecFilter::MessageCallbacks {
 public:
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool keep_alive) override {
    headers_received_ = true;
    headers_ = headers;
    keep_alive_ = keep_alive;
  }

  void onBody(const std::string& data, bool end_stream) override {
    body_received_ = true;
    body_ = data;
    body_chunks_.push_back(data);
    body_end_streams_.push_back(end_stream);
    end_stream_ = end_stream;
  }

  void onMessageComplete() override { message_complete_ = true; }

  void onError(const std::string& error) override {
    error_received_ = true;
    error_message_ = error;
  }

  // Test state
  bool headers_received_{false};
  bool body_received_{false};
  bool message_complete_{false};
  bool error_received_{false};
  std::map<std::string, std::string> headers_;
  std::string body_;
  std::vector<std::string> body_chunks_;
  std::vector<bool> body_end_streams_;
  std::string error_message_;
  bool keep_alive_{false};
  bool end_stream_{false};
};

class HttpCodecFilterIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    dispatcher_->run(event::RunType::NonBlock);

    // Create filter (server mode)
    filter_ = std::make_unique<HttpCodecFilter>(callbacks_, *dispatcher_, true);
  }

  void TearDown() override {
    filter_.reset();
    dispatcher_.reset();
  }

  // Helper to create HTTP request data
  OwnedBuffer createGetRequest(const std::string& path) {
    OwnedBuffer buffer;
    std::string request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: example.com\r\n";
    request += "User-Agent: test-client\r\n";
    request += "\r\n";

    buffer.add(request.c_str(), request.length());
    return buffer;
  }

  OwnedBuffer createPostRequest(const std::string& path,
                                const std::string& body) {
    OwnedBuffer buffer;
    std::string request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: example.com\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    request += "\r\n";
    request += body;

    buffer.add(request.c_str(), request.length());
    return buffer;
  }

  // Helper to run dispatcher briefly
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  TestRequestCallbacks callbacks_;
  std::unique_ptr<HttpCodecFilter> filter_;
};

// ===== Basic Integration Tests =====

TEST_F(HttpCodecFilterIntegrationTest, FilterCreation) {
  // Test that filter can be created successfully
  EXPECT_NE(filter_, nullptr);
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(HttpCodecFilterIntegrationTest, SimpleGetRequest) {
  filter_->onNewConnection();

  auto request = createGetRequest("/test");
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);

  runFor(10ms);

  // Verify request was processed
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.message_complete_);
  EXPECT_FALSE(callbacks_.error_received_);

  // Check headers
  auto url_it = callbacks_.headers_.find("url");
  EXPECT_NE(url_it, callbacks_.headers_.end());
  EXPECT_EQ(url_it->second, "/test");

  auto host_it = callbacks_.headers_.find("host");
  EXPECT_NE(host_it, callbacks_.headers_.end());
  EXPECT_EQ(host_it->second, "example.com");
}

TEST_F(HttpCodecFilterIntegrationTest, PostRequestWithBody) {
  filter_->onNewConnection();

  std::string body = R"({"message": "hello world"})";
  auto request = createPostRequest("/api/data", body);

  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);

  runFor(10ms);

  // Verify request was processed
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.body_received_);
  EXPECT_TRUE(callbacks_.message_complete_);
  EXPECT_FALSE(callbacks_.error_received_);

  // Check body
  EXPECT_EQ(callbacks_.body_, body);
  EXPECT_TRUE(callbacks_.end_stream_);

  // Check content type header
  auto content_type_it = callbacks_.headers_.find("content-type");
  EXPECT_NE(content_type_it, callbacks_.headers_.end());
  EXPECT_EQ(content_type_it->second, "application/json");
}

TEST(HttpCodecClientModeTest, ResponseBodyIsStreamedWithoutCompletionReplay) {
  auto factory = event::createLibeventDispatcherFactory();
  auto dispatcher = factory->createDispatcher("test");
  dispatcher->run(event::RunType::NonBlock);

  TestRequestCallbacks callbacks;
  HttpCodecFilter filter(callbacks, *dispatcher, false /* is_server */);
  ASSERT_EQ(filter.onNewConnection(), network::FilterStatus::Continue);

  const std::string body = "event: message\ndata: one\n\n";
  std::string response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Content-Length: " +
      std::to_string(body.size()) +
      "\r\n"
      "\r\n" +
      body;

  OwnedBuffer buffer;
  buffer.add(response);
  EXPECT_EQ(filter.onData(buffer, false), network::FilterStatus::Continue);

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < 10ms) {
    dispatcher->run(event::RunType::NonBlock);
    std::this_thread::sleep_for(1ms);
  }

  ASSERT_TRUE(callbacks.headers_received_);
  ASSERT_TRUE(callbacks.message_complete_);
  ASSERT_EQ(callbacks.body_chunks_.size(), 1u);
  EXPECT_EQ(callbacks.body_chunks_[0], body);
  EXPECT_FALSE(callbacks.body_end_streams_[0]);
}

TEST_F(HttpCodecFilterIntegrationTest, OversizedRequestBodyCallbackIsRejected) {
  ASSERT_EQ(filter_->parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  EXPECT_EQ(filter_->parser_callbacks_->onBody(&kBodyByte,
                                               kOversizedHttpBodyChunk),
            http::ParserCallbackResult::Error);

  EXPECT_FALSE(callbacks_.body_received_);
  EXPECT_FALSE(callbacks_.message_complete_);
}

TEST_F(HttpCodecFilterIntegrationTest,
       RequestBodyAccumulationOverLimitIsRejected) {
  ASSERT_EQ(filter_->parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  const std::string half_body(kMaxHttpBodySize / 2, kBodyByte);
  ASSERT_EQ(filter_->parser_callbacks_->onBody(half_body.data(),
                                               half_body.size()),
            http::ParserCallbackResult::Success);
  ASSERT_EQ(filter_->parser_callbacks_->onBody(half_body.data(),
                                               half_body.size()),
            http::ParserCallbackResult::Success);
  ASSERT_EQ(filter_->current_stream_->body.length(), kMaxHttpBodySize);

  EXPECT_EQ(filter_->parser_callbacks_->onBody(&kBodyByte, 1),
            http::ParserCallbackResult::Error);

  ASSERT_TRUE(filter_->pending_parser_error_.has_value());
  EXPECT_EQ(filter_->pending_parser_error_.value(),
            "HTTP body exceeds codec limit");
  EXPECT_FALSE(callbacks_.body_received_);
  EXPECT_FALSE(callbacks_.message_complete_);
}

TEST_F(HttpCodecFilterIntegrationTest, NullRequestBodyCallbackIsRejected) {
  ASSERT_EQ(filter_->parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  EXPECT_EQ(filter_->parser_callbacks_->onBody(nullptr, 1),
            http::ParserCallbackResult::Error);

  EXPECT_FALSE(callbacks_.body_received_);
  EXPECT_FALSE(callbacks_.message_complete_);
}

TEST(HttpCodecClientModeTest, OversizedResponseBodyCallbackIsRejected) {
  auto factory = event::createLibeventDispatcherFactory();
  auto dispatcher = factory->createDispatcher("test");
  dispatcher->run(event::RunType::NonBlock);

  TestRequestCallbacks callbacks;
  HttpCodecFilter filter(callbacks, *dispatcher, false /* is_server */);
  ASSERT_EQ(filter.onNewConnection(), network::FilterStatus::Continue);
  ASSERT_EQ(filter.parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  EXPECT_EQ(filter.parser_callbacks_->onBody(&kBodyByte,
                                             kOversizedHttpBodyChunk),
            http::ParserCallbackResult::Error);

  EXPECT_FALSE(callbacks.body_received_);
  EXPECT_FALSE(callbacks.message_complete_);
}

TEST(HttpCodecClientModeTest, NullResponseBodyCallbackIsRejected) {
  auto factory = event::createLibeventDispatcherFactory();
  auto dispatcher = factory->createDispatcher("test");
  dispatcher->run(event::RunType::NonBlock);

  TestRequestCallbacks callbacks;
  HttpCodecFilter filter(callbacks, *dispatcher, false /* is_server */);
  ASSERT_EQ(filter.onNewConnection(), network::FilterStatus::Continue);
  ASSERT_EQ(filter.parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  EXPECT_EQ(filter.parser_callbacks_->onBody(nullptr, 1),
            http::ParserCallbackResult::Error);

  EXPECT_FALSE(callbacks.body_received_);
  EXPECT_FALSE(callbacks.message_complete_);
}

TEST_F(HttpCodecFilterIntegrationTest,
       ParserCallbackErrorIsDeferredUntilDispatchUnwinds) {
  ASSERT_EQ(filter_->parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  filter_->parser_callbacks_->onError("synthetic parser callback error");

  ASSERT_TRUE(filter_->pending_parser_error_.has_value());
  EXPECT_FALSE(callbacks_.error_received_);

  auto request = createGetRequest("/deferred-error");
  filter_->dispatch(request);
  runFor(10ms);

  EXPECT_FALSE(filter_->pending_parser_error_.has_value());
  EXPECT_TRUE(callbacks_.error_received_);
  EXPECT_EQ(callbacks_.error_message_, "synthetic parser callback error");
}

TEST_F(HttpCodecFilterIntegrationTest,
       BodyCallbackErrorIsDeferredUntilDispatchUnwinds) {
  ASSERT_EQ(filter_->parser_callbacks_->onMessageBegin(),
            http::ParserCallbackResult::Success);

  EXPECT_EQ(filter_->parser_callbacks_->onBody(nullptr, 1),
            http::ParserCallbackResult::Error);

  ASSERT_TRUE(filter_->pending_parser_error_.has_value());
  EXPECT_FALSE(callbacks_.error_received_);

  auto request = createGetRequest("/deferred-body-error");
  filter_->dispatch(request);
  runFor(10ms);

  EXPECT_FALSE(filter_->pending_parser_error_.has_value());
  EXPECT_TRUE(callbacks_.error_received_);
  EXPECT_EQ(callbacks_.error_message_, "HTTP body chunk exceeds codec limit");
}

TEST_F(HttpCodecFilterIntegrationTest, KeepAliveConnection) {
  filter_->onNewConnection();

  auto request = createGetRequest("/test1");
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.keep_alive_);

  // Reset callbacks for second request
  callbacks_ = TestRequestCallbacks{};

  // Send second request
  auto request2 = createGetRequest("/test2");
  EXPECT_EQ(filter_->onData(request2, false), network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.headers_received_);
  auto url_it = callbacks_.headers_.find("url");
  EXPECT_NE(url_it, callbacks_.headers_.end());
  EXPECT_EQ(url_it->second, "/test2");
}

TEST_F(HttpCodecFilterIntegrationTest, MalformedRequest) {
  filter_->onNewConnection();

  OwnedBuffer malformed;
  malformed.add("INVALID HTTP REQUEST\r\n\r\n", 24);

  EXPECT_EQ(filter_->onData(malformed, false), network::FilterStatus::Continue);

  runFor(10ms);

  // Should receive error
  EXPECT_TRUE(callbacks_.error_received_);
  EXPECT_FALSE(callbacks_.error_message_.empty());
}

// ===== State Machine Integration Tests =====

TEST_F(HttpCodecFilterIntegrationTest, StateMachineIntegration) {
  filter_->onNewConnection();

  // The state machine should be properly integrated and handle the request
  // lifecycle
  auto request = createGetRequest("/state-test");
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);

  runFor(10ms);

  // Should complete successfully with state machine managing the flow
  EXPECT_TRUE(callbacks_.headers_received_);
  EXPECT_TRUE(callbacks_.message_complete_);
  EXPECT_FALSE(callbacks_.error_received_);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
