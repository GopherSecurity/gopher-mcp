// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Elicitation, in the shapes the spec gives it.
 *
 *   request  elicitation/create
 *            {"mode"?: "form", "message": "...",
 *             "requestedSchema": {"type": "object", "properties": {...},
 *                                 "required"?: [...]}}
 *   result   {"action": "accept" | "decline" | "cancel", "content"?: {...}}
 *
 * The same in both revisions: a request of its own in the older ones, an
 * entry in inputRequests and inputResponses in 2026-07-28. These compare
 * against the JSON another implementation sends and expects, since a round
 * trip through this SDK alone would pass with any shape at all.
 */

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/protocol/elicitation.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::JsonValue;
namespace elicitation = protocol::elicitation;

ElicitRequest deploymentQuestion() {
  EnumSchema env;
  env.values = {"staging", "production"};
  NumberSchema replicas;
  replicas.type = "integer";
  return make<ElicitRequest>("Which environment?")
      .formMode()
      .field("env", env)
      .field("replicas", replicas)
      .field("notify", BooleanSchema())
      .required("env")
      .build();
}

TEST(Elicitation, ARequestGoesOutAsElicitationCreate) {
  const jsonrpc::Request request =
      elicitation::toRequest(deploymentQuestion(), make_request_id(7));
  const JsonValue wire = json::to_json(request);

  EXPECT_EQ(wire["method"].getString(), "elicitation/create");
  const auto& params = wire["params"];
  EXPECT_EQ(params["mode"].getString(), "form");
  EXPECT_EQ(params["message"].getString(), "Which environment?");

  const auto& schema = params["requestedSchema"];
  EXPECT_EQ(schema["type"].getString(), "object");
  EXPECT_EQ(schema["properties"]["env"]["type"].getString(), "string");
  EXPECT_EQ(schema["properties"]["env"]["enum"][1].getString(), "production");
  EXPECT_EQ(schema["properties"]["replicas"]["type"].getString(), "integer");
  EXPECT_EQ(schema["properties"]["notify"]["type"].getString(), "boolean");
  ASSERT_TRUE(schema["required"].isArray());
  EXPECT_EQ(schema["required"][0].getString(), "env");

  // None of the old shape.
  EXPECT_FALSE(params.contains("name"));
  EXPECT_FALSE(params.contains("schema"));
  EXPECT_FALSE(params.contains("prompt"));
}

TEST(Elicitation, ItIsAnInputRequestInTheNewestRevision) {
  const auto input = elicitation::toInputRequest(deploymentQuestion());

  EXPECT_EQ(input.method, "elicitation/create");
  EXPECT_EQ(input.params["message"].getString(), "Which environment?");
  EXPECT_TRUE(input.params["requestedSchema"]["properties"].contains("env"));
}

// What another implementation sends, read on the client whichever way it
// arrived: with its params as JSON, or through the flat params map.
TEST(Elicitation, TheClientReadsARequestFromAnotherSdk) {
  const JsonValue params = JsonValue::parse(R"({
    "message": "Your name?",
    "requestedSchema": {
      "type": "object",
      "properties": {"name": {"type": "string", "minLength": 1}},
      "required": ["name"]
    }
  })");

  jsonrpc::Request as_json;
  as_json.method = "elicitation/create";
  as_json.id = make_request_id(3);
  as_json.params_json = mcp::make_optional(params);

  jsonrpc::Request as_map = as_json;
  as_map.params_json = nullopt;
  as_map.params = mcp::make_optional(json::jsonToMetadata(params));

  for (const auto& request : {as_json, as_map}) {
    const ElicitRequest elicit = elicitation::fromRequest(request);
    EXPECT_EQ(elicit.message, "Your name?");
    EXPECT_FALSE(elicit.mode.has_value());
    ASSERT_EQ(elicit.requestedSchema.properties.count("name"), 1u);
    const auto* name =
        get_if<StringSchema>(&elicit.requestedSchema.properties.at("name"));
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->minLength.value(), 1);
    EXPECT_EQ(elicit.requestedSchema.required->at(0), "name");
  }
}

TEST(Elicitation, ARequestItCannotReadIsRefused) {
  // No message
  EXPECT_THROW(json::from_json<ElicitRequest>(JsonValue::parse(
                   R"({"requestedSchema": {"type": "object",
                                           "properties": {}}})")),
               json::JsonException);
  // No form
  EXPECT_THROW(
      json::from_json<ElicitRequest>(JsonValue::parse(R"({"message": "m"})")),
      json::JsonException);
  // URL mode, which carries a url instead of a form
  EXPECT_THROW(json::from_json<ElicitRequest>(JsonValue::parse(
                   R"({"mode": "url", "message": "m",
                       "url": "https://example.com"})")),
               json::JsonException);
  // A form that does not say it is an object
  EXPECT_THROW(json::from_json<ElicitRequest>(JsonValue::parse(
                   R"({"message": "m",
                       "requestedSchema": {"properties": {}}})")),
               json::JsonException);
  // A field of a type a form cannot hold, which must not become a string
  EXPECT_THROW(json::from_json<ElicitRequest>(JsonValue::parse(
                   R"({"message": "m",
                       "requestedSchema": {"type": "object", "properties":
                         {"address": {"type": "object"}}}})")),
               json::JsonException);
  // A field with no type at all
  EXPECT_THROW(json::from_json<ElicitRequest>(JsonValue::parse(
                   R"({"message": "m",
                       "requestedSchema": {"type": "object", "properties":
                         {"x": {"description": "untyped"}}}})")),
               json::JsonException);
}

// Only form mode is written; a request naming another mode is refused on the
// way out rather than sent with a form a client would not expect.
TEST(Elicitation, AnotherModeIsNotWritten) {
  ElicitRequest request = deploymentQuestion();
  request.mode = mcp::make_optional(std::string("url"));
  EXPECT_THROW(json::to_json(request), json::JsonException);

  request.mode = nullopt;
  EXPECT_FALSE(json::to_json(request).contains("mode"));
}

TEST(Elicitation, AnAnswerGoesBackAsActionAndContent) {
  std::vector<std::string> tags = {"a", "b"};
  const ElicitResult accepted = make<ElicitResult>(ElicitAction::Accept)
                                    .field("env", "staging")
                                    .field("replicas", 3)
                                    .field("ratio", ElicitContentValue(0.5))
                                    .field("notify", ElicitContentValue(true))
                                    .field("tags", ElicitContentValue(tags))
                                    .build();
  const JsonValue wire = json::to_json(elicitation::toResult(accepted));

  EXPECT_EQ(wire["action"].getString(), "accept");
  EXPECT_EQ(wire["content"]["env"].getString(), "staging");
  EXPECT_TRUE(wire["content"]["replicas"].isInteger());
  EXPECT_EQ(wire["content"]["replicas"].getInt64(), 3);
  EXPECT_EQ(wire["content"]["ratio"].getFloat(), 0.5);
  EXPECT_TRUE(wire["content"]["notify"].getBool());
  EXPECT_EQ(wire["content"]["tags"][1].getString(), "b");

  // Anything but an accept carries nothing the user entered, even when
  // the result was given some.
  ElicitResult declined = accepted;
  declined.action = ElicitAction::Decline;
  const JsonValue declined_wire = json::to_json(declined);
  EXPECT_EQ(declined_wire["action"].getString(), "decline");
  EXPECT_FALSE(declined_wire.contains("content"));
  EXPECT_EQ(
      json::to_json(ElicitResult(ElicitAction::Cancel))["action"].getString(),
      "cancel");
}

TEST(Elicitation, TheServerReadsAnAnswerFromAnotherSdk) {
  const ElicitResult result = elicitation::resultFrom(JsonValue::parse(R"({
    "action": "accept",
    "content": {"env": "staging", "replicas": 3, "notify": false,
                "tags": ["x"]}
  })"));
  EXPECT_EQ(result.action, ElicitAction::Accept);
  ASSERT_TRUE(result.content.has_value());
  EXPECT_EQ(get<std::string>(result.content->at("env")), "staging");
  EXPECT_EQ(get<int64_t>(result.content->at("replicas")), 3);
  EXPECT_FALSE(get<bool>(result.content->at("notify")));
  EXPECT_EQ(get<std::vector<std::string>>(result.content->at("tags"))[0], "x");

  // Content that came with a decline is not something the user entered.
  const ElicitResult declined = elicitation::resultFrom(JsonValue::parse(
      R"({"action": "decline", "content": {"env": "staging"}})"));
  EXPECT_EQ(declined.action, ElicitAction::Decline);
  EXPECT_FALSE(declined.content.has_value());
}

TEST(Elicitation, AnAnswerItCannotReadIsRefused) {
  EXPECT_THROW(elicitation::resultFrom(JsonValue::parse(R"({})")),
               json::JsonException);
  EXPECT_THROW(
      elicitation::resultFrom(JsonValue::parse(R"({"action": "maybe"})")),
      json::JsonException);
  EXPECT_THROW(elicitation::resultFrom(JsonValue::parse(
                   R"({"action": "accept", "content": {"tags": [1, 2]}})")),
               json::JsonException);
  EXPECT_THROW(elicitation::resultFrom(JsonValue::parse(
                   R"({"action": "accept", "content": {"x": {"y": 1}}})")),
               json::JsonException);

  // And a response that is an error says so.
  EXPECT_THROW(elicitation::resultFrom(jsonrpc::Response::make_error(
                   make_request_id(1), Error(-32603, "the user closed it"))),
               std::runtime_error);
}

}  // namespace
}  // namespace mcp
