// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#ifndef MCP_SERVER_LIST_PAGING_H
#define MCP_SERVER_LIST_PAGING_H

#include <array>
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
 * Opaque to clients, as the spec requires, and accepted only by the
 * signer that issued it: each carries an HMAC-SHA256 tag, over the list
 * it belongs to and the key it names, under a key made when the signer
 * is. A client cannot write one naming an item of its choosing, and a
 * cursor from one list, one server, or one before a restart is refused.
 */
class CursorSigner {
 public:
  CursorSigner();

  std::string make(const std::string& list, const std::string& last_key) const;

  /** The key a cursor names, if this signer issued it for this list. */
  bool read(const std::string& cursor,
            const std::string& list,
            std::string* last_key) const;

 private:
  std::string tagOf(const std::string& body) const;

  std::array<unsigned char, 32> key_;
};

/**
 * One page of a sorted map. A page size of 0 puts everything on one page.
 * Sets next_cursor when more items follow the page. Throws InvalidCursor
 * for any cursor this signer did not issue for this list, including an
 * empty one: no cursor at all is how the first page is asked for.
 */
template <typename T>
std::vector<T> pageOf(const std::map<std::string, T>& items,
                      const CursorSigner& signer,
                      const std::string& list,
                      const optional<std::string>& cursor,
                      size_t page_size,
                      optional<std::string>* next_cursor) {
  auto it = items.begin();
  if (cursor.has_value()) {
    std::string last_key;
    if (!signer.read(cursor.value(), list, &last_key)) {
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
    *next_cursor = signer.make(list, std::prev(it)->first);
  }
  return page;
}

}  // namespace paging
}  // namespace server
}  // namespace mcp

#endif  // MCP_SERVER_LIST_PAGING_H
