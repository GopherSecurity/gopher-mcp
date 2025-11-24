#include "mcp/auth/jwks_client.h"
#include "mcp/auth/http_client.h"
#include "mcp/auth/memory_cache.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <thread>
#include <atomic>
#include <sstream>
#include <regex>

namespace mcp {
namespace auth {

// JsonWebKey implementation
bool JsonWebKey::is_valid() const {
  if (kid.empty() || kty.empty()) {
    return false;
  }
  
  if (kty == "RSA") {
    return !n.empty() && !e.empty();
  } else if (kty == "EC") {
    return !crv.empty() && !x.empty() && !y.empty();
  } else if (kty == "oct") {
    // Symmetric key - not commonly used for JWKS
    return false; // We don't support symmetric keys in JWKS
  }
  
  return false;
}

JsonWebKey::KeyType JsonWebKey::get_key_type() const {
  if (kty == "RSA") return KeyType::RSA;
  if (kty == "EC") return KeyType::EC;
  if (kty == "oct") return KeyType::OCT;
  return KeyType::UNKNOWN;
}

// JwksResponse implementation
mcp::optional<JsonWebKey> JwksResponse::find_key(const std::string& kid) const {
  for (const auto& key : keys) {
    if (key.kid == kid && key.is_valid()) {
      return key;
    }
  }
  return mcp::nullopt;
}

bool JwksResponse::is_expired() const {
  auto now = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - fetched_at);
  return elapsed >= cache_duration;
}

// JwksClientConfig implementation
JwksClientConfig::JwksClientConfig()
    : default_cache_duration(3600),
      min_cache_duration(60),
      max_cache_duration(86400),
      respect_cache_control(true),
      max_keys_cached(100),
      request_timeout(30),
      auto_refresh(false),
      refresh_before_expiry(60) {}

// JwksClient::Impl class
class JwksClient::Impl {
public:
  explicit Impl(const JwksClientConfig& config)
      : config_(config),
        http_client_(HttpClient::Config()),
        cache_(config.max_keys_cached, config.default_cache_duration),
        auto_refresh_active_(false),
        cache_hits_(0),
        cache_misses_(0),
        refresh_count_(0),
        error_count_(0) {}
  
  ~Impl() {
    stop_auto_refresh();
  }
  
  mcp::optional<JwksResponse> fetch_keys(bool force_refresh) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check cache first unless forced refresh
    if (!force_refresh) {
      auto cached = cache_.get("jwks_response");
      if (cached.has_value()) {
        cache_hits_++;
        return cached.value();
      }
    }
    
    cache_misses_++;
    
    // Fetch from endpoint
    HttpRequest request;
    request.url = config_.jwks_uri;
    request.timeout = config_.request_timeout;
    
    auto response = http_client_.request(request);
    if (response.status_code != 200 || !response.error.empty()) {
      error_count_++;
      return mcp::nullopt;
    }
    
    // Parse response
    auto jwks = parse_jwks_internal(response.body);
    if (!jwks.has_value()) {
      error_count_++;
      return mcp::nullopt;
    }
    
    // Set cache duration based on headers
    auto cache_duration = config_.default_cache_duration;
    if (config_.respect_cache_control) {
      auto it = response.headers.find("cache-control");
      if (it == response.headers.end()) {
        it = response.headers.find("Cache-Control");
      }
      if (it != response.headers.end()) {
        auto parsed_duration = parse_cache_control_internal(it->second);
        cache_duration = std::max(config_.min_cache_duration,
                                 std::min(parsed_duration, config_.max_cache_duration));
      }
    }
    
    jwks.value().cache_duration = cache_duration;
    jwks.value().fetched_at = std::chrono::system_clock::now();
    
    // Store in cache
    cache_.put("jwks_response", jwks.value(), cache_duration);
    
    last_refresh_ = std::chrono::system_clock::now();
    next_refresh_ = last_refresh_ + cache_duration;
    refresh_count_++;
    
    return jwks;
  }
  
  mcp::optional<JsonWebKey> get_key(const std::string& kid) {
    auto jwks = fetch_keys(false);
    if (jwks.has_value()) {
      return jwks.value().find_key(kid);
    }
    return mcp::nullopt;
  }
  
  std::vector<JsonWebKey> get_all_keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto cached = cache_.get("jwks_response");
    if (cached.has_value()) {
      return cached.value().keys;
    }
    return {};
  }
  
  void start_auto_refresh(RefreshCallback on_refresh, ErrorCallback on_error) {
    std::lock_guard<std::mutex> lock(refresh_mutex_);
    if (auto_refresh_active_) {
      return;
    }
    
    auto_refresh_active_ = true;
    refresh_thread_ = std::thread([this, on_refresh, on_error]() {
      while (auto_refresh_active_) {
        // Calculate time until next refresh
        auto now = std::chrono::system_clock::now();
        auto time_until_refresh = next_refresh_ - config_.refresh_before_expiry - now;
        
        if (time_until_refresh <= std::chrono::seconds(0)) {
          // Time to refresh
          auto jwks = fetch_keys(true);
          if (jwks.has_value()) {
            if (on_refresh) {
              on_refresh(jwks.value());
            }
          } else {
            if (on_error) {
              on_error("Failed to refresh JWKS");
            }
          }
          
          // Sleep for a bit before checking again
          std::this_thread::sleep_for(std::chrono::seconds(10));
        } else {
          // Sleep until it's time to refresh
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!auto_refresh_active_) {
          break;
        }
      }
    });
  }
  
  void stop_auto_refresh() {
    {
      std::lock_guard<std::mutex> lock(refresh_mutex_);
      auto_refresh_active_ = false;
    }
    if (refresh_thread_.joinable()) {
      refresh_thread_.join();
    }
  }
  
  bool is_auto_refresh_active() const {
    std::lock_guard<std::mutex> lock(refresh_mutex_);
    return auto_refresh_active_;
  }
  
  void clear_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
  }
  
  JwksClient::CacheStats get_cache_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CacheStats stats;
    stats.keys_cached = cache_.size();
    stats.cache_hits = cache_hits_;
    stats.cache_misses = cache_misses_;
    stats.refresh_count = refresh_count_;
    stats.error_count = error_count_;
    stats.last_refresh = last_refresh_;
    stats.next_refresh = next_refresh_;
    return stats;
  }
  
  static mcp::optional<JwksResponse> parse_jwks_internal(const std::string& json) {
    try {
      auto j = nlohmann::json::parse(json);
      
      JwksResponse response;
      
      if (!j.contains("keys") || !j["keys"].is_array()) {
        return mcp::nullopt;
      }
      
      for (const auto& key_json : j["keys"]) {
        JsonWebKey key;
        
        // Required fields
        if (key_json.contains("kid")) key.kid = key_json["kid"];
        if (key_json.contains("kty")) key.kty = key_json["kty"];
        if (key_json.contains("use")) key.use = key_json["use"];
        if (key_json.contains("alg")) key.alg = key_json["alg"];
        
        // RSA fields
        if (key_json.contains("n")) key.n = key_json["n"];
        if (key_json.contains("e")) key.e = key_json["e"];
        
        // EC fields
        if (key_json.contains("crv")) key.crv = key_json["crv"];
        if (key_json.contains("x")) key.x = key_json["x"];
        if (key_json.contains("y")) key.y = key_json["y"];
        
        // Optional fields
        if (key_json.contains("x5c")) key.x5c = key_json["x5c"];
        if (key_json.contains("x5t")) key.x5t = key_json["x5t"];
        
        if (key.is_valid()) {
          response.keys.push_back(key);
        }
      }
      
      return response;
    } catch (...) {
      return mcp::nullopt;
    }
  }
  
  static std::chrono::seconds parse_cache_control_internal(const std::string& header) {
    // Look for max-age directive
    std::regex max_age_regex("max-age=(\\d+)");
    std::smatch match;
    
    if (std::regex_search(header, match, max_age_regex)) {
      if (match.size() > 1) {
        try {
          int seconds = std::stoi(match[1]);
          return std::chrono::seconds(seconds);
        } catch (...) {
          // Fall through to default
        }
      }
    }
    
    // Default to 1 hour if not found
    return std::chrono::seconds(3600);
  }

private:
  JwksClientConfig config_;
  HttpClient http_client_;
  mutable MemoryCache<std::string, JwksResponse, std::hash<std::string>> cache_;
  mutable std::mutex mutex_;
  mutable std::mutex refresh_mutex_;
  
  std::thread refresh_thread_;
  std::atomic<bool> auto_refresh_active_;
  
  mutable size_t cache_hits_;
  mutable size_t cache_misses_;
  size_t refresh_count_;
  size_t error_count_;
  
  std::chrono::system_clock::time_point last_refresh_;
  std::chrono::system_clock::time_point next_refresh_;
};

// JwksClient public implementation
JwksClient::JwksClient(const JwksClientConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

JwksClient::~JwksClient() = default;

mcp::optional<JwksResponse> JwksClient::fetch_keys(bool force_refresh) {
  return impl_->fetch_keys(force_refresh);
}

mcp::optional<JsonWebKey> JwksClient::get_key(const std::string& kid) {
  return impl_->get_key(kid);
}

std::vector<JsonWebKey> JwksClient::get_all_keys() const {
  return impl_->get_all_keys();
}

void JwksClient::start_auto_refresh(RefreshCallback on_refresh, ErrorCallback on_error) {
  impl_->start_auto_refresh(on_refresh, on_error);
}

void JwksClient::stop_auto_refresh() {
  impl_->stop_auto_refresh();
}

bool JwksClient::is_auto_refresh_active() const {
  return impl_->is_auto_refresh_active();
}

void JwksClient::clear_cache() {
  impl_->clear_cache();
}

JwksClient::CacheStats JwksClient::get_cache_stats() const {
  return impl_->get_cache_stats();
}

mcp::optional<JwksResponse> JwksClient::parse_jwks(const std::string& json) {
  return Impl::parse_jwks_internal(json);
}

std::chrono::seconds JwksClient::parse_cache_control(const std::string& header) {
  return Impl::parse_cache_control_internal(header);
}

} // namespace auth
} // namespace mcp