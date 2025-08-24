/**
 * @file mcp_raii.cc
 * @brief Implementation of specialized RAII deleters
 *
 * This file contains the implementation of specialized deleters and
 * other implementation details for the MCP RAII utilities.
 *
 * @copyright Copyright (c) 2025 MCP Project
 * @license MIT License
 */

#define MCP_RAII_IMPLEMENTATION
#include "mcp/c_api/mcp_raii.h"

// Include C API types for specializations
extern "C" {
#include "mcp/c_api/mcp_c_types.h"
}

#include <cstdlib>
#include <cstring>

namespace mcp {
namespace raii {

/* ============================================================================
 * Specialized Deleter Implementations
 * ============================================================================ */

/**
 * Specialized deleter for mcp_string_t
 * Properly cleans up both the string data and the structure
 */
template<>
void c_deleter<mcp_string_t>::operator()(mcp_string_t* ptr) const noexcept {
    if (ptr) {
        // Free string data first
        if (ptr->data) {
            free(const_cast<char*>(ptr->data));
        }
        
        // Free the structure itself
        free(ptr);
    }
}

/**
 * Specialized deleter for mcp_json_value_t
 * Properly cleans up JSON value and its nested content
 */
template<>
void c_deleter<mcp_json_value_t>::operator()(mcp_json_value_t* ptr) const noexcept {
    if (ptr) {
        // Check if we have a proper JSON cleanup function available
        // This is safer than assuming free() is correct for JSON values
        extern void mcp_json_value_free(mcp_json_value_t* json_ptr);
        
        try {
            // Use the proper JSON library cleanup function
            mcp_json_value_free(ptr);
        } catch (...) {
            // JSON cleanup threw exception - this should not happen but be defensive
            // In production, log this error to monitoring system
            
            // As a last resort, try basic free (this may crash if JSON has complex structure)
            // In a real implementation, you would configure this based on your JSON library
            free(ptr);
        }
    }
}

} // namespace raii
} // namespace mcp