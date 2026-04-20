#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace filter {

/**
 * SseSessionRegistry — dispatcher-owned map of SSE session IDs to the
 * network::Connection* streaming SSE back to each client.
 *
 * MCP SSE transport splits a request/response pair across two TCP
 * connections on the server:
 *   1. A long-lived GET /sse stream the client leaves open for
 *      server-sent events. The server registers this connection under a
 *      fresh session ID and announces a POST callback URL containing
 *      that ID in the "endpoint" event.
 *   2. Short POST /callback/{session_id} connections — one per outbound
 *      JSON-RPC request. The server returns 202 Accepted immediately and
 *      routes the JSON-RPC response through the SSE connection registered
 *      under the matching session ID.
 *
 * The registry is what lets the POST handler find the SSE connection it
 * must route the response through. It is owned by the HTTP+SSE filter
 * chain factory (one per McpServer), not a process-wide singleton:
 *   - Independent McpServer instances in the same process do not share
 *     session IDs or leak into each other.
 *   - Lifetime is bounded by the factory, which is owned by McpServer —
 *     no global state to reason about at shutdown.
 *
 * Threading model:
 *   - The MCP server runs on a single dispatcher thread. All filter
 *     callbacks (onHeaders, onWrite, filter destructor) fire on that
 *     thread, so registry mutations are naturally single-threaded.
 *   - Every public method asserts isThreadSafe() so a future move to a
 *     worker-thread model fails loudly instead of silently corrupting
 *     the map.
 */
class SseSessionRegistry {
 public:
  explicit SseSessionRegistry(event::Dispatcher& dispatcher);

  // Record an SSE stream connection and hand back a stable session ID.
  // Caller must call removeSession() when the stream closes — the SSE
  // filter's destructor does this.
  std::string registerSession(network::Connection* connection);

  // Drop a session. Safe to call with an unknown ID (no-op).
  void removeSession(const std::string& session_id);

  // Write a JSON-RPC response through the SSE stream registered under
  // session_id. Returns true if the session existed and the write was
  // handed to the connection (the SSE codec filter further down the
  // write chain frames the bytes into a `data: ...\n\n` SSE event).
  // Returns false if the session has gone away (e.g. client already
  // disconnected); the caller should drop the response rather than
  // pretending it was delivered.
  bool sendResponse(const std::string& session_id,
                    const std::string& json_data);

  // Test / introspection: current session count. Asserts dispatcher
  // thread to match the rest of the API.
  size_t sessionCount() const;

  // Test / introspection: whether a given ID is currently registered.
  bool hasSession(const std::string& session_id) const;

 private:
  event::Dispatcher& dispatcher_;
  std::map<std::string, network::Connection*> sessions_;
  uint64_t next_id_{1};
};

}  // namespace filter
}  // namespace mcp
