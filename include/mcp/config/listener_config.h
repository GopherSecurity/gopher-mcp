/**
 * @file listener_config.h
 * @brief Listener configuration schema for config-driven filter chains
 *
 * This file defines the configuration structures for the simplified Option A
 * implementation focusing on listener-based configuration with TCP-only support
 * in the current phase.
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include "mcp/json/json_bridge.h"
#include "mcp/config/types.h"  // For ConfigValidationError

namespace mcp {
namespace config {

/**
 * @brief Socket address configuration
 *
 * Defines IP address and port binding for TCP listeners
 */
struct SocketAddress {
  /// IP address to bind to ("0.0.0.0", "127.0.0.1", etc.)
  std::string address = "127.0.0.1";

  /// Port number to bind to
  uint16_t port_value = 8080;

  /**
   * @brief Validate socket address configuration
   */
  void validate() const {
    if (address.empty()) {
      throw ConfigValidationError("socket_address.address",
                                  "IP address cannot be empty");
    }

    // Basic IP address format validation
    if (address != "0.0.0.0" && address != "127.0.0.1" &&
        address != "::1" && address != "::") {
      // Check for valid characters in IP address
      bool valid_chars = true;
      for (char c : address) {
        if (!std::isdigit(c) && c != '.' && c != ':') {
          valid_chars = false;
          break;
        }
      }
      if (!valid_chars) {
        throw ConfigValidationError("socket_address.address",
                                    "Invalid IP address format: " + address);
      }
    }

    if (port_value == 0) {
      throw ConfigValidationError("socket_address.port_value",
                                  "Port cannot be 0");
    }
  }

  /**
   * @brief Convert to JSON
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;
    builder.add("address", address);
    builder.add("port_value", static_cast<int>(port_value));
    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static SocketAddress fromJson(const json::JsonValue& j) {
    SocketAddress addr;

    if (j.contains("address") && !j["address"].isNull()) {
      try {
        addr.address = j["address"].getString();
      } catch (const json::JsonException& e) {
        throw ConfigValidationError("socket_address.address",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("port_value") && !j["port_value"].isNull()) {
      try {
        addr.port_value = static_cast<uint16_t>(j["port_value"].getInt());
      } catch (const json::JsonException& e) {
        throw ConfigValidationError("socket_address.port_value",
                                    "Type error: " + std::string(e.what()));
      }
    }

    return addr;
  }

  bool operator==(const SocketAddress& other) const {
    return address == other.address && port_value == other.port_value;
  }

  bool operator!=(const SocketAddress& other) const {
    return !(*this == other);
  }
};

/**
 * @brief Address configuration wrapper
 *
 * Contains socket address for the listener
 */
struct AddressConfig {
  /// Socket address configuration
  SocketAddress socket_address;

  /**
   * @brief Validate address configuration
   */
  void validate() const {
    try {
      socket_address.validate();
    } catch (const ConfigValidationError& e) {
      throw ConfigValidationError("address." + e.field(), e.reason());
    }
  }

  /**
   * @brief Convert to JSON
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;
    builder.add("socket_address", socket_address.toJson());
    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static AddressConfig fromJson(const json::JsonValue& j) {
    AddressConfig config;

    if (j.contains("socket_address") && j["socket_address"].isObject()) {
      try {
        config.socket_address = SocketAddress::fromJson(j["socket_address"]);
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("address.socket_address." + e.field(),
                                    e.reason());
      } catch (const std::exception& e) {
        throw ConfigValidationError("address.socket_address",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    return config;
  }

  bool operator==(const AddressConfig& other) const {
    return socket_address == other.socket_address;
  }

  bool operator!=(const AddressConfig& other) const {
    return !(*this == other);
  }
};

// Use FilterConfig and FilterChainConfig from types.h to avoid conflicts

/**
 * @brief Listener configuration
 *
 * Defines a listener with address binding and filter chain configuration
 */
struct ListenerConfig {
  /// Listener name (e.g., "default_listener")
  std::string name = "default_listener";

  /// Address configuration (IP and port)
  AddressConfig address;

  /// Filter chain configurations for this listener
  std::vector<mcp::config::FilterChainConfig> filter_chains;

  /**
   * @brief Validate listener configuration
   */
  void validate() const {
    if (name.empty()) {
      throw ConfigValidationError("listener.name",
                                  "Listener name cannot be empty");
    }

    // Validate name format
    for (char c : name) {
      if (!std::isalnum(c) && c != '_' && c != '-') {
        throw ConfigValidationError("listener.name",
                                    "Listener name contains invalid characters: " + name);
      }
    }

    // Validate address
    try {
      address.validate();
    } catch (const ConfigValidationError& e) {
      throw ConfigValidationError("listener.address." + e.field(), e.reason());
    }

    // Must have at least one filter chain
    if (filter_chains.empty()) {
      throw ConfigValidationError("listener.filter_chains",
                                  "Listener must have at least one filter chain");
    }

    // Validate each filter chain
    for (size_t i = 0; i < filter_chains.size(); ++i) {
      try {
        filter_chains[i].validate();
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError(
            "listener.filter_chains[" + std::to_string(i) + "]." + e.field(),
            e.reason());
      }
    }
  }

  /**
   * @brief Convert to JSON
   */
  json::JsonValue toJson() const {
    json::JsonObjectBuilder builder;
    builder.add("name", name);
    builder.add("address", address.toJson());

    json::JsonArrayBuilder chains_arr;
    for (const auto& chain : filter_chains) {
      chains_arr.add(chain.toJson());
    }
    builder.add("filter_chains", chains_arr.build());

    return builder.build();
  }

  /**
   * @brief Create from JSON
   */
  static ListenerConfig fromJson(const json::JsonValue& j) {
    ListenerConfig lc;

    if (j.contains("name") && !j["name"].isNull()) {
      try {
        lc.name = j["name"].getString();
      } catch (const json::JsonException& e) {
        throw ConfigValidationError("listener.name",
                                    "Type error: " + std::string(e.what()));
      }
    }

    if (j.contains("address") && j["address"].isObject()) {
      try {
        lc.address = AddressConfig::fromJson(j["address"]);
      } catch (const ConfigValidationError& e) {
        throw ConfigValidationError("listener." + e.field(), e.reason());
      } catch (const std::exception& e) {
        throw ConfigValidationError("listener.address",
                                    "Parse error: " + std::string(e.what()));
      }
    }

    if (j.contains("filter_chains") && j["filter_chains"].isArray()) {
      for (size_t i = 0; i < j["filter_chains"].size(); ++i) {
        try {
          lc.filter_chains.push_back(
              FilterChainConfig::fromJson(j["filter_chains"][i]));
        } catch (const ConfigValidationError& e) {
          throw ConfigValidationError(
              "listener.filter_chains[" + std::to_string(i) + "]." + e.field(),
              e.reason());
        } catch (const std::exception& e) {
          throw ConfigValidationError(
              "listener.filter_chains[" + std::to_string(i) + "]",
              "Parse error: " + std::string(e.what()));
        }
      }
    }

    return lc;
  }

  bool operator==(const ListenerConfig& other) const {
    return name == other.name && address == other.address &&
           filter_chains == other.filter_chains;
  }

  bool operator!=(const ListenerConfig& other) const {
    return !(*this == other);
  }
};

// Use ServerConfig from types.h to avoid conflicts

/**
 * @brief Listener-based server configuration utility
 *
 * Provides utilities for working with listener configurations within the
 * broader ServerConfig from types.h
 */
struct ListenerServerConfig {
  /// List of listener configurations
  std::vector<ListenerConfig> listeners;

  /**
   * @brief Create from JSON file
   */
  static ListenerServerConfig fromJsonFile(const std::string& file_path) {
    try {
      std::ifstream file(file_path);
      if (!file.is_open()) {
        throw ConfigValidationError("server.config_file",
                                    "Cannot open config file: " + file_path);
      }

      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

      auto json_value = json::JsonValue::parse(content);

      ListenerServerConfig config;
      if (json_value.contains("listeners") && json_value["listeners"].isArray()) {
        for (size_t i = 0; i < json_value["listeners"].size(); ++i) {
          config.listeners.push_back(ListenerConfig::fromJson(json_value["listeners"][i]));
        }
      }

      return config;

    } catch (const json::JsonException& e) {
      throw ConfigValidationError("server.config_file",
                                  "JSON parse error: " + std::string(e.what()));
    }
  }
};

}  // namespace config
}  // namespace mcp