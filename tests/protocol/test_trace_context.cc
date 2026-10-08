// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * W3C trace context in _meta: the formats each key is checked against,
 * how a context is added to a message, and how one is made current.
 *
 *   {"_meta": {"traceparent": "00-<trace-id>-<parent-id>-<flags>",
 *              "tracestate": "vendor=value,...",
 *              "baggage": "key=value;property,..."}}
 */

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/trace_context.h"
#include "mcp/types.h"

namespace mcp {
namespace protocol {
namespace trace {
namespace {

using json::JsonValue;

const char* const kParent =
    "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";

TraceContext full() {
  TraceContext context;
  context.traceparent = std::string(kParent);
  context.tracestate = std::string("congo=t61rcWkgMzE,rojo=00f067aa0ba902b7");
  context.baggage = std::string("userId=alice,isProduction=false");
  return context;
}

// ── The formats ────────────────────────────────────────────────────────

TEST(TraceContext, TraceParentFollowsTheW3CFormat) {
  EXPECT_TRUE(isValidTraceParent(kParent));
  EXPECT_TRUE(isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00"));
  // A later version may carry more after another dash.
  EXPECT_TRUE(isValidTraceParent(
      "01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-extra"));

  for (const char* bad : {
           "",
           "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7",
           // Upper case
           "00-4BF92F3577B34DA6A3CE929D0E0E4736-00f067aa0ba902b7-01",
           // An all-zero trace id, and an all-zero parent id
           "00-00000000000000000000000000000000-00f067aa0ba902b7-01",
           "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01",
           // Version ff is forbidden
           "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
           // Version 00 has nothing after the flags
           "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-x",
           // A later version runs on only after a dash
           "01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01x",
           "00_4bf92f3577b34da6a3ce929d0e0e4736_00f067aa0ba902b7_01",
           "00-4bf92f3577b34da6a3ce929d0e0e473g-00f067aa0ba902b7-01",
       }) {
    EXPECT_FALSE(isValidTraceParent(bad)) << bad;
  }
}

TEST(TraceContext, TraceStateFollowsTheW3CFormat) {
  for (const char* good : {
           "congo=t61rcWkgMzE",
           "rojo=00f067aa0ba902b7,congo=t61rcWkgMzE",
           " rojo=1 , congo=2 ",
           "tenant@system=value",
           "0tenant@vendor=v",
           "k=v with spaces",
           "a=1,,b=2",
       }) {
    EXPECT_TRUE(isValidTraceState(good)) << good;
  }

  std::string too_many;
  for (int i = 0; i < 33; ++i) {
    too_many += (i ? "," : "") + std::string("k") + std::to_string(i) + "=v";
  }
  for (const std::string& bad : {
           std::string(""),
           std::string(","),
           std::string("novalue"),
           std::string("Upper=v"),
           std::string("k=a=b"),
           std::string("k=control\x01char"),
           std::string("k="),
           std::string("dup=1,dup=2"),
           std::string("tenant@System=v"),
           std::string("@system=v"),
           too_many,
       }) {
    EXPECT_FALSE(isValidTraceState(bad)) << bad;
  }
}

TEST(TraceContext, BaggageFollowsTheW3CFormat) {
  for (const char* good : {
           "userId=alice",
           "userId=alice,isProduction=false",
           "userId = alice , serverNode=DF%2028",
           "key1=value1;property1;property2, key2 = value2",
           "key=value;prop=1",
           "empty=",
           "user=100%25",
           "name=%C3%A9t%c3%a9",
       }) {
    EXPECT_TRUE(isValidBaggage(good)) << good;
  }

  std::string too_long = "k=" + std::string(8200, 'a');
  std::string too_many;
  for (int i = 0; i < 65; ++i) {
    too_many += (i ? "," : "") + std::string("k") + std::to_string(i) + "=v";
  }
  for (const std::string& bad : {
           std::string(""),
           std::string("novalue"),
           std::string("=value"),
           std::string("key=has space"),
           std::string("key=\"quoted\""),
           std::string("k(ey)=v"),
           std::string("a=1,"),
           std::string("key=value;=p"),
           std::string("user=100%"),
           std::string("user=%GG"),
           std::string("user=%2"),
           std::string("key=v;prop=%zz"),
           too_long,
           too_many,
       }) {
    EXPECT_FALSE(isValidBaggage(bad)) << bad.substr(0, 40);
  }
}

// What may be sent: each value in its format, and a tracestate only
// beside a traceparent.
TEST(TraceContext, OnlyWellFormedValuesAreKept) {
  const TraceContext kept = sanitized(full());
  EXPECT_EQ(kept.traceparent, full().traceparent);
  EXPECT_EQ(kept.tracestate, full().tracestate);
  EXPECT_EQ(kept.baggage, full().baggage);

  TraceContext bad_parent = full();
  bad_parent.traceparent = std::string("not-a-traceparent");
  const TraceContext without = sanitized(bad_parent);
  EXPECT_FALSE(without.traceparent.has_value());
  EXPECT_FALSE(without.tracestate.has_value())
      << "a tracestate went without the traceparent it belongs to";
  EXPECT_EQ(without.baggage, full().baggage);

  TraceContext bad_rest = full();
  bad_rest.tracestate = std::string("Bad=1");
  bad_rest.baggage = std::string("not baggage");
  const TraceContext parent_only = sanitized(bad_rest);
  EXPECT_EQ(parent_only.traceparent, full().traceparent);
  EXPECT_FALSE(parent_only.tracestate.has_value());
  EXPECT_FALSE(parent_only.baggage.has_value());
}

// ── Reading and writing _meta ──────────────────────────────────────────

TEST(TraceContext, TheContextIsWrittenUnprefixedIntoMeta) {
  const JsonValue params = withContext(
      JsonValue::parse(R"({"name":"lint","_meta":{"progressToken":7}})"),
      full());
  EXPECT_EQ(params["name"].getString(), "lint");
  const auto& meta = params["_meta"];
  EXPECT_EQ(meta["progressToken"].getInt64(), 7);
  EXPECT_EQ(meta[kTraceParent].getString(), kParent);
  EXPECT_EQ(meta[kTraceState].getString(), full().tracestate.value());
  EXPECT_EQ(meta[kBaggage].getString(), full().baggage.value());

  const TraceContext back = fromParams(params);
  EXPECT_EQ(back.traceparent, full().traceparent);
  EXPECT_EQ(back.tracestate, full().tracestate);
  EXPECT_EQ(back.baggage, full().baggage);
}

TEST(TraceContext, OnlyWhatIsSetIsWritten) {
  TraceContext parent_only;
  parent_only.traceparent = std::string(kParent);
  const JsonValue params = withContext(JsonValue::object(), parent_only);
  EXPECT_EQ(params["_meta"].keys(), std::vector<std::string>{kTraceParent});

  const JsonValue untouched =
      withContext(JsonValue::parse(R"({"a":1})"), TraceContext());
  EXPECT_EQ(untouched.toString(), JsonValue::parse(R"({"a":1})").toString());
}

// What the application put in _meta itself wins.
TEST(TraceContext, WellFormedKeysAlreadyInMetaAreKept) {
  const std::string mine =
      "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01";
  const JsonValue params = withContext(
      JsonValue::parse(R"({"_meta":{"traceparent":")" + mine +
                       R"(","tracestate":"mine=1","baggage":"b=1","k":2}})"),
      full());
  EXPECT_EQ(params["_meta"][kTraceParent].getString(), mine);
  EXPECT_EQ(params["_meta"][kTraceState].getString(), "mine=1");
  EXPECT_EQ(params["_meta"][kBaggage].getString(), "b=1");
  EXPECT_EQ(params["_meta"]["k"].getInt64(), 2);

  // The application's traceparent brings its own tracestate, or none: the
  // context's tracestate belongs to the context's traceparent.
  const JsonValue alone = withContext(
      JsonValue::parse(R"({"_meta":{"traceparent":")" + mine + R"("}})"),
      full());
  EXPECT_EQ(alone["_meta"][kTraceParent].getString(), mine);
  EXPECT_FALSE(alone["_meta"].contains(kTraceState)) << alone.toString();
  EXPECT_EQ(alone["_meta"][kBaggage].getString(), full().baggage.value());
}

// A reserved key the application set in another format never goes out:
// the context's replaces it, or it is removed.
TEST(TraceContext, MalformedKeysAlreadyInMetaAreNotSent) {
  const JsonValue replaced = withContext(
      JsonValue::parse(
          R"({"_meta":{"traceparent":"mine","baggage":"not baggage"}})"),
      full());
  EXPECT_EQ(replaced["_meta"][kTraceParent].getString(), kParent);
  EXPECT_EQ(replaced["_meta"][kTraceState].getString(),
            full().tracestate.value());
  EXPECT_EQ(replaced["_meta"][kBaggage].getString(), full().baggage.value());

  const JsonValue removed = withContext(
      JsonValue::parse(R"({"_meta":{"traceparent":"mine","tracestate":"a=1",
                                    "baggage":7,"k":1}})"),
      TraceContext());
  EXPECT_EQ(removed["_meta"].toString(),
            JsonValue::parse(R"({"k":1})").toString());

  // A message is checked even when no context is added to it.
  jsonrpc::Request request;
  request.method = "tools/list";
  request.params_json = mcp::make_optional(
      JsonValue::parse(R"({"_meta":{"traceparent":"mine"}})"));
  const jsonrpc::Request sent = withContext(request, TraceContext());
  EXPECT_FALSE(sent.params_json.value()["_meta"].contains(kTraceParent));
}

TEST(TraceContext, MessagesWithoutParamsGainThem) {
  jsonrpc::Request request;
  request.method = "tools/list";
  const jsonrpc::Request traced = withContext(request, full());
  ASSERT_TRUE(traced.params_json.has_value());
  EXPECT_EQ(fromRequest(traced).traceparent, full().traceparent);

  jsonrpc::Notification notification("notifications/progress");
  Metadata flat;
  flat["progress"] = static_cast<int64_t>(5);
  notification.params = flat;
  const jsonrpc::Notification sent = withContext(notification, full());
  ASSERT_TRUE(sent.params_json.has_value());
  EXPECT_EQ(sent.params_json.value()["progress"].getInt64(), 5);
  EXPECT_EQ(sent.params_json.value()["_meta"][kBaggage].getString(),
            full().baggage.value());
}

// A malformed context is read as none, never as a failure.
TEST(TraceContext, MalformedValuesAreIgnoredWhenRead) {
  const TraceContext read = fromParams(JsonValue::parse(R"({"_meta":{
      "traceparent":"00-zz-yy-01","tracestate":"a=1","baggage":7}})"));
  EXPECT_TRUE(read.empty());
  EXPECT_TRUE(fromParams(JsonValue::parse(R"({"_meta":"x"})")).empty());
  EXPECT_TRUE(fromParams(JsonValue::parse(R"([1,2])")).empty());
}

// ── Current ────────────────────────────────────────────────────────────

// A scope is active even when what it holds is empty: that says the work
// carries no trace, which is not the same as saying nothing.
TEST(TraceContext, AnEmptyScopeIsStillAScope) {
  EXPECT_FALSE(inScope());
  {
    TraceScope empty{TraceContext()};
    EXPECT_TRUE(inScope());
    EXPECT_TRUE(current().empty());
    {
      TraceScope inner(full());
      EXPECT_TRUE(inScope());
    }
    EXPECT_TRUE(inScope());
    EXPECT_TRUE(current().empty());
  }
  EXPECT_FALSE(inScope());
}

TEST(TraceContext, AScopeMakesAContextCurrentUntilItEnds) {
  EXPECT_TRUE(current().empty());
  {
    TraceScope outer(full());
    EXPECT_EQ(current().traceparent, full().traceparent);
    {
      TraceContext other;
      other.baggage = std::string("k=v");
      TraceScope inner(other);
      EXPECT_FALSE(current().traceparent.has_value());
      EXPECT_EQ(current().baggage, mcp::make_optional(std::string("k=v")));
    }
    EXPECT_EQ(current().traceparent, full().traceparent);

    // Each thread has its own.
    bool other_thread_empty = false;
    std::thread([&other_thread_empty]() {
      other_thread_empty = current().empty();
    }).join();
    EXPECT_TRUE(other_thread_empty);
  }
  EXPECT_TRUE(current().empty());

  TraceContext bad;
  bad.traceparent = std::string("bad");
  TraceScope scope(bad);
  EXPECT_TRUE(current().empty()) << "a malformed context was made current";
}

// ── Spans ──────────────────────────────────────────────────────────────

TEST(TraceContext, ASpanEndsOnceWithItsOutcome) {
  std::vector<SpanStart> started;
  std::vector<optional<Error>> ended;
  SpanHook hook = [&](const SpanStart& start) -> SpanEnd {
    started.push_back(start);
    return [&ended](const optional<Error>& error) { ended.push_back(error); };
  };

  SpanStart start{SpanKind::Client, "tools/call", full()};
  {
    Span span(hook, start);
    span.end(nullopt);
    span.end(mcp::make_optional(Error(1, "again")));
  }
  ASSERT_EQ(started.size(), 1u);
  EXPECT_EQ(started[0].method, "tools/call");
  EXPECT_EQ(started[0].context.traceparent, full().traceparent);
  ASSERT_EQ(ended.size(), 1u);
  EXPECT_FALSE(ended[0].has_value());

  // One never ended is ended when it goes, as a request with no answer.
  {
    Span abandoned(hook, start);
  }
  ASSERT_EQ(ended.size(), 2u);
  ASSERT_TRUE(ended[1].has_value());
  EXPECT_EQ(ended[1]->code, jsonrpc::INTERNAL_ERROR);

  // A tracer that throws, starting or ending, costs nothing but its span.
  SpanHook throws_starting = [](const SpanStart&) -> SpanEnd {
    throw std::runtime_error("tracer down");
  };
  EXPECT_NO_THROW({
    Span span(throws_starting, start);
    span.end(nullopt);
  });
  SpanHook throws_ending = [](const SpanStart&) -> SpanEnd {
    return [](const optional<Error>&) {
      throw std::runtime_error("exporter down");
    };
  };
  EXPECT_NO_THROW({
    Span span(throws_ending, start);
    span.end(nullopt);
  });
  EXPECT_NO_THROW({ Span abandoned(throws_ending, start); });

  // No hook, nothing at all.
  Span untraced(SpanHook(), start);
  untraced.end(nullopt);
  EXPECT_EQ(started.size(), 2u);
}

}  // namespace
}  // namespace trace
}  // namespace protocol
}  // namespace mcp
