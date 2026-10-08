// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * W3C trace context in _meta. See the header.
 */

#include "mcp/protocol/trace_context.h"

#include <set>
#include <utility>
#include <vector>

#include "mcp/json/json_serialization.h"

namespace mcp {
namespace protocol {
namespace trace {

namespace {

bool isLowerHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool isLowerHex(const std::string& value, size_t from, size_t count) {
  for (size_t i = from; i < from + count; ++i) {
    if (!isLowerHex(value[i])) {
      return false;
    }
  }
  return true;
}

bool allZero(const std::string& value, size_t from, size_t count) {
  for (size_t i = from; i < from + count; ++i) {
    if (value[i] != '0') {
      return false;
    }
  }
  return true;
}

bool isOws(char c) { return c == ' ' || c == '\t'; }

std::string trimmed(const std::string& value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && isOws(value[begin])) {
    ++begin;
  }
  while (end > begin && isOws(value[end - 1])) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& value, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t at = value.find(separator, start);
    if (at == std::string::npos) {
      parts.push_back(value.substr(start));
      return parts;
    }
    parts.push_back(value.substr(start, at - start));
    start = at + 1;
  }
}

// ── tracestate ─────────────────────────────────────────────────────────

bool isLowerAlpha(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isKeyChar(char c) {
  return isLowerAlpha(c) || isDigit(c) || c == '_' || c == '-' || c == '*' ||
         c == '/';
}

// first, then up to `more` key characters.
bool isKeyPart(const std::string& part, bool first_may_be_digit, size_t more) {
  if (part.empty() || part.size() > more + 1) {
    return false;
  }
  if (!isLowerAlpha(part[0]) && !(first_may_be_digit && isDigit(part[0]))) {
    return false;
  }
  for (size_t i = 1; i < part.size(); ++i) {
    if (!isKeyChar(part[i])) {
      return false;
    }
  }
  return true;
}

// A simple key, or tenant@system.
bool isTraceStateKey(const std::string& key) {
  const size_t at = key.find('@');
  if (at == std::string::npos) {
    return isKeyPart(key, true, 255);
  }
  return isKeyPart(key.substr(0, at), true, 240) &&
         isKeyPart(key.substr(at + 1), false, 13);
}

// Printable ASCII but ',' and '=', not ending in a space.
bool isTraceStateValue(const std::string& value) {
  if (value.empty() || value.size() > 256 || value.back() == ' ') {
    return false;
  }
  for (char c : value) {
    if (c < 0x20 || c > 0x7E || c == ',' || c == '=') {
      return false;
    }
  }
  return true;
}

// ── baggage ────────────────────────────────────────────────────────────

constexpr size_t kMaxBaggageMembers = 64;
constexpr size_t kMaxBaggageBytes = 8192;

bool isTokenChar(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c)) {
    return true;
  }
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

bool isToken(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  for (char c : value) {
    if (!isTokenChar(c)) {
      return false;
    }
  }
  return true;
}

// Printable ASCII but space, '"', ',', ';' and '\'.
bool isBaggageValue(const std::string& value) {
  for (char c : value) {
    if (c < 0x21 || c > 0x7E || c == '"' || c == ',' || c == ';' || c == '\\') {
      return false;
    }
  }
  return true;
}

// key OWS "=" OWS value, with value allowed absent only when optional.
bool isKeyValue(const std::string& text, bool value_optional) {
  const size_t equals = text.find('=');
  if (equals == std::string::npos) {
    return value_optional && isToken(trimmed(text));
  }
  return isToken(trimmed(text.substr(0, equals))) &&
         isBaggageValue(trimmed(text.substr(equals + 1)));
}

bool isBaggageMember(const std::string& member) {
  const auto parts = split(member, ';');
  if (!isKeyValue(parts[0], false)) {
    return false;
  }
  for (size_t i = 1; i < parts.size(); ++i) {
    if (!isKeyValue(parts[i], true)) {
      return false;
    }
  }
  return true;
}

optional<std::string> stringAt(const json::JsonValue& meta, const char* key) {
  if (meta.contains(key) && meta[key].isString()) {
    return meta[key].getString();
  }
  return nullopt;
}

json::JsonValue paramsJsonOf(const optional<json::JsonValue>& params_json,
                             const optional<Metadata>& params) {
  if (params_json.has_value()) {
    return params_json.value();
  }
  if (params.has_value()) {
    return json::metadataToJson(params.value());
  }
  return json::JsonValue::object();
}

TraceContext& currentOnThisThread() {
  static thread_local TraceContext context;
  return context;
}

}  // namespace

bool isValidTraceParent(const std::string& value) {
  // version "-" trace-id "-" parent-id "-" flags: 2, 32, 16 and 2 lowercase
  // hex digits. A later version may append fields after another "-".
  if (value.size() < 55 || value[2] != '-' || value[35] != '-' ||
      value[52] != '-') {
    return false;
  }
  if (!isLowerHex(value, 0, 2) || !isLowerHex(value, 3, 32) ||
      !isLowerHex(value, 36, 16) || !isLowerHex(value, 53, 2)) {
    return false;
  }
  const std::string version = value.substr(0, 2);
  if (version == "ff") {
    return false;
  }
  if (version == "00" ? value.size() != 55
                      : value.size() > 55 && value[55] != '-') {
    return false;
  }
  return !allZero(value, 3, 32) && !allZero(value, 36, 16);
}

bool isValidTraceState(const std::string& value) {
  std::set<std::string> keys;
  for (const auto& entry : split(value, ',')) {
    const std::string member = trimmed(entry);
    if (member.empty()) {
      continue;
    }
    const size_t equals = member.find('=');
    if (equals == std::string::npos) {
      return false;
    }
    const std::string key = member.substr(0, equals);
    if (!isTraceStateKey(key) ||
        !isTraceStateValue(member.substr(equals + 1)) ||
        !keys.insert(key).second) {
      return false;
    }
  }
  return !keys.empty() && keys.size() <= 32;
}

bool isValidBaggage(const std::string& value) {
  if (value.empty() || value.size() > kMaxBaggageBytes) {
    return false;
  }
  const auto members = split(value, ',');
  if (members.size() > kMaxBaggageMembers) {
    return false;
  }
  for (const auto& member : members) {
    if (!isBaggageMember(member)) {
      return false;
    }
  }
  return true;
}

TraceContext sanitized(const TraceContext& context) {
  TraceContext kept;
  if (context.traceparent.has_value() &&
      isValidTraceParent(context.traceparent.value())) {
    kept.traceparent = context.traceparent;
    if (context.tracestate.has_value() &&
        isValidTraceState(context.tracestate.value())) {
      kept.tracestate = context.tracestate;
    }
  }
  if (context.baggage.has_value() && isValidBaggage(context.baggage.value())) {
    kept.baggage = context.baggage;
  }
  return kept;
}

TraceContext fromMeta(const json::JsonValue& meta) {
  if (!meta.isObject()) {
    return TraceContext();
  }
  TraceContext found;
  found.traceparent = stringAt(meta, kTraceParent);
  found.tracestate = stringAt(meta, kTraceState);
  found.baggage = stringAt(meta, kBaggage);
  return sanitized(found);
}

TraceContext fromParams(const json::JsonValue& params) {
  if (!params.isObject() || !params.contains("_meta")) {
    return TraceContext();
  }
  return fromMeta(params["_meta"]);
}

TraceContext fromRequest(const jsonrpc::Request& request) {
  return fromParams(paramsJsonOf(request.params_json, request.params));
}

json::JsonValue withContext(const json::JsonValue& params,
                            const TraceContext& context) {
  const bool has_meta = params.isObject() && params.contains("_meta") &&
                        params["_meta"].isObject();
  const json::JsonValue given =
      has_meta ? params["_meta"] : json::JsonValue::object();
  const bool sets_any = given.contains(kTraceParent) ||
                        given.contains(kTraceState) || given.contains(kBaggage);
  const TraceContext ours = sanitized(context);
  if (ours.empty() && !sets_any) {
    return params;
  }

  // What the application set itself wins, but only where it is well formed:
  // the reserved keys are never sent in any other format. A tracestate
  // belongs to the traceparent it came with, so the two go together from
  // whichever side the traceparent is taken.
  TraceContext own;
  own.traceparent = stringAt(given, kTraceParent);
  own.tracestate = stringAt(given, kTraceState);
  own.baggage = stringAt(given, kBaggage);
  own = sanitized(own);
  TraceContext sent;
  if (own.traceparent.has_value()) {
    sent.traceparent = own.traceparent;
    sent.tracestate = own.tracestate;
  } else {
    sent.traceparent = ours.traceparent;
    sent.tracestate = ours.tracestate;
  }
  sent.baggage = own.baggage.has_value() ? own.baggage : ours.baggage;

  json::JsonValue meta = json::JsonValue::object();
  for (const auto& key : given.keys()) {
    if (key != kTraceParent && key != kTraceState && key != kBaggage) {
      meta.set(key, given[key]);
    }
  }
  const std::pair<const char*, const optional<std::string>*> keys[] = {
      {kTraceParent, &sent.traceparent},
      {kTraceState, &sent.tracestate},
      {kBaggage, &sent.baggage}};
  for (const auto& key : keys) {
    if (key.second->has_value()) {
      meta.set(key.first, json::JsonValue(key.second->value()));
    }
  }
  json::JsonValue out = params.isObject() ? params : json::JsonValue::object();
  out.set("_meta", meta);
  return out;
}

namespace {
// Whether a message's own params set any of the reserved keys, which are
// checked before it goes out whether or not a context is added.
bool setsTraceKeys(const json::JsonValue& params) {
  if (!params.isObject() || !params.contains("_meta") ||
      !params["_meta"].isObject()) {
    return false;
  }
  const auto& meta = params["_meta"];
  return meta.contains(kTraceParent) || meta.contains(kTraceState) ||
         meta.contains(kBaggage);
}
}  // namespace

jsonrpc::Request withContext(const jsonrpc::Request& request,
                             const TraceContext& context) {
  if (sanitized(context).empty() &&
      !setsTraceKeys(paramsJsonOf(request.params_json, request.params))) {
    return request;
  }
  jsonrpc::Request out = request;
  out.params_json = mcp::make_optional(
      withContext(paramsJsonOf(request.params_json, request.params), context));
  return out;
}

jsonrpc::Notification withContext(const jsonrpc::Notification& notification,
                                  const TraceContext& context) {
  if (sanitized(context).empty() &&
      !setsTraceKeys(
          paramsJsonOf(notification.params_json, notification.params))) {
    return notification;
  }
  jsonrpc::Notification out = notification;
  out.params_json = mcp::make_optional(withContext(
      paramsJsonOf(notification.params_json, notification.params), context));
  return out;
}

const TraceContext& current() { return currentOnThisThread(); }

TraceScope::TraceScope(const TraceContext& context)
    : previous_(currentOnThisThread()) {
  currentOnThisThread() = sanitized(context);
}

TraceScope::~TraceScope() { currentOnThisThread() = std::move(previous_); }

Span::Span(const SpanHook& hook, SpanStart start) {
  if (hook) {
    end_ = hook(start);
  }
}

Span::~Span() {
  end(mcp::make_optional(
      Error(jsonrpc::INTERNAL_ERROR, "the request ended without an answer")));
}

void Span::end(const optional<Error>& error) {
  if (!end_) {
    return;
  }
  auto finished = std::move(end_);
  end_ = nullptr;
  finished(error);
}

}  // namespace trace
}  // namespace protocol
}  // namespace mcp
