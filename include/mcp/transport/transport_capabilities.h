#ifndef MCP_TRANSPORT_TRANSPORT_CAPABILITIES_H
#define MCP_TRANSPORT_TRANSPORT_CAPABILITIES_H

#include <memory>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/types.h"

namespace mcp {
namespace transport {

/**
 * Transport type enumeration
 */
enum class TransportType {
  STDIO,      // Standard input/output
  HTTP_SSE,   // HTTP with Server-Sent Events
  WEBSOCKET,  // WebSocket (future)
  TCP,        // Raw TCP (future)
  UNIX,       // Unix domain socket (future)
  UNKNOWN
};

/**
 * Transport capability descriptor
 */
struct TransportCapability {
  TransportType type;
  std::string name;
  std::string version;
  bool bidirectional{true};
  bool supports_streaming{false};
  bool requires_handshake{false};
  optional<Metadata> configuration;

  TransportCapability() = default;

  TransportCapability(TransportType t,
                      const std::string& n,
                      const std::string& v)
      : type(t), name(n), version(v) {}
};

/**
 * Transport negotiation preferences
 */
struct TransportPreferences {
  std::vector<TransportType> preferred_order;
  bool allow_fallback{true};
  optional<TransportType> required_type;
  optional<Metadata> transport_config;

  TransportPreferences() = default;
};

/**
 * Transport capabilities for client/server
 */
class TransportCapabilities {
 public:
  TransportCapabilities() = default;
  virtual ~TransportCapabilities() = default;

  /**
   * Add a supported transport capability
   */
  void addCapability(const TransportCapability& capability) {
    capabilities_.push_back(capability);
  }

  /**
   * Get all supported capabilities
   */
  const std::vector<TransportCapability>& getCapabilities() const {
    return capabilities_;
  }

  /**
   * Check if transport type is supported
   */
  bool supports(TransportType type) const {
    for (const auto& cap : capabilities_) {
      if (cap.type == type) {
        return true;
      }
    }
    return false;
  }

  /**
   * Get capability for specific transport type
   */
  optional<TransportCapability> getCapability(TransportType type) const {
    for (const auto& cap : capabilities_) {
      if (cap.type == type) {
        return mcp::make_optional(cap);
      }
    }
    return nullopt;
  }

  /**
   * Negotiate best transport based on preferences
   */
  optional<TransportCapability> negotiate(
      const TransportPreferences& prefs) const {
    // If required type is specified, use it if available
    if (prefs.required_type.has_value()) {
      return getCapability(prefs.required_type.value());
    }

    // Try preferred order
    for (TransportType type : prefs.preferred_order) {
      auto cap = getCapability(type);
      if (cap.has_value()) {
        return cap;
      }
    }

    // Fallback to first available if allowed
    if (prefs.allow_fallback && !capabilities_.empty()) {
      return mcp::make_optional(capabilities_.front());
    }

    return nullopt;
  }

  /**
   * Convert to MCP protocol capabilities format
   */
  Metadata toMetadata() const {
    Metadata meta = make_metadata();

    std::vector<Metadata> transports;
    for (const auto& cap : capabilities_) {
      Metadata transport = make_metadata();
      transport["type"] = transportTypeToString(cap.type);
      transport["name"] = cap.name;
      transport["version"] = cap.version;
      transport["bidirectional"] = cap.bidirectional;
      transport["streaming"] = cap.supports_streaming;
      transport["handshake"] = cap.requires_handshake;

      if (cap.configuration.has_value()) {
        transport["config"] = cap.configuration.value();
      }

      transports.push_back(transport);
    }

    meta["transports"] = transports;
    return meta;
  }

  /**
   * Parse from MCP protocol capabilities format
   */
  static TransportCapabilities fromMetadata(const Metadata& meta) {
    TransportCapabilities caps;

    auto transports_it = meta.find("transports");
    if (transports_it != meta.end() && transports_it->second.isArray()) {
      for (const auto& transport_meta : transports_it->second.asArray()) {
        if (!transport_meta.isObject())
          continue;

        TransportCapability cap;

        auto type_it = transport_meta.find("type");
        if (type_it != transport_meta.end() && type_it->second.isString()) {
          cap.type = transportTypeFromString(type_it->second.asString());
        }

        auto name_it = transport_meta.find("name");
        if (name_it != transport_meta.end() && name_it->second.isString()) {
          cap.name = name_it->second.asString();
        }

        auto version_it = transport_meta.find("version");
        if (version_it != transport_meta.end() &&
            version_it->second.isString()) {
          cap.version = version_it->second.asString();
        }

        auto bidirectional_it = transport_meta.find("bidirectional");
        if (bidirectional_it != transport_meta.end() &&
            bidirectional_it->second.isBool()) {
          cap.bidirectional = bidirectional_it->second.asBool();
        }

        auto streaming_it = transport_meta.find("streaming");
        if (streaming_it != transport_meta.end() &&
            streaming_it->second.isBool()) {
          cap.supports_streaming = streaming_it->second.asBool();
        }

        auto handshake_it = transport_meta.find("handshake");
        if (handshake_it != transport_meta.end() &&
            handshake_it->second.isBool()) {
          cap.requires_handshake = handshake_it->second.asBool();
        }

        auto config_it = transport_meta.find("config");
        if (config_it != transport_meta.end()) {
          cap.configuration = mcp::make_optional(config_it->second);
        }

        caps.addCapability(cap);
      }
    }

    return caps;
  }

 private:
  std::vector<TransportCapability> capabilities_;

  static std::string transportTypeToString(TransportType type) {
    switch (type) {
      case TransportType::STDIO:
        return "stdio";
      case TransportType::HTTP_SSE:
        return "http+sse";
      case TransportType::WEBSOCKET:
        return "websocket";
      case TransportType::TCP:
        return "tcp";
      case TransportType::UNIX:
        return "unix";
      default:
        return "unknown";
    }
  }

  static TransportType transportTypeFromString(const std::string& str) {
    if (str == "stdio")
      return TransportType::STDIO;
    if (str == "http+sse")
      return TransportType::HTTP_SSE;
    if (str == "websocket")
      return TransportType::WEBSOCKET;
    if (str == "tcp")
      return TransportType::TCP;
    if (str == "unix")
      return TransportType::UNIX;
    return TransportType::UNKNOWN;
  }
};

using TransportCapabilitiesPtr = std::unique_ptr<TransportCapabilities>;
using TransportCapabilitiesSharedPtr = std::shared_ptr<TransportCapabilities>;

/**
 * Factory for creating transport capabilities with default configurations
 */
class TransportCapabilitiesFactory {
 public:
  /**
   * Create default client capabilities
   */
  static TransportCapabilitiesPtr createDefaultClientCapabilities() {
    auto caps = std::make_unique<TransportCapabilities>();

    // Add stdio support
    TransportCapability stdio(TransportType::STDIO, "stdio", "1.0");
    stdio.bidirectional = true;
    stdio.supports_streaming = true;
    stdio.requires_handshake = false;
    caps->addCapability(stdio);

    // Add HTTP+SSE support if available
#if MCP_HAS_LLHTTP
    TransportCapability http_sse(TransportType::HTTP_SSE, "http+sse", "1.0");
    http_sse.bidirectional = true;
    http_sse.supports_streaming = true;
    http_sse.requires_handshake = true;
    caps->addCapability(http_sse);
#endif

    return caps;
  }

  /**
   * Create default server capabilities
   */
  static TransportCapabilitiesPtr createDefaultServerCapabilities() {
    // Same as client for now
    return createDefaultClientCapabilities();
  }

  /**
   * Create capabilities from configuration
   */
  static TransportCapabilitiesPtr createFromConfig(const Metadata& config) {
    auto caps = std::make_unique<TransportCapabilities>();

    auto transports = config.find("transports");
    if (transports != config.end() && transports->second.isArray()) {
      for (const auto& transport : transports->second.asArray()) {
        if (!transport.isObject())
          continue;

        auto type_it = transport.find("type");
        if (type_it == transport.end() || !type_it->second.isString())
          continue;

        std::string type_str = type_it->second.asString();

        if (type_str == "stdio") {
          TransportCapability cap(TransportType::STDIO, "stdio", "1.0");
          cap.bidirectional = true;
          cap.supports_streaming = true;
          caps->addCapability(cap);
        } else if (type_str == "http+sse") {
#if MCP_HAS_LLHTTP
          TransportCapability cap(TransportType::HTTP_SSE, "http+sse", "1.0");
          cap.bidirectional = true;
          cap.supports_streaming = true;
          cap.requires_handshake = true;

          auto config_it = transport.find("config");
          if (config_it != transport.end()) {
            cap.configuration = mcp::make_optional(config_it->second);
          }

          caps->addCapability(cap);
#endif
        }
      }
    }

    return caps;
  }
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_TRANSPORT_CAPABILITIES_H