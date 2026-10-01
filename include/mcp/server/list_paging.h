// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#ifndef MCP_SERVER_LIST_PAGING_H
#define MCP_SERVER_LIST_PAGING_H

#include <cstddef>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/types.h"

namespace mcp {
namespace server {

/**
 * Thrown when a list request carries a cursor this server did not issue,
 * or one issued for a different list. Answered with -32602.
 */
class InvalidCursor : public std::runtime_error {
 public:
  InvalidCursor() : std::runtime_error("Invalid cursor") {}
};

namespace paging {

/**
 * A cursor names the last item of the page it follows, not a position.
 * Items are kept sorted by key, so the next page is whatever sorts after
 * that key: one registered or removed between pages moves nothing else,
 * and no item is skipped or seen twice.
 *
 * Opaque to clients, as the spec requires. It says which list it belongs
 * to, so a cursor from one list is refused by another, and the key is hex
 * so any key travels safely.
 */
constexpr const char* kCursorVersion = "c1";

inline std::string makeCursor(const std::string& list,
                              const std::string& last_key) {
  static const char* digits = "0123456789abcdef";
  std::string cursor = std::string(kCursorVersion) + "." + list + ".";
  for (unsigned char byte : last_key) {
    cursor += digits[byte >> 4];
    cursor += digits[byte & 0x0f];
  }
  return cursor;
}

/** The key a cursor names, if this server issued it for this list. */
inline bool readCursor(const std::string& cursor,
                       const std::string& list,
                       std::string* last_key) {
  const std::string prefix = std::string(kCursorVersion) + "." + list + ".";
  if (cursor.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  const std::string hex = cursor.substr(prefix.size());
  if (hex.size() % 2 != 0) {
    return false;
  }
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return -1;
  };
  std::string key;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int high = nibble(hex[i]);
    const int low = nibble(hex[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    key += static_cast<char>((high << 4) | low);
  }
  *last_key = key;
  return true;
}

/**
 * One page of a sorted map. A page size of 0 puts everything on one page.
 * An empty cursor is read as the first page, the same as none. Sets
 * next_cursor when more items follow the page, and throws InvalidCursor
 * for a cursor this list did not issue.
 */
template <typename T>
std::vector<T> pageOf(const std::map<std::string, T>& items,
                      const std::string& list,
                      const optional<std::string>& cursor,
                      size_t page_size,
                      optional<std::string>* next_cursor) {
  auto it = items.begin();
  if (cursor.has_value() && !cursor->empty()) {
    std::string last_key;
    if (!readCursor(cursor.value(), list, &last_key)) {
      throw InvalidCursor();
    }
    it = items.upper_bound(last_key);
  }

  std::vector<T> page;
  for (; it != items.end(); ++it) {
    if (page_size != 0 && page.size() == page_size) {
      break;
    }
    page.push_back(it->second);
  }
  if (it != items.end() && !page.empty()) {
    *next_cursor = makeCursor(list, std::prev(it)->first);
  }
  return page;
}

}  // namespace paging
}  // namespace server
}  // namespace mcp

#endif  // MCP_SERVER_LIST_PAGING_H
