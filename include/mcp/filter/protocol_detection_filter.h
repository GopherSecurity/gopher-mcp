/**
 * Protocol Detection Filter
 * 
 * Detects whether incoming data is HTTP or MCP JSON-RPC and routes accordingly.
 * Following production patterns:
 * - Inspects initial bytes to determine protocol
 * - Creates appropriate filter chain based on detection
 * - Supports transparent handling of both protocols
 */

#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <map>

#include "mcp/network/filter.h"
#include "mcp/buffer.h"
#include "mcp/types.h"

// Forward declarations to avoid circular dependencies
namespace mcp {
namespace filter {
class HttpCodecFilter;
class JsonRpcProtocolFilter;
class SseCodecFilter;
class HttpRoutingFilter;
} // namespace filter
} // namespace mcp

namespace mcp {
namespace filter {

/**
 * Protocol type detected
 */
enum class DetectedProtocol {
  Unknown,    // Not yet detected
  HTTP,       // HTTP/1.x or HTTP/2
  JsonRpc,    // MCP JSON-RPC (direct or stdio)
  SSE,        // Server-Sent Events (HTTP-based)
  WebSocket   // WebSocket (future)
};

/**
 * Protocol detection result
 */
struct ProtocolDetectionResult {
  DetectedProtocol protocol{DetectedProtocol::Unknown};
  bool needs_more_data{false};  // Need more bytes to determine
  size_t bytes_inspected{0};    // How many bytes were examined
  
  // HTTP-specific detection
  std::string http_method;      // GET, POST, etc.
  std::string http_path;        // Request path if HTTP
  bool is_sse_request{false};   // Has Accept: text/event-stream
};

/**
 * Protocol detection filter
 * 
 * This filter inspects the initial bytes of a connection to determine
 * the protocol and installs appropriate filter chain.
 */
class ProtocolDetectionFilter : public network::Filter,
                                public std::enable_shared_from_this<ProtocolDetectionFilter> {
public:
  /**
   * Protocol handler callback
   * Called when protocol is detected to install appropriate filters
   */
  using ProtocolHandler = std::function<void(DetectedProtocol protocol, 
                                             const ProtocolDetectionResult& result)>;

  explicit ProtocolDetectionFilter(ProtocolHandler handler);
  ~ProtocolDetectionFilter() override = default;

  // network::Filter interface
  network::FilterStatus onNewConnection() override;
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  void initializeReadFilterCallbacks(network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  void initializeWriteFilterCallbacks(network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  /**
   * Detect protocol from buffer
   * @param data Buffer containing initial connection data
   * @return Detection result
   */
  static ProtocolDetectionResult detectProtocol(const Buffer& data);

  /**
   * Check if buffer starts with HTTP method
   */
  static bool isHttpRequest(const Buffer& data);

  /**
   * Check if buffer contains JSON-RPC request
   */
  static bool isJsonRpcRequest(const Buffer& data);

  /**
   * Extract HTTP headers from buffer (if HTTP)
   */
  static std::map<std::string, std::string> extractHttpHeaders(const Buffer& data);

private:
  // Detection state
  bool detection_complete_{false};
  DetectedProtocol detected_protocol_{DetectedProtocol::Unknown};
  std::unique_ptr<Buffer> detection_buffer_;  // Buffer data until detection completes
  
  // Callbacks
  network::ReadFilterCallbacks* read_callbacks_{nullptr};
  network::WriteFilterCallbacks* write_callbacks_{nullptr};
  ProtocolHandler protocol_handler_;
  
  // Detection parameters
  static constexpr size_t kMinDetectionBytes = 16;     // Minimum bytes needed
  static constexpr size_t kMaxDetectionBytes = 4096;   // Maximum to buffer for detection
  
  /**
   * Complete protocol detection and invoke handler
   */
  void completeDetection(const ProtocolDetectionResult& result);
  
  /**
   * Pass buffered data to next filter after detection
   */
  void injectBufferedData();
};

/**
 * Protocol routing filter factory
 * 
 * Creates appropriate filter chains based on detected protocol
 */
class ProtocolRoutingFilterFactory {
public:
  struct Config {
    // Enable specific protocols
    bool enable_http{true};
    bool enable_mcp{true};
    bool enable_sse{true};
    
    // HTTP configuration
    bool http_server_mode{true};
    
    // MCP configuration  
    bool mcp_use_framing{true};
    
    // Callbacks for protocol-specific handling
    std::function<void(network::Connection&)> on_http_connection;
    std::function<void(network::Connection&)> on_mcp_connection;
  };

  explicit ProtocolRoutingFilterFactory(const Config& config) : config_(config) {}

  /**
   * Create protocol detection filter with routing
   */
  network::FilterSharedPtr createProtocolDetectionFilter();

  /**
   * Create HTTP filter chain
   */
  std::vector<network::FilterSharedPtr> createHttpFilterChain(
      network::Connection& connection,
      bool is_sse_request);

  /**
   * Create MCP filter chain
   */
  std::vector<network::FilterSharedPtr> createMcpFilterChain(
      network::Connection& connection);

private:
  Config config_;

  /**
   * Handle detected protocol and install filters
   */
  void handleDetectedProtocol(network::Connection& connection,
                              DetectedProtocol protocol,
                              const ProtocolDetectionResult& result);
};

/**
 * Transparent protocol handler
 * 
 * Manages transparent handling of both HTTP and MCP on same connection
 */
class TransparentProtocolHandler {
public:
  struct Config {
    // MCP endpoint configuration
    std::string mcp_path{"/mcp"};           // HTTP path for MCP over HTTP
    std::string mcp_sse_path{"/mcp/sse"};   // HTTP SSE path for MCP
    
    // HTTP routing configuration
    bool enable_http_routing{true};         // Enable arbitrary HTTP routing
    
    // Protocol preferences
    bool prefer_sse_for_mcp{true};          // Use SSE when Accept header allows
  };

  explicit TransparentProtocolHandler(const Config& config) : config_(config) {}

  /**
   * Check if HTTP request should be handled as MCP
   */
  bool isMcpHttpRequest(const std::string& path, 
                        const std::map<std::string, std::string>& headers) const;

  /**
   * Route HTTP request appropriately
   */
  void routeHttpRequest(const std::string& method,
                       const std::string& path,
                       const std::map<std::string, std::string>& headers,
                       std::function<void(bool is_mcp)> callback);

  /**
   * Convert HTTP request to MCP if needed
   * Returns true if conversion successful
   */
  bool convertHttpToMcp(const std::string& body,
                       std::string& out_json_rpc) const;

  /**
   * Convert MCP response to HTTP
   */
  std::string convertMcpToHttp(const std::string& json_rpc,
                               bool use_sse) const;

private:
  Config config_;
};

} // namespace filter
} // namespace mcp