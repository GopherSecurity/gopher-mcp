/**
 * @file listener_config.cc
 * @brief Implementation for listener configuration file I/O operations
 *
 * Contains implementation of file I/O operations that cannot be inline.
 */

#include "mcp/config/listener_config.h"

#include <fstream>
#include <sstream>

namespace mcp {
namespace config {

// Note: Most implementations are inline in the header file following
// the existing codebase pattern. This file contains only operations
// that require additional includes or cannot be inline.

// The fromJsonFile method is already implemented inline in the header,
// but if needed, additional file I/O utility methods could be added here.

}  // namespace config
}  // namespace mcp