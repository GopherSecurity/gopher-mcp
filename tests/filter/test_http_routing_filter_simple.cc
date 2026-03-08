/**
 * Simple HTTP Routing Filter Unit Tests
 *
 * Basic tests for HTTP routing filter functionality using real I/O
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_routing_filter.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::Return;

// Mock callbacks for testing
class MockMessageCallbacks : public HttpCodecFilter::MessageCallbacks {
 public:
  MOCK_METHOD(void,
              onHeaders,
              ((const std::map<std::string, std::string>&), bool),
              (override));
  MOCK_METHOD(void, onBody, (const std::string&, bool), (override));
  MOCK_METHOD(void, onMessageComplete, (), (override));
  MOCK_METHOD(void, onError, (const std::string&), (override));
};

// Mock encoder for testing
class MockMessageEncoder : public HttpCodecFilter::MessageEncoder {
 public:
  MOCK_METHOD(void,
              encodeHeaders,
              (const std::string&,
               (const std::map<std::string, std::string>&),
               bool,
               const std::string&),
              (override));
  MOCK_METHOD(void, encodeData, (Buffer&, bool), (override));
};

// Test fixture using real I/O test base
class HttpRoutingFilterSimpleTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();

    // Create mocks and filter in dispatcher thread
    executeInDispatcher([this]() {
      next_callbacks_ = std::make_unique<MockMessageCallbacks>();
      encoder_ = std::make_unique<MockMessageEncoder>();
      filter_ = std::make_unique<HttpRoutingFilter>(next_callbacks_.get(),
                                                    encoder_.get(), true);
    });
  }

  void TearDown() override {
    // Clean up filter in dispatcher thread
    executeInDispatcher([this]() {
      filter_.reset();
      encoder_.reset();
      next_callbacks_.reset();
    });

    RealIoTestBase::TearDown();
  }

  // Helper to create HTTP request
  std::string createHttpRequest(const std::string& method,
                                const std::string& path,
                                const std::string& body = "") {
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: localhost\r\n";
    if (!body.empty()) {
      request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    }
    request += "Connection: keep-alive\r\n";
    request += "\r\n";
    if (!body.empty()) {
      request += body;
    }
    return request;
  }

  std::unique_ptr<HttpRoutingFilter> filter_;
  std::unique_ptr<MockMessageCallbacks> next_callbacks_;
  std::unique_ptr<MockMessageEncoder> encoder_;
};

// Test handler registration
TEST_F(HttpRoutingFilterSimpleTest, RegisterHandler) {
  std::atomic<bool> handler_called(false);

  // Register a handler in dispatcher thread
  executeInDispatcher([this, &handler_called]() {
    filter_->registerHandler(
        "GET", "/test",
        [&handler_called](const HttpRoutingFilter::RequestContext& req) {
          handler_called = true;
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          resp.headers["content-type"] = "text/plain";
          resp.body = "Test response";
          resp.headers["content-length"] = std::to_string(resp.body.length());
          return resp;
        });
  });

  // The handler is registered, but won't be called until we send data
  EXPECT_FALSE(handler_called);
}

// Test multiple handler registration
TEST_F(HttpRoutingFilterSimpleTest, MultipleHandlers) {
  std::atomic<int> handler1_called(0);
  std::atomic<int> handler2_called(0);

  // Register handlers in dispatcher thread
  executeInDispatcher([this, &handler1_called, &handler2_called]() {
    // Register first handler
    filter_->registerHandler(
        "GET", "/endpoint1",
        [&handler1_called](const HttpRoutingFilter::RequestContext& req) {
          handler1_called++;
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          resp.body = "Endpoint 1";
          resp.headers["content-length"] = std::to_string(resp.body.length());
          return resp;
        });

    // Register second handler
    filter_->registerHandler(
        "GET", "/endpoint2",
        [&handler2_called](const HttpRoutingFilter::RequestContext& req) {
          handler2_called++;
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          resp.body = "Endpoint 2";
          resp.headers["content-length"] = std::to_string(resp.body.length());
          return resp;
        });
  });

  // Handlers are registered but not called yet
  EXPECT_EQ(handler1_called, 0);
  EXPECT_EQ(handler2_called, 0);
}

// Test custom default handler
TEST_F(HttpRoutingFilterSimpleTest, CustomDefaultHandler) {
  std::atomic<bool> default_handler_called(false);

  // Register custom default handler in dispatcher thread
  executeInDispatcher([this, &default_handler_called]() {
    filter_->registerDefaultHandler(
        [&default_handler_called](
            const HttpRoutingFilter::RequestContext& req) {
          default_handler_called = true;
          HttpRoutingFilter::Response resp;
          resp.status_code = 503;
          resp.body = "Service Unavailable";
          resp.headers["content-type"] = "text/plain";
          resp.headers["content-length"] = std::to_string(resp.body.length());
          return resp;
        });
  });

  // Handler is registered but not called yet
  EXPECT_FALSE(default_handler_called);
}

// Test request context structure
TEST_F(HttpRoutingFilterSimpleTest, RequestContext) {
  HttpRoutingFilter::RequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/api/data";
  ctx.headers["content-type"] = "application/json";
  ctx.body = "{\"key\":\"value\"}";
  ctx.keep_alive = true;

  EXPECT_EQ(ctx.method, "POST");
  EXPECT_EQ(ctx.path, "/api/data");
  EXPECT_EQ(ctx.headers["content-type"], "application/json");
  EXPECT_EQ(ctx.body, "{\"key\":\"value\"}");
  EXPECT_TRUE(ctx.keep_alive);
}

// Test response structure
TEST_F(HttpRoutingFilterSimpleTest, ResponseStructure) {
  HttpRoutingFilter::Response resp;
  resp.status_code = 201;
  resp.headers["content-type"] = "application/json";
  resp.headers["location"] = "/api/data/123";
  resp.body = "{\"id\":123}";

  EXPECT_EQ(resp.status_code, 201);
  EXPECT_EQ(resp.headers["content-type"], "application/json");
  EXPECT_EQ(resp.headers["location"], "/api/data/123");
  EXPECT_EQ(resp.body, "{\"id\":123}");
}

// Test method-based routing
TEST_F(HttpRoutingFilterSimpleTest, MethodBasedRouting) {
  std::atomic<int> last_method_id(0);

  // Register handlers for different methods on same path in dispatcher thread
  executeInDispatcher([this, &last_method_id]() {
    filter_->registerHandler(
        "GET", "/resource",
        [&last_method_id](const HttpRoutingFilter::RequestContext& req) {
          last_method_id = 1;
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          return resp;
        });

    filter_->registerHandler(
        "POST", "/resource",
        [&last_method_id](const HttpRoutingFilter::RequestContext& req) {
          last_method_id = 2;
          HttpRoutingFilter::Response resp;
          resp.status_code = 201;
          return resp;
        });

    filter_->registerHandler(
        "DELETE", "/resource",
        [&last_method_id](const HttpRoutingFilter::RequestContext& req) {
          last_method_id = 3;
          HttpRoutingFilter::Response resp;
          resp.status_code = 204;
          return resp;
        });
  });

  // Handlers registered but not called
  EXPECT_EQ(last_method_id, 0);
}

// Test path variations
TEST_F(HttpRoutingFilterSimpleTest, PathVariations) {
  std::vector<std::string> paths = {
      "/",       "/api",    "/api/v1", "/api/v1/users", "/api/v1/users/123",
      "/health", "/metrics"};

  std::atomic<int> handlers_registered(0);

  executeInDispatcher([this, &paths, &handlers_registered]() {
    for (const auto& path : paths) {
      filter_->registerHandler(
          "GET", path, [](const HttpRoutingFilter::RequestContext& req) {
            HttpRoutingFilter::Response resp;
            resp.status_code = 200;
            return resp;
          });
      handlers_registered++;
    }
  });

  EXPECT_EQ(handlers_registered, paths.size());
}

// Test HTTP request creation helper
TEST_F(HttpRoutingFilterSimpleTest, RequestCreation) {
  std::string req1 = createHttpRequest("GET", "/test");
  EXPECT_NE(req1.find("GET /test HTTP/1.1"), std::string::npos);
  EXPECT_NE(req1.find("Host: localhost"), std::string::npos);
  EXPECT_NE(req1.find("Connection: keep-alive"), std::string::npos);

  std::string req2 = createHttpRequest("POST", "/api", "data");
  EXPECT_NE(req2.find("POST /api HTTP/1.1"), std::string::npos);
  EXPECT_NE(req2.find("Content-Length: 4"), std::string::npos);
  EXPECT_NE(req2.find("data"), std::string::npos);
}

// Test filter initialization
TEST_F(HttpRoutingFilterSimpleTest, FilterInitialization) {
  // Filter should be properly initialized
  EXPECT_NE(filter_, nullptr);

  // HttpRoutingFilter doesn't have onNewConnection - it's a MessageCallbacks
  // not a Filter Just verify it was created successfully
  executeInDispatcher([this]() {
    // Filter is ready to handle HTTP messages
    EXPECT_NE(filter_, nullptr);
  });
}

// Test POST request body handling
// POST handlers should receive the complete request body
TEST_F(HttpRoutingFilterSimpleTest, PostBodyHandling) {
  std::atomic<bool> handler_called(false);
  std::string received_body;

  executeInDispatcher([this, &handler_called, &received_body]() {
    filter_->registerHandler("POST", "/api/data",
                             [&handler_called, &received_body](
                                 const HttpRoutingFilter::RequestContext& req) {
                               handler_called = true;
                               received_body = req.body;
                               HttpRoutingFilter::Response resp;
                               resp.status_code = 201;
                               resp.body = "Created";
                               resp.headers["content-length"] =
                                   std::to_string(resp.body.length());
                               return resp;
                             });

    // Simulate POST request with headers
    std::map<std::string, std::string> headers;
    headers[":method"] = "POST";
    headers[":path"] = "/api/data";
    headers["content-type"] = "application/json";

    // Call onHeaders - should NOT call handler yet (waiting for body)
    filter_->onHeaders(headers, true);
  });

  // Handler should not be called yet - waiting for body
  EXPECT_FALSE(handler_called);
}

// Test POST body accumulation
TEST_F(HttpRoutingFilterSimpleTest, PostBodyAccumulation) {
  std::string received_body;
  std::atomic<bool> handler_called(false);

  executeInDispatcher([this, &handler_called, &received_body]() {
    filter_->registerHandler("POST", "/upload",
                             [&handler_called, &received_body](
                                 const HttpRoutingFilter::RequestContext& req) {
                               handler_called = true;
                               received_body = req.body;
                               HttpRoutingFilter::Response resp;
                               resp.status_code = 200;
                               return resp;
                             });

    std::map<std::string, std::string> headers;
    headers[":method"] = "POST";
    headers[":path"] = "/upload";

    filter_->onHeaders(headers, true);

    // Accumulate body in chunks
    filter_->onBody("chunk1", false);
    filter_->onBody("chunk2", false);
    filter_->onBody("chunk3", true);

    // Complete the message - handler should be called with full body
    filter_->onMessageComplete();
  });

  EXPECT_TRUE(handler_called);
  EXPECT_EQ(received_body, "chunk1chunk2chunk3");
}

// Test query string handling - routing uses path without query string
TEST_F(HttpRoutingFilterSimpleTest, QueryStringRouting) {
  std::atomic<bool> handler_called(false);
  std::string received_path;

  executeInDispatcher([this, &handler_called, &received_path]() {
    // Register handler for /search (without query string)
    filter_->registerHandler("GET", "/search",
                             [&handler_called, &received_path](
                                 const HttpRoutingFilter::RequestContext& req) {
                               handler_called = true;
                               received_path = req.path;
                               HttpRoutingFilter::Response resp;
                               resp.status_code = 200;
                               return resp;
                             });

    // Request with query string should still route to /search handler
    std::map<std::string, std::string> headers;
    headers[":method"] = "GET";
    headers[":path"] = "/search?q=test&page=1";
    headers["url"] = "/search?q=test&page=1";

    filter_->onHeaders(headers, true);
  });

  EXPECT_TRUE(handler_called);
  // Handler should receive full URL with query string
  EXPECT_EQ(received_path, "/search?q=test&page=1");
}

// Test PUT request body handling (similar to POST)
TEST_F(HttpRoutingFilterSimpleTest, PutBodyHandling) {
  std::string received_body;
  std::atomic<bool> handler_called(false);

  executeInDispatcher([this, &handler_called, &received_body]() {
    filter_->registerHandler("PUT", "/resource/123",
                             [&handler_called, &received_body](
                                 const HttpRoutingFilter::RequestContext& req) {
                               handler_called = true;
                               received_body = req.body;
                               HttpRoutingFilter::Response resp;
                               resp.status_code = 200;
                               return resp;
                             });

    std::map<std::string, std::string> headers;
    headers[":method"] = "PUT";
    headers[":path"] = "/resource/123";

    filter_->onHeaders(headers, true);
    filter_->onBody("{\"updated\": true}", true);
    filter_->onMessageComplete();
  });

  EXPECT_TRUE(handler_called);
  EXPECT_EQ(received_body, "{\"updated\": true}");
}

// Test GET request does not wait for body
TEST_F(HttpRoutingFilterSimpleTest, GetRequestImmediateExecution) {
  std::atomic<bool> handler_called(false);

  executeInDispatcher([this, &handler_called]() {
    filter_->registerHandler(
        "GET", "/immediate",
        [&handler_called](const HttpRoutingFilter::RequestContext& req) {
          handler_called = true;
          HttpRoutingFilter::Response resp;
          resp.status_code = 200;
          return resp;
        });

    std::map<std::string, std::string> headers;
    headers[":method"] = "GET";
    headers[":path"] = "/immediate";

    // GET should execute handler immediately in onHeaders
    filter_->onHeaders(headers, true);
  });

  // Handler should be called immediately for GET
  EXPECT_TRUE(handler_called);
}

}  // namespace
}  // namespace filter
}  // namespace mcp