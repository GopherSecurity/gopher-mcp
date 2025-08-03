#ifndef MCP_JSON_FWD_H
#define MCP_JSON_FWD_H

// Forward declarations for JSON types to avoid circular dependencies
// This simply includes the official nlohmann json forward declarations

#include <nlohmann/json_fwd.hpp>

// MCP-specific type alias
namespace mcp {
using JSONObject = nlohmann::json;
}

#endif  // MCP_JSON_FWD_H