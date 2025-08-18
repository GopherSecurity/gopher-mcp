/**
 * @file test_http_server_codec_filter.cc
 * @brief Real IO integration tests for HTTP server codec filter
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <map>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/network/connection.h"
#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

// Real request callbacks implementation for testing  
class TestRequestCallbacks : public HttpCodecFilter::MessageCallbacks {
public:
  void onHeaders(const std::map<std::string, std::string>& headers, bool keep_alive) {
    std::lock_guard<std::mutex> lock(mutex_);
    headers_received_ = true;
    headers_ = headers;
    keep_alive_ = keep_alive;
    headers_cv_.notify_all();
  }
  
  void onBody(const std::string& data, bool end_stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    body_received_ = true;
    body_data_ += data;
    end_stream_ = end_stream;
    body_cv_.notify_all();
  }
  
  void onMessageComplete() {
    std::lock_guard<std::mutex> lock(mutex_);
    message_complete_ = true;
    complete_cv_.notify_all();
  }
  
  void onError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_received_ = true;
    error_message_ = error;
    error_cv_.notify_all();
  }

  // Wait functions with timeout
  bool waitForHeaders(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return headers_cv_.wait_for(lock, timeout, [this] { return headers_received_; });
  }
  
  bool waitForBody(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return body_cv_.wait_for(lock, timeout, [this] { return body_received_; });
  }
  
  bool waitForComplete(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return complete_cv_.wait_for(lock, timeout, [this] { return message_complete_; });
  }
  
  bool waitForError(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return error_cv_.wait_for(lock, timeout, [this] { return error_received_; });
  }

  // Thread-safe accessors
  std::map<std::string, std::string> getHeaders() {
    std::lock_guard<std::mutex> lock(mutex_);
    return headers_;
  }
  
  std::string getBodyData() {
    std::lock_guard<std::mutex> lock(mutex_);
    return body_data_;
  }
  
  std::string getErrorMessage() {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
  }
  
  bool isKeepAlive() {
    std::lock_guard<std::mutex> lock(mutex_);
    return keep_alive_;
  }
  
  bool isEndStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    return end_stream_;
  }
  
  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    headers_received_ = false;
    body_received_ = false;
    message_complete_ = false;
    error_received_ = false;
    headers_.clear();
    body_data_.clear();
    error_message_.clear();
    keep_alive_ = false;
    end_stream_ = false;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable headers_cv_;
  std::condition_variable body_cv_;
  std::condition_variable complete_cv_;
  std::condition_variable error_cv_;
  
  bool headers_received_{false};
  bool body_received_{false};
  bool message_complete_{false};
  bool error_received_{false};
  std::map<std::string, std::string> headers_;
  std::string body_data_;
  std::string error_message_;
  bool keep_alive_{false};
  bool end_stream_{false};
};

// Real write filter callbacks implementation for testing
class TestWriteFilterCallbacks : public network::WriteFilterCallbacks {
public:
  explicit TestWriteFilterCallbacks(event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}
    
  network::Connection& connection() {
    static network::Connection* stub = nullptr;
    return *stub;  // This is a stub - will crash if called
  }
  
  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    write_called_ = true;
    
    // Copy the data
    size_t length = data.length();
    if (length > 0) {
      std::vector<char> buffer_data(length);
      data.copyOut(0, length, buffer_data.data());
      write_data_.append(buffer_data.data(), length);
    }
    
    end_stream_ = end_stream;
    write_cv_.notify_all();
  }
  
  void injectReadDataToFilterChain(Buffer& data, bool end_stream) {}
  
  event::Dispatcher& dispatcher() {
    return dispatcher_;
  }
  
  bool aboveWriteBufferHighWatermark() const { return false; }
  
  // Wait and access methods
  bool waitForWrite(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return write_cv_.wait_for(lock, timeout, [this] { return write_called_; });
  }
  
  std::string getWriteData() {
    std::lock_guard<std::mutex> lock(mutex_);
    return write_data_;
  }
  
  bool isEndStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    return end_stream_;
  }
  
  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_called_ = false;
    write_data_.clear();
    end_stream_ = false;
  }

private:
  event::Dispatcher& dispatcher_;
  mutable std::mutex mutex_;
  std::condition_variable write_cv_;
  bool write_called_{false};
  std::string write_data_;
  bool end_stream_{false};
};

// Stub read filter callbacks  
class StubReadFilterCallbacks : public network::ReadFilterCallbacks {
public:
  explicit StubReadFilterCallbacks(event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}
    
  network::Connection& connection() { 
    static network::Connection* stub = nullptr;
    return *stub;  // This is a stub - will crash if called
  }
  void continueReading() {}
  void injectReadDataToFilterChain(Buffer& data, bool end_stream) {}
  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) {}
  void onFilterInbound() {}
  void requestDecoder() {}
  const network::ConnectionInfo& connectionInfo() const {
    static const network::ConnectionInfo* stub_ptr = nullptr;
    return *stub_ptr;  // This is a stub - will crash if called
  }
  event::Dispatcher& dispatcher() { return dispatcher_; }
  void setDecoderBufferLimit(uint32_t limit) {}
  uint32_t decoderBufferLimit() { return 0; }
  bool cannotEncodeFrame() { return false; }
  void markUpstreamFilterChainComplete() {}
  const std::string& upstreamHost() const {
    static std::string stub;
    return stub;
  }
  void setUpstreamHost(const std::string& host) {}
  bool shouldContinueFilterChain() { return true; }
  
private:
  event::Dispatcher& dispatcher_;
};

class HttpCodecFilterRealIoTest : public test::RealIoTestBase {
protected:
  void SetUp() {
    test::RealIoTestBase::SetUp();
    
    // Create test callbacks
    request_callbacks_ = std::make_unique<TestRequestCallbacks>();
    
    // Create filter and callbacks in dispatcher context
    executeInDispatcher([this]() {
      filter_ = std::make_unique<HttpCodecFilter>(*request_callbacks_, *dispatcher_, true);
      
      read_callbacks_ = std::make_unique<StubReadFilterCallbacks>(*dispatcher_);
      write_callbacks_ = std::make_unique<TestWriteFilterCallbacks>(*dispatcher_);
      
      filter_->initializeReadFilterCallbacks(*read_callbacks_);
      filter_->initializeWriteFilterCallbacks(*write_callbacks_);
    });
  }

  void TearDown() {
    // Clean up in dispatcher context
    executeInDispatcher([this]() {
      filter_.reset();
      write_callbacks_.reset();
      read_callbacks_.reset();
    });
    
    request_callbacks_.reset();
    test::RealIoTestBase::TearDown();
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

  // Helper to process data in filter context
  void processHttpRequest(const OwnedBuffer& request) {
    executeInDispatcher([this, &request]() {
      auto& mutable_request = const_cast<OwnedBuffer&>(request);
      filter_->onNewConnection();
      filter_->onData(mutable_request, false);
    });
  }

  std::unique_ptr<TestRequestCallbacks> request_callbacks_;
  std::unique_ptr<StubReadFilterCallbacks> read_callbacks_;
  std::unique_ptr<TestWriteFilterCallbacks> write_callbacks_;
  std::unique_ptr<HttpCodecFilter> filter_;
};

// ===== Basic Request Processing Tests =====

TEST_F(HttpCodecFilterRealIoTest, InitialState) {
  auto status = executeInDispatcher([this]() {
    return filter_->onNewConnection();
  });
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

TEST_F(HttpCodecFilterRealIoTest, SimpleGetRequest) {
  auto request = createHttpRequest("GET", "/test", {
    {"Host", "example.com"},
    {"User-Agent", "test-client"}
  });
  
  processHttpRequest(request);
  
  // Wait for headers to be processed
  ASSERT_TRUE(request_callbacks_->waitForHeaders());
  ASSERT_TRUE(request_callbacks_->waitForComplete());
  
  // Verify headers
  auto headers = request_callbacks_->getHeaders();
  EXPECT_TRUE(request_callbacks_->isKeepAlive());
  
  EXPECT_EQ(headers["host"], "example.com");
  EXPECT_EQ(headers["user-agent"], "test-client");
  EXPECT_EQ(headers["url"], "/test");
}

TEST_F(HttpCodecFilterRealIoTest, PostRequestWithBody) {
  std::string expected_body = "{\"message\": \"hello world\"}";
  
  auto request = createHttpRequest("POST", "/api/test", {
    {"Host", "example.com"},
    {"Content-Type", "application/json"}
  }, expected_body);
  
  processHttpRequest(request);
  
  // Wait for all processing to complete
  ASSERT_TRUE(request_callbacks_->waitForHeaders());
  ASSERT_TRUE(request_callbacks_->waitForBody());
  ASSERT_TRUE(request_callbacks_->waitForComplete());
  
  // Verify headers
  auto headers = request_callbacks_->getHeaders();
  EXPECT_TRUE(request_callbacks_->isKeepAlive());
  EXPECT_EQ(headers["content-type"], "application/json");
  EXPECT_EQ(headers["url"], "/api/test");
  
  // Verify body
  EXPECT_EQ(request_callbacks_->getBodyData(), expected_body);
  EXPECT_TRUE(request_callbacks_->isEndStream());
}

TEST_F(HttpCodecFilterRealIoTest, MalformedRequest) {
  executeInDispatcher([this]() {
    filter_->onNewConnection();
    
    // Send malformed HTTP request
    OwnedBuffer malformed;
    malformed.add("INVALID HTTP REQUEST\r\n\r\n", 24);
    
    filter_->onData(malformed, false);
  });
  
  // Should receive error
  ASSERT_TRUE(request_callbacks_->waitForError());
  EXPECT_FALSE(request_callbacks_->getErrorMessage().empty());
}

TEST_F(HttpCodecFilterRealIoTest, KeepAliveConnection) {
  // First request
  auto request1 = createHttpRequest("GET", "/first", {
    {"Host", "example.com"},
    {"Connection", "keep-alive"}
  });
  
  processHttpRequest(request1);
  
  ASSERT_TRUE(request_callbacks_->waitForHeaders());
  ASSERT_TRUE(request_callbacks_->waitForComplete());
  EXPECT_TRUE(request_callbacks_->isKeepAlive());
  
  // Reset for second request
  request_callbacks_->reset();
  
  // Second request on same connection
  auto request2 = createHttpRequest("GET", "/second", {
    {"Host", "example.com"},
    {"Connection", "keep-alive"}
  });
  
  executeInDispatcher([this, &request2]() {
    auto& mutable_request = const_cast<OwnedBuffer&>(request2);
    filter_->onData(mutable_request, false);
  });
  
  ASSERT_TRUE(request_callbacks_->waitForHeaders());
  ASSERT_TRUE(request_callbacks_->waitForComplete());
  
  auto headers = request_callbacks_->getHeaders();
  EXPECT_EQ(headers["url"], "/second");
  EXPECT_TRUE(request_callbacks_->isKeepAlive());
}

TEST_F(HttpCodecFilterRealIoTest, ConnectionClose) {
  auto request = createHttpRequest("GET", "/test", {
    {"Host", "example.com"},
    {"Connection", "close"}
  });
  
  processHttpRequest(request);
  
  ASSERT_TRUE(request_callbacks_->waitForHeaders());
  ASSERT_TRUE(request_callbacks_->waitForComplete());
  
  // Should not keep alive
  EXPECT_FALSE(request_callbacks_->isKeepAlive());
}

// ===== Response Encoding Tests =====

TEST_F(HttpCodecFilterRealIoTest, SimpleResponse) {
  executeInDispatcher([this]() {
    filter_->onNewConnection();
    
    auto& encoder = filter_->messageEncoder();
    encoder.encodeHeaders("200", {
      {"Content-Type", "application/json"},
      {"Cache-Control", "no-cache"}
    }, true);
  });
  
  // Wait for write callback
  ASSERT_TRUE(write_callbacks_->waitForWrite());
  
  std::string response = write_callbacks_->getWriteData();
  EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
  EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
  EXPECT_TRUE(response.find("\r\n\r\n") != std::string::npos);
}

TEST_F(HttpCodecFilterRealIoTest, ResponseWithBody) {
  std::string response_body = "{\"status\": \"success\"}";
  
  // Send headers first
  executeInDispatcher([this]() {
    filter_->onNewConnection();
    
    auto& encoder = filter_->messageEncoder();
    encoder.encodeHeaders("201", {
      {"Content-Type", "application/json"}
    }, false);
  });
  
  // Wait for headers write
  ASSERT_TRUE(write_callbacks_->waitForWrite());
  
  std::string headers_write = write_callbacks_->getWriteData();
  EXPECT_TRUE(headers_write.find("HTTP/1.1 201 Created") != std::string::npos);
  
  // Reset callbacks for body write
  write_callbacks_->reset();
  
  // Send body separately
  executeInDispatcher([this, &response_body]() {
    auto& encoder = filter_->messageEncoder();
    OwnedBuffer body_buffer;
    body_buffer.add(response_body.c_str(), response_body.length());
    encoder.encodeData(body_buffer, true);
  });
  
  // Wait for body write
  ASSERT_TRUE(write_callbacks_->waitForWrite());
  
  std::string body_write = write_callbacks_->getWriteData();
  EXPECT_EQ(body_write, response_body);
}

// ===== State Machine Integration Tests =====

TEST_F(HttpCodecFilterRealIoTest, StateMachineIntegration) {
  // This test verifies that the state machine is properly integrated
  auto request = createHttpRequest("GET", "/state-test", {
    {"Host", "example.com"}
  });
  
  processHttpRequest(request);
  
  // Should complete successfully with state machine managing the flow
  ASSERT_TRUE(request_callbacks_->waitForHeaders());
  ASSERT_TRUE(request_callbacks_->waitForComplete());
  
  auto headers = request_callbacks_->getHeaders();
  EXPECT_EQ(headers["url"], "/state-test");
}

} // namespace
} // namespace filter
} // namespace mcp