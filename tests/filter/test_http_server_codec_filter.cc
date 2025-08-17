/**
 * @file test_http_server_codec_filter.cc
 * @brief Comprehensive tests for HTTP server codec filter
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <memory>
#include <string>
#include <map>

#include "mcp/filter/http_server_codec_filter.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::InSequence;
using ::testing::StrictMock;

// Mock request callbacks
class MockRequestCallbacks : public HttpServerCodecFilter::RequestCallbacks {
public:
  MOCK_METHOD2(onHeaders, void(const std::map<std::string, std::string>& headers, bool keep_alive));
  MOCK_METHOD2(onBody, void(const std::string& data, bool end_stream));
  MOCK_METHOD0(onMessageComplete, void());
  MOCK_METHOD1(onError, void(const std::string& error));
};

// Simplified mock write filter callbacks
class MockWriteFilterCallbacks : public network::WriteFilterCallbacks {
public:
  MOCK_METHOD2(injectWriteDataToFilterChain, void(Buffer& data, bool end_stream));
  
  // Add stub implementations for required pure virtual methods
  network::Connection& connection() override {
    static network::Connection* stub = nullptr;
    return *stub;  // Will crash if actually called, but that's fine for our tests
  }
  
  void injectReadDataToFilterChain(Buffer& data, bool end_stream) override {}
  event::Dispatcher& dispatcher() override {
    static event::Dispatcher* stub = nullptr;
    return *stub;
  }
  bool aboveWriteBufferHighWatermark() const override { return false; }
};

// Simplified stub read filter callbacks
class StubReadFilterCallbacks : public network::ReadFilterCallbacks {
public:
  network::Connection& connection() override {
    static network::Connection* stub = nullptr;
    return *stub;
  }
  void continueReading() override {}
  void injectReadDataToFilterChain(Buffer& data, bool end_stream) override {}
  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) override {}
  void onFilterInbound() override {}
  void requestDecoder() override {}
  const network::ConnectionInfo& connectionInfo() const override {
    static network::ConnectionInfo* stub = nullptr;
    return *stub;
  }
  event::Dispatcher& dispatcher() override {
    static event::Dispatcher* stub = nullptr;
    return *stub;
  }
  void setDecoderBufferLimit(uint32_t limit) override {}
  uint32_t decoderBufferLimit() override { return 0; }
  bool cannotEncodeFrame() override { return false; }
  void markUpstreamFilterChainComplete() override {}
  const std::string& upstreamHost() const override {
    static std::string stub;
    return stub;
  }
  void setUpstreamHost(const std::string& host) override {}
  bool shouldContinueFilterChain() override { return true; }
};

class HttpServerCodecFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    dispatcher_->run(event::RunType::NonBlock);
    
    // Create filter
    filter_ = std::make_unique<HttpServerCodecFilter>(request_callbacks_, *dispatcher_);
    
    // Initialize filter callbacks
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void TearDown() override {
    filter_.reset();
    dispatcher_.reset();
  }

  // Helper to create HTTP request data
  OwnedBuffer createHttpRequest(const std::string& method,
                                const std::string& path,
                                const std::map<std::string, std::string>& headers,
                                const std::string& body = "") {
    OwnedBuffer buffer;
    
    // Request line
    std::string request_line = method + " " + path + " HTTP/1.1\r\n";
    buffer.add(request_line.c_str(), request_line.length());
    
    // Headers
    for (const auto& header : headers) {
      std::string header_line = header.first + ": " + header.second + "\r\n";
      buffer.add(header_line.c_str(), header_line.length());
    }
    
    // Content-Length for body
    if (!body.empty()) {
      std::string content_length = "Content-Length: " + std::to_string(body.length()) + "\r\n";
      buffer.add(content_length.c_str(), content_length.length());
    }
    
    // End of headers
    buffer.add("\r\n", 2);
    
    // Body
    if (!body.empty()) {
      buffer.add(body.c_str(), body.length());
    }
    
    return buffer;
  }

  // Helper to run dispatcher for a duration
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  MockRequestCallbacks request_callbacks_;
  StubReadFilterCallbacks read_callbacks_;
  MockWriteFilterCallbacks write_callbacks_;
  std::unique_ptr<HttpServerCodecFilter> filter_;
};

// ===== Basic Request Processing Tests =====

TEST_F(HttpServerCodecFilterTest, InitialState) {
  EXPECT_EQ(filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(HttpServerCodecFilterTest, SimpleGetRequest) {
  filter_->onNewConnection();
  
  std::map<std::string, std::string> expected_headers = {
    {"host", "example.com"},
    {"user-agent", "test-client"},
    {"url", "/test"}
  };
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, true))
    .WillOnce([&expected_headers](const auto& headers, bool keep_alive) {
      EXPECT_TRUE(keep_alive);
      for (const auto& expected : expected_headers) {
        auto it = headers.find(expected.first);
        EXPECT_NE(it, headers.end()) << "Missing header: " << expected.first;
        if (it != headers.end()) {
          EXPECT_EQ(it->second, expected.second);
        }
      }
    });
  
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request = createHttpRequest("GET", "/test", {
    {"Host", "example.com"},
    {"User-Agent", "test-client"}
  });
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(HttpServerCodecFilterTest, PostRequestWithBody) {
  filter_->onNewConnection();
  
  std::string expected_body = "{'message': 'hello world'}";
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, true))
    .WillOnce([](const auto& headers, bool keep_alive) {
      EXPECT_TRUE(keep_alive);
      auto it = headers.find("content-type");
      EXPECT_NE(it, headers.end());
      EXPECT_EQ(it->second, "application/json");
    });
  
  EXPECT_CALL(request_callbacks_, onBody(expected_body, true));
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request = createHttpRequest("POST", "/api/test", {
    {"Host", "example.com"},
    {"Content-Type", "application/json"}
  }, expected_body);
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(HttpServerCodecFilterTest, ChunkedRequest) {
  filter_->onNewConnection();
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, true))
    .WillOnce([](const auto& headers, bool keep_alive) {
      auto it = headers.find("transfer-encoding");
      EXPECT_NE(it, headers.end());
      EXPECT_EQ(it->second, "chunked");
    });
  
  EXPECT_CALL(request_callbacks_, onBody(_, true));
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request = createHttpRequest("POST", "/api/chunked", {
    {"Host", "example.com"},
    {"Transfer-Encoding", "chunked"}
  });
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(10ms);
}

// ===== Response Encoding Tests =====

TEST_F(HttpServerCodecFilterTest, SimpleResponse) {
  filter_->onNewConnection();
  
  // Set up expectation for write callback
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      // Verify response format
      size_t length = data.length();
      std::vector<char> response_data(length);
      data.copyOut(0, length, response_data.data());
      std::string response(response_data.begin(), response_data.end());
      
      EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
      EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
      EXPECT_TRUE(response.find("\r\n\r\n") != std::string::npos);
    });
  
  auto& encoder = filter_->responseEncoder();
  encoder.encodeHeaders(200, {
    {"Content-Type", "application/json"},
    {"Cache-Control", "no-cache"}
  }, true);
  
  runFor(10ms);
}

TEST_F(HttpServerCodecFilterTest, ResponseWithBody) {
  filter_->onNewConnection();
  
  std::string response_body = "{\"status\": \"success\"}";
  
  // Expect headers first
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> response_data(length);
      data.copyOut(0, length, response_data.data());
      std::string response(response_data.begin(), response_data.end());
      EXPECT_TRUE(response.find("HTTP/1.1 201 Created") != std::string::npos);
    });
  
  // Then expect body
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([&response_body](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> body_data(length);
      data.copyOut(0, length, body_data.data());
      std::string body(body_data.begin(), body_data.end());
      EXPECT_EQ(body, response_body);
    });
  
  auto& encoder = filter_->responseEncoder();
  encoder.encodeHeaders(201, {
    {"Content-Type", "application/json"}
  }, false);
  
  OwnedBuffer body_buffer;
  body_buffer.add(response_body.c_str(), response_body.length());
  encoder.encodeData(body_buffer, true);
  
  runFor(10ms);
}

// ===== Error Handling Tests =====

TEST_F(HttpServerCodecFilterTest, MalformedRequest) {
  filter_->onNewConnection();
  
  EXPECT_CALL(request_callbacks_, onError(_))
    .WillOnce([](const std::string& error) {
      EXPECT_FALSE(error.empty());
    });
  
  // Send malformed HTTP request
  OwnedBuffer malformed;
  malformed.add("INVALID HTTP REQUEST\r\n\r\n", 24);
  
  EXPECT_EQ(filter_->onData(malformed, false), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(HttpServerCodecFilterTest, IncompleteRequest) {
  filter_->onNewConnection();
  
  // Send partial request (headers only, no end)
  OwnedBuffer partial;
  partial.add("GET /test HTTP/1.1\r\nHost: example.com\r\n", 36);
  
  // Should not trigger any callbacks yet
  EXPECT_EQ(filter_->onData(partial, false), network::FilterStatus::Continue);
  runFor(10ms);
  
  // Complete the request
  EXPECT_CALL(request_callbacks_, onHeaders(_, true));
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  OwnedBuffer completion;
  completion.add("\r\n", 2);
  
  EXPECT_EQ(filter_->onData(completion, false), network::FilterStatus::Continue);
  runFor(10ms);
}

// ===== Keep-Alive Tests =====

TEST_F(HttpServerCodecFilterTest, KeepAliveConnection) {
  filter_->onNewConnection();
  
  // First request
  EXPECT_CALL(request_callbacks_, onHeaders(_, true));
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request1 = createHttpRequest("GET", "/first", {
    {"Host", "example.com"},
    {"Connection", "keep-alive"}
  });
  
  EXPECT_EQ(filter_->onData(request1, false), network::FilterStatus::Continue);
  runFor(10ms);
  
  // Second request on same connection
  EXPECT_CALL(request_callbacks_, onHeaders(_, true));
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request2 = createHttpRequest("GET", "/second", {
    {"Host", "example.com"},
    {"Connection", "keep-alive"}
  });
  
  EXPECT_EQ(filter_->onData(request2, false), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(HttpServerCodecFilterTest, ConnectionClose) {
  filter_->onNewConnection();
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, false))
    .WillOnce([](const auto& headers, bool keep_alive) {
      EXPECT_FALSE(keep_alive);
    });
  
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request = createHttpRequest("GET", "/test", {
    {"Host", "example.com"},
    {"Connection", "close"}
  });
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(10ms);
}

// ===== Multiple Request Processing Tests =====

TEST_F(HttpServerCodecFilterTest, MultipleRequests) {
  filter_->onNewConnection();
  
  for (int i = 0; i < 5; ++i) {
    EXPECT_CALL(request_callbacks_, onHeaders(_, true))
      .WillOnce([i](const auto& headers, bool keep_alive) {
        auto it = headers.find("url");
        EXPECT_NE(it, headers.end());
        EXPECT_EQ(it->second, "/request" + std::to_string(i));
      });
    
    EXPECT_CALL(request_callbacks_, onMessageComplete());
    
    auto request = createHttpRequest("GET", "/request" + std::to_string(i), {
      {"Host", "example.com"}
    });
    
    EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
    runFor(10ms);
  }
}

// ===== Header Processing Tests =====

TEST_F(HttpServerCodecFilterTest, CaseInsensitiveHeaders) {
  filter_->onNewConnection();
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, true))
    .WillOnce([](const auto& headers, bool keep_alive) {
      // Headers should be stored in lowercase
      EXPECT_NE(headers.find("content-type"), headers.end());
      EXPECT_NE(headers.find("accept"), headers.end());
      EXPECT_NE(headers.find("user-agent"), headers.end());
      
      // Verify values are preserved exactly
      EXPECT_EQ(headers.at("content-type"), "application/JSON");
      EXPECT_EQ(headers.at("accept"), "application/json, text/plain");
    });
  
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request = createHttpRequest("GET", "/test", {
    {"Content-Type", "application/JSON"},  // Mixed case
    {"ACCEPT", "application/json, text/plain"},  // Uppercase
    {"User-Agent", "TestClient/1.0"}  // Standard case
  });
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(HttpServerCodecFilterTest, MultilineHeaders) {
  filter_->onNewConnection();
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, true))
    .WillOnce([](const auto& headers, bool keep_alive) {
      auto it = headers.find("accept");
      EXPECT_NE(it, headers.end());
      // Should handle continuation properly
      EXPECT_FALSE(it->second.empty());
    });
  
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  // Create request with complex headers
  OwnedBuffer request;
  request.add("GET /test HTTP/1.1\r\n", 18);
  request.add("Host: example.com\r\n", 19);
  request.add("Accept: application/json,\r\n", 27);
  request.add(" text/plain\r\n", 13);  // Continuation line
  request.add("\r\n", 2);
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(10ms);
}

// ===== Status Code Tests =====

TEST_F(HttpServerCodecFilterTest, VariousStatusCodes) {
  filter_->onNewConnection();
  
  std::vector<std::pair<int, std::string>> status_codes = {
    {200, "OK"},
    {201, "Created"},
    {204, "No Content"},
    {400, "Bad Request"},
    {404, "Not Found"},
    {500, "Internal Server Error"},
    {418, "Unknown"}  // Custom status
  };
  
  for (const auto& [code, expected_text] : status_codes) {
    EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
      .WillOnce([code, expected_text](Buffer& data, bool end_stream) {
        size_t length = data.length();
        std::vector<char> response_data(length);
        data.copyOut(0, length, response_data.data());
        std::string response(response_data.begin(), response_data.end());
        
        std::string expected_line = "HTTP/1.1 " + std::to_string(code) + " " + expected_text;
        EXPECT_TRUE(response.find(expected_line) != std::string::npos) 
          << "Expected: " << expected_line << " in response: " << response;
      });
    
    auto& encoder = filter_->responseEncoder();
    encoder.encodeHeaders(code, {}, true);
    runFor(5ms);
  }
}

// ===== Performance and Memory Tests =====

TEST_F(HttpServerCodecFilterTest, LargeRequestBody) {
  filter_->onNewConnection();
  
  // Create 1MB body
  std::string large_body(1024 * 1024, 'X');
  
  EXPECT_CALL(request_callbacks_, onHeaders(_, true));
  EXPECT_CALL(request_callbacks_, onBody(large_body, true));
  EXPECT_CALL(request_callbacks_, onMessageComplete());
  
  auto request = createHttpRequest("POST", "/upload", {
    {"Host", "example.com"},
    {"Content-Type", "application/octet-stream"}
  }, large_body);
  
  EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
  runFor(100ms);  // Allow more time for large data
}

TEST_F(HttpServerCodecFilterTest, ManySmallRequests) {
  filter_->onNewConnection();
  
  const int num_requests = 1000;
  
  for (int i = 0; i < num_requests; ++i) {
    EXPECT_CALL(request_callbacks_, onHeaders(_, true));
    EXPECT_CALL(request_callbacks_, onMessageComplete());
    
    auto request = createHttpRequest("GET", "/small" + std::to_string(i), {
      {"Host", "example.com"}
    });
    
    EXPECT_EQ(filter_->onData(request, false), network::FilterStatus::Continue);
    
    if (i % 100 == 0) {
      runFor(5ms);  // Periodic processing
    }
  }
  
  runFor(50ms);  // Final processing
}

} // namespace
} // namespace filter
} // namespace mcp