/**
 * What a client asks a subscription to tell it.
 *
 * One shape, read by both ends: a client renders it into the request
 * that opens a subscription, a server parses it back out, and the
 * server renders what it will honour into the acknowledgement for the
 * client to parse in turn. Keeping the two ends of that in one place is
 * what stops them drifting.
 */

#pragma once

#include <string>
#include <vector>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/modern_era.h"

namespace mcp {
namespace protocol {
namespace modern {

/**
 * What one subscription asked to hear.
 *
 * A server must send nothing a client did not ask for, so a filter that
 * asks for nothing hears nothing — never everything.
 */
struct NotificationFilter {
  bool tools_list_changed{false};
  bool prompts_list_changed{false};
  bool resources_list_changed{false};
  /** The resources whose updates this subscription wants, by URI. */
  std::vector<std::string> resource_uris;

  /** Read out of a listen request's params. */
  static NotificationFilter parse(const json::JsonValue& params);

  /**
   * What the acknowledgement echoes: the subset actually honoured, so a
   * client can see what it will not be getting rather than wait for
   * something that was never coming.
   */
  json::JsonValue render() const;

  /**
   * Whether this subscription asked for this notification. The URI
   * matters only for a resource update — a subscription names the
   * resources it cares about rather than asking for all of them.
   */
  bool wants(const std::string& method, const std::string& uri) const;

  bool empty() const {
    return !tools_list_changed && !prompts_list_changed &&
           !resources_list_changed && resource_uris.empty();
  }
};

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
