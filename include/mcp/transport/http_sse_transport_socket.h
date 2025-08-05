#ifndef MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H
#define MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H

#include <memory>
#include <queue>

#include "mcp/network/transport_socket.h"

namespace mcp {
namespace transport {

/**
 * HTTP/SSE transport socket configuration
 */
struct HttpSseTransportSocketConfig {
  // HTTP endpoint URL
  std::string endpoint_url;
  
  // HTTP headers
  std::map<std::string, std::string> headers;
  
  // Connection timeout
  std::chrono::milliseconds connect_timeout{30000};
  
  // Request timeout
  std::chrono::milliseconds request_timeout{60000};
  
  // Keep-alive settings
  bool enable_keepalive{true};
  std::chrono::milliseconds keepalive_timeout{60000};
  
  // TLS configuration (if using HTTPS)
  bool verify_ssl{true};
  optional<std::string> ca_cert_path;
  optional<std::string> client_cert_path;
  optional<std::string> client_key_path;
};

/**
 * HTTP/SSE transport socket
 * 
 * Implements bidirectional JSON-RPC over HTTP with Server-Sent Events
 */
class HttpSseTransportSocket : public network::TransportSocket {
public:
  explicit HttpSseTransportSocket(const HttpSseTransportSocketConfig& config);
  ~HttpSseTransportSocket() override;

  // TransportSocket interface
  void setTransportSocketCallbacks(network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override { return "http+sse"; }
  std::string failureReason() const override { return failure_reason_; }
  bool canFlushClose() override { return true; }
  VoidResult connect(network::Socket& socket) override;
  void closeSocket(network::ConnectionEvent event) override;
  TransportIoResult doRead(Buffer& buffer) override;
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override;
  void onConnected() override;

private:
  // SSE event structure
  struct SseEvent {
    optional<std::string> id;
    optional<std::string> event;
    std::string data;
  };

  // HTTP connection states
  enum class HttpState {
    Disconnected,
    Connecting,
    Connected,
    Closing,
    Closed
  };

  // Configuration
  HttpSseTransportSocketConfig config_;
  
  // State
  network::TransportSocketCallbacks* callbacks_{nullptr};
  std::string failure_reason_;
  HttpState state_{HttpState::Disconnected};
  
  // HTTP connection (using underlying transport)
  network::TransportSocketPtr underlying_transport_;
  
  // SSE parsing state
  std::string sse_buffer_;
  std::queue<SseEvent> pending_events_;
  
  // Request/response handling
  std::queue<std::string> pending_requests_;
  bool request_in_flight_{false};
  
  // Helper methods
  void sendHttpRequest(const std::string& body);
  void processHttpResponse(Buffer& buffer);
  void parseSseEvent(const std::string& line);
  void processSseEvents(Buffer& output);
  
  // HTTP protocol helpers
  std::string buildHttpRequest(const std::string& body);
  bool parseHttpResponse(const std::string& data, std::string& body);
};

/**
 * HTTP/SSE transport socket factory
 */
class HttpSseTransportSocketFactory : public network::ClientTransportSocketFactory {
public:
  explicit HttpSseTransportSocketFactory(const HttpSseTransportSocketConfig& config);

  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override;
  std::string name() const override { return "http+sse"; }

  // ClientTransportSocketFactory interface
  network::TransportSocketPtr createTransportSocket(
      network::TransportSocketOptionsSharedPtr options) const override;
  bool supportsAlpn() const override { return true; }
  std::string defaultServerNameIndication() const override;
  void hashKey(std::vector<uint8_t>& key,
               network::TransportSocketOptionsSharedPtr options) const override;

private:
  HttpSseTransportSocketConfig config_;
};

/**
 * Create an HTTP/SSE transport socket factory
 */
inline std::unique_ptr<network::ClientTransportSocketFactory>
createHttpSseTransportSocketFactory(const HttpSseTransportSocketConfig& config) {
  return std::make_unique<HttpSseTransportSocketFactory>(config);
}

} // namespace transport
} // namespace mcp

#endif // MCP_TRANSPORT_HTTP_SSE_TRANSPORT_SOCKET_H