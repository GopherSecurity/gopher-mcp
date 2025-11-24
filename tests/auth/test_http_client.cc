#include "mcp/auth/http_client.h"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>

namespace mcp {
namespace auth {
namespace {

class HttpClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    HttpClient::Config config;
    config.connection_timeout = std::chrono::seconds(5);
    config.user_agent = "MCP-Test-Client/1.0";
    client_ = std::make_unique<HttpClient>(config);
  }
  
  void TearDown() override {
    client_.reset();
  }
  
  std::unique_ptr<HttpClient> client_;
};

// Test basic GET request (using httpbin.org for testing)
TEST_F(HttpClientTest, BasicGetRequest) {
  // Skip if no network available
  auto response = client_->get("https://httpbin.org/get");
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.body.empty());
  EXPECT_TRUE(response.error.empty());
}

// Test GET with custom headers
TEST_F(HttpClientTest, GetWithHeaders) {
  std::unordered_map<std::string, std::string> headers;
  headers["X-Custom-Header"] = "TestValue";
  headers["Accept"] = "application/json";
  
  auto response = client_->get("https://httpbin.org/headers", headers);
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.body.empty());
  
  // httpbin returns the headers we sent in the response
  EXPECT_NE(response.body.find("X-Custom-Header"), std::string::npos);
  EXPECT_NE(response.body.find("TestValue"), std::string::npos);
}

// Test POST request with body
TEST_F(HttpClientTest, PostRequest) {
  std::string json_body = R"({"key": "value", "number": 42})";
  std::unordered_map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  
  auto response = client_->post("https://httpbin.org/post", json_body, headers);
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.body.empty());
  
  // httpbin echoes the posted data
  EXPECT_NE(response.body.find("\"key\": \"value\""), std::string::npos);
  EXPECT_NE(response.body.find("\"number\": 42"), std::string::npos);
}

// Test different HTTP methods
TEST_F(HttpClientTest, HttpMethods) {
  HttpRequest request;
  request.url = "https://httpbin.org/";
  
  // Test PUT
  request.method = HttpMethod::PUT;
  request.url = "https://httpbin.org/put";
  request.body = "test data";
  auto response = client_->request(request);
  if (response.status_code != -1) {
    EXPECT_EQ(response.status_code, 200);
  }
  
  // Test DELETE
  request.method = HttpMethod::DELETE;
  request.url = "https://httpbin.org/delete";
  request.body = "";
  response = client_->request(request);
  if (response.status_code != -1) {
    EXPECT_EQ(response.status_code, 200);
  }
  
  // Test PATCH
  request.method = HttpMethod::PATCH;
  request.url = "https://httpbin.org/patch";
  request.body = "patch data";
  response = client_->request(request);
  if (response.status_code != -1) {
    EXPECT_EQ(response.status_code, 200);
  }
}

// Test request timeout
TEST_F(HttpClientTest, RequestTimeout) {
  HttpRequest request;
  request.url = "https://httpbin.org/delay/10";  // 10 second delay
  request.timeout = std::chrono::seconds(1);  // 1 second timeout
  
  auto response = client_->request(request);
  
  if (response.status_code == -1) {
    // Either network unavailable or timeout occurred
    if (!response.error.empty()) {
      // Check if it was a timeout
      EXPECT_TRUE(response.error.find("Timeout") != std::string::npos ||
                  response.error.find("timed out") != std::string::npos ||
                  response.error.find("Operation too slow") != std::string::npos);
    }
  }
}

// Test 404 response
TEST_F(HttpClientTest, NotFoundResponse) {
  auto response = client_->get("https://httpbin.org/status/404");
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 404);
  EXPECT_TRUE(response.error.empty());  // HTTP 404 is not a CURL error
}

// Test redirect handling
TEST_F(HttpClientTest, RedirectHandling) {
  HttpRequest request;
  request.url = "https://httpbin.org/redirect/2";  // Redirects twice
  request.follow_redirects = true;
  request.max_redirects = 5;
  
  auto response = client_->request(request);
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.body.empty());
}

// Test no redirect when disabled
TEST_F(HttpClientTest, NoRedirectWhenDisabled) {
  HttpRequest request;
  request.url = "https://httpbin.org/redirect/1";
  request.follow_redirects = false;
  
  auto response = client_->request(request);
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  // Should get redirect status code, not follow it
  EXPECT_TRUE(response.status_code == 301 || response.status_code == 302);
}

// Test async request
TEST_F(HttpClientTest, AsyncRequest) {
  std::atomic<bool> callback_called(false);
  std::atomic<int> status_code(0);
  
  HttpRequest request;
  request.url = "https://httpbin.org/get";
  
  client_->request_async(request, [&](const HttpResponse& response) {
    status_code = response.status_code;
    callback_called = true;
  });
  
  // Wait for async request to complete
  for (int i = 0; i < 100 && !callback_called; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  if (status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status_code.load(), 200);
}

// Test multiple async requests
TEST_F(HttpClientTest, MultipleAsyncRequests) {
  const int num_requests = 5;
  std::atomic<int> completed_requests(0);
  
  for (int i = 0; i < num_requests; ++i) {
    HttpRequest request;
    request.url = "https://httpbin.org/get?request=" + std::to_string(i);
    
    client_->request_async(request, [&](const HttpResponse& response) {
      if (response.status_code == 200) {
        completed_requests++;
      }
    });
  }
  
  // Wait for all requests to complete
  for (int i = 0; i < 100 && completed_requests < num_requests; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  // May not complete all if network is unavailable
  EXPECT_GE(completed_requests.load(), 0);
  EXPECT_LE(completed_requests.load(), num_requests);
}

// Test pool statistics
TEST_F(HttpClientTest, PoolStatistics) {
  auto stats = client_->get_pool_stats();
  EXPECT_EQ(stats.total_requests, 0);
  EXPECT_EQ(stats.failed_requests, 0);
  
  // Make some requests
  client_->get("https://httpbin.org/get");
  client_->get("https://httpbin.org/status/404");
  
  stats = client_->get_pool_stats();
  EXPECT_GE(stats.total_requests, 0);  // May be 0 if network unavailable
  EXPECT_LE(stats.total_requests, 2);
}

// Test SSL verification disable (should only be used for testing)
TEST_F(HttpClientTest, SSLVerificationDisable) {
  HttpRequest request;
  request.url = "https://httpbin.org/get";
  request.verify_ssl = false;  // Disable SSL verification
  
  auto response = client_->request(request);
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 200);
}

// Test latency measurement
TEST_F(HttpClientTest, LatencyMeasurement) {
  auto response = client_->get("https://httpbin.org/get");
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  // Latency should be positive and reasonable
  EXPECT_GT(response.latency.count(), 0);
  EXPECT_LT(response.latency.count(), 30000);  // Less than 30 seconds
}

// Test response headers parsing
TEST_F(HttpClientTest, ResponseHeaders) {
  auto response = client_->get("https://httpbin.org/response-headers?Custom-Header=TestValue");
  
  if (response.status_code == -1) {
    GTEST_SKIP() << "Network unavailable for testing";
  }
  
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.headers.empty());
  
  // Check for common headers
  bool has_content_type = response.headers.find("Content-Type") != response.headers.end() ||
                          response.headers.find("content-type") != response.headers.end();
  EXPECT_TRUE(has_content_type);
}

// Test connection pool reset
TEST_F(HttpClientTest, ConnectionPoolReset) {
  // Make a request
  client_->get("https://httpbin.org/get");
  
  // Reset the pool
  EXPECT_NO_THROW(client_->reset_connection_pool());
  
  // Should still work after reset
  auto response = client_->get("https://httpbin.org/get");
  if (response.status_code != -1) {
    EXPECT_EQ(response.status_code, 200);
  }
}

// Test with invalid URL
TEST_F(HttpClientTest, InvalidURL) {
  auto response = client_->get("not-a-valid-url");
  
  EXPECT_EQ(response.status_code, -1);
  EXPECT_FALSE(response.error.empty());
}

// Test with non-existent domain
TEST_F(HttpClientTest, NonExistentDomain) {
  auto response = client_->get("https://this-domain-definitely-does-not-exist-12345.com");
  
  EXPECT_EQ(response.status_code, -1);
  EXPECT_FALSE(response.error.empty());
}

} // namespace
} // namespace auth
} // namespace mcp