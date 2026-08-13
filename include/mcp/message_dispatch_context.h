#pragma once

#include <memory>
#include <string>

#include "mcp/core/result.h"
#include "mcp/json/json_bridge.h"
#include "mcp/types.h"

namespace mcp {

namespace network {
class Connection;
}

/**
 * A response that is still being produced after its dispatch returned.
 *
 * The dispatch context deliberately dies when the callback does, which is
 * what makes a stale reply path unrepresentable — but it also leaves a
 * handler with nowhere to put work it has not finished. This is that
 * place: it is reference-counted, a handler may keep it, and it stays
 * usable until the response is sent.
 *
 * Notifications sent here belong to the request being answered — progress,
 * logging — and arrive before the response. Sending the response ends the
 * stream; nothing may follow it.
 *
 * Dispatcher-thread confined, like everything it writes through.
 */
class ResponseStream {
 public:
  virtual ~ResponseStream() = default;

  /** Emit a notification related to the request being answered. */
  virtual VoidResult sendNotification(
      const jsonrpc::Notification& notification) = 0;

  /**
   * Ask the client something on the way to answering it.
   *
   * A server that needs the client to sample a model, or to be asked
   * anything else mid-request, sends the question here: it belongs to the
   * request being answered, so it goes down the same stream the answer
   * will. The client's reply comes back as an ordinary inbound message on
   * a connection of its choosing, and nothing but the JSON-RPC id
   * connects the two — so whoever sends one has to be waiting for that id.
   *
   * Virtual rather than pure: a transport that answers a request with
   * exactly one message has nowhere to put a question, and says so.
   */
  virtual VoidResult sendRequest(const jsonrpc::Request& request) {
    (void)request;
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "this response cannot carry a question to the client";
    return makeVoidError(err);
  }

  /**
   * Refuse the request with a status of its own.
   *
   * Some refusals are about the request rather than about what it asked
   * for, and the revision that defines them says which HTTP status each
   * carries — a transport that answered them all 200 would leave a
   * client reading a success that contains a failure.
   *
   * Falls back to an ordinary error response where a transport has no
   * status to set, which is every transport that is not HTTP.
   */
  virtual VoidResult sendRefusal(int http_status,
                                 const Error& error,
                                 const json::JsonValue& data) {
    (void)http_status;
    (void)data;
    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.error = mcp::make_optional(error);
    return sendResponse(response);
  }

  /** Emit the response and end the stream. */
  virtual VoidResult sendResponse(const jsonrpc::Response& response) = 0;

  /**
   * Whether anything sent now would still reach the client.
   *
   * False once the client has gone. That is not a reason to stop: a
   * disconnect is not a cancellation, and work already under way keeps its
   * output for a client that comes back. It is a reason to stop *waiting*
   * for anything the client would have to send.
   */
  virtual bool alive() const = 0;
};

using ResponseStreamPtr = std::shared_ptr<ResponseStream>;

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

  /**
   * Ask for somewhere to put an answer that is not ready yet.
   *
   * Null when this transport cannot stream one, which is every transport
   * that answers a request with exactly one message. A handler that gets
   * null has to answer through sendResponse and cannot report progress.
   */
  virtual ResponseStreamPtr beginResponseStream() { return nullptr; }
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
