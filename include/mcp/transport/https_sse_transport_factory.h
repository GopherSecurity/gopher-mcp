/**
 * @file https_sse_transport_factory.h
 * @brief HTTPS+SSE transport factory with SSL/TLS support
 *
 * This provides a factory that creates HTTP+SSE transport sockets
 * with optional SSL/TLS encryption, following layered design.
 *
 * Architecture:
 * - Auto-detects HTTPS from URL and enables SSL
 * - Layers SSL transport over TCP transport
 * - HTTP+SSE protocol over SSL transport
 * - All operations in dispatcher thread
 */

#ifndef MCP_TRANSPORT_HTTPS_SSE_TRANSPORT_FACTORY_H
#define MCP_TRANSPORT_HTTPS_SSE_TRANSPORT_FACTORY_H

#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/transport/ssl_context.h"
#include "mcp/transport/ssl_transport_socket.h"

namespace mcp {
namespace transport {

/**
 * HTTPS+SSE Transport Factory
 *
 * Creates transport sockets with proper layering:
 * - TCP socket (base layer)
 * - SSL socket (optional encryption layer)
 * - HTTP+SSE socket (application protocol layer)
 *
 * Follows some good design principles:
 * - Clean separation of concerns
 * - Composable transport layers
 * - All async operations in dispatcher thread
 */
class HttpsSseTransportFactory : public network::ClientTransportSocketFactory,
                                 public network::ServerTransportSocketFactory {
 public:
  /**
   * Create HTTPS+SSE transport factory
   *
   * @param config HTTP+SSE configuration (includes SSL settings)
   * @param dispatcher Event dispatcher for async operations
   *
   * Flow:
   * 1. Parse configuration
   * 2. Detect SSL requirement from URL
   * 3. Create SSL context if needed
   * 4. Store factory configuration
   */
  HttpsSseTransportFactory(const HttpSseTransportSocketConfig& config,
                           event::Dispatcher& dispatcher);

  ~HttpsSseTransportFactory() override = default;

  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override;
  std::string name() const override;

  // ClientTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const override;
  bool supportsAlpn() const override;
  std::string defaultServerNameIndication() const override;
  void hashKey(std::vector<uint8_t>& key,
               network::TransportSocketOptionsSharedPtr options) const override;

  // ServerTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket() const override;

  /**
   * Create client transport socket with automatic SSL detection
   *
   * @param options Transport options
   * @return Layered transport socket
   *
   * Flow:
   * 1. Create base TCP socket
   * 2. If HTTPS detected or configured:
   *    a. Create SSL context from config
   *    b. Wrap TCP socket with SSL transport
   * 3. Wrap with HTTP+SSE transport
   * 4. Return fully layered socket
   */
  network::TransportSocketPtr createClientTransport(
      network::TransportSocketOptionsSharedPtr options) const;

  /**
   * Create server transport socket with optional SSL
   *
   * @return Layered transport socket for server
   *
   * Flow:
   * 1. Create base TCP socket
   * 2. If SSL configured:
   *    a. Create SSL context for server
   *    b. Wrap TCP socket with SSL transport
   * 3. Wrap with HTTP+SSE transport
   * 4. Return fully layered socket
   */
  network::TransportSocketPtr createServerTransport() const;

 private:
  /**
   * Detect if SSL is needed based on URL
   *
   * @param url Endpoint URL
   * @return true if HTTPS detected
   */
  bool detectSslFromUrl(const std::string& url) const;

  /**
   * Create SSL context from configuration
   *
   * @param is_client true for client context, false for server
   * @return SSL context or error
   *
   * Flow:
   * 1. Build SslContextConfig from HTTP+SSE config
   * 2. Set certificates and keys if provided
   * 3. Configure verification settings
   * 4. Set ALPN protocols if specified
   * 5. Create and cache context
   */
  Result<SslContextSharedPtr> createSslContext(bool is_client) const;

  /**
   * Create base TCP transport socket
   *
   * @return TCP transport socket
   */
  network::TransportSocketPtr createTcpSocket() const;

  /**
   * Wrap socket with SSL if needed
   *
   * @param inner_socket Socket to wrap
   * @param is_client Client or server mode
   * @return SSL-wrapped socket or original if SSL not needed
   *
   * Flow:
   * 1. Check if SSL is needed
   * 2. Get or create SSL context
   * 3. Create SSL transport socket
   * 4. Set handshake callbacks if needed
   * 5. Return wrapped socket
   */
  network::TransportSocketPtr wrapWithSsl(
      network::TransportSocketPtr inner_socket, bool is_client) const;

  /**
   * Extract hostname from URL for SNI
   *
   * @param url Full URL
   * @return Hostname portion
   */
  std::string extractHostname(const std::string& url) const;

  /**
   * Build ALPN protocol list
   * Default includes h2 and http/1.1 for HTTP
   */
  std::vector<std::string> buildAlpnProtocols() const;

 private:
  HttpSseTransportSocketConfig config_;  // Configuration
  event::Dispatcher& dispatcher_;        // Event dispatcher
  bool use_ssl_;                         // SSL enabled flag

  // Cached SSL contexts (created on demand)
  mutable SslContextSharedPtr client_ssl_context_;
  mutable SslContextSharedPtr server_ssl_context_;
  mutable std::mutex ssl_context_mutex_;  // Protect context creation

  // Statistics
  mutable std::atomic<uint64_t> sockets_created_{0};
  mutable std::atomic<uint64_t> ssl_sockets_created_{0};
};

/**
 * Factory function for creating HTTPS+SSE transport factory
 *
 * Automatically detects HTTPS from URL and configures SSL
 *
 * @param config HTTP+SSE configuration
 * @param dispatcher Event dispatcher
 * @return Transport socket factory
 *
 * Example usage:
 * ```cpp
 * HttpSseTransportSocketConfig config;
 * config.endpoint_url = "https://api.example.com";
 * config.client_cert_path = "/path/to/client.crt";
 * config.client_key_path = "/path/to/client.key";
 * config.ca_cert_path = "/path/to/ca.crt";
 *
 * auto factory = createHttpsSseTransportFactory(config, dispatcher);
 * auto socket = factory->createTransportSocket(options);
 * ```
 */
inline std::unique_ptr<HttpsSseTransportFactory> createHttpsSseTransportFactory(
    const HttpSseTransportSocketConfig& config, event::Dispatcher& dispatcher) {
  return std::make_unique<HttpsSseTransportFactory>(config, dispatcher);
}

/**
 * Helper to create client HTTPS+SSE transport directly
 *
 * @param url Endpoint URL (https:// for SSL)
 * @param dispatcher Event dispatcher
 * @param verify_ssl Enable SSL verification (default true)
 * @return Transport socket
 *
 * Example:
 * ```cpp
 * auto socket = createHttpsSseClientTransport(
 *     "https://api.example.com", dispatcher, true);
 * ```
 */
inline network::TransportSocketPtr createHttpsSseClientTransport(
    const std::string& url,
    event::Dispatcher& dispatcher,
    bool verify_ssl = true) {
  HttpSseTransportSocketConfig config;
  config.endpoint_url = url;
  config.verify_ssl = verify_ssl;

  auto factory = createHttpsSseTransportFactory(config, dispatcher);
  return factory->createTransportSocket(nullptr);
}

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_HTTPS_SSE_TRANSPORT_FACTORY_H