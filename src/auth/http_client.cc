#include "mcp/auth/http_client.h"
#include <curl/curl.h>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>

namespace mcp {
namespace auth {

// CURL write callback
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  std::string* response = static_cast<std::string*>(userdata);
  response->append(ptr, size * nmemb);
  return size * nmemb;
}

// CURL header callback
static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
  auto* headers = static_cast<std::unordered_map<std::string, std::string>*>(userdata);
  std::string header(buffer, size * nitems);
  
  // Parse header line
  size_t colon_pos = header.find(':');
  if (colon_pos != std::string::npos) {
    std::string name = header.substr(0, colon_pos);
    std::string value = header.substr(colon_pos + 1);
    
    // Trim whitespace
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t\r\n") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t\r\n") + 1);
    
    if (!name.empty()) {
      (*headers)[name] = value;
    }
  }
  
  return size * nitems;
}

class HttpClient::Impl {
public:
  explicit Impl(const Config& config) 
      : config_(config),
        total_requests_(0),
        failed_requests_(0),
        total_latency_ms_(0) {
    // Initialize CURL globally (thread-safe)
    curl_global_init(CURL_GLOBAL_ALL);
  }
  
  ~Impl() {
    // Clean up CURL
    curl_global_cleanup();
  }
  
  HttpResponse request(const HttpRequest& request) {
    auto start = std::chrono::steady_clock::now();
    HttpResponse response;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
      response.status_code = -1;
      response.error = "Failed to initialize CURL";
      return response;
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    
    // Set method
    switch (request.method) {
      case HttpMethod::POST:
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        break;
      case HttpMethod::PUT:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        break;
      case HttpMethod::DELETE:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        break;
      case HttpMethod::HEAD:
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        break;
      case HttpMethod::OPTIONS:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
        break;
      case HttpMethod::PATCH:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        break;
      default: // GET
        break;
    }
    
    // Set request body
    if (!request.body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
    }
    
    // Set headers
    struct curl_slist* headers = nullptr;
    for (const auto& header_pair : request.headers) {
      std::string header = header_pair.first + ": " + header_pair.second;
      headers = curl_slist_append(headers, header.c_str());
    }
    if (headers) {
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // Set user agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());
    
    // Set SSL options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.verify_ssl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.verify_ssl ? 2L : 0L);
    
    if (!config_.ca_bundle_path.empty()) {
      curl_easy_setopt(curl, CURLOPT_CAINFO, config_.ca_bundle_path.c_str());
    }
    
    // Set redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(request.max_redirects));
    
    // Set timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connection_timeout.count());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout.count());
    
    // Set callbacks
    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Get response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response.status_code = static_cast<int>(http_code);
    
    // If curl failed, set status code to -1
    if (res != CURLE_OK && response.status_code == 0) {
      response.status_code = -1;
    }
    
    // Set response body
    response.body = response_body;
    
    // Handle errors
    if (res != CURLE_OK) {
      response.error = curl_easy_strerror(res);
      ++failed_requests_;
    }
    
    // Calculate latency
    auto end = std::chrono::steady_clock::now();
    response.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Update statistics
    ++total_requests_;
    total_latency_ms_ += response.latency.count();
    
    // Clean up
    if (headers) {
      curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    
    return response;
  }
  
  void request_async(const HttpRequest& request, ResponseCallback callback) {
    // Simple async implementation using std::thread
    // In production, would use a thread pool or async I/O
    std::thread([this, request, callback]() {
      HttpResponse response = this->request(request);
      callback(response);
    }).detach();
  }
  
  HttpClient::PoolStats get_pool_stats() const {
    PoolStats stats;
    stats.total_requests = total_requests_;
    stats.failed_requests = failed_requests_;
    stats.active_connections = 0; // Not implemented in this simple version
    stats.idle_connections = 0;   // Not implemented in this simple version
    
    if (total_requests_ > 0) {
      stats.avg_latency = std::chrono::milliseconds(total_latency_ms_ / total_requests_);
    } else {
      stats.avg_latency = std::chrono::milliseconds(0);
    }
    
    return stats;
  }
  
  void reset_connection_pool() {
    // In this simple implementation, there's no persistent pool
    // In production, would close all pooled connections
  }
  
  void set_ssl_verify_callback(std::function<bool(const std::string&)> callback) {
    ssl_verify_callback_ = callback;
  }

private:
  Config config_;
  std::atomic<size_t> total_requests_;
  std::atomic<size_t> failed_requests_;
  std::atomic<long long> total_latency_ms_;
  std::function<bool(const std::string&)> ssl_verify_callback_;
};

// HttpClient public interface implementation

HttpClient::HttpClient(const Config& config) 
    : impl_(std::make_unique<Impl>(config)) {
}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::request(const HttpRequest& request) {
  return impl_->request(request);
}

void HttpClient::request_async(const HttpRequest& request, ResponseCallback callback) {
  impl_->request_async(request, callback);
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::unordered_map<std::string, std::string>& headers) {
  HttpRequest request;
  request.url = url;
  request.method = HttpMethod::GET;
  request.headers = headers;
  return impl_->request(request);
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::string& body,
                              const std::unordered_map<std::string, std::string>& headers) {
  HttpRequest request;
  request.url = url;
  request.method = HttpMethod::POST;
  request.body = body;
  request.headers = headers;
  return impl_->request(request);
}

void HttpClient::reset_connection_pool() {
  impl_->reset_connection_pool();
}

HttpClient::PoolStats HttpClient::get_pool_stats() const {
  return impl_->get_pool_stats();
}

void HttpClient::set_ssl_verify_callback(std::function<bool(const std::string&)> callback) {
  impl_->set_ssl_verify_callback(callback);
}

} // namespace auth
} // namespace mcp