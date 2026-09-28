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
#include <vector>

#include <gtest/gtest.h>

#include "mcp/builders.h"
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

const char* const kModern = "2026-07-28";

/**
 * The answer to a request, as a peer would read it. With a revision, the
 * request declares it, as every request of 2026-07-28 does; without one it
 * is a request of an earlier revision.
 */
JsonValue answerTo(DispatchTestServer& server,
                   const std::string& method,
                   const std::string& tool = std::string(),
                   const std::string& revision = std::string()) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  Metadata params;
  if (!tool.empty()) {
    params["name"] = MetadataValue(tool);
  }
  if (!revision.empty()) {
    JsonValue meta = JsonValue::object();
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue(revision));
    params["_meta"] = MetadataValue(meta.toString());
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

// A JSON Schema is an object, whatever it describes. A schema for a list is
// a schema; a bare string is not.
TEST(StructuredToolOutput, AnOutputSchemaMustBeASchemaObject) {
  DispatchTestServer server(testConfig());
  auto handler = [](const std::string&, const optional<Metadata>&,
                    SessionContext&) { return CallToolResult(); };

  EXPECT_TRUE(server.registerTool(
      make<Tool>("list_tool")
          .outputSchema(JsonValue::parse(R"({"type": "array"})"))
          .build(),
      handler));
  EXPECT_FALSE(server.registerTool(
      make<Tool>("string_tool").outputSchema(JsonValue("object")).build(),
      handler));
}

/** A tool whose result is a list rather than an object. */
void addListTool(DispatchTestServer& server) {
  ASSERT_TRUE(server.registerTool(
      make<Tool>("readings")
          .outputSchema(JsonValue::parse(
              R"({"type": "array", "items": {"type": "number"}})"))
          .build(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>()
            .structuredContent(JsonValue::parse("[21, 22.5, 24]"))
            .build();
      }));
}

// 2026-07-28 allows any JSON value as a structured result, and a schema for
// any of them; its callers get both as given, with the text beside them.
TEST(StructuredToolOutput, TheNewestRevisionIsSentAnyJsonValue) {
  DispatchTestServer server(testConfig());
  addListTool(server);

  const JsonValue listed =
      answerTo(server, "tools/list", std::string(), kModern);
  EXPECT_EQ(listed["result"]["tools"][0]["outputSchema"]["type"].getString(),
            "array")
      << listed.toString();

  const JsonValue called = answerTo(server, "tools/call", "readings", kModern);
  ASSERT_TRUE(called.contains("result")) << called.toString();
  ASSERT_TRUE(called["result"]["structuredContent"].isArray());
  EXPECT_EQ(called["result"]["structuredContent"].size(), 3u);
  ASSERT_EQ(called["result"]["content"].size(), 1u);
  EXPECT_EQ(JsonValue::parse(called["result"]["content"][0]["text"].getString())
                .size(),
            3u);
}

// The earlier revisions allow only objects. Their callers still see the
// tool, and its answer as text, but neither the schema nor the value they
// have no way to read.
TEST(StructuredToolOutput, AnOlderRevisionIsSentOnlyObjects) {
  DispatchTestServer server(testConfig());
  addListTool(server);
  ASSERT_TRUE(server.registerTool(
      weatherTool(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>().structuredContent(weatherData()).build();
      }));

  const JsonValue listed = answerTo(server, "tools/list");
  ASSERT_EQ(listed["result"]["tools"].size(), 2u) << listed.toString();
  for (size_t i = 0; i < listed["result"]["tools"].size(); ++i) {
    const auto& tool = listed["result"]["tools"][i];
    if (tool["name"].getString() == "readings") {
      EXPECT_FALSE(tool.contains("outputSchema")) << listed.toString();
    } else {
      EXPECT_TRUE(tool.contains("outputSchema")) << listed.toString();
    }
  }

  const JsonValue list_call = answerTo(server, "tools/call", "readings");
  ASSERT_TRUE(list_call.contains("result")) << list_call.toString();
  EXPECT_FALSE(list_call["result"].contains("structuredContent"));
  ASSERT_EQ(list_call["result"]["content"].size(), 1u);
  EXPECT_EQ(list_call["result"]["content"][0]["text"].getString(),
            "[21,22.5,24]");

  const JsonValue object_call = answerTo(server, "tools/call", "get_weather");
  EXPECT_TRUE(object_call["result"]["structuredContent"].isObject());
}

/** Settle a revision for this server's session, as a client's handshake does.
 */
void introduceAs(DispatchTestServer& server, const std::string& revision) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(100);
  request.method = "initialize";
  Metadata params;
  params["protocolVersion"] = MetadataValue(revision);
  request.params = mcp::make_optional(params);
  CapturingContext context;
  server.onRequestWithContext(request, context);
  ASSERT_TRUE(context.captured.has_value());
  const JsonValue wire = json::to_json(context.captured.value());
  ASSERT_EQ(wire["result"]["protocolVersion"].getString(), revision)
      << wire.toString();
}

/** The text blocks of a tool result. */
std::vector<std::string> textsOf(const JsonValue& result) {
  std::vector<std::string> texts;
  for (size_t i = 0; i < result["content"].size(); ++i) {
    if (result["content"][i]["type"].getString() == "text") {
      texts.push_back(result["content"][i]["text"].getString());
    }
  }
  return texts;
}

// A revision from before structured output has neither field, so a caller
// speaking it is sent neither, and reads the data as text instead.
TEST(StructuredToolOutput, ARevisionBeforeItIsSentNeither) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      weatherTool(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>()
            .addText("22.5 and cloudy")
            .structuredContent(weatherData())
            .build();
      }));
  introduceAs(server, "2025-03-26");

  const JsonValue listed = answerTo(server, "tools/list");
  EXPECT_FALSE(listed["result"]["tools"][0].contains("outputSchema"))
      << listed.toString();

  const JsonValue called = answerTo(server, "tools/call", "get_weather");
  ASSERT_TRUE(called.contains("result")) << called.toString();
  EXPECT_FALSE(called["result"].contains("structuredContent"));
  const auto texts = textsOf(called["result"]);
  ASSERT_EQ(texts.size(), 2u) << called.toString();
  EXPECT_EQ(texts[0], "22.5 and cloudy");
  EXPECT_EQ(JsonValue::parse(texts[1]).toString(), weatherData().toString());
}

// A tool that gave a summary as well as a list: a caller that cannot be
// sent the list still gets it, as text, beside the summary.
TEST(StructuredToolOutput, DataLeftOutIsStillSentAsText) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      make<Tool>("readings").build(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return make<CallToolResult>()
            .addText("three readings")
            .structuredContent(JsonValue::parse("[21, 22.5, 24]"))
            .build();
      }));

  // An earlier revision: objects only, so the list goes out as text.
  const JsonValue older = answerTo(server, "tools/call", "readings");
  EXPECT_FALSE(older["result"].contains("structuredContent"));
  const auto texts = textsOf(older["result"]);
  ASSERT_EQ(texts.size(), 2u) << older.toString();
  EXPECT_EQ(texts[0], "three readings");
  EXPECT_EQ(JsonValue::parse(texts[1]).size(), 3u);

  // The newest: the list goes out as it is, and nothing is added.
  const JsonValue newest = answerTo(server, "tools/call", "readings", kModern);
  EXPECT_TRUE(newest["result"]["structuredContent"].isArray());
  EXPECT_EQ(textsOf(newest["result"]).size(), 1u) << newest.toString();
}

// When the tool already said exactly what the data says, it is not said
// twice.
TEST(StructuredToolOutput, DataAlreadyInTheTextIsNotRepeated) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      make<Tool>("readings").build(),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        const JsonValue data = JsonValue::parse("[21, 22.5, 24]");
        return make<CallToolResult>()
            .addText(data.toString())
            .structuredContent(data)
            .build();
      }));

  const JsonValue older = answerTo(server, "tools/call", "readings");
  EXPECT_FALSE(older["result"].contains("structuredContent"));
  EXPECT_EQ(textsOf(older["result"]).size(), 1u) << older.toString();
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

  // Any JSON value is read as given, a list and a null included; a null
  // that was sent is not the same as nothing sent.
  const auto list = json::from_json<CallToolResult>(
      JsonValue::parse(R"({"content": [], "structuredContent": [1, 2]})"));
  ASSERT_TRUE(list.structuredContent.has_value());
  EXPECT_TRUE(list.structuredContent->isArray());

  const auto null_result = json::from_json<CallToolResult>(
      JsonValue::parse(R"({"content": [], "structuredContent": null})"));
  ASSERT_TRUE(null_result.structuredContent.has_value());
  EXPECT_TRUE(null_result.structuredContent->isNull());
  EXPECT_TRUE(json::to_json(null_result).contains("structuredContent"));

  const auto none =
      json::from_json<CallToolResult>(JsonValue::parse(R"({"content": []})"));
  EXPECT_FALSE(none.structuredContent.has_value());
  EXPECT_FALSE(json::to_json(none).contains("structuredContent"));
}

}  // namespace
}  // namespace server
}  // namespace mcp
