#ifndef MCP_AUTH_HTTP_CLIENT_H
#define MCP_AUTH_HTTP_CLIENT_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file http_client.h
 * @brief Async HTTP client interface for authentication module
 */

namespace mcp {
namespace auth {

/**
 * @brief HTTP request method
 */
enum class HttpMethod {
  GET,
  POST,
  PUT,
  DELETE,
  HEAD,
  OPTIONS,
  PATCH
};

/**
 * @brief HTTP response structure
 */
struct HttpResponse {
  int status_code;                                     // HTTP status code
  std::unordered_map<std::string, std::string> headers; // Response headers
  std::string body;                                    // Response body
  std::string error;                                   // Error message if request failed
  std::chrono::milliseconds latency;                   // Request latency
};

/**
 * @brief HTTP request configuration
 */
struct HttpRequest {
  std::string url;                                     // Request URL
  HttpMethod method;                 // HTTP method
  std::unordered_map<std::string, std::string> headers; // Request headers
  std::string body;                                    // Request body
  std::chrono::seconds timeout;                    // Request timeout
  bool verify_ssl;                              // Verify SSL certificates
  bool follow_redirects;                        // Follow HTTP redirects
  int max_redirects;                              // Maximum number of redirects
  
  HttpRequest() 
      : method(HttpMethod::GET),
        timeout(30),
        verify_ssl(true),
        follow_redirects(true),
        max_redirects(10) {}
};

/**
 * @brief Async HTTP client with connection pooling and SSL support
 */
class HttpClient {
public:
  using ResponseCallback = std::function<void(HttpResponse)>;
  
  /**
   * @brief Configuration for HTTP client
   */
  struct Config {
    size_t max_connections_per_host;              // Max concurrent connections per host
    size_t max_total_connections;                // Max total connections
    std::chrono::seconds connection_timeout;       // Connection timeout
    std::chrono::seconds read_timeout;             // Read timeout
    bool enable_connection_pooling;             // Enable connection pooling
    bool verify_ssl_certificates;               // Verify SSL certificates
    std::string ca_bundle_path;                        // Path to CA bundle file
    std::string user_agent;    // User agent string
    
    Config() 
        : max_connections_per_host(10),
          max_total_connections(100),
          connection_timeout(10),
          read_timeout(30),
          enable_connection_pooling(true),
          verify_ssl_certificates(true),
          user_agent("MCP-Auth-Client/1.0") {}
  };
  
  /**
   * @brief Construct HTTP client with configuration
   * @param config Client configuration
   */
  explicit HttpClient(const Config& config = Config());
  
  /**
   * @brief Destructor
   */
  ~HttpClient();
  
  /**
   * @brief Perform synchronous HTTP request
   * @param request Request configuration
   * @return HTTP response
   */
  HttpResponse request(const HttpRequest& request);
  
  /**
   * @brief Perform asynchronous HTTP request
   * @param request Request configuration
   * @param callback Callback to invoke with response
   */
  void request_async(const HttpRequest& request, ResponseCallback callback);
  
  /**
   * @brief Convenience method for GET request
   * @param url Request URL
   * @param headers Optional headers
   * @return HTTP response
   */
  HttpResponse get(const std::string& url,
                   const std::unordered_map<std::string, std::string>& headers = {});
  
  /**
   * @brief Convenience method for POST request
   * @param url Request URL
   * @param body Request body
   * @param headers Optional headers
   * @return HTTP response
   */
  HttpResponse post(const std::string& url,
                    const std::string& body,
                    const std::unordered_map<std::string, std::string>& headers = {});
  
  /**
   * @brief Reset connection pool (close all connections)
   */
  void reset_connection_pool();
  
  /**
   * @brief Get connection pool statistics
   */
  struct PoolStats {
    size_t active_connections;
    size_t idle_connections;
    size_t total_requests;
    size_t failed_requests;
    std::chrono::milliseconds avg_latency;
  };
  
  PoolStats get_pool_stats() const;
  
  /**
   * @brief Set custom SSL certificate verification callback
   * @param callback Verification callback returning true if certificate is valid
   */
  void set_ssl_verify_callback(std::function<bool(const std::string&)> callback);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_HTTP_CLIENT_H