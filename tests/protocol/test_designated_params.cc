/**
 * Which tool arguments may be carried in a header, and which tools are
 * refused for asking.
 *
 * A designation that both ends cannot resolve to the same value is worse
 * than none: the client puts one thing in a header, the server reads
 * another out of the body, and the request is refused for a mismatch
 * neither end introduced. So the constraints are strict, and a violation
 * invalidates the whole tool rather than just the annotation — a tool
 * nobody can call correctly is not a tool.
 *
 * All of it is a pure function of a schema, so none of it needs a server.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/designated_params.h"

namespace mcp {
namespace protocol {
namespace modern {
namespace {

Tool toolWithSchema(const std::string& schema_json) {
  Tool tool("execute_sql");
  tool.inputSchema = mcp::make_optional(json::JsonValue::parse(schema_json));
  return tool;
}

/** The parameters a schema designates, or the failure it earned. */
std::vector<DesignatedParam> designated(const std::string& schema_json) {
  std::vector<DesignatedParam> found;
  auto result = designatedParams(toolWithSchema(schema_json), &found);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(result))
      << (holds_alternative<Error>(result) ? get<Error>(result).message : "");
  return found;
}

std::string refusalFor(const std::string& schema_json) {
  std::vector<DesignatedParam> found;
  auto result = designatedParams(toolWithSchema(schema_json), &found);
  if (holds_alternative<std::nullptr_t>(result)) {
    return std::string();
  }
  EXPECT_TRUE(found.empty())
      << "a refused tool still handed back parameters to mirror";
  return get<Error>(result).message;
}

TEST(DesignatedParams, TheOrdinaryCaseIsNoneAtAll) {
  EXPECT_TRUE(designated(R"({"type":"object","properties":{
      "query":{"type":"string"}}})")
                  .empty());

  // And a tool with no schema at all designates nothing rather than
  // failing: most tools say nothing about headers.
  std::vector<DesignatedParam> found;
  Tool bare("bare");
  EXPECT_TRUE(
      holds_alternative<std::nullptr_t>(designatedParams(bare, &found)));
  EXPECT_TRUE(found.empty());
}

TEST(DesignatedParams, ADesignatedParameterIsFoundWithItsPath) {
  const auto found = designated(R"({"type":"object","properties":{
      "region":{"type":"string","x-mcp-header":"Region"},
      "query":{"type":"string"}}})");

  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].header_name, "Region");
  EXPECT_EQ(found[0].headerName(), "Mcp-Param-Region");
  ASSERT_EQ(found[0].path.size(), 1u);
  EXPECT_EQ(found[0].path[0], "region");
}

// Nested objects are fine as long as every step of the way is a property
// name, because that is a path both ends can walk without evaluating the
// schema.
TEST(DesignatedParams, ANestedParameterIsReachedByItsChain) {
  const auto found = designated(R"({"type":"object","properties":{
      "target":{"type":"object","properties":{
        "region":{"type":"string","x-mcp-header":"Region"}}}}})");

  ASSERT_EQ(found.size(), 1u);
  ASSERT_EQ(found[0].path.size(), 2u);
  EXPECT_EQ(found[0].path[0], "target");
  EXPECT_EQ(found[0].path[1], "region");
}

// The three types with one unambiguous header form. A number is excluded
// on purpose: 42 and 42.0 are one number and two strings, and the two
// ends would disagree about which they were carrying.
TEST(DesignatedParams, OnlyAValueWithOneHeaderFormMayBeDesignated) {
  EXPECT_TRUE(refusalFor(R"({"type":"object","properties":{
      "n":{"type":"integer","x-mcp-header":"N"}}})")
                  .empty());
  EXPECT_TRUE(refusalFor(R"({"type":"object","properties":{
      "b":{"type":"boolean","x-mcp-header":"B"}}})")
                  .empty());

  EXPECT_NE(refusalFor(R"({"type":"object","properties":{
      "n":{"type":"number","x-mcp-header":"N"}}})")
                .find("header can carry"),
            std::string::npos)
      << "a number was allowed to be mirrored";
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "o":{"type":"object","x-mcp-header":"O"}}})")
                   .empty());
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "a":{"type":"array","x-mcp-header":"A"}}})")
                   .empty());
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "u":{"type":["string","null"],"x-mcp-header":"U"}}})")
                   .empty())
      << "a union of types has no single header form either";
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "t":{"x-mcp-header":"T"}}})")
                   .empty())
      << "a parameter with no stated type was allowed to be mirrored";
}

TEST(DesignatedParams, AHeaderNameHasToBeOne) {
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "r":{"type":"string","x-mcp-header":""}}})")
                   .empty())
      << "an empty designation was accepted";
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "r":{"type":"string","x-mcp-header":"Bad Name"}}})")
                   .empty())
      << "a space is not allowed in a header name";
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "r":{"type":"string","x-mcp-header":"Bad\nName"}}})")
                   .empty())
      << "a newline in a header name is how a header is forged";
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "r":{"type":"string","x-mcp-header":"Bad:Name"}}})")
                   .empty());
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "r":{"type":"string","x-mcp-header":42}}})")
                   .empty())
      << "a designation that is not a name was accepted";

  // The punctuation a token may hold is allowed, because it is.
  EXPECT_TRUE(refusalFor(R"({"type":"object","properties":{
      "r":{"type":"string","x-mcp-header":"X-Tenant_Id.v2"}}})")
                  .empty());
}

// Header names do not distinguish case, so two designations that differ
// only in case are one header carrying two values.
TEST(DesignatedParams, TwoNamesThatAreOneHeaderAreRefused) {
  const std::string why = refusalFor(R"({"type":"object","properties":{
      "a":{"type":"string","x-mcp-header":"Region"},
      "b":{"type":"string","x-mcp-header":"region"}}})");
  EXPECT_NE(why.find("case"), std::string::npos) << why;
}

// An annotation somewhere no fixed path can reach could not be resolved
// at call time, so its presence invalidates the definition rather than
// being quietly ignored — ignoring it would leave the client sending a
// header the server never expects.
TEST(DesignatedParams, ADesignationOffTheReachablePathIsRefused) {
  const char* const unreachable[] = {
      R"({"type":"object","properties":{"xs":{"type":"array","items":{
          "type":"object","properties":{
            "r":{"type":"string","x-mcp-header":"R"}}}}}})",
      R"({"type":"object","properties":{"x":{"oneOf":[
          {"type":"string","x-mcp-header":"R"}]}}})",
      R"({"type":"object","properties":{"x":{"anyOf":[
          {"type":"string","x-mcp-header":"R"}]}}})",
      R"({"type":"object","properties":{"x":{"allOf":[
          {"type":"string","x-mcp-header":"R"}]}}})",
      R"({"type":"object","properties":{"x":{"not":
          {"type":"string","x-mcp-header":"R"}}}})",
      R"({"type":"object","properties":{"x":{"if":{"type":"string"},
          "then":{"type":"string","x-mcp-header":"R"}}}})",
      R"({"type":"object","properties":{"x":{"$ref":"#/$defs/thing",
          "x-mcp-header":"R"}}})",
  };

  for (const char* schema : unreachable) {
    EXPECT_FALSE(refusalFor(schema).empty())
        << "a designation no fixed path can reach was accepted: " << schema;
  }
}

// Finding the value again at call time, which is the whole point of the
// path.
TEST(DesignatedParams, TheValueIsFoundWhereTheSchemaSaidItWouldBe) {
  const auto found = designated(R"({"type":"object","properties":{
      "target":{"type":"object","properties":{
        "region":{"type":"string","x-mcp-header":"Region"}}}}})");
  ASSERT_EQ(found.size(), 1u);

  json::JsonValue value;
  const auto arguments = json::JsonValue::parse(
      R"({"target":{"region":"us-west1"},"query":"SELECT 1"})");
  ASSERT_TRUE(valueAtPath(arguments, found[0].path, &value));
  EXPECT_EQ(value.getString(), "us-west1");

  // An argument that was not given is not a fault: the client omits the
  // header, and the server must not expect one.
  EXPECT_FALSE(valueAtPath(json::JsonValue::parse(R"({"query":"SELECT 1"})"),
                           found[0].path, &value));
  EXPECT_FALSE(valueAtPath(json::JsonValue::parse(R"({"target":{}})"),
                           found[0].path, &value));
  EXPECT_FALSE(
      valueAtPath(json::JsonValue::parse(R"({"target":{"region":null}})"),
                  found[0].path, &value))
      << "a null argument is one that was not given";
}

// A schema is free to use references. Only a designation *on* one is a
// problem, because where its value lives would depend on resolving the
// reference — and refusing every schema that merely contains a $ref
// refuses tools that have nothing to do with headers at all.
TEST(DesignatedParams, AnOrdinaryReferenceIsNotAViolation) {
  EXPECT_TRUE(refusalFor(R"({"type":"object","$defs":{
      "region":{"type":"string"}},
      "properties":{"region":{"$ref":"#/$defs/region"},
                    "query":{"type":"string"}}})")
                  .empty())
      << "a tool using an ordinary schema reference was refused";

  EXPECT_TRUE(designated(R"({"type":"object","$defs":{
      "region":{"type":"string"}},
      "properties":{"region":{"$ref":"#/$defs/region"},
                    "zone":{"type":"string","x-mcp-header":"Zone"}}})")
                  .size() == 1u)
      << "a reference beside a designation cost the designation";

  // And a designation on the reference itself is still refused.
  EXPECT_FALSE(refusalFor(R"({"type":"object","properties":{
      "region":{"$ref":"#/$defs/region","x-mcp-header":"Region"}}})")
                   .empty());
}

// A number too large to be held exactly would be rounded, and two ends
// that round differently disagree about a value neither of them changed.
TEST(DesignatedParams, AnIntegerTooLargeToCarryExactlyIsRefused) {
  EXPECT_TRUE(isExactlyCarryableInteger(42));
  EXPECT_TRUE(isExactlyCarryableInteger(-42));
  EXPECT_TRUE(isExactlyCarryableInteger(9007199254740991LL));
  EXPECT_TRUE(isExactlyCarryableInteger(-9007199254740991LL));

  EXPECT_FALSE(isExactlyCarryableInteger(9007199254740992LL));
  EXPECT_FALSE(isExactlyCarryableInteger(-9007199254740992LL));
}

}  // namespace
}  // namespace modern
}  // namespace protocol
}  // namespace mcp
