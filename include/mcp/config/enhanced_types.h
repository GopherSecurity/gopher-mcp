/**
 * @file enhanced_types.h
 * @brief Configuration types with enhanced error diagnostics
 *
 * Extends basic types with detailed error reporting during parsing.
 */

#pragma once

#include <fstream>

#include "mcp/config/parse_error.h"
#include "mcp/config/types.h"
#include "mcp/config/units.h"

namespace mcp {
namespace config {

/**
 * @brief Enhanced NodeConfig with detailed error reporting
 */
struct NodeConfigEnhanced : public NodeConfig {
  static NodeConfigEnhanced fromJson(const mcp::json::JsonValue& j,
                                     ParseContext& ctx) {
    NodeConfigEnhanced config;

    // Required fields
    config.id = getJsonField<std::string>(j, "id", ctx);
    config.cluster = getJsonField<std::string>(j, "cluster", ctx);

    // Optional fields
    if (j.contains("metadata")) {
      ParseContext::FieldScope scope(ctx, "metadata");
      validateJsonType(j["metadata"], mcp::json::JsonType::Object,
                       "metadata", ctx);

      try {
        std::map<std::string, std::string> meta;
        for (const auto& key : j["metadata"].keys()) {
          meta[key] = j["metadata"][key].getString();
        }
        config.metadata = meta;
      } catch (const mcp::json::JsonException& e) {
        throw ctx.createError("Invalid metadata format: " +
                              std::string(e.what()));
      }
    }

    getOptionalJsonField(j, "region", config.region, ctx);
    getOptionalJsonField(j, "zone", config.zone, ctx);

    // Validate after parsing
    try {
      config.validate();
    } catch (const ConfigValidationError& e) {
      throw ConfigParseError(e.reason(),
                             ctx.getCurrentPath() + "." + e.field());
    }

    return config;
  }
};

/**
 * @brief Enhanced CapabilitiesConfig with unit parsing and error reporting
 */
struct CapabilitiesConfigEnhanced : public CapabilitiesConfig {
  static CapabilitiesConfigEnhanced fromJson(const mcp::json::JsonValue& j,
                                             ParseContext& ctx) {
    CapabilitiesConfigEnhanced config;

    // Features array
    if (j.contains("features")) {
      ParseContext::FieldScope scope(ctx, "features");
      validateJsonType(j["features"], mcp::json::JsonType::Array,
                       "features", ctx);

      try {
        std::vector<std::string> features;
        for (size_t i = 0; i < j["features"].size(); ++i) {
          features.push_back(j["features"][i].getString());
        }
        config.features = features;
      } catch (const mcp::json::JsonException& e) {
        throw ctx.createError("Invalid features array: " +
                              std::string(e.what()));
      }
    }

    // Size fields with unit parsing
    if (j.contains("max_request_size")) {
      ParseContext::FieldScope scope(ctx, "max_request_size");
      try {
        config.max_request_size =
            parseJsonSize<size_t>(j["max_request_size"], "max_request_size");
      } catch (const UnitParseError& e) {
        throw ctx.createError("Invalid size format: " + std::string(e.what()));
      }
    }

    if (j.contains("max_response_size")) {
      ParseContext::FieldScope scope(ctx, "max_response_size");
      try {
        config.max_response_size =
            parseJsonSize<size_t>(j["max_response_size"], "max_response_size");
      } catch (const UnitParseError& e) {
        throw ctx.createError("Invalid size format: " + std::string(e.what()));
      }
    }

    // Duration fields with unit parsing
    if (j.contains("request_timeout_ms")) {
      ParseContext::FieldScope scope(ctx, "request_timeout_ms");
      try {
        config.request_timeout_ms = parseJsonDuration<uint32_t>(
            j["request_timeout_ms"], "request_timeout_ms");
      } catch (const UnitParseError& e) {
        throw ctx.createError("Invalid duration format: " +
                              std::string(e.what()));
      }
    } else if (j.contains("request_timeout")) {
      ParseContext::FieldScope scope(ctx, "request_timeout");
      try {
        config.request_timeout_ms = parseJsonDuration<uint32_t>(
            j["request_timeout"], "request_timeout");
      } catch (const UnitParseError& e) {
        throw ctx.createError("Invalid duration format: " +
                              std::string(e.what()));
      }
    }

    return config;
  }
};

/**
 * @brief Enhanced FilterConfig with detailed error reporting
 */
struct FilterConfigEnhanced : public FilterConfig {
  static FilterConfigEnhanced fromJson(const mcp::json::JsonValue& j,
                                       ParseContext& ctx) {
    FilterConfigEnhanced config;

    // Required fields
    config.type = getJsonField<std::string>(j, "type", ctx);
    config.name = getJsonField<std::string>(j, "name", ctx);

    // Optional config object
    if (j.contains("config")) {
      ParseContext::FieldScope scope(ctx, "config");
      validateJsonType(j["config"], mcp::json::JsonType::Object, "config",
                       ctx);
      config.config = j["config"];

      // Apply unit parsing to known size/duration fields in filter config
      if (config.type == "buffer" && config.config.contains("max_size")) {
        try {
          auto size =
              parseJsonSize<size_t>(config.config["max_size"], "max_size");
          config.config["max_size"] = mcp::json::JsonValue(static_cast<int64_t>(size));
        } catch (const UnitParseError& e) {
          throw ctx.createError("Invalid buffer size: " +
                                std::string(e.what()));
        }
      }

      if (config.type == "rate_limit") {
        if (config.config.contains("window_duration")) {
          try {
            auto duration = parseJsonDuration<uint32_t>(
                config.config["window_duration"], "window_duration");
            config.config["window_duration"] = mcp::json::JsonValue(static_cast<int64_t>(duration));
          } catch (const UnitParseError& e) {
            throw ctx.createError("Invalid window duration: " +
                                  std::string(e.what()));
          }
        }
      }
    }

    // Optional enabled flag
    getOptionalJsonField(j, "enabled", config.enabled, ctx);

    // Validate
    try {
      config.validate();
    } catch (const ConfigValidationError& e) {
      throw ConfigParseError(e.reason(),
                             ctx.getCurrentPath() + "." + e.field());
    }

    return config;
  }
};

/**
 * @brief Enhanced ServerConfig with comprehensive error reporting
 */
struct ServerConfigEnhanced : public ServerConfig {
  static ServerConfigEnhanced fromJson(const mcp::json::JsonValue& j,
                                       ParseContext& ctx) {
    ServerConfigEnhanced config;

    // Basic fields
    getOptionalJsonField(j, "name", config.name, ctx);

    // Version field
    if (j.contains("version")) {
      ParseContext::FieldScope scope(ctx, "version");
      try {
        std::string version_str = j["version"].getString();
        config.version = ConfigVersion::parse(version_str);
      } catch (const ConfigValidationError& e) {
        throw ctx.createError("Invalid version format: " +
                              std::string(e.what()));
      } catch (const mcp::json::JsonException& e) {
        throw ctx.createError("Version must be a string");
      }
    }

    // Capabilities
    if (j.contains("capabilities")) {
      config.capabilities =
          ConfigParser<CapabilitiesConfigEnhanced>::parseField(
              j, "capabilities", ctx);
    }

    // Numeric fields
    getOptionalJsonField(j, "max_sessions", config.max_sessions, ctx);
    getOptionalJsonField(j, "worker_threads", config.worker_threads, ctx);
    getOptionalJsonField(j, "event_threads", config.event_threads, ctx);

    // Session timeout with unit support
    if (j.contains("session_timeout_ms")) {
      ParseContext::FieldScope scope(ctx, "session_timeout_ms");
      try {
        config.session_timeout_ms = parseJsonDuration<uint32_t>(
            j["session_timeout_ms"], "session_timeout_ms");
      } catch (const UnitParseError& e) {
        throw ctx.createError("Invalid duration: " + std::string(e.what()));
      }
    } else if (j.contains("session_timeout")) {
      ParseContext::FieldScope scope(ctx, "session_timeout");
      try {
        config.session_timeout_ms = parseJsonDuration<uint32_t>(
            j["session_timeout"], "session_timeout");
      } catch (const UnitParseError& e) {
        throw ctx.createError("Invalid duration: " + std::string(e.what()));
      }
    }

    // Filter chains array
    if (j.contains("filter_chains")) {
      ParseContext::FieldScope scope(ctx, "filter_chains");
      validateJsonType(j["filter_chains"], mcp::json::JsonType::Array,
                       "filter_chains", ctx);

      const auto& chains = j["filter_chains"];
      for (size_t i = 0; i < chains.size(); ++i) {
        ParseContext::FieldScope idx_scope(ctx, "[" + std::to_string(i) + "]");

        try {
          auto chain = FilterChainConfig::fromJson(chains[i]);

          // Parse filters with enhanced error handling
          if (chains[i].contains("filters")) {
            chain.filters.clear();
            const auto& filters = chains[i]["filters"];

            for (size_t j = 0; j < filters.size(); ++j) {
              ParseContext::FieldScope filter_scope(
                  ctx, "filters[" + std::to_string(j) + "]");
              chain.filters.push_back(
                  FilterConfigEnhanced::fromJson(filters[j], ctx));
            }
          }

          config.filter_chains.push_back(chain);
        } catch (const ConfigValidationError& e) {
          throw ctx.createError("Invalid filter chain: " +
                                std::string(e.what()));
        } catch (const mcp::json::JsonException& e) {
          throw ctx.createError("Invalid filter chain: " +
                                std::string(e.what()));
        }
      }
    }

    // Transports array
    if (j.contains("transports")) {
      ParseContext::FieldScope scope(ctx, "transports");
      validateJsonType(j["transports"], mcp::json::JsonType::Array,
                       "transports", ctx);

      const auto& transports = j["transports"];
      for (size_t i = 0; i < transports.size(); ++i) {
        ParseContext::FieldScope idx_scope(ctx, "[" + std::to_string(i) + "]");

        try {
          config.transports.push_back(TransportConfig::fromJson(transports[i]));
        } catch (const ConfigValidationError& e) {
          throw ctx.createError("Invalid transport: " + std::string(e.what()));
        }
      }
    }

    // Validate the complete configuration
    try {
      config.validate();
    } catch (const ConfigValidationError& e) {
      throw ConfigParseError(e.reason(),
                             ctx.getCurrentPath() + "." + e.field());
    }

    return config;
  }
};

/**
 * @brief Load configuration with detailed error messages
 */
template <typename ConfigType>
ConfigType loadEnhancedConfig(const std::string& filename) {
  ParseContext ctx;
  ctx.setFile(filename);

  std::ifstream file(filename);
  if (!file.is_open()) {
    throw ConfigParseError("Cannot open configuration file", "", filename);
  }

  // Track line number for syntax errors
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  mcp::json::JsonValue j;
  try {
    j = mcp::json::JsonValue::parse(content);
  } catch (const mcp::json::JsonException& e) {
    // JsonException doesn't provide byte position, so just report the error
    throw ConfigParseError("JSON syntax error: " + std::string(e.what()), "",
                           filename);
  }

  return ConfigParser<ConfigType>::parse(j, ctx);
}

/**
 * @brief Try to parse config and provide helpful error messages
 */
template <typename ConfigType>
bool tryParseConfig(const mcp::json::JsonValue& j,
                    ConfigType& result,
                    std::string& error_message) {
  ParseContext ctx;

  try {
    result = ConfigParser<ConfigType>::parse(j, ctx);
    return true;
  } catch (const ConfigParseError& e) {
    error_message = e.what();

    // Add context stack if available
    if (!e.contextStack().empty()) {
      error_message += "\nContext:";
      for (const auto& context : e.contextStack()) {
        error_message += "\n  - " + context;
      }
    }

    return false;
  } catch (const std::exception& e) {
    error_message = std::string("Unexpected error: ") + e.what();
    return false;
  }
}

}  // namespace config
}  // namespace mcp