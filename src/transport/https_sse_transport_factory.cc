/**
 * @file https_sse_transport_factory.cc
 * @brief HTTPS+SSE transport factory implementation
 */

#include "mcp/transport/https_sse_transport_factory.h"

#include <algorithm>
#include <cctype>

#include "mcp/transport/tcp_transport_socket.h"

namespace mcp {
namespace transport {

HttpsSseTransportFactory::HttpsSseTransportFactory(
    const HttpSseTransportSocketConfig& config, event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(&dispatcher) {
  // Check if SSL should be used based on configuration
  use_ssl_ = (config_.underlying_transport == HttpSseTransportSocketConfig::UnderlyingTransport::SSL);

  // If using SSL and ssl_config is not set, create default SSL config
  if (use_ssl_ && !config_.ssl_config.has_value()) {
    // Create a mutable copy to set defaults
    config_.ssl_config = HttpSseTransportSocketConfig::SslConfig{};
  }

  // Set SNI hostname if not provided but we have a server address
  if (use_ssl_ && config_.ssl_config.has_value()) {
    auto& ssl_config = const_cast<HttpSseTransportSocketConfig::SslConfig&>(config_.ssl_config.value());
    if (!ssl_config.sni_hostname.has_value() && !config_.server_address.empty()) {
      ssl_config.sni_hostname = extractHostname(config_.server_address);
    }

    // Set default ALPN protocols if not provided
    if (!ssl_config.alpn_protocols.has_value()) {
      ssl_config.alpn_protocols = buildAlpnProtocols();
    }
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
  return use_ssl_ && config_.ssl_config.has_value() && 
         config_.ssl_config.value().alpn_protocols.has_value();
}

std::string HttpsSseTransportFactory::defaultServerNameIndication() const {
  if (!use_ssl_ || !config_.ssl_config.has_value() || 
      !config_.ssl_config.value().sni_hostname.has_value()) {
    return "";
  }
  return config_.ssl_config.value().sni_hostname.value();
}

void HttpsSseTransportFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Add factory-specific hash components
  const std::string factory_id = name();
  key.insert(key.end(), factory_id.begin(), factory_id.end());

  // Add server address to hash
  key.insert(key.end(), config_.server_address.begin(),
             config_.server_address.end());

  // Add SSL config to hash if using SSL
  if (use_ssl_ && config_.ssl_config.has_value()) {
    const auto& ssl_config = config_.ssl_config.value();
    key.push_back(ssl_config.verify_peer ? 1 : 0);
    if (ssl_config.sni_hostname.has_value()) {
      const auto& sni = ssl_config.sni_hostname.value();
      key.insert(key.end(), sni.begin(), sni.end());
    }
  }
}

network::TransportSocketPtr HttpsSseTransportFactory::createTransportSocket()
    const {
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

  // Create HTTP+SSE transport socket
  // The new architecture uses FilterManager for protocol processing
  auto http_sse_socket = std::make_unique<HttpSseTransportSocket>(
      config_, *dispatcher_, nullptr);  // nullptr = no custom filter chain

  return http_sse_socket;
}

network::TransportSocketPtr HttpsSseTransportFactory::createServerTransport()
    const {
  sockets_created_++;

  // Create base TCP socket
  auto tcp_socket = createTcpSocket();

  // Wrap with SSL if needed
  auto transport_socket = wrapWithSsl(std::move(tcp_socket), false);

  // Create HTTP+SSE transport socket for server
  auto http_sse_socket = std::make_unique<HttpSseTransportSocket>(
      config_, *dispatcher_, nullptr);  // nullptr = no custom filter chain

  return http_sse_socket;
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

  // Set certificates and keys from SSL config
  if (config_.ssl_config.has_value()) {
    const auto& ssl = config_.ssl_config.value();
    
    if (ssl.client_cert_path.has_value()) {
      ssl_config.cert_chain_file = ssl.client_cert_path.value();
    }
    if (ssl.client_key_path.has_value()) {
      ssl_config.private_key_file = ssl.client_key_path.value();
    }
    if (ssl.ca_cert_path.has_value()) {
      ssl_config.ca_cert_file = ssl.ca_cert_path.value();
    }

    // Set verification
    ssl_config.verify_peer = ssl.verify_peer;

    // Set SNI for client
    if (is_client && ssl.sni_hostname.has_value()) {
      ssl_config.sni_hostname = ssl.sni_hostname.value();
    }

    // Set ALPN protocols
    if (ssl.alpn_protocols.has_value()) {
      ssl_config.alpn_protocols = ssl.alpn_protocols.value();
    }
  } else {
    // Default SSL config if not provided
    ssl_config.verify_peer = true;
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
  // Use TcpTransportSocket for standard TCP connections

  // Create TCP transport socket configuration
  TcpTransportSocketConfig tcp_config;
  tcp_config.tcp_nodelay = true;    // Enable TCP_NODELAY for low latency
  tcp_config.tcp_keepalive = true;  // Enable keep-alive
  tcp_config.connect_timeout =
      std::chrono::milliseconds(30000);  // 30 second timeout
  tcp_config.io_timeout =
      std::chrono::milliseconds(60000);  // 60 second I/O timeout

  // Create and return TCP transport socket
  // dispatcher_ is mutable so we can use it in const methods
  return std::make_unique<TcpTransportSocket>(*dispatcher_, tcp_config);
}

network::TransportSocketPtr HttpsSseTransportFactory::wrapWithSsl(
    network::TransportSocketPtr inner_socket, bool is_client) const {
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
  auto role = is_client ? SslTransportSocket::InitialRole::Client
                        : SslTransportSocket::InitialRole::Server;

  // Create SSL transport socket wrapping inner socket
  auto ssl_socket = std::make_unique<SslTransportSocket>(
      std::move(inner_socket), get<SslContextSharedPtr>(context_result), role,
      *dispatcher_);

  // Could set handshake callbacks here if needed
  // ssl_socket->setHandshakeCallbacks(...);

  return ssl_socket;
}

std::string HttpsSseTransportFactory::extractHostname(
    const std::string& address) const {
  // Extract hostname from server address
  // Format: "hostname:port" or "127.0.0.1:8080"
  
  if (address.empty()) {
    return "";
  }

  // Find port separator (colon from the end to handle IPv6)
  size_t port_sep = address.rfind(':');
  if (port_sep == std::string::npos) {
    // No port, entire string is hostname
    return address;
  }

  // Extract hostname part before port
  return address.substr(0, port_sep);
}

std::vector<std::string> HttpsSseTransportFactory::buildAlpnProtocols() const {
  // Build default ALPN protocol list
  std::vector<std::string> protocols;

  // For now, we support HTTP/1.1 only with SSE
  // HTTP/2 support can be added later if needed
  protocols.push_back("http/1.1");

  return protocols;
}

}  // namespace transport
}  // namespace mcp