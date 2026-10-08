// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * A server carrying a request's trace context.
 *
 * The context a request carries in _meta is current while its handler
 * runs, so what the handler sends while answering it carries the context
 * on: progress, questions to the client, and in 2026-07-28 the requests an
 * input_required answer asks the client to fulfil. A span hook hears each
 * request start and end with its outcome. A malformed context is ignored,
 * never a reason to refuse.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/protocol/trace_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;
namespace trace = protocol::trace;

const char* const kParent =
    "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
const char* const kState = "congo=t61rcWkgMzE";
const char* const kBaggage = "userId=alice";

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

/** A stream that keeps everything written to it. */
class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(
      const jsonrpc::Notification& notification) override {
    notified.push_back(notification);
    return makeVoidSuccess();
  }
  VoidResult sendRequest(const jsonrpc::Request& request) override {
    asked.push_back(request);
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  bool alive() const override { return true; }
  std::vector<jsonrpc::Notification> notified;
  std::vector<jsonrpc::Request> asked;
  std::vector<jsonrpc::Response> answered;
};

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  ResponseStreamPtr beginResponseStream() override { return stream; }
  optional<jsonrpc::Response> captured;
  std::shared_ptr<RecordingStream> stream = std::make_shared<RecordingStream>();
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "trace-test";
  config.server_version = "1.0.0";
  config.send_server_info = false;
  return config;
}

/** _meta with the trace keys, plus 2026-07-28 ones when modern. */
JsonValue tracedMeta(bool modern,
                     const std::string& parent = kParent,
                     const std::string& state = kState,
                     const std::string& baggage = kBaggage) {
  JsonValue meta = JsonValue::object();
  meta.set(trace::kTraceParent, JsonValue(parent));
  meta.set(trace::kTraceState, JsonValue(state));
  meta.set(trace::kBaggage, JsonValue(baggage));
  if (modern) {
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue("2026-07-28"));
    meta.set(protocol::modern::kMetaClientCapabilities,
             JsonValue::parse(R"({"elicitation":{}})"));
  }
  return meta;
}

jsonrpc::Request requestFor(const std::string& method,
                            const JsonValue& meta,
                            JsonValue params = JsonValue::object()) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(9);
  request.method = method;
  params.set("_meta", meta);
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(json::jsonToMetadata(params));
  return request;
}

void dispatch(McpServer& server,
              const jsonrpc::Request& request,
              CapturingContext& context) {
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
}

bool carriesTheTrace(const JsonValue& params) {
  if (!params.isObject() || !params.contains("_meta")) {
    return false;
  }
  const auto& meta = params["_meta"];
  return meta.contains(trace::kTraceParent) &&
         meta[trace::kTraceParent].getString() == kParent &&
         meta[trace::kTraceState].getString() == kState &&
         meta[trace::kBaggage].getString() == kBaggage;
}

/** Records each span, as an application's tracer would. */
struct RecordingTracer {
  struct Ended {
    trace::SpanStart start;
    optional<Error> error;
  };
  std::vector<trace::SpanStart> started;
  std::vector<Ended> ended;

  trace::SpanHook hook() {
    return [this](const trace::SpanStart& start) -> trace::SpanEnd {
      started.push_back(start);
      return [this, start](const optional<Error>& error) {
        ended.push_back({start, error});
      };
    };
  }
};

}  // namespace

// The handler sees the context its request carried.
TEST(TraceServer, TheHandlerSeesItsRequestsContext) {
  DispatchTestServer server(testConfig());
  trace::TraceContext seen;
  server.registerRequestHandler(
      "example/work",
      [&seen](const jsonrpc::Request& request, SessionContext&) {
        seen = trace::current();
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      });

  CapturingContext context;
  dispatch(server, requestFor("example/work", tracedMeta(false)), context);
  EXPECT_EQ(seen.traceparent, mcp::make_optional(std::string(kParent)));
  EXPECT_EQ(seen.tracestate, mcp::make_optional(std::string(kState)));
  EXPECT_EQ(seen.baggage, mcp::make_optional(std::string(kBaggage)));
  EXPECT_TRUE(trace::current().empty())
      << "a request's context outlived its dispatch";
}

// A malformed context is ignored, and the request served all the same.
TEST(TraceServer, AMalformedContextIsIgnored) {
  DispatchTestServer server(testConfig());
  bool ran = false;
  trace::TraceContext seen;
  server.registerRequestHandler(
      "example/work", [&](const jsonrpc::Request& request, SessionContext&) {
        ran = true;
        seen = trace::current();
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      });

  CapturingContext context;
  dispatch(server,
           requestFor("example/work",
                      tracedMeta(false, "00-bad", "Bad=1", "not baggage")),
           context);
  EXPECT_TRUE(ran);
  EXPECT_TRUE(seen.empty());
  ASSERT_TRUE(context.captured.has_value());
  EXPECT_FALSE(context.captured->error.has_value());
}

// Progress, and a question to the client, sent while answering carry the
// request's context.
TEST(TraceServer, WhatAHandlerSendsCarriesTheContext) {
  DispatchTestServer server(testConfig());
  server.registerRequestHandler(
      "example/work",
      [](const jsonrpc::Request& request, SessionContext& session) {
        const auto& stream = session.responseStream();
        jsonrpc::Notification progress("notifications/progress");
        progress.params_json = mcp::make_optional(
            JsonValue::parse(R"({"progressToken":1,"progress":50})"));
        stream->sendNotification(progress);
        jsonrpc::Request question;
        question.jsonrpc = "2.0";
        question.id = make_request_id(100);
        question.method = "roots/list";
        stream->sendRequest(question);
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      },
      StreamingMode::Optional);

  CapturingContext context;
  dispatch(server, requestFor("example/work", tracedMeta(false)), context);

  ASSERT_EQ(context.stream->notified.size(), 1u);
  const auto& progress = context.stream->notified[0];
  ASSERT_TRUE(progress.params_json.has_value());
  EXPECT_TRUE(carriesTheTrace(progress.params_json.value()))
      << progress.params_json->toString();
  EXPECT_EQ(progress.params_json.value()["progress"].getInt64(), 50);

  ASSERT_EQ(context.stream->asked.size(), 1u);
  ASSERT_TRUE(context.stream->asked[0].params_json.has_value());
  EXPECT_TRUE(carriesTheTrace(context.stream->asked[0].params_json.value()));
}

// A request with no context sends nothing extra.
TEST(TraceServer, NoContextAddsNothing) {
  DispatchTestServer server(testConfig());
  server.registerRequestHandler(
      "example/work",
      [](const jsonrpc::Request& request, SessionContext& session) {
        jsonrpc::Notification progress("notifications/progress");
        progress.params_json =
            mcp::make_optional(JsonValue::parse(R"({"progress":1})"));
        session.responseStream()->sendNotification(progress);
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      },
      StreamingMode::Optional);

  CapturingContext context;
  dispatch(server, requestFor("example/work", JsonValue::object()), context);
  ASSERT_EQ(context.stream->notified.size(), 1u);
  EXPECT_FALSE(context.stream->notified[0].params_json->contains("_meta"));
}

// In 2026-07-28, the requests an input_required answer asks the client
// to fulfil belong to the request's trace too.
TEST(TraceServer, InputRequestsCarryTheContext) {
  DispatchTestServer server(testConfig());
  server.registerAsyncRequestHandler(
      "tools/call",
      [&server](const jsonrpc::Request& request, SessionContext& session,
                const ResponseStreamPtr& stream) {
        protocol::modern::NeedsInput needed;
        protocol::modern::InputRequest ask;
        ask.method = protocol::modern::kMethodElicitation;
        ask.params = JsonValue::parse(R"({"message":"Which region?"})");
        needed.requests["region"] = ask;
        server.answerWithInput(stream, request, session, needed);
      });

  CapturingContext context;
  dispatch(server,
           requestFor("tools/call", tracedMeta(true),
                      JsonValue::parse(R"({"name":"deploy"})")),
           context);

  ASSERT_EQ(context.stream->answered.size(), 1u);
  const auto& result = context.stream->answered[0].result;
  ASSERT_TRUE(result.has_value());
  const JsonValue body = json::to_json(result.value());
  const auto& asked =
      body[protocol::modern::kInputRequestsField]["region"]["params"];
  EXPECT_TRUE(carriesTheTrace(asked)) << body.toString();
  EXPECT_EQ(asked["message"].getString(), "Which region?");
}

// The span hook hears each request start and end, with its outcome.
TEST(TraceServer, EachRequestIsASpan) {
  RecordingTracer tracer;
  McpServerConfig config = testConfig();
  config.span_hook = tracer.hook();
  DispatchTestServer server(config);
  server.registerRequestHandler(
      "example/fails", [](const jsonrpc::Request& request, SessionContext&) {
        return jsonrpc::Response::make_error(
            request.id, Error(jsonrpc::INVALID_PARAMS, "no"));
      });

  CapturingContext ok;
  dispatch(server, requestFor("ping", tracedMeta(false)), ok);
  CapturingContext failed;
  dispatch(server, requestFor("example/fails", JsonValue::object()), failed);

  ASSERT_EQ(tracer.started.size(), 2u);
  ASSERT_EQ(tracer.ended.size(), 2u);
  EXPECT_EQ(tracer.ended[0].start.kind, trace::SpanKind::Server);
  EXPECT_EQ(tracer.ended[0].start.method, "ping");
  EXPECT_EQ(tracer.ended[0].start.context.traceparent,
            mcp::make_optional(std::string(kParent)));
  EXPECT_FALSE(tracer.ended[0].error.has_value());
  EXPECT_EQ(tracer.ended[1].start.method, "example/fails");
  EXPECT_TRUE(tracer.ended[1].start.context.empty());
  ASSERT_TRUE(tracer.ended[1].error.has_value());
  EXPECT_EQ(tracer.ended[1].error->code, jsonrpc::INVALID_PARAMS);
}

// A request answered later ends its span when the answer goes.
TEST(TraceServer, ADeferredAnswerEndsItsSpan) {
  RecordingTracer tracer;
  McpServerConfig config = testConfig();
  config.span_hook = tracer.hook();
  DispatchTestServer server(config);
  ResponseStreamPtr held;
  jsonrpc::Request pending;
  server.registerAsyncRequestHandler(
      "example/later", [&](const jsonrpc::Request& request, SessionContext&,
                           const ResponseStreamPtr& stream) {
        held = stream;
        pending = request;
      });

  CapturingContext context;
  dispatch(server, requestFor("example/later", tracedMeta(false)), context);
  ASSERT_TRUE(held);
  EXPECT_TRUE(tracer.ended.empty()) << "the span ended before the answer";

  held->sendResponse(jsonrpc::Response::success(
      pending.id, jsonrpc::ResponseResult(JsonValue::object())));
  ASSERT_EQ(tracer.ended.size(), 1u);
  EXPECT_EQ(tracer.ended[0].start.method, "example/later");
  EXPECT_FALSE(tracer.ended[0].error.has_value());
}

}  // namespace server
}  // namespace mcp
