/**
 * @file https_sse_transport_factory.cc
 * @brief HTTPS+SSE transport factory implementation
 */

#include "mcp/transport/https_sse_transport_factory.h"

#include <algorithm>
#include <cctype>

namespace mcp {
namespace transport {

HttpsSseTransportFactory::HttpsSseTransportFactory(
    const HttpSseTransportSocketConfig& config,
    event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {
  
  // Check if SSL should be used based on URL or explicit configuration
  // Only auto-detect if use_ssl was not explicitly set
  // Since bool doesn't have an "unset" state, we'll respect the config value
  // and only auto-detect if it's false AND the URL is HTTPS
  use_ssl_ = config_.use_ssl || detectSslFromUrl(config_.endpoint_url);
  
  // However, if use_ssl is explicitly false, respect that even for HTTPS URLs
  // This requires tracking whether it was explicitly set
  // For now, we'll use the simpler logic: HTTPS always means SSL
  // unless there's a way to track explicit setting
  
  // Set SNI hostname if not provided but URL has hostname
  if (use_ssl_ && !config_.sni_hostname.has_value()) {
    config_.sni_hostname = extractHostname(config_.endpoint_url);
  }
  
  // Set default ALPN protocols if not provided
  if (use_ssl_ && !config_.alpn_protocols.has_value()) {
    config_.alpn_protocols = buildAlpnProtocols();
  }
}

bool HttpsSseTransportFactory::implementsSecureTransport() const {
  return use_ssl_;
}

std::string HttpsSseTransportFactory::name() const {
  return use_ssl_ ? "https+sse" : "http+sse";
}

network::TransportSocketPtr HttpsSseTransportFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Create client transport
  return createClientTransport(options);
}

bool HttpsSseTransportFactory::supportsAlpn() const {
  return use_ssl_ && config_.alpn_protocols.has_value();
}

std::string HttpsSseTransportFactory::defaultServerNameIndication() const {
  if (!use_ssl_ || !config_.sni_hostname.has_value()) {
    return "";
  }
  return config_.sni_hostname.value();
}

void HttpsSseTransportFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Add factory-specific hash components
  const std::string factory_id = name();
  key.insert(key.end(), factory_id.begin(), factory_id.end());
  
  // Add endpoint URL to hash
  key.insert(key.end(), config_.endpoint_url.begin(), config_.endpoint_url.end());
  
  // Add SSL config to hash if using SSL
  if (use_ssl_) {
    key.push_back(config_.verify_ssl ? 1 : 0);
    if (config_.sni_hostname.has_value()) {
      const auto& sni = config_.sni_hostname.value();
      key.insert(key.end(), sni.begin(), sni.end());
    }
  }
}

network::TransportSocketPtr HttpsSseTransportFactory::createTransportSocket() const {
  // Create server transport
  return createServerTransport();
}

network::TransportSocketPtr HttpsSseTransportFactory::createClientTransport(
    network::TransportSocketOptionsSharedPtr options) const {
  
  sockets_created_++;
  
  // Create base TCP socket
  auto tcp_socket = createTcpSocket();
  
  // Wrap with SSL if needed
  auto transport_socket = wrapWithSsl(std::move(tcp_socket), true);
  
  // Wrap with HTTP+SSE
  // Note: HttpSseTransportSocket takes ownership of inner socket
  auto http_sse_socket = std::make_unique<HttpSseTransportSocket>(
      config_, dispatcher_, false);  // false = client mode
  
  return http_sse_socket;
}

network::TransportSocketPtr HttpsSseTransportFactory::createServerTransport() const {
  
  sockets_created_++;
  
  // Create base TCP socket
  auto tcp_socket = createTcpSocket();
  
  // Wrap with SSL if needed
  auto transport_socket = wrapWithSsl(std::move(tcp_socket), false);
  
  // Wrap with HTTP+SSE
  auto http_sse_socket = std::make_unique<HttpSseTransportSocket>(
      config_, dispatcher_, true);  // true = server mode
  
  return http_sse_socket;
}

bool HttpsSseTransportFactory::detectSslFromUrl(const std::string& url) const {
  // Check if URL starts with https://
  if (url.size() >= 8) {
    std::string protocol = url.substr(0, 8);
    std::transform(protocol.begin(), protocol.end(), protocol.begin(), ::tolower);
    return protocol == "https://";
  }
  return false;
}

Result<SslContextSharedPtr> HttpsSseTransportFactory::createSslContext(
    bool is_client) const {
  
  std::lock_guard<std::mutex> lock(ssl_context_mutex_);
  
  // Check cached context
  if (is_client && client_ssl_context_) {
    return client_ssl_context_;
  }
  if (!is_client && server_ssl_context_) {
    return server_ssl_context_;
  }
  
  // Build SSL context configuration
  SslContextConfig ssl_config;
  ssl_config.is_client = is_client;
  
  // Set certificates and keys
  if (config_.client_cert_path.has_value()) {
    ssl_config.cert_chain_file = config_.client_cert_path.value();
  }
  if (config_.client_key_path.has_value()) {
    ssl_config.private_key_file = config_.client_key_path.value();
  }
  if (config_.ca_cert_path.has_value()) {
    ssl_config.ca_cert_file = config_.ca_cert_path.value();
  }
  
  // Set verification
  ssl_config.verify_peer = config_.verify_ssl;
  
  // Set SNI for client
  if (is_client && config_.sni_hostname.has_value()) {
    ssl_config.sni_hostname = config_.sni_hostname.value();
  }
  
  // Set ALPN protocols
  if (config_.alpn_protocols.has_value()) {
    ssl_config.alpn_protocols = config_.alpn_protocols.value();
  }
  
  // Set protocols (TLS versions)
  ssl_config.protocols = {"TLSv1.2", "TLSv1.3"};
  
  // Create context through manager (for caching)
  auto result = SslContextManager::getInstance().getOrCreateContext(ssl_config);
  if (holds_alternative<Error>(result)) {
    return result;
  }
  
  // Cache context
  if (is_client) {
    client_ssl_context_ = get<SslContextSharedPtr>(result);
  } else {
    server_ssl_context_ = get<SslContextSharedPtr>(result);
  }
  
  return get<SslContextSharedPtr>(result);
}

network::TransportSocketPtr HttpsSseTransportFactory::createTcpSocket() const {
  // Create a basic TCP socket
  // This would typically use a TCP transport socket implementation
  // For now, return nullptr as placeholder - actual implementation
  // would create real TCP socket
  
  // In real implementation:
  // return std::make_unique<TcpTransportSocket>(dispatcher_);
  
  // Placeholder - actual TCP socket creation would go here
  return nullptr;
}

network::TransportSocketPtr HttpsSseTransportFactory::wrapWithSsl(
    network::TransportSocketPtr inner_socket,
    bool is_client) const {
  
  if (!use_ssl_) {
    // No SSL needed, return inner socket as-is
    return inner_socket;
  }
  
  // Create SSL context
  auto context_result = createSslContext(is_client);
  if (holds_alternative<Error>(context_result)) {
    // Log error and return inner socket without SSL
    // In production, might want to throw or handle differently
    return inner_socket;
  }
  
  ssl_sockets_created_++;
  
  // Determine SSL role
  auto role = is_client ? 
              SslTransportSocket::InitialRole::Client :
              SslTransportSocket::InitialRole::Server;
  
  // Create SSL transport socket wrapping inner socket
  auto ssl_socket = std::make_unique<SslTransportSocket>(
      std::move(inner_socket),
      get<SslContextSharedPtr>(context_result),
      role,
      dispatcher_);
  
  // Could set handshake callbacks here if needed
  // ssl_socket->setHandshakeCallbacks(...);
  
  return ssl_socket;
}

std::string HttpsSseTransportFactory::extractHostname(const std::string& url) const {
  // Extract hostname from URL
  // Format: https://hostname:port/path
  
  // Find protocol end
  size_t protocol_end = url.find("://");
  if (protocol_end == std::string::npos) {
    return "";
  }
  
  size_t hostname_start = protocol_end + 3;
  if (hostname_start >= url.size()) {
    return "";
  }
  
  // Find hostname end (port or path)
  size_t hostname_end = url.find_first_of(":/?", hostname_start);
  if (hostname_end == std::string::npos) {
    hostname_end = url.size();
  }
  
  return url.substr(hostname_start, hostname_end - hostname_start);
}

std::vector<std::string> HttpsSseTransportFactory::buildAlpnProtocols() const {
  // Build default ALPN protocol list
  std::vector<std::string> protocols;
  
  // Add HTTP/2 if supported
  if (config_.preferred_version == http::HttpVersion::HTTP_2) {
    protocols.push_back("h2");
  }
  
  // Always support HTTP/1.1
  protocols.push_back("http/1.1");
  
  return protocols;
}

}  // namespace transport
}  // namespace mcp