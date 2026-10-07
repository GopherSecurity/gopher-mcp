// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The server's name on every 2026-07-28 result.
 *
 * With no handshake in that revision, a server names itself in each
 * result's _meta, under io.modelcontextprotocol/serverInfo, unless it is
 * configured not to. Earlier revisions said it in the handshake and never
 * get it here.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
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
  using McpServer::onRequestWithContext;
};

/** A stream that keeps what was written. */
class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(const jsonrpc::Notification&) override {
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  bool alive() const override { return true; }
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
  config.server_name = "server-info-test";
  config.server_version = "4.2.0";
  return config;
}

/** A request, of 2026-07-28 when modern, else of an earlier revision. */
jsonrpc::Request requestFor(const std::string& method, bool modern) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(5);
  request.method = method;
  JsonValue params = JsonValue::object();
  if (modern) {
    JsonValue meta = JsonValue::object();
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue("2026-07-28"));
    params.set("_meta", meta);
  }
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(json::jsonToMetadata(params));
  return request;
}

/** The answer as a peer reads it: the synchronous one, or the streamed. */
JsonValue answerTo(McpServer& server, const jsonrpc::Request& request) {
  CapturingContext context;
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  if (context.captured.has_value()) {
    return json::to_json(context.captured.value());
  }
  if (!context.stream->answered.empty()) {
    return json::to_json(context.stream->answered.back());
  }
  ADD_FAILURE() << request.method << " went unanswered";
  return JsonValue::object();
}

bool namesTheServer(const JsonValue& answer) {
  if (!answer.contains("result") || !answer["result"].contains("_meta")) {
    return false;
  }
  const auto& meta = answer["result"]["_meta"];
  if (!meta.contains(protocol::modern::kMetaServerInfo)) {
    return false;
  }
  const auto& who = meta[protocol::modern::kMetaServerInfo];
  return who["name"].getString() == "server-info-test" &&
         who["version"].getString() == "4.2.0";
}

}  // namespace

// Every result a 2026-07-28 caller gets names the server, empty results
// included.
TEST(ServerInfoMeta, EveryNewestRevisionResultNamesTheServer) {
  DispatchTestServer server(testConfig());
  for (const char* method : {"ping", "tools/list", "prompts/list",
                             "resources/list", "resources/templates/list"}) {
    SCOPED_TRACE(method);
    const JsonValue answer = answerTo(server, requestFor(method, true));
    EXPECT_TRUE(namesTheServer(answer)) << answer.toString();
  }
}

// A caller of an earlier revision heard it in the handshake, and never
// gets it on a result.
TEST(ServerInfoMeta, AnEarlierRevisionNeverGetsIt) {
  DispatchTestServer server(testConfig());
  for (const char* method : {"ping", "tools/list"}) {
    SCOPED_TRACE(method);
    const JsonValue answer = answerTo(server, requestFor(method, false));
    ASSERT_TRUE(answer.contains("result")) << answer.toString();
    EXPECT_FALSE(answer["result"].contains("_meta")) << answer.toString();
  }
}

// An error is left as it is.
TEST(ServerInfoMeta, AnErrorIsLeftAlone) {
  DispatchTestServer server(testConfig());
  jsonrpc::Request call = requestFor("tools/call", true);
  JsonValue params = call.params_json.value();
  params.set("name", JsonValue("no_such_tool"));
  call.params_json = mcp::make_optional(params);
  call.params = mcp::make_optional(json::jsonToMetadata(params));
  const JsonValue answer = answerTo(server, call);
  ASSERT_TRUE(answer.contains("error")) << answer.toString();
  EXPECT_FALSE(answer.contains("result"));
  EXPECT_FALSE(answer["error"].contains("_meta"));
}

// What a handler put in _meta is kept, and a serverInfo of its own is not
// overwritten.
TEST(ServerInfoMeta, AHandlersOwnMetaIsKept) {
  DispatchTestServer server(testConfig());
  server.registerRequestHandler(
      "example/own", [](const jsonrpc::Request& request, SessionContext&) {
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::parse(R"({
              "_meta": {"com.example/trace": "t-1"}})")));
      });
  server.registerRequestHandler(
      "example/named", [](const jsonrpc::Request& request, SessionContext&) {
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::parse(R"({
              "_meta": {"io.modelcontextprotocol/serverInfo":
                        {"name": "gateway", "version": "9"}}})")));
      });

  const JsonValue own = answerTo(server, requestFor("example/own", true));
  EXPECT_EQ(own["result"]["_meta"]["com.example/trace"].getString(), "t-1");
  EXPECT_TRUE(namesTheServer(own)) << own.toString();

  const JsonValue named = answerTo(server, requestFor("example/named", true));
  EXPECT_EQ(named["result"]["_meta"][protocol::modern::kMetaServerInfo]["name"]
                .getString(),
            "gateway")
      << "the handler's own serverInfo was overwritten";
}

// An answer sent later, by a handler that answers asynchronously, names the
// server too.
TEST(ServerInfoMeta, ADeferredAnswerNamesTheServer) {
  DispatchTestServer server(testConfig());
  server.registerAsyncRequestHandler(
      "example/later", [](const jsonrpc::Request& request, SessionContext&,
                          const ResponseStreamPtr& answer) {
        answer->sendResponse(jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object())));
      });
  const JsonValue answer = answerTo(server, requestFor("example/later", true));
  EXPECT_TRUE(namesTheServer(answer)) << answer.toString();
}

// A server configured not to name itself sends none of it.
TEST(ServerInfoMeta, ItCanBeTurnedOff) {
  McpServerConfig config = testConfig();
  config.send_server_info = false;
  DispatchTestServer server(config);
  const JsonValue answer = answerTo(server, requestFor("tools/list", true));
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  EXPECT_FALSE(namesTheServer(answer)) << answer.toString();
}

}  // namespace server
}  // namespace mcp
