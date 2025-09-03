/**
 * @file types_with_validation.h
 * @brief Configuration types with unknown field validation
 * 
 * This file extends the basic configuration types with proper unknown
 * field detection and validation policy support.
 */

#pragma once

#include "mcp/config/types.h"
#include "mcp/config/validation_policy.h"
#include <set>

namespace mcp {
namespace config {

/**
 * @brief Extended NodeConfig with validation
 */
struct NodeConfigWithValidation : public NodeConfig {
    /**
     * @brief Create from JSON with validation context
     */
    static NodeConfigWithValidation fromJson(const nlohmann::json& j, ValidationContext& ctx = getDefaultValidationContext()) {
        // Known fields for NodeConfig
        static const std::set<std::string> known_fields = {
            "id", "cluster", "metadata", "region", "zone"
        };
        
        // Check for unknown fields
        validateJsonFields(j, known_fields, "node", ctx);
        
        // Parse known fields
        NodeConfigWithValidation config;
        config.NodeConfig::operator=(NodeConfig::fromJson(j));
        return config;
    }
};

/**
 * @brief Extended AdminConfig with validation
 */
struct AdminConfigWithValidation : public AdminConfig {
    /**
     * @brief Create from JSON with validation context
     */
    static AdminConfigWithValidation fromJson(const nlohmann::json& j, ValidationContext& ctx = getDefaultValidationContext()) {
        // Known fields for AdminConfig  
        static const std::set<std::string> known_fields = {
            "address", "port", "allowed_ips", "enabled",
            "path_prefix", "enable_cors", "cors_origins"
        };
        
        // Check for unknown fields
        validateJsonFields(j, known_fields, "admin", ctx);
        
        // Parse known fields
        AdminConfigWithValidation config;
        config.AdminConfig::operator=(AdminConfig::fromJson(j));
        return config;
    }
};

/**
 * @brief Extended FilterConfig with validation
 */
struct FilterConfigWithValidation : public FilterConfig {
    /**
     * @brief Create from JSON with validation context
     */
    static FilterConfigWithValidation fromJson(const nlohmann::json& j, ValidationContext& ctx = getDefaultValidationContext()) {
        // Known fields for FilterConfig
        static const std::set<std::string> known_fields = {
            "type", "name", "config", "enabled"
        };
        
        // Check for unknown fields at filter level
        std::string path = "filter";
        if (j.contains("name")) {
            path = "filter[" + j["name"].get<std::string>() + "]";
        }
        validateJsonFields(j, known_fields, path, ctx);
        
        // Note: We don't validate fields within "config" as they are filter-specific
        
        // Parse known fields
        FilterConfigWithValidation config;
        config.FilterConfig::operator=(FilterConfig::fromJson(j));
        return config;
    }
};

/**
 * @brief Extended CapabilitiesConfig with validation
 */
struct CapabilitiesConfigWithValidation : public CapabilitiesConfig {
    /**
     * @brief Create from JSON with validation context
     */
    static CapabilitiesConfigWithValidation fromJson(const nlohmann::json& j, ValidationContext& ctx = getDefaultValidationContext()) {
        // Known fields for CapabilitiesConfig
        static const std::set<std::string> known_fields = {
            "features", "max_request_size", "max_response_size",
            "request_timeout_ms", "request_timeout"  // Support both variants
        };
        
        // Check for unknown fields
        validateJsonFields(j, known_fields, "capabilities", ctx);
        
        // Parse known fields
        CapabilitiesConfigWithValidation config;
        config.CapabilitiesConfig::operator=(CapabilitiesConfig::fromJson(j));
        return config;
    }
};

/**
 * @brief Extended ServerConfig with validation
 */
struct ServerConfigWithValidation : public ServerConfig {
    /**
     * @brief Create from JSON with validation context
     */
    static ServerConfigWithValidation fromJson(const nlohmann::json& j, ValidationContext& ctx = getDefaultValidationContext()) {
        // Known fields for ServerConfig
        static const std::set<std::string> known_fields = {
            "name", "version", "capabilities", "max_sessions",
            "session_timeout_ms", "session_timeout",  // Support both variants
            "worker_threads", "event_threads",
            "transports", "filter_chains"
        };
        
        // Check for unknown fields at server level
        validateJsonFields(j, known_fields, "server", ctx);
        
        ServerConfigWithValidation config;
        
        // Parse basic fields
        if (j.contains("name")) {
            config.name = j["name"].get<std::string>();
        }
        
        if (j.contains("version")) {
            config.version = ConfigVersion::parse(j["version"].get<std::string>());
        }
        
        // Parse nested objects with validation
        if (j.contains("capabilities")) {
            // Note: We already validated unknown fields at the capabilities level
            // when we called validateJsonFields for the server level
            config.capabilities = CapabilitiesConfigWithValidation::fromJson(j["capabilities"], ctx);
        }
        
        // Parse other fields using base implementation
        if (j.contains("max_sessions")) {
            config.max_sessions = j["max_sessions"].get<uint32_t>();
        }
        
        if (j.contains("session_timeout_ms")) {
            config.session_timeout_ms = parseJsonDuration<uint32_t>(j["session_timeout_ms"], "session_timeout_ms");
        } else if (j.contains("session_timeout")) {
            config.session_timeout_ms = parseJsonDuration<uint32_t>(j["session_timeout"], "session_timeout");
        }
        
        if (j.contains("worker_threads")) {
            config.worker_threads = j["worker_threads"].get<uint32_t>();
        }
        
        if (j.contains("event_threads")) {
            config.event_threads = j["event_threads"].get<uint32_t>();
        }
        
        // Parse arrays with per-element validation
        if (j.contains("filter_chains") && j["filter_chains"].is_array()) {
            size_t idx = 0;
            for (const auto& chain_json : j["filter_chains"]) {
                // Known fields for FilterChainConfig
                static const std::set<std::string> chain_fields = {
                    "name", "transport_type", "filters"
                };
                
                std::string path = "filter_chains[" + std::to_string(idx) + "]";
                validateJsonFields(chain_json, chain_fields, path, ctx);
                
                config.filter_chains.push_back(FilterChainConfig::fromJson(chain_json));
                idx++;
            }
        }
        
        if (j.contains("transports") && j["transports"].is_array()) {
            size_t idx = 0;
            for (const auto& transport_json : j["transports"]) {
                // Known fields for TransportConfig
                static const std::set<std::string> transport_fields = {
                    "type", "address", "port", "tls", "filter_chain", "enabled"
                };
                
                std::string path = "transports[" + std::to_string(idx) + "]";
                validateJsonFields(transport_json, transport_fields, path, ctx);
                
                // Validate TLS sub-object if present
                if (transport_json.contains("tls") && transport_json["tls"].is_object()) {
                    static const std::set<std::string> tls_fields = {
                        "enabled", "cert_file", "key_file", "ca_file",
                        "verify_client", "min_version", "cipher_suites"
                    };
                    
                    std::string tls_path = path + ".tls";
                    validateJsonFields(transport_json["tls"], tls_fields, tls_path, ctx);
                }
                
                config.transports.push_back(TransportConfig::fromJson(transport_json));
                idx++;
            }
        }
        
        return config;
    }
};

/**
 * @brief Helper function to load config with validation
 */
template<typename ConfigType>
ConfigType loadConfigWithValidation(
    const nlohmann::json& j,
    UnknownFieldPolicy policy = UnknownFieldPolicy::WARN) {
    
    ValidationContext ctx(policy);
    return ConfigType::fromJson(j, ctx);
}

/**
 * @brief Load and validate configuration from file
 */
template<typename ConfigType>
ConfigType loadConfigFromFile(
    const std::string& filename,
    UnknownFieldPolicy policy = UnknownFieldPolicy::WARN) {
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + filename);
    }
    
    nlohmann::json j;
    file >> j;
    
    return loadConfigWithValidation<ConfigType>(j, policy);
}

} // namespace config
} // namespace mcp