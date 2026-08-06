#ifndef MCP_CORE_REQUEST_ID_KEY_H
#define MCP_CORE_REQUEST_ID_KEY_H

#include <cstdint>
#include <string>

#include "mcp/types.h"

namespace mcp {

/**
 * A JSON-RPC request id in a form that can key an ordered container.
 *
 * RequestId is a variant, and the variant this project ships for C++14
 * defines no comparison operators, so it cannot be used as a map key
 * directly. Stringifying it is the obvious workaround and the wrong one:
 * JSON-RPC treats the string id "5" and the number 5 as different requests,
 * and collapsing them lets one request's response resolve another's.
 *
 * The tag is therefore part of the key, and ordering puts every number
 * before every string so the ordering is total rather than merely
 * consistent.
 */
struct RequestIdKey {
  bool is_string = false;
  std::string text;    // meaningful when is_string
  int64_t number = 0;  // meaningful otherwise
};

inline bool operator==(const RequestIdKey& lhs, const RequestIdKey& rhs) {
  if (lhs.is_string != rhs.is_string) {
    return false;
  }
  return lhs.is_string ? lhs.text == rhs.text : lhs.number == rhs.number;
}

inline bool operator!=(const RequestIdKey& lhs, const RequestIdKey& rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const RequestIdKey& lhs, const RequestIdKey& rhs) {
  if (lhs.is_string != rhs.is_string) {
    // Numbers sort ahead of strings. The choice is arbitrary; having one is
    // not, or the ordering would not be total across the two kinds.
    return !lhs.is_string;
  }
  return lhs.is_string ? lhs.text < rhs.text : lhs.number < rhs.number;
}

/** Build a key from a request id, preserving which kind it was. */
inline RequestIdKey requestIdKey(const RequestId& id) {
  RequestIdKey key;
  if (holds_alternative<std::string>(id)) {
    key.is_string = true;
    key.text = get<std::string>(id);
  } else {
    key.number = get<int64_t>(id);
  }
  return key;
}

/** Human-readable form, for logs. Not suitable as a key — see above. */
inline std::string requestIdKeyToString(const RequestIdKey& key) {
  return key.is_string ? key.text : std::to_string(key.number);
}

}  // namespace mcp

#endif  // MCP_CORE_REQUEST_ID_KEY_H
