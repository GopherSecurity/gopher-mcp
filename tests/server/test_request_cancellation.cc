// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * A server learning that a request was cancelled.
 *
 * A request is cancelled when the client sends notifications/cancelled
 * naming it, or, where the transport says a disconnect means it, when the
 * client leaves the request's stream. Every handler, plain or streaming,
 * can check, and can register to be told. The answer to a cancelled
 * request is not sent.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onNotificationWithContext;
  using McpServer::onRequestWithContext;
};

/** A stream that keeps what was written and can be disconnected. */
class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(const jsonrpc::Notification&) override {
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  bool alive() const override { return !gone; }
  bool onCancelled(std::function<void()> observer) override {
    observers.push_back(std::move(observer));
    return true;
  }
  /** The client leaves, as a 2026-07-28 client cancelling does. */
  void disconnect() {
    gone = true;
    for (auto& observer : observers) {
      observer();
    }
  }
  std::vector<jsonrpc::Response> answered;
  std::vector<std::function<void()>> observers;
  bool gone{false};
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

/** Messages from another client: a session of their own. */
class OtherClientContext : public CapturingContext {
 public:
  const std::string& transportSessionId() const override { return id_; }

 private:
  std::string id_ = "another-client";
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "cancellation-test";
  config.server_version = "1.0.0";
  config.send_server_info = false;
  return config;
}

jsonrpc::Request requestFor(const std::string& method, const RequestId& id) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = id;
  request.method = method;
  request.params_json = mcp::make_optional(JsonValue::object());
  request.params = mcp::make_optional(Metadata());
  return request;
}

jsonrpc::Notification cancelledFor(const JsonValue& request_id) {
  jsonrpc::Notification notification("notifications/cancelled");
  JsonValue params = JsonValue::object();
  params.set("requestId", request_id);
  params.set("reason", JsonValue("User requested cancellation"));
  notification.params_json = mcp::make_optional(params);
  notification.params = mcp::make_optional(json::jsonToMetadata(params));
  return notification;
}

/** A server whose "example/later" handler answers only when told to. */
struct LaterServer {
  DispatchTestServer server{testConfig()};
  ResponseStreamPtr answer;
  CancellationPtr cancellation;
  jsonrpc::Request pending;
  int told{0};

  LaterServer() {
    server.registerAsyncRequestHandler(
        "example/later",
        [this](const jsonrpc::Request& request, SessionContext& session,
               const ResponseStreamPtr& stream) {
          answer = stream;
          pending = request;
          cancellation = session.cancellation();
          if (cancellation) {
            cancellation->onCancelled([this]() { ++told; });
          }
        });
  }

  void finish() {
    answer->sendResponse(jsonrpc::Response::success(
        pending.id, jsonrpc::ResponseResult(JsonValue::object())));
  }
};

}  // namespace

// ── The shared state ───────────────────────────────────────────────────

TEST(RequestCancellation, ACancellationTellsEachObserverOnce) {
  Cancellation cancellation;
  int told = 0;
  cancellation.onCancelled([&told]() { ++told; });
  EXPECT_FALSE(cancellation.isCancelled());
  EXPECT_TRUE(cancellation.cancel());
  EXPECT_FALSE(cancellation.cancel()) << "cancelled twice";
  EXPECT_TRUE(cancellation.isCancelled());
  EXPECT_EQ(told, 1);

  // An observer arriving late is told at once.
  cancellation.onCancelled([&told]() { ++told; });
  EXPECT_EQ(told, 2);

  // One observer failing does not keep the next from hearing.
  Cancellation other;
  other.onCancelled([]() { throw std::runtime_error("observer failed"); });
  other.onCancelled([&told]() { ++told; });
  EXPECT_NO_THROW(other.cancel());
  EXPECT_EQ(told, 3);
}

// ── Every handler can check ────────────────────────────────────────────

TEST(RequestCancellation, APlainHandlerCanCheck) {
  DispatchTestServer server(testConfig());
  bool had_cancellation = false;
  bool cancelled = true;
  server.registerRequestHandler(
      "example/work",
      [&](const jsonrpc::Request& request, SessionContext& session) {
        had_cancellation = session.cancellation() != nullptr;
        cancelled = session.isCancelled();
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      });

  CapturingContext context;
  server.onRequestWithContext(requestFor("example/work", make_request_id(1)),
                              context);
  EXPECT_TRUE(had_cancellation);
  EXPECT_FALSE(cancelled);
  ASSERT_TRUE(context.captured.has_value());
}

// ── Cancelled by message ───────────────────────────────────────────────

TEST(RequestCancellation, ACancelledNotificationCancelsTheRequest) {
  for (const bool string_id : {true, false}) {
    SCOPED_TRACE(string_id ? "string id" : "number id");
    LaterServer later;
    CapturingContext context;
    const RequestId id = string_id ? RequestId(std::string("123"))
                                   : RequestId(static_cast<int64_t>(123));
    later.server.onRequestWithContext(requestFor("example/later", id), context);
    ASSERT_TRUE(later.cancellation);
    EXPECT_FALSE(later.cancellation->isCancelled());

    CapturingContext again;
    later.server.onNotificationWithContext(
        cancelledFor(string_id ? JsonValue("123") : JsonValue(123)), again);
    EXPECT_TRUE(later.cancellation->isCancelled());
    EXPECT_EQ(later.told, 1);

    // Its answer is not sent.
    later.finish();
    EXPECT_TRUE(context.stream->answered.empty())
        << "a cancelled request was answered";
  }
}

// The string id "123" and the number 123 are different requests, and a
// request is cancelled only from the session that sent it.
TEST(RequestCancellation, OnlyTheRequestNamedIsCancelled) {
  LaterServer later;
  CapturingContext context;
  later.server.onRequestWithContext(
      requestFor("example/later", RequestId(static_cast<int64_t>(123))),
      context);
  ASSERT_TRUE(later.cancellation);

  CapturingContext same_client;
  later.server.onNotificationWithContext(cancelledFor(JsonValue("123")),
                                         same_client);
  later.server.onNotificationWithContext(cancelledFor(JsonValue(124)),
                                         same_client);
  OtherClientContext other_client;
  later.server.onNotificationWithContext(cancelledFor(JsonValue(123)),
                                         other_client);
  EXPECT_FALSE(later.cancellation->isCancelled());

  later.finish();
  EXPECT_EQ(context.stream->answered.size(), 1u);
}

// ── Cancelled by leaving ───────────────────────────────────────────────

// Where the transport says a disconnect cancels, it does, and the handler
// is told.
TEST(RequestCancellation, LeavingTheStreamCancelsTheRequest) {
  LaterServer later;
  CapturingContext context;
  later.server.onRequestWithContext(
      requestFor("example/later", make_request_id(7)), context);
  ASSERT_TRUE(later.cancellation);
  ASSERT_FALSE(context.stream->observers.empty());

  context.stream->disconnect();
  EXPECT_TRUE(later.cancellation->isCancelled());
  EXPECT_EQ(later.told, 1);
  later.finish();
  EXPECT_TRUE(context.stream->answered.empty());
}

// A request answered before anyone cancelled it is answered as usual.
TEST(RequestCancellation, ARequestNotCancelledIsAnswered) {
  LaterServer later;
  CapturingContext context;
  later.server.onRequestWithContext(
      requestFor("example/later", make_request_id(8)), context);
  later.finish();
  ASSERT_EQ(context.stream->answered.size(), 1u);
  EXPECT_FALSE(context.stream->answered[0].error.has_value());

  // And one cancelled after it was answered changes nothing.
  CapturingContext again;
  later.server.onNotificationWithContext(cancelledFor(JsonValue(8)), again);
  EXPECT_EQ(later.told, 0);
}

// A span around a cancelled request ends as cancelled.
TEST(RequestCancellation, ACancelledRequestsSpanEndsAsCancelled) {
  McpServerConfig config = testConfig();
  std::vector<optional<Error>> ended;
  config.span_hook =
      [&ended](const protocol::trace::SpanStart&) -> protocol::trace::SpanEnd {
    return [&ended](const optional<Error>& error) { ended.push_back(error); };
  };
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
  server.onRequestWithContext(requestFor("example/later", make_request_id(9)),
                              context);
  CapturingContext again;
  server.onNotificationWithContext(cancelledFor(JsonValue(9)), again);
  held->sendResponse(jsonrpc::Response::success(
      pending.id, jsonrpc::ResponseResult(JsonValue::object())));

  ASSERT_EQ(ended.size(), 1u);
  ASSERT_TRUE(ended[0].has_value());
  EXPECT_EQ(ended[0]->code, jsonrpc::REQUEST_CANCELLED);
}

}  // namespace server
}  // namespace mcp
