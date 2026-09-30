// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * A call to a tool the server does not have, and the errors a server sends.
 *
 * An unknown tool is the caller's mistake and is answered with the protocol
 * error -32602 in every revision. A tool that exists and fails is a result
 * with isError set, which the model can read and act on. The two must stay
 * apart, or a client cannot tell a request it got wrong from a tool that
 * ran and did not work.
 */

#include <stdexcept>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;
using namespace mcp::server;

namespace {

/** Widens the dispatch entry so a request can be handed straight in. */
class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

/** A return path that keeps what was sent instead of writing it. */
class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }

  optional<jsonrpc::Response> captured;
};

/** A request, declaring a revision in _meta when given one. */
jsonrpc::Request requestFor(const std::string& method,
                            Metadata params,
                            const std::string& revision = std::string()) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  if (!revision.empty()) {
    JsonValue meta = JsonValue::object();
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue(revision));
    params["_meta"] = MetadataValue(meta.toString());
  }
  request.params = mcp::make_optional(params);
  return request;
}

jsonrpc::Request callFor(const std::string& tool,
                         const std::string& revision = std::string()) {
  Metadata params;
  params["name"] = MetadataValue(tool);
  return requestFor("tools/call", params, revision);
}

/** The answer to a request, as a peer would read it. */
JsonValue answerTo(McpServer& server, const jsonrpc::Request& request) {
  CapturingContext context;
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  if (!context.captured.has_value()) {
    ADD_FAILURE() << request.method << " went unanswered";
    return JsonValue::object();
  }
  return json::to_json(context.captured.value());
}

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "unknown-tool-test";
  config.server_version = "0.0.1";
  return config;
}

const std::vector<std::string> kRevisions = {std::string(), "2025-06-18",
                                             "2026-07-28"};

}  // namespace

// Every revision names an unknown tool among the protocol errors.
TEST(UnknownTool, IsAnsweredWithInvalidParams) {
  DispatchTestServer server(testConfig());

  for (const auto& revision : kRevisions) {
    SCOPED_TRACE(revision.empty() ? "no revision declared" : revision);
    const JsonValue answer =
        answerTo(server, callFor("no_such_tool", revision));

    EXPECT_FALSE(answer.contains("result"))
        << "an unknown tool was answered as a tool that ran: "
        << answer.toString();
    ASSERT_TRUE(answer.contains("error")) << answer.toString();
    EXPECT_EQ(answer["error"]["code"].getInt(), -32602);
    EXPECT_NE(answer["error"]["message"].getString().find("no_such_tool"),
              std::string::npos)
        << "the error does not name the tool";
  }
}

// A tool that is there and fails is still a result the model can read.
TEST(UnknownTool, AToolThatThrowsIsStillAResult) {
  DispatchTestServer server(testConfig());
  Tool tool;
  tool.name = "broken";
  server.registerTool(tool,
                      [](const std::string&, const optional<Metadata>&,
                         SessionContext&) -> CallToolResult {
                        throw std::runtime_error("the disk is gone");
                      });

  for (const auto& revision : kRevisions) {
    SCOPED_TRACE(revision.empty() ? "no revision declared" : revision);
    const JsonValue answer = answerTo(server, callFor("broken", revision));

    EXPECT_FALSE(answer.contains("error")) << answer.toString();
    ASSERT_TRUE(answer.contains("result")) << answer.toString();
    EXPECT_TRUE(answer["result"]["isError"].getBool());
    EXPECT_NE(answer["result"]["content"][0]["text"].getString().find(
                  "the disk is gone"),
              std::string::npos);
  }
}

// And so is one that reports its own failure.
TEST(UnknownTool, AToolReportingFailureIsStillAResult) {
  DispatchTestServer server(testConfig());
  Tool tool;
  tool.name = "refuses";
  server.registerTool(
      tool, [](const std::string&, const optional<Metadata>&, SessionContext&) {
        CallToolResult result;
        result.isError = true;
        result.content.push_back(TextContent("no, not today"));
        return result;
      });

  const JsonValue answer = answerTo(server, callFor("refuses"));

  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  EXPECT_TRUE(answer["result"]["isError"].getBool());
  EXPECT_EQ(answer["result"]["content"][0]["text"].getString(),
            "no, not today");
}

// The registry says which it was: a handler that throws is a result, a
// name nothing registered is ToolNotFound.
TEST(UnknownTool, TheRegistryTellsTheTwoApart) {
  McpServerStats stats;
  ToolRegistry registry(stats);
  SessionContext session("test-session", nullptr);

  try {
    registry.callTool("no_such_tool", nullopt, session);
    FAIL() << "an unknown tool did not throw";
  } catch (const ToolNotFound& unknown) {
    EXPECT_EQ(unknown.name(), "no_such_tool");
  }
}

// An error an application sends from its own handler goes out with its data
// exactly as given, whatever its shape.
TEST(ErrorDataOnTheWire, AHandlersDataIsSentAsGiven) {
  DispatchTestServer server(testConfig());
  const JsonValue data = JsonValue::parse(
      R"({"retryAfter":1,"limits":{"window":[60,3600]},"ids":[1,"two"]})");
  server.registerRequestHandler(
      "example/refuse",
      [data](const jsonrpc::Request& request, SessionContext&) {
        Error error(-32000, "Slow down");
        error.data = mcp::make_optional(ErrorData(data));
        return jsonrpc::Response::make_error(request.id, error);
      });

  const JsonValue answer =
      answerTo(server, requestFor("example/refuse", Metadata()));

  ASSERT_TRUE(answer.contains("error")) << answer.toString();
  EXPECT_EQ(answer["error"]["code"].getInt(), -32000);
  EXPECT_EQ(answer["error"]["message"].getString(), "Slow down");
  EXPECT_EQ(answer["error"]["data"].toString(), data.toString())
      << "the error's data was not sent as given";
}
