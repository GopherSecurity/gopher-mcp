/**
 * @file ssl_context.h
 * @brief SSL/TLS context management following good architecture
 *
 * This provides SSL context management with:
 * - Certificate and key management
 * - SSL/TLS protocol configuration
 * - Context sharing across connections
 * - Thread-safe context initialization
 * 
 * Following some good design principles:
 * - Shared context model for efficient memory usage
 * - Immutable configuration after creation
 * - Support for both client and server contexts
 */

#ifndef MCP_TRANSPORT_SSL_CONTEXT_H
#define MCP_TRANSPORT_SSL_CONTEXT_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mcp/core/result.h"

// Forward declare OpenSSL types to avoid exposing OpenSSL headers
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct x509_store_ctx_st X509_STORE_CTX;

namespace mcp {
namespace transport {

// Forward declarations
class SslContext;
using SslContextSharedPtr = std::shared_ptr<SslContext>;

/**
 * SSL/TLS configuration
 * Immutable configuration for SSL context creation
 */
struct SslContextConfig {
  // Certificate and key files
  std::string cert_chain_file;     // Server/client certificate chain
  std::string private_key_file;    // Private key for certificate
  std::string ca_cert_file;        // CA certificates for verification
  
  // Verification settings
  bool verify_peer{true};          // Verify peer certificate
  bool verify_peer_cert_chain{true}; // Verify full cert chain
  std::vector<std::string> verify_san;  // Expected Subject Alternative Names
  std::string verify_hostname;     // Expected hostname for verification
  
  // Protocol settings
  std::vector<std::string> protocols{"TLSv1.2", "TLSv1.3"};  // Allowed protocols
  std::string cipher_suites{
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:"
    "ECDHE-RSA-CHACHA20-POLY1305"
  };  // TLS cipher suites
  
  // Session settings
  bool enable_session_resumption{true};  // Enable TLS session resumption
  uint32_t session_timeout{300};         // Session timeout in seconds
  
  // ALPN settings (Application-Layer Protocol Negotiation)
  std::vector<std::string> alpn_protocols;  // e.g., {"h2", "http/1.1"}
  
  // Client-specific settings
  bool is_client{false};           // True for client context, false for server
  std::string sni_hostname;        // Server Name Indication for client
};

/**
 * SSL Context wrapper
 * 
 * Manages OpenSSL context lifecycle with RAII pattern.
 * Thread-safe and can be shared across multiple connections.
 * 
 * Following some good design principles:
 * - Immutable after creation
 * - Shared across connections for efficiency
 * - Proper cleanup via RAII
 */
class SslContext {
public:
  /**
   * Create SSL context with given configuration
   * 
   * @param config SSL configuration
   * @return Result containing shared context or error
   * 
   * Flow:
   * 1. Create OpenSSL context
   * 2. Load certificates and keys
   * 3. Configure protocols and ciphers
   * 4. Setup verification callbacks
   * 5. Return immutable context
   */
  static Result<SslContextSharedPtr> create(const SslContextConfig& config);
  
  ~SslContext();
  
  /**
   * Create new SSL connection from this context
   * 
   * @return New SSL connection object (caller owns)
   * 
   * Thread-safe: Can be called from any thread
   * The returned SSL* must be used only in dispatcher thread
   */
  SSL* newSsl() const;
  
  /**
   * Get the underlying SSL_CTX
   * For advanced operations and testing
   */
  SSL_CTX* getNativeContext() const { return ctx_; }
  
  /**
   * Check if this is a client context
   */
  bool isClient() const { return config_.is_client; }
  
  /**
   * Get configuration (read-only)
   */
  const SslContextConfig& getConfig() const { return config_; }
  
  /**
   * Set SNI hostname for client connections
   * Must be called before SSL handshake
   */
  static VoidResult setSniHostname(SSL* ssl, const std::string& hostname);
  
  /**
   * Verify result after handshake
   * Should be called after successful handshake to verify peer
   */
  static Result<bool> verifyPeer(SSL* ssl);

private:
  /**
   * Private constructor - use create() factory method
   */
  SslContext(SSL_CTX* ctx, const SslContextConfig& config);
  
  /**
   * Initialize SSL context with configuration
   * Called from create() factory method
   */
  static VoidResult initialize(SSL_CTX* ctx, const SslContextConfig& config);
  
  /**
   * Load certificate chain from file
   */
  static VoidResult loadCertificateChain(SSL_CTX* ctx, const std::string& cert_file);
  
  /**
   * Load private key from file
   */
  static VoidResult loadPrivateKey(SSL_CTX* ctx, const std::string& key_file);
  
  /**
   * Load CA certificates for verification
   */
  static VoidResult loadCaCertificates(SSL_CTX* ctx, const std::string& ca_file);
  
  /**
   * Configure allowed protocols (TLS versions)
   */
  static VoidResult configureProtocols(SSL_CTX* ctx, 
                                       const std::vector<std::string>& protocols);
  
  /**
   * Configure cipher suites
   */
  static VoidResult configureCipherSuites(SSL_CTX* ctx, const std::string& ciphers);
  
  /**
   * Setup certificate verification
   */
  static VoidResult setupVerification(SSL_CTX* ctx, const SslContextConfig& config);
  
  /**
   * Certificate verification callback
   * Called during handshake to verify peer certificate
   */
  static int verifyCallback(int preverify_ok, X509_STORE_CTX* ctx);
  
  /**
   * Configure ALPN if protocols specified
   */
  static VoidResult configureAlpn(SSL_CTX* ctx, 
                                  const std::vector<std::string>& alpn_protocols);

private:
  SSL_CTX* ctx_;                    // OpenSSL context (owned)
  SslContextConfig config_;         // Immutable configuration
  mutable std::mutex ssl_mutex_;    // Protect SSL creation
  
  // Statistics
  mutable std::atomic<uint64_t> ssl_connections_created_{0};
  
  // Disable copy
  SslContext(const SslContext&) = delete;
  SslContext& operator=(const SslContext&) = delete;
};

/**
 * SSL context manager for caching and sharing contexts
 * 
 * Manages a cache of SSL contexts to avoid recreating identical contexts.
 * Thread-safe and can be accessed from multiple threads.
 */
class SslContextManager {
public:
  static SslContextManager& getInstance() {
    static SslContextManager instance;
    return instance;
  }
  
  /**
   * Get or create SSL context for configuration
   * 
   * @param config SSL configuration
   * @return Shared context (may be cached)
   * 
   * Thread-safe: Can be called from any thread
   */
  Result<SslContextSharedPtr> getOrCreateContext(const SslContextConfig& config);
  
  /**
   * Clear context cache
   * Useful for testing or configuration reload
   */
  void clearCache();

private:
  SslContextManager() = default;
  ~SslContextManager() = default;
  
  // Generate cache key from config
  std::string generateCacheKey(const SslContextConfig& config) const;
  
  // Context cache
  std::mutex cache_mutex_;
  std::unordered_map<std::string, SslContextSharedPtr> context_cache_;
  
  // Disable copy
  SslContextManager(const SslContextManager&) = delete;
  SslContextManager& operator=(const SslContextManager&) = delete;
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_SSL_CONTEXT_H