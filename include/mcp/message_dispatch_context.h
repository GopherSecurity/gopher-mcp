#pragma once

#include <string>

#include "mcp/core/result.h"
#include "mcp/types.h"

namespace mcp {

namespace network {
class Connection;
}

/**
 * Per-message dispatch context.
 *
 * Carries where a JSON-RPC message came from and how to send a reply back
 * along the same path. The transport producer (the filter or connection
 * manager that parsed the message) constructs one immediately before
 * dispatching the message and passes it through the callback chain, so the
 * origin travels *with* the message instead of living in ambient state on
 * the receiver. A stale binding is unrepresentable by construction: the
 * context dies when the dispatch call returns.
 *
 * Lifetime contract: valid only for the duration of the dispatch call, on
 * the dispatcher thread. Receivers must not retain a pointer or reference
 * past the callback's return; a handler that wants to finish work
 * asynchronously must resolve what it needs (session id, transport session
 * id) while the context is live.
 */
class MessageDispatchContext {
 public:
  virtual ~MessageDispatchContext() = default;

  /**
   * The connection the message physically arrived on. May be null when the
   * producer is not connection-backed (e.g. a legacy dispatch path with no
   * origin information). Only guaranteed valid while the context is live.
   */
  virtual network::Connection* originConnection() const = 0;

  /**
   * Durable transport-level session id the message belongs to (e.g. the SSE
   * stream id from a POST /callback/{id} path — the client identity that
   * outlives the one-shot POST connection). Empty when the transport has no
   * session concept (stdio, plain HTTP); receivers then fall back to
   * connection identity.
   */
  virtual const std::string& transportSessionId() const = 0;

  /**
   * Send a JSON-RPC response back along this message's own return path.
   * Returns an error (rather than silently dropping) when the path is gone,
   * e.g. the origin connection already closed.
   */
  virtual VoidResult sendResponse(const jsonrpc::Response& response) = 0;
};

/**
 * Context for dispatch paths that carry no origin information. Session
 * resolution falls back to "no connection" and any attempted reply fails
 * loudly instead of being written to an unrelated connection.
 */
class NullMessageDispatchContext : public MessageDispatchContext {
 public:
  network::Connection* originConnection() const override { return nullptr; }

  const std::string& transportSessionId() const override {
    static const std::string empty;
    return empty;
  }

  VoidResult sendResponse(const jsonrpc::Response&) override {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "no dispatch context: response has no return path";
    return makeVoidError(err);
  }
};

}  // namespace mcp
