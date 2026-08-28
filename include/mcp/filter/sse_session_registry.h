#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>

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

  // Record an SSE stream connection under a caller-provided stable session ID.
  // Used by Streamable HTTP, where the client repeats Mcp-Session-Id on the
  // GET event stream and POST request connections.
  std::string registerSession(const std::string& session_id,
                              network::Connection* connection);

  using StreamWriter = std::function<bool(const std::string&)>;

  // Record an SSE stream connection with a generated session ID and explicit
  // event-stream writer.
  std::string registerSession(network::Connection* connection,
                              StreamWriter writer);

  // Record an SSE stream connection with an explicit writer that emits bytes
  // already framed for the event stream. This avoids re-entering the generic
  // HTTP response path for server-initiated messages.
  std::string registerSession(const std::string& session_id,
                              network::Connection* connection,
                              StreamWriter writer);

  // Drop a session. Safe to call with an unknown ID (no-op).
  void removeSession(const std::string& session_id);

  // Drop every session streaming through the given connection. Called
  // from the server's connection-close handler: the filter that
  // registered the session cannot reliably do this itself because the
  // factory keeps filters alive past their connection's death, so the
  // filter destructor (the other removal path) may run long after the
  // connection pointer has dangled — and a push or POST-callback routed
  // through a stale entry is a write into freed memory. Fires the
  // session-closed callback for each removed session, like removeSession.
  void removeConnection(network::Connection* connection);

  // Observer for session teardown. Invoked on the dispatcher thread from
  // removeSession() after the entry is gone, once per actually-removed
  // session. This is how the server layer learns that a client's SSE
  // stream — and therefore the MCP session keyed on it — has ended, so it
  // can release session state (subscriptions etc.) that lives above the
  // filter layer. At most one callback; setting replaces the previous one.
  using SessionClosedCallback = std::function<void(const std::string&)>;
  void setSessionClosedCallback(SessionClosedCallback callback) {
    session_closed_callback_ = std::move(callback);
  }

  // Write a JSON-RPC response through the SSE stream registered under
  // session_id. Returns true if the session existed and the write was
  // handed to the connection (the SSE codec filter further down the
  // write chain frames the bytes into a `data: ...\n\n` SSE event).
  // Returns false if the session has gone away (e.g. client already
  // disconnected); the caller should drop the response rather than
  // pretending it was delivered.
  //
  // writing_connection is the connection the caller is currently inside a
  // write() on, when there is one. Connection writes are not re-entrant, so
  // routing a response back into that same connection would corrupt the
  // write in progress; the registry refuses instead. This only happens if a
  // client POSTs a callback down its own event stream.
  bool sendResponse(const std::string& session_id,
                    const std::string& json_data,
                    const network::Connection* writing_connection = nullptr);

  // Test / introspection: current session count. Asserts dispatcher
  // thread to match the rest of the API.
  size_t sessionCount() const;

  // Test / introspection: whether a given ID is currently registered.
  bool hasSession(const std::string& session_id) const;

 private:
  struct SessionEntry {
    network::Connection* connection{nullptr};
    StreamWriter writer;
  };

  event::Dispatcher& dispatcher_;
  std::map<std::string, SessionEntry> sessions_;
  uint64_t next_id_{1};
  SessionClosedCallback session_closed_callback_;
};

}  // namespace filter
}  // namespace mcp
