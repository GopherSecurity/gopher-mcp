#ifndef MCP_AUTH_AUTH_H
#define MCP_AUTH_AUTH_H

/**
 * @file auth.h
 * @brief Main authentication module interface for MCP
 */

namespace mcp {
namespace auth {

// Forward declarations
class JWTValidator;
class JWKSClient;
class ScopeValidator;
class MetadataGenerator;

/**
 * @brief Initialize the authentication module
 * @return true on success, false on failure
 */
bool initialize();

/**
 * @brief Shutdown the authentication module
 */
void shutdown();

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_AUTH_H