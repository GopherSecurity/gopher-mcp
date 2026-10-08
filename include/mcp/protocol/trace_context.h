// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * W3C trace context, carried in a message's _meta.
 *
 * A call through an agent, a client, a gateway and an MCP server can be
 * followed as one trace only if the context travels with it. The spec
 * reserves three _meta keys for it, without the prefix other keys need:
 *
 *   traceparent  W3C Trace Context, such as
 *                00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01
 *   tracestate   W3C Trace Context vendor entries, such as congo=t61rcWkgMzE
 *   baggage      W3C Baggage, such as userId=alice,isProduction=false
 *
 * Values are checked against those formats both ways: one that isn't in
 * its format is neither sent nor read, and the message goes ahead without
 * it. A tracestate means nothing without a traceparent, so it goes only
 * with one.
 *
 * The context is current for a thread inside a TraceScope. The client
 * sends the current context with each request it makes; the server makes
 * each request's context current while its handler runs, so what the
 * handler sends carries it on.
 */

#pragma once

#include <functional>
#include <string>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"
#include "mcp/types.h"

namespace mcp {
namespace protocol {
namespace trace {

constexpr const char* kTraceParent = "traceparent";
constexpr const char* kTraceState = "tracestate";
constexpr const char* kBaggage = "baggage";

/** A trace context: any of the three, each only when set. */
struct TraceContext {
  optional<std::string> traceparent;
  optional<std::string> tracestate;
  optional<std::string> baggage;

  bool empty() const {
    return !traceparent.has_value() && !tracestate.has_value() &&
           !baggage.has_value();
  }
};

bool isValidTraceParent(const std::string& value);
bool isValidTraceState(const std::string& value);
bool isValidBaggage(const std::string& value);

/**
 * What of a context may be sent: each value in its format, and a
 * tracestate only beside a traceparent. Values are kept exactly as given.
 */
TraceContext sanitized(const TraceContext& context);

/** The context in a _meta object, checked; empty for anything else. */
TraceContext fromMeta(const json::JsonValue& meta);

/** The context in a message's params, read from their _meta. */
TraceContext fromParams(const json::JsonValue& params);

/** The context a request carries. */
TraceContext fromRequest(const jsonrpc::Request& request);

/**
 * params with the context added to their _meta. Only keys that are set
 * are written. A well-formed key the application already put in _meta
 * wins, its traceparent bringing its own tracestate with it; one that is
 * not well formed is replaced by the context's, or removed, so the
 * reserved keys never go out in any other format. Anything but an object,
 * or no params at all, becomes an object holding just _meta.
 */
json::JsonValue withContext(const json::JsonValue& params,
                            const TraceContext& context);

/** The request or notification, its params carrying the context. */
jsonrpc::Request withContext(const jsonrpc::Request& request,
                             const TraceContext& context);
jsonrpc::Notification withContext(const jsonrpc::Notification& notification,
                                  const TraceContext& context);

/** The context current on this thread; empty outside any TraceScope. */
const TraceContext& current();

/**
 * Makes a context current on this thread until it goes out of scope, when
 * the one before it is current again. Only what sanitized() keeps is made
 * current.
 */
class TraceScope {
 public:
  explicit TraceScope(const TraceContext& context);
  ~TraceScope();
  TraceScope(const TraceScope&) = delete;
  TraceScope& operator=(const TraceScope&) = delete;

 private:
  TraceContext previous_;
};

// ── Spans ──────────────────────────────────────────────────────────────

enum class SpanKind {
  // A request this side received and is handling.
  Server,
  // A request this side sent and is waiting on.
  Client,
};

/** What a span is about, as it starts. */
struct SpanStart {
  SpanKind kind;
  std::string method;
  // The context the request carried in, or is carrying out.
  TraceContext context;
};

/**
 * Ends a span: told the error when the request failed, nothing when it
 * succeeded. Called once.
 */
using SpanEnd = std::function<void(const optional<Error>& error)>;

/**
 * Starts a span around a request, returning what ends it, so an
 * application can connect requests to its tracer, OpenTelemetry or
 * otherwise. May return an empty function when there is nothing to end.
 */
using SpanHook = std::function<SpanEnd(const SpanStart& span)>;

/**
 * One span, ended exactly once: by end(), or, if that never happens, when
 * the last copy goes, as a request that ended without an answer. Whatever
 * the hook or what it returned throws is dropped: a tracer never costs a
 * request its answer.
 */
class Span {
 public:
  Span() = default;
  Span(const SpanHook& hook, SpanStart start);
  ~Span();
  Span(const Span&) = delete;
  Span& operator=(const Span&) = delete;

  void end(const optional<Error>& error);

 private:
  SpanEnd end_;
};

}  // namespace trace
}  // namespace protocol
}  // namespace mcp
