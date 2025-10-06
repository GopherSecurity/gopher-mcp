/**
 * @file filter_context.h
 * @brief Filter creation context for config-driven filter chains
 *
 * This file defines the context and metadata structures needed for creating
 * filters in a config-driven environment. Provides runtime context to filters
 * during creation including dispatcher, callbacks, and transport metadata.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mcp/json/json_bridge.h"

namespace mcp {

// Forward declarations
namespace event {
class Dispatcher;
}

class McpProtocolCallbacks;

namespace filter {

/**
 * @brief Connection mode for filter creation
 *
 * Indicates whether the filter is being created for a server or client connection
 */
enum class ConnectionMode {
  /// Server mode - accepting incoming connections
  Server,
  /// Client mode - establishing outgoing connections
  Client
};

/**
 * @brief Transport metadata abstraction
 *
 * Generic transport metadata interface that currently supports TCP-only
 * implementation but is designed for future extensibility to UDP, STDIO, etc.
 */
struct TransportMetadata {
  /// Negotiated ALPN protocols (e.g., ["http/1.1", "h2"])
  std::vector<std::string> alpn;

  /// SNI hostname for TLS connections (TCP-specific)
  std::string sni;

  /// Local endpoint address (TCP socket address format)
  std::string local_address;

  /// Remote endpoint address (TCP socket address format)
  std::string remote_address;

  /// Local port number (TCP-specific)
  uint16_t local_port = 0;

  /// Remote port number (TCP-specific)
  uint16_t remote_port = 0;

  /// Transport-specific custom metadata
  json::JsonValue custom = json::JsonValue::object();

  /**
   * @brief Default constructor
   */
  TransportMetadata() = default;

  /**
   * @brief Constructor for TCP transport
   */
  TransportMetadata(const std::string& local_addr,
                    uint16_t local_p,
                    const std::string& remote_addr = "",
                    uint16_t remote_p = 0)
      : local_address(local_addr),
        remote_address(remote_addr),
        local_port(local_p),
        remote_port(remote_p) {}

  /**
   * @brief Check if this is a TCP transport
   */
  bool isTcp() const {
    return local_port > 0 || remote_port > 0;
  }

  /**
   * @brief Get transport type as string
   */
  std::string getTransportType() const {
    if (isTcp()) {
      return "tcp";
    }
    // Future: add UDP, STDIO detection
    return "unknown";
  }

  /**
   * @brief Convert to JSON for debugging/logging
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;

    if (!alpn.empty()) {
      json::JsonArrayBuilder alpn_arr;
      for (const auto& proto : alpn) {
        alpn_arr.add(proto);
      }
      builder.add("alpn", alpn_arr.build());
    }

    if (!sni.empty()) {
      builder.add("sni", sni);
    }

    if (!local_address.empty()) {
      builder.add("local_address", local_address);
    }

    if (!remote_address.empty()) {
      builder.add("remote_address", remote_address);
    }

    if (local_port > 0) {
      builder.add("local_port", static_cast<int>(local_port));
    }

    if (remote_port > 0) {
      builder.add("remote_port", static_cast<int>(remote_port));
    }

    if (!custom.isNull() && custom.isObject() && custom.size() > 0) {
      builder.add("custom", custom);
    }

    builder.add("transport_type", getTransportType());

    return builder.build();
  }

  bool operator==(const TransportMetadata& other) const {
    return alpn == other.alpn &&
           sni == other.sni &&
           local_address == other.local_address &&
           remote_address == other.remote_address &&
           local_port == other.local_port &&
           remote_port == other.remote_port &&
           custom.toString() == other.custom.toString();
  }

  bool operator!=(const TransportMetadata& other) const {
    return !(*this == other);
  }
};

/**
 * @brief Filter creation context
 *
 * Provides runtime context to filters during creation, including references
 * to the event dispatcher, protocol callbacks, and transport metadata.
 */
struct FilterCreationContext {
  /// Reference to the event dispatcher for async operations
  event::Dispatcher& dispatcher;

  /// Reference to protocol callback interface
  McpProtocolCallbacks& callbacks;

  /// Connection mode (server or client)
  ConnectionMode mode;

  /// Transport connection metadata
  TransportMetadata transport;

  /// Optional shared services handle for dependency injection
  std::shared_ptr<void> shared_services;

  /**
   * @brief Constructor
   */
  FilterCreationContext(event::Dispatcher& d,
                        McpProtocolCallbacks& c,
                        ConnectionMode m,
                        const TransportMetadata& t)
      : dispatcher(d), callbacks(c), mode(m), transport(t) {}

  /**
   * @brief Check if this is a server-side filter
   */
  bool isServer() const {
    return mode == ConnectionMode::Server;
  }

  /**
   * @brief Check if this is a client-side filter
   */
  bool isClient() const {
    return mode == ConnectionMode::Client;
  }

  /**
   * @brief Get connection mode as string
   */
  std::string getModeString() const {
    return isServer() ? "server" : "client";
  }

  /**
   * @brief Convert to JSON for debugging/logging
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;
    builder.add("mode", getModeString());
    builder.add("transport", transport.toJson());

    if (shared_services) {
      builder.add("has_shared_services", true);
    } else {
      builder.add("has_shared_services", false);
    }

    return builder.build();
  }
};

/**
 * @brief Basic filter metadata 
 *
 * Simplified metadata structure focusing on essential information
 * needed for filter registration and validation.
 */
struct BasicFilterMetadata {
  /// Filter type name (e.g., "http.codec", "sse.codec")
  std::string name;

  /// Semantic version string (e.g., "1.0.0")
  std::string version = "1.0.0";

  /// Default configuration for the filter
  json::JsonValue default_config = json::JsonValue::object();

  /// Human-readable description
  std::string description;

  /**
   * @brief Default constructor
   */
  BasicFilterMetadata() = default;

  /**
   * @brief Constructor with name and description
   */
  BasicFilterMetadata(const std::string& n, const std::string& desc)
      : name(n), description(desc) {}

  /**
   * @brief Validate metadata
   */
  void validate() const {
    if (name.empty()) {
      throw std::runtime_error("Filter metadata name cannot be empty");
    }

    if (version.empty()) {
      throw std::runtime_error("Filter metadata version cannot be empty");
    }

    // Basic semantic version validation (X.Y.Z format)
    size_t first_dot = version.find('.');
    size_t second_dot = version.find('.', first_dot + 1);

    if (first_dot == std::string::npos || second_dot == std::string::npos) {
      throw std::runtime_error("Filter metadata version must be in X.Y.Z format");
    }
  }

  /**
   * @brief Convert to JSON
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;
    builder.add("name", name);
    builder.add("version", version);
    builder.add("description", description);

    if (!default_config.isNull() && default_config.isObject() && default_config.size() > 0) {
      builder.add("default_config", default_config);
    }

    return builder.build();
  }

  bool operator==(const BasicFilterMetadata& other) const {
    return name == other.name &&
           version == other.version &&
           description == other.description &&
           default_config.toString() == other.default_config.toString();
  }

  bool operator!=(const BasicFilterMetadata& other) const {
    return !(*this == other);
  }
};

/**
 * @brief Connection mode utility functions
 */
namespace ConnectionModeUtils {

/**
 * @brief Convert connection mode to string
 */
inline std::string toString(ConnectionMode mode) {
  switch (mode) {
    case ConnectionMode::Server:
      return "server";
    case ConnectionMode::Client:
      return "client";
    default:
      return "unknown";
  }
}

/**
 * @brief Parse connection mode from string
 */
inline ConnectionMode fromString(const std::string& mode_str) {
  if (mode_str == "server") {
    return ConnectionMode::Server;
  } else if (mode_str == "client") {
    return ConnectionMode::Client;
  } else {
    throw std::runtime_error("Invalid connection mode: " + mode_str);
  }
}

}  // namespace ConnectionModeUtils

}  // namespace filter
}  // namespace mcp
