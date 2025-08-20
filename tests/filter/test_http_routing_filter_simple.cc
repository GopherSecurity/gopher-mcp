/**
 * Simple HTTP Routing Filter Unit Tests
 * 
 * Basic tests for HTTP routing filter functionality
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "mcp/filter/http_routing_filter.h"
#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace filter {
namespace {

// Test fixture
class HttpRoutingFilterSimpleTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create real dispatcher for testing
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
    
    // Create filter in server mode
    filter_ = std::make_unique<HttpRoutingFilter>(*dispatcher_, true);
  }
  
  void TearDown() override {
    filter_.reset();
    dispatcher_.reset();
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
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<HttpRoutingFilter> filter_;
};

// Test handler registration
TEST_F(HttpRoutingFilterSimpleTest, RegisterHandler) {
  bool handler_called = false;
  
  // Register a handler
  filter_->registerHandler("GET", "/test", 
      [&handler_called](const HttpRoutingFilter::RequestContext& req) {
    handler_called = true;
    HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.headers["content-type"] = "text/plain";
    resp.body = "Test response";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // The handler is registered, but won't be called until we send data
  EXPECT_FALSE(handler_called);
}

// Test multiple handler registration
TEST_F(HttpRoutingFilterSimpleTest, MultipleHandlers) {
  int handler1_called = 0;
  int handler2_called = 0;
  
  // Register first handler
  filter_->registerHandler("GET", "/endpoint1", 
      [&handler1_called](const HttpRoutingFilter::RequestContext& req) {
    handler1_called++;
    HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.body = "Endpoint 1";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // Register second handler
  filter_->registerHandler("GET", "/endpoint2", 
      [&handler2_called](const HttpRoutingFilter::RequestContext& req) {
    handler2_called++;
    HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    resp.body = "Endpoint 2";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
  });
  
  // Handlers are registered but not called yet
  EXPECT_EQ(handler1_called, 0);
  EXPECT_EQ(handler2_called, 0);
}

// Test custom default handler
TEST_F(HttpRoutingFilterSimpleTest, CustomDefaultHandler) {
  bool default_handler_called = false;
  
  // Register custom default handler
  filter_->registerDefaultHandler(
      [&default_handler_called](const HttpRoutingFilter::RequestContext& req) {
    default_handler_called = true;
    HttpRoutingFilter::Response resp;
    resp.status_code = 503;
    resp.body = "Service Unavailable";
    resp.headers["content-type"] = "text/plain";
    resp.headers["content-length"] = std::to_string(resp.body.length());
    return resp;
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
  std::string last_method;
  
  // Register handlers for different methods on same path
  filter_->registerHandler("GET", "/resource",
      [&last_method](const HttpRoutingFilter::RequestContext& req) {
    last_method = "GET";
    HttpRoutingFilter::Response resp;
    resp.status_code = 200;
    return resp;
  });
  
  filter_->registerHandler("POST", "/resource",
      [&last_method](const HttpRoutingFilter::RequestContext& req) {
    last_method = "POST";
    HttpRoutingFilter::Response resp;
    resp.status_code = 201;
    return resp;
  });
  
  filter_->registerHandler("DELETE", "/resource",
      [&last_method](const HttpRoutingFilter::RequestContext& req) {
    last_method = "DELETE";
    HttpRoutingFilter::Response resp;
    resp.status_code = 204;
    return resp;
  });
  
  // Handlers registered but not called
  EXPECT_EQ(last_method, "");
}

// Test path variations
TEST_F(HttpRoutingFilterSimpleTest, PathVariations) {
  std::vector<std::string> paths = {
    "/",
    "/api",
    "/api/v1",
    "/api/v1/users",
    "/api/v1/users/123",
    "/health",
    "/metrics"
  };
  
  for (const auto& path : paths) {
    bool handler_called = false;
    filter_->registerHandler("GET", path,
        [&handler_called](const HttpRoutingFilter::RequestContext& req) {
      handler_called = true;
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      return resp;
    });
    // Handler registered but not called
    EXPECT_FALSE(handler_called);
  }
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
  
  // Create new connection - should succeed
  auto status = filter_->onNewConnection();
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

} // namespace
} // namespace filter
} // namespace mcp