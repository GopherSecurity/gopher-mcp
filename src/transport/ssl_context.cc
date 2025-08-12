/**
 * @file ssl_context.cc
 * @brief SSL/TLS context implementation following some good design patterns
 */

#include "mcp/transport/ssl_context.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <sstream>

namespace mcp {
namespace transport {

namespace {

/**
 * Initialize OpenSSL library
 * Thread-safe initialization using std::once_flag
 */
void initializeOpenSSL() {
  static std::once_flag init_flag;
  std::call_once(init_flag, []() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Initialize random number generator
    RAND_poll();
  });
}

/**
 * Get OpenSSL error string
 * Extracts error from OpenSSL error queue
 */
std::string getOpenSSLError() {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
  return std::string(buf);
}

/**
 * Convert protocol string to OpenSSL protocol version
 */
int protocolToVersion(const std::string& protocol) {
  if (protocol == "TLSv1") return TLS1_VERSION;
  if (protocol == "TLSv1.1") return TLS1_1_VERSION;
  if (protocol == "TLSv1.2") return TLS1_2_VERSION;
  if (protocol == "TLSv1.3") return TLS1_3_VERSION;
  return 0;
}

}  // namespace

// SslContext implementation

Result<SslContextSharedPtr> SslContext::create(const SslContextConfig& config) {
  // Initialize OpenSSL if needed
  initializeOpenSSL();
  
  // Create SSL context
  // Use TLS_method() for flexibility (supports all TLS versions)
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());
  if (!ctx) {
    return makeError("Failed to create SSL context: " + getOpenSSLError());
  }
  
  // Initialize context with configuration
  auto result = initialize(ctx, config);
  if (!result.ok()) {
    SSL_CTX_free(ctx);
    return makeError(result.error());
  }
  
  // Create and return shared context
  // Using shared_ptr with custom deleter for proper cleanup
  return std::shared_ptr<SslContext>(new SslContext(ctx, config));
}

SslContext::SslContext(SSL_CTX* ctx, const SslContextConfig& config)
    : ctx_(ctx), config_(config) {
  // Context is immutable after creation
}

SslContext::~SslContext() {
  if (ctx_) {
    SSL_CTX_free(ctx_);
    ctx_ = nullptr;
  }
}

SSL* SslContext::newSsl() const {
  std::lock_guard<std::mutex> lock(ssl_mutex_);
  
  SSL* ssl = SSL_new(ctx_);
  if (ssl) {
    ssl_connections_created_++;
    
    // Set SNI hostname for client connections
    if (config_.is_client && !config_.sni_hostname.empty()) {
      SSL_set_tlsext_host_name(ssl, config_.sni_hostname.c_str());
    }
  }
  
  return ssl;
}

VoidResult SslContext::setSniHostname(SSL* ssl, const std::string& hostname) {
  if (!ssl || hostname.empty()) {
    return makeError("Invalid SSL or hostname");
  }
  
  if (SSL_set_tlsext_host_name(ssl, hostname.c_str()) != 1) {
    return makeError("Failed to set SNI hostname: " + getOpenSSLError());
  }
  
  return VoidResult::success();
}

Result<bool> SslContext::verifyPeer(SSL* ssl) {
  if (!ssl) {
    return makeError("Invalid SSL connection");
  }
  
  // Get verification result
  long verify_result = SSL_get_verify_result(ssl);
  if (verify_result != X509_V_OK) {
    return makeError("Certificate verification failed: " +
                    std::string(X509_verify_cert_error_string(verify_result)));
  }
  
  // Get peer certificate
  X509* peer_cert = SSL_get_peer_certificate(ssl);
  if (!peer_cert) {
    return makeError("No peer certificate presented");
  }
  
  // Clean up certificate
  X509_free(peer_cert);
  
  return true;
}

VoidResult SslContext::initialize(SSL_CTX* ctx, const SslContextConfig& config) {
  // Configure protocols (TLS versions)
  auto result = configureProtocols(ctx, config.protocols);
  if (!result.ok()) {
    return result;
  }
  
  // Configure cipher suites
  result = configureCipherSuites(ctx, config.cipher_suites);
  if (!result.ok()) {
    return result;
  }
  
  // Load certificates and keys if provided
  if (!config.cert_chain_file.empty()) {
    result = loadCertificateChain(ctx, config.cert_chain_file);
    if (!result.ok()) {
      return result;
    }
  }
  
  if (!config.private_key_file.empty()) {
    result = loadPrivateKey(ctx, config.private_key_file);
    if (!result.ok()) {
      return result;
    }
  }
  
  // Load CA certificates for verification
  if (!config.ca_cert_file.empty()) {
    result = loadCaCertificates(ctx, config.ca_cert_file);
    if (!result.ok()) {
      return result;
    }
  }
  
  // Setup verification
  result = setupVerification(ctx, config);
  if (!result.ok()) {
    return result;
  }
  
  // Configure ALPN if specified
  if (!config.alpn_protocols.empty()) {
    result = configureAlpn(ctx, config.alpn_protocols);
    if (!result.ok()) {
      return result;
    }
  }
  
  // Configure session resumption
  if (config.enable_session_resumption) {
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);
    SSL_CTX_set_timeout(ctx, config.session_timeout);
  } else {
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
  }
  
  // Set mode for better performance
  // SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER: Allow buffer address to change between writes
  // SSL_MODE_ENABLE_PARTIAL_WRITE: Allow partial writes
  SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER |
                        SSL_MODE_ENABLE_PARTIAL_WRITE);
  
  return VoidResult::success();
}

VoidResult SslContext::loadCertificateChain(SSL_CTX* ctx, const std::string& cert_file) {
  if (SSL_CTX_use_certificate_chain_file(ctx, cert_file.c_str()) != 1) {
    return makeError("Failed to load certificate chain from " + cert_file +
                    ": " + getOpenSSLError());
  }
  return VoidResult::success();
}

VoidResult SslContext::loadPrivateKey(SSL_CTX* ctx, const std::string& key_file) {
  if (SSL_CTX_use_PrivateKey_file(ctx, key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
    return makeError("Failed to load private key from " + key_file +
                    ": " + getOpenSSLError());
  }
  
  // Verify that private key matches certificate
  if (SSL_CTX_check_private_key(ctx) != 1) {
    return makeError("Private key does not match certificate: " + getOpenSSLError());
  }
  
  return VoidResult::success();
}

VoidResult SslContext::loadCaCertificates(SSL_CTX* ctx, const std::string& ca_file) {
  if (SSL_CTX_load_verify_locations(ctx, ca_file.c_str(), nullptr) != 1) {
    return makeError("Failed to load CA certificates from " + ca_file +
                    ": " + getOpenSSLError());
  }
  return VoidResult::success();
}

VoidResult SslContext::configureProtocols(SSL_CTX* ctx,
                                         const std::vector<std::string>& protocols) {
  if (protocols.empty()) {
    return VoidResult::success();  // Use OpenSSL defaults
  }
  
  // Determine min and max protocol versions
  int min_version = TLS1_3_VERSION;
  int max_version = 0;
  
  for (const auto& protocol : protocols) {
    int version = protocolToVersion(protocol);
    if (version == 0) {
      return makeError("Unknown protocol: " + protocol);
    }
    min_version = std::min(min_version, version);
    max_version = std::max(max_version, version);
  }
  
  // Set protocol version range
  if (SSL_CTX_set_min_proto_version(ctx, min_version) != 1) {
    return makeError("Failed to set minimum protocol version: " + getOpenSSLError());
  }
  
  if (SSL_CTX_set_max_proto_version(ctx, max_version) != 1) {
    return makeError("Failed to set maximum protocol version: " + getOpenSSLError());
  }
  
  return VoidResult::success();
}

VoidResult SslContext::configureCipherSuites(SSL_CTX* ctx, const std::string& ciphers) {
  if (ciphers.empty()) {
    return VoidResult::success();  // Use OpenSSL defaults
  }
  
  // Set TLS 1.2 and below cipher suites
  if (SSL_CTX_set_cipher_list(ctx, ciphers.c_str()) != 1) {
    return makeError("Failed to set cipher suites: " + getOpenSSLError());
  }
  
  // For TLS 1.3, use separate ciphersuites setting
  // Default TLS 1.3 ciphers are usually fine
  
  return VoidResult::success();
}

VoidResult SslContext::setupVerification(SSL_CTX* ctx, const SslContextConfig& config) {
  if (config.verify_peer) {
    // Set verification mode
    int verify_mode = SSL_VERIFY_PEER;
    if (!config.is_client) {
      // Server: request client certificate but don't require it
      verify_mode |= SSL_VERIFY_CLIENT_ONCE;
    } else {
      // Client: fail if server certificate verification fails
      verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    
    SSL_CTX_set_verify(ctx, verify_mode, verifyCallback);
    
    // Set verification depth (certificate chain depth)
    SSL_CTX_set_verify_depth(ctx, 10);
  } else {
    // No verification
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  
  return VoidResult::success();
}

int SslContext::verifyCallback(int preverify_ok, X509_STORE_CTX* ctx) {
  // This callback is called for each certificate in the chain
  // preverify_ok indicates whether basic verification passed
  
  if (!preverify_ok) {
    // Basic verification failed
    // Could log the error here for debugging
    return 0;  // Fail verification
  }
  
  // Could add custom verification logic here
  // For example, checking specific certificate fields
  
  return 1;  // Accept certificate
}

VoidResult SslContext::configureAlpn(SSL_CTX* ctx,
                                    const std::vector<std::string>& alpn_protocols) {
  if (alpn_protocols.empty()) {
    return VoidResult::success();
  }
  
  // Build ALPN protocol list in wire format
  // Format: [length][protocol][length][protocol]...
  std::vector<unsigned char> alpn_list;
  for (const auto& protocol : alpn_protocols) {
    if (protocol.size() > 255) {
      return makeError("ALPN protocol too long: " + protocol);
    }
    alpn_list.push_back(static_cast<unsigned char>(protocol.size()));
    alpn_list.insert(alpn_list.end(), protocol.begin(), protocol.end());
  }
  
  // Set ALPN protocols
  if (SSL_CTX_set_alpn_protos(ctx, alpn_list.data(), alpn_list.size()) != 0) {
    return makeError("Failed to set ALPN protocols: " + getOpenSSLError());
  }
  
  return VoidResult::success();
}

// SslContextManager implementation

Result<SslContextSharedPtr> SslContextManager::getOrCreateContext(
    const SslContextConfig& config) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  // Generate cache key
  std::string cache_key = generateCacheKey(config);
  
  // Check cache
  auto it = context_cache_.find(cache_key);
  if (it != context_cache_.end()) {
    return it->second;
  }
  
  // Create new context
  auto result = SslContext::create(config);
  if (!result.ok()) {
    return result;
  }
  
  // Cache and return
  context_cache_[cache_key] = result.value();
  return result.value();
}

void SslContextManager::clearCache() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  context_cache_.clear();
}

std::string SslContextManager::generateCacheKey(const SslContextConfig& config) const {
  // Generate unique key from configuration
  // This is a simple implementation; could use hash for better performance
  std::stringstream ss;
  ss << config.cert_chain_file << "|"
     << config.private_key_file << "|"
     << config.ca_cert_file << "|"
     << config.verify_peer << "|"
     << config.is_client << "|"
     << config.cipher_suites;
  
  for (const auto& protocol : config.protocols) {
    ss << "|" << protocol;
  }
  
  return ss.str();
}

}  // namespace transport
}  // namespace mcp