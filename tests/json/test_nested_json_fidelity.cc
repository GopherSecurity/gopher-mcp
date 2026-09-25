// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Nested JSON through the JSON-RPC codec, without loss.
 *
 * Params used to be decoded into nothing but a flat map, where a nested
 * object became its JSON text and was turned back into an object on the
 * way out only if it still looked like one. That lost the difference
 * between an object and a string that happens to look like one, and
 * results with no recognised shape went the same way. These pin down
 * that what arrives is what leaves.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::from_json;
using json::JsonValue;
using json::to_json;

// A tool call whose arguments nest two levels down.
const char* kNestedCall =
    R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":)"
    R"({"name":"q","arguments":{"filter":{"region":"eu","limit":5}}}})";

TEST(NestedJsonFidelity, ARequestKeepsItsParamsAsTheyCame) {
  const auto request =
      from_json<jsonrpc::Request>(JsonValue::parse(kNestedCall));

  ASSERT_TRUE(request.params_json.has_value());
  const auto& params = request.params_json.value();
  ASSERT_TRUE(params["arguments"].isObject());
  ASSERT_TRUE(params["arguments"]["filter"].isObject());
  EXPECT_EQ(params["arguments"]["filter"]["region"].getString(), "eu");
  EXPECT_EQ(params["arguments"]["filter"]["limit"].getInt(), 5);

  EXPECT_EQ(to_json(request).toString(),
            JsonValue::parse(kNestedCall).toString());
}

TEST(NestedJsonFidelity, ExistingHandlersStillGetTheFlatView) {
  const auto request =
      from_json<jsonrpc::Request>(JsonValue::parse(kNestedCall));

  ASSERT_TRUE(request.params.has_value());
  const auto& flat = request.params.value();

  auto name = flat.find("name");
  ASSERT_NE(name, flat.end());
  ASSERT_TRUE(holds_alternative<std::string>(name->second));
  EXPECT_EQ(get<std::string>(name->second), "q");

  // Nested values are still their JSON text in this view, as before.
  auto arguments = flat.find("arguments");
  ASSERT_NE(arguments, flat.end());
  ASSERT_TRUE(holds_alternative<std::string>(arguments->second));
  const auto reparsed = JsonValue::parse(get<std::string>(arguments->second));
  EXPECT_EQ(reparsed["filter"]["region"].getString(), "eu");
}

TEST(NestedJsonFidelity, ANotificationKeepsItsParamsAsTheyCame) {
  const char* body =
      R"({"jsonrpc":"2.0","method":"notifications/progress","params":)"
      R"({"progressToken":"t","progress":1,"_meta":{"trace":{"id":"a"}}}})";
  const auto notification =
      from_json<jsonrpc::Notification>(JsonValue::parse(body));

  ASSERT_TRUE(notification.params_json.has_value());
  EXPECT_TRUE(notification.params_json.value()["_meta"]["trace"].isObject());
  EXPECT_EQ(to_json(notification).toString(),
            JsonValue::parse(body).toString());
}

// The state a server hands its client is opaque bytes and has to come
// back as exactly those. One that looks like JSON is still a string.
TEST(NestedJsonFidelity, AStringThatLooksLikeJsonStaysAString) {
  const std::string state = R"({"step":2,"items":[1,2]})";

  jsonrpc::Request request;
  request.id = static_cast<int64_t>(3);
  request.method = "tools/call";
  JsonValue params = JsonValue::object();
  params.set("name", JsonValue("q"));
  params.set("requestState", JsonValue(state));
  request.params_json = mcp::make_optional(params);

  const JsonValue wire = to_json(request);
  ASSERT_TRUE(wire["params"]["requestState"].isString());
  EXPECT_EQ(wire["params"]["requestState"].getString(), state);

  // And through a full decode and encode, as a server echoing it would.
  const auto decoded =
      from_json<jsonrpc::Request>(JsonValue::parse(wire.toString()));
  const JsonValue again = to_json(decoded);
  ASSERT_TRUE(again["params"]["requestState"].isString());
  EXPECT_EQ(again["params"]["requestState"].getString(), state);
}

// A result of a shape the decoder does not recognise, such as the answer
// to server/discover, is kept whole rather than flattened.
TEST(NestedJsonFidelity, AnUnrecognisedResultIsKeptWhole) {
  const char* body =
      R"({"jsonrpc":"2.0","id":1,"result":{)"
      R"("supportedVersions":["2026-07-28","2025-11-25"],)"
      R"("capabilities":{"tools":{"listChanged":true},"logging":{}},)"
      R"("_meta":{"io.modelcontextprotocol/serverInfo":)"
      R"({"name":"s","version":"1"}}}})";
  const auto response = from_json<jsonrpc::Response>(JsonValue::parse(body));

  ASSERT_TRUE(response.result.has_value());
  ASSERT_TRUE(holds_alternative<JsonValue>(response.result.value()));
  const auto& result = get<JsonValue>(response.result.value());
  EXPECT_TRUE(result["supportedVersions"].isArray());
  EXPECT_TRUE(result["capabilities"]["tools"]["listChanged"].getBool());
  EXPECT_EQ(
      result["_meta"]["io.modelcontextprotocol/serverInfo"]["name"].getString(),
      "s");

  EXPECT_EQ(to_json(response).toString(), JsonValue::parse(body).toString());
}

TEST(NestedJsonFidelity, AnEmptyArrayResultIsStillAnArray) {
  const auto response = from_json<jsonrpc::Response>(
      JsonValue::parse(R"({"jsonrpc":"2.0","id":1,"result":[]})"));

  ASSERT_TRUE(response.result.has_value());
  ASSERT_TRUE(holds_alternative<JsonValue>(response.result.value()));
  EXPECT_TRUE(get<JsonValue>(response.result.value()).isArray());
}

// Capabilities are declared by objects, with bool flags inside them.
TEST(NestedJsonFidelity, CapabilitiesDeclaredAsObjectsAreRead) {
  const auto caps = from_json<ServerCapabilities>(JsonValue::parse(
      R"({"tools":{"listChanged":true},"prompts":{},"logging":{},)"
      R"("resources":{"subscribe":false,"listChanged":true}})"));

  ASSERT_TRUE(caps.tools.has_value());
  EXPECT_TRUE(caps.tools.value());
  ASSERT_TRUE(caps.tools->listChanged.has_value());
  EXPECT_TRUE(caps.tools->listChanged.value());
  ASSERT_TRUE(caps.prompts.has_value());
  EXPECT_TRUE(caps.prompts.value());
  ASSERT_TRUE(caps.logging.has_value());
  EXPECT_TRUE(caps.logging.value());

  ASSERT_TRUE(caps.resources.has_value());
  ASSERT_TRUE(holds_alternative<ResourcesCapability>(caps.resources.value()));
  const auto& resources = get<ResourcesCapability>(caps.resources.value());
  // The flags are booleans, read as the server sent them.
  ASSERT_TRUE(resources.subscribe.has_value());
  EXPECT_FALSE(resources.subscribe.value())
      << "a flag declared false was read as declared";
  ASSERT_TRUE(resources.listChanged.has_value());
  EXPECT_TRUE(resources.listChanged.value());
}

TEST(NestedJsonFidelity, ThePromptResultIsReadAsTheSpecShapesIt) {
  const auto result = from_json<GetPromptResult>(JsonValue::parse(
      R"({"description":"A greeting","messages":[)"
      R"({"role":"user","content":{"type":"text","text":"hello"}}]})"));

  ASSERT_TRUE(result.description.has_value());
  EXPECT_EQ(result.description.value(), "A greeting");
  ASSERT_EQ(result.messages.size(), 1u);
  EXPECT_EQ(result.messages[0].role, enums::Role::USER);
  ASSERT_TRUE(holds_alternative<TextContent>(result.messages[0].content));
  EXPECT_EQ(get<TextContent>(result.messages[0].content).text, "hello");
}

// A prompts/list answer decodes into its own type, the same as a tools or
// resources listing does, so a caller of sendRequest gets it typed.
TEST(NestedJsonFidelity, APromptListingDecodesAsAListPromptsResult) {
  const char* body = R"({"jsonrpc":"2.0","id":1,"result":{"prompts":[)"
                     R"({"name":"greet","description":"Say hello",)"
                     R"("arguments":[{"name":"who","required":true}]}],)"
                     R"("nextCursor":"page-2"}})";
  const auto response = from_json<jsonrpc::Response>(JsonValue::parse(body));

  ASSERT_TRUE(response.result.has_value());
  ASSERT_TRUE(holds_alternative<ListPromptsResult>(response.result.value()));
  const auto& listed = get<ListPromptsResult>(response.result.value());
  ASSERT_EQ(listed.prompts.size(), 1u);
  EXPECT_EQ(listed.prompts[0].name, "greet");
  ASSERT_TRUE(listed.prompts[0].arguments.has_value());
  EXPECT_EQ(listed.prompts[0].arguments->at(0).name, "who");
  EXPECT_TRUE(listed.prompts[0].arguments->at(0).required);
  ASSERT_TRUE(listed.nextCursor.has_value());
  EXPECT_EQ(listed.nextCursor.value(), "page-2");

  // And back out in the same shape it came in.
  const JsonValue again = to_json(response);
  EXPECT_TRUE(again["result"]["prompts"].isArray());
  EXPECT_EQ(again["result"]["prompts"][0]["name"].getString(), "greet");
  EXPECT_EQ(again["result"]["nextCursor"].getString(), "page-2");
}

}  // namespace
}  // namespace mcp
