// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Structured tool output.
 *
 *   tools/list   {"name": ..., "inputSchema": {...},
 *                 "outputSchema": {"type": "object", ...}}
 *   tools/call   {"content": [...], "structuredContent": {...}}
 *
 * The content blocks are for the model; structuredContent is the same
 * result as data, for programs. A tool that declares an outputSchema owes a
 * structured result with every success, and a client that reads only the
 * content blocks still gets the data, as text.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;

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

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "structured-output-test";
  config.server_version = "0.0.1";
  config.capabilities.tools = mcp::make_optional(true);
  return config;
}

JsonValue weatherSchema() {
  return JsonValue::parse(R"({
    "type": "object",
    "properties": {"temp": {"type": "number"},
                   "conditions": {"type": "string"},
                   "hourly": {"type": "array", "items": {"type": "number"}}},
    "required": ["temp", "conditions"]
  })");
}

JsonValue weatherData() {
  return JsonValue::parse(
      R"({"temp": 22.5, "conditions": "Cloudy", "hourly": [21, 22.5, 24]})");
}

/** The answer to a request, as a peer would read it. */
JsonValue answerTo(DispatchTestServer& server,
                   const std::string& method,
                   const std::string& tool = std::string()) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  Metadata params;
  if (!tool.empty()) {
    params["name"] = MetadataValue(tool);
  }
  request.params = mcp::make_optional(params);

  CapturingContext context;
  server.onRequestWithContext(request, context);
  if (!context.captured.has_value()) {
    ADD_FAILURE() << method << " went unanswered";
    return JsonValue::object();
  }
  return json::to_json(context.captured.value());
}

Tool weatherTool() {
  return make<Tool>("get_weather").outputSchema(weatherSchema()).build();
}

// The schema goes out on the listing exactly as it was declared.
TEST(StructuredToolOutput, TheOutputSchemaIsListed) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      weatherTool(), [](const std::string&, const optional<Metadata>&,
                        SessionContext&) { return CallToolResult(); }));

  const JsonValue answer = answerTo(server, "tools/list");
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  const auto& tool = answer["result"]["tools"][0];
  ASSERT_TRUE(tool.contains("outputSchema")) << answer.toString();
  EXPECT_EQ(tool["outputSchema"].toString(), weatherSchema().toString());
}

// The structured result goes out as it was given, nested values and all,
// with the tool's own content blocks untouched.
TEST(StructuredToolOutput, TheStructuredResultIsSentAsGiven) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      weatherTool(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>()
            .addText("22.5 and cloudy")
            .structuredContent(weatherData())
            .build();
      }));

  const JsonValue answer = answerTo(server, "tools/call", "get_weather");
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  const auto& result = answer["result"];
  EXPECT_EQ(result["structuredContent"].toString(), weatherData().toString());
  ASSERT_EQ(result["content"].size(), 1u);
  EXPECT_EQ(result["content"][0]["text"].getString(), "22.5 and cloudy");
}

// A tool that returned only the data still reaches a client that reads only
// the content blocks: the same JSON, as text.
TEST(StructuredToolOutput, DataAloneIsAlsoSentAsText) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      weatherTool(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>().structuredContent(weatherData()).build();
      }));

  const JsonValue answer = answerTo(server, "tools/call", "get_weather");
  const auto& result = answer["result"];
  ASSERT_EQ(result["content"].size(), 1u) << answer.toString();
  EXPECT_EQ(result["content"][0]["type"].getString(), "text");
  EXPECT_EQ(
      JsonValue::parse(result["content"][0]["text"].getString()).toString(),
      weatherData().toString());
}

// A tool that declared the shape of its result and then did not give one
// has not answered what it promised.
TEST(StructuredToolOutput, ADeclaredResultThatIsMissingIsAnError) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      weatherTool(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>().addText("no data today").build();
      }));

  const JsonValue answer = answerTo(server, "tools/call", "get_weather");
  EXPECT_FALSE(answer.contains("result")) << answer.toString();
  ASSERT_TRUE(answer.contains("error")) << answer.toString();
  EXPECT_NE(answer["error"]["message"].getString().find("structuredContent"),
            std::string::npos);
}

// A tool that failed owes nothing structured: the failure is the answer.
TEST(StructuredToolOutput, AFailedCallNeedsNoStructuredResult) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      weatherTool(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>()
            .addText("the station is down")
            .isError(true)
            .build();
      }));

  const JsonValue answer = answerTo(server, "tools/call", "get_weather");
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  EXPECT_TRUE(answer["result"]["isError"].getBool());
  EXPECT_FALSE(answer["result"].contains("structuredContent"));
}

// A structured result is always an object, so a schema for anything else
// could never be met, and the tool is refused.
TEST(StructuredToolOutput, AnOutputSchemaThatIsNotAnObjectIsRefused) {
  DispatchTestServer server(testConfig());
  auto handler = [](const std::string&, const optional<Metadata>&,
                    SessionContext&) { return CallToolResult(); };

  EXPECT_FALSE(server.registerTool(
      make<Tool>("list_tool")
          .outputSchema(JsonValue::parse(R"({"type": "array"})"))
          .build(),
      handler));
  EXPECT_FALSE(server.registerTool(
      make<Tool>("untyped_tool")
          .outputSchema(JsonValue::parse(R"({"properties": {}})"))
          .build(),
      handler));
  EXPECT_FALSE(server.registerTool(
      make<Tool>("string_tool").outputSchema(JsonValue("object")).build(),
      handler));
}

// Nothing changes for a tool that does not use either field.
TEST(StructuredToolOutput, AToolWithoutEitherIsUnchanged) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      make<Tool>("echo").build(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>().addText("hello").build();
      }));

  const JsonValue listed = answerTo(server, "tools/list");
  EXPECT_FALSE(listed["result"]["tools"][0].contains("outputSchema"));

  const JsonValue called = answerTo(server, "tools/call", "echo");
  EXPECT_FALSE(called["result"].contains("structuredContent"));
  ASSERT_EQ(called["result"]["content"].size(), 1u);
  EXPECT_EQ(called["result"]["content"][0]["text"].getString(), "hello");
}

// What a client reads, from this server or any other.
TEST(StructuredToolOutput, TheClientReadsBothFields) {
  const auto listed = json::from_json<ListToolsResult>(JsonValue::parse(
      R"({"tools": [{"name": "get_weather", "inputSchema": {"type": "object"},
                     "outputSchema": {"type": "object",
                                      "properties": {"temp": {"type": "number"}}}}]})"));
  ASSERT_EQ(listed.tools.size(), 1u);
  ASSERT_TRUE(listed.tools[0].outputSchema.has_value());
  EXPECT_EQ(
      (*listed.tools[0].outputSchema)["properties"]["temp"]["type"].getString(),
      "number");

  const auto called = json::from_json<CallToolResult>(JsonValue::parse(
      R"({"content": [{"type": "text", "text": "22.5"}],
          "structuredContent": {"temp": 22.5, "hourly": [21, 22.5]}})"));
  ASSERT_TRUE(called.structuredContent.has_value());
  EXPECT_EQ((*called.structuredContent)["temp"].getFloat(), 22.5);
  EXPECT_EQ((*called.structuredContent)["hourly"].size(), 2u);

  // Out again unchanged.
  EXPECT_EQ(json::to_json(called)["structuredContent"].toString(),
            called.structuredContent->toString());

  // A structured result that is not an object is refused.
  EXPECT_THROW(json::from_json<CallToolResult>(JsonValue::parse(
                   R"({"content": [], "structuredContent": [1, 2]})")),
               json::JsonException);
}

}  // namespace
}  // namespace server
}  // namespace mcp
