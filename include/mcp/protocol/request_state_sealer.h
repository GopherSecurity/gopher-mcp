// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#ifndef MCP_PROTOCOL_REQUEST_STATE_SEALER_H
#define MCP_PROTOCOL_REQUEST_STATE_SEALER_H

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace protocol {
namespace modern {

/**
 * What a sealed requestState is bound to: who asked, and what they asked.
 *
 * The state goes out through the client and comes back through the client,
 * so the spec has a server treat it as something an attacker wrote. Sealed
 * against these, a state can't be edited, can't be handed to another
 * caller, and can't be presented on a request other than the one it was
 * issued for.
 */
struct RequestStateContext {
  /** The authenticated caller; empty when the transport names no one. */
  std::string principal;
  /** The request's method, such as "tools/call". */
  std::string method;
  /**
   * The request's params. requestState, inputResponses and _meta are left
   * out of the digest, being what changes between a request and its retry.
   */
  json::JsonValue params;
};

/**
 * Seals a handler's requestState so the server can trust it when it comes
 * back.
 *
 * Sealing protects the state with HMAC-SHA256 under a server-held key and
 * binds it to the caller, an expiry, and the request it was issued for.
 * Opening checks every one of those, and gives back the state only if all
 * of them hold. The result is opaque base64url, safe in JSON and in HTTP
 * headers. The state itself isn't encrypted: it is protected against
 * change, not hidden.
 *
 * Keys rotate: the first key seals, and every key verifies, so a key can
 * be added in front and the old one removed once its states have expired.
 *
 * Sealing bounds replay but doesn't stop it within the lifetime: a state
 * that must be used at most once has to be tracked by the server.
 */
class RequestStateSealer {
 public:
  struct Key {
    /** Names the key inside each sealed state. Letters, digits, '-', '_'. */
    std::string id;
    /** At least 32 bytes, kept secret by the server. */
    std::string secret;
  };

  using Clock = std::function<std::chrono::system_clock::time_point()>;

  /**
   * @param keys The first seals; all verify. Throws std::invalid_argument
   *        for no keys, a short secret, a bad id, or two keys sharing an id.
   * @param lifetime How long a sealed state stays good.
   * @param clock Where the time comes from; the system clock by default.
   */
  explicit RequestStateSealer(
      std::vector<Key> keys,
      std::chrono::seconds lifetime = std::chrono::seconds(300),
      Clock clock = nullptr);

  /** The state, sealed to this context. */
  std::string seal(const std::string& state,
                   const RequestStateContext& context) const;

  /**
   * The state a sealed value holds, if it was sealed by one of these keys,
   * hasn't expired, and was sealed to this same context. Never throws:
   * anything malformed is simply not opened.
   */
  optional<std::string> open(const std::string& sealed,
                             const RequestStateContext& context) const;

  std::chrono::seconds lifetime() const { return lifetime_; }

 private:
  std::string tagFor(const Key& key,
                     int64_t expires,
                     const std::string& state,
                     const RequestStateContext& context) const;

  std::vector<Key> keys_;
  std::chrono::seconds lifetime_;
  Clock clock_;
};

}  // namespace modern
}  // namespace protocol
}  // namespace mcp

#endif  // MCP_PROTOCOL_REQUEST_STATE_SEALER_H
