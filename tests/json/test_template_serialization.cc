#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;

// ============= Template Serialization Tests =============

TEST(TemplateSerializationTest, SerializeTextContent) {
  TextContent content;
  content.type = "text";
  content.text = "Hello World";

  // Old way - specific function name
  JsonValue json1 = JsonSerializer::serialize(content);

  // New way - template-based
  JsonValue json2 = JsonSerializer::serialize<TextContent>(content);

  EXPECT_EQ(json1.toString(), json2.toString());
}

TEST(TemplateSerializationTest, SerializeVector) {
  std::vector<TextContent> contents;
  TextContent c1, c2;
  c1.type = "text";
  c1.text = "Item 1";
  c2.type = "text";
  c2.text = "Item 2";
  contents.push_back(c1);
  contents.push_back(c2);

  // Direct serialization using unified template approach
  JsonValue jsonArray = JsonSerializer::serialize(contents);

  ASSERT_TRUE(jsonArray.isArray());
  EXPECT_EQ(jsonArray.size(), 2u);
  EXPECT_EQ(jsonArray[0]["text"].getString(), "Item 1");
  EXPECT_EQ(jsonArray[1]["text"].getString(), "Item 2");
}

TEST(TemplateSerializationTest, SerializeOptional) {
  Tool tool;
  tool.name = "test-tool";
  tool.description = mcp::make_optional(std::string("A test tool"));

  // Test the new unified approach for optionals
  JsonValue optJson = JsonSerializer::serialize(tool.description);
  ASSERT_FALSE(optJson.isNull());
  EXPECT_EQ(optJson.getString(), "A test tool");

  // Also test empty optional
  optional<std::string> empty;
  JsonValue emptyJson = JsonSerializer::serialize(empty);
  EXPECT_TRUE(emptyJson.isNull());
}

TEST(TemplateSerializationTest, RoundTripSerialization) {
  Error error;
  error.code = -32601;
  error.message = "Method not found";

  // Serialize using template
  JsonValue json = JsonSerializer::serialize<Error>(error);

  // Deserialize using template
  Error error2 = JsonDeserializer::deserialize<Error>(json);

  EXPECT_EQ(error.code, error2.code);
  EXPECT_EQ(error.message, error2.message);
}

// ============= Template Deserialization Tests =============

TEST(TemplateDeserializationTest, DeserializeTextContent) {
  JsonValue json = JsonObjectBuilder()
                       .add("type", "text")
                       .add("text", "Hello World")
                       .build();

  // Template-based deserialization
  TextContent content = JsonDeserializer::deserialize<TextContent>(json);

  EXPECT_EQ(content.text, "Hello World");
}

TEST(TemplateDeserializationTest, DeserializeVector) {
  JsonArrayBuilder builder;
  builder.add(
      JsonObjectBuilder().add("type", "text").add("text", "Item 1").build());
  builder.add(
      JsonObjectBuilder().add("type", "text").add("text", "Item 2").build());
  JsonValue jsonArray = builder.build();

  // Direct deserialization using unified template approach
  std::vector<TextContent> contents =
      JsonDeserializer::deserialize<std::vector<TextContent>>(jsonArray);

  ASSERT_EQ(contents.size(), 2u);
  EXPECT_EQ(contents[0].text, "Item 1");
  EXPECT_EQ(contents[1].text, "Item 2");
}

TEST(TemplateDeserializationTest, DeserializeOptional) {
  // Test with value
  JsonValue json = JsonValue("test value");
  optional<std::string> result =
      JsonDeserializer::deserialize<optional<std::string>>(json);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), "test value");

  // Test with null
  json = JsonValue::null();
  result = JsonDeserializer::deserialize<optional<std::string>>(json);
  EXPECT_FALSE(result.has_value());
}

TEST(TemplateDeserializationTest, DeserializeComplexType) {
  JsonValue json =
      JsonObjectBuilder()
          .add("name", "test-prompt")
          .add("description", "Test prompt")
          .add("arguments", JsonArrayBuilder()
                                .add(JsonObjectBuilder()
                                         .add("name", "arg1")
                                         .add("description", "First argument")
                                         .add("required", true)
                                         .build())
                                .build())
          .build();

  // Both ways work, but template is more consistent
  Prompt prompt1 = JsonDeserializer::deserialize<Prompt>(json);
  Prompt prompt2 = JsonDeserializer::deserialize<Prompt>(json);

  EXPECT_EQ(prompt1.name, prompt2.name);
}

TEST(TemplateDeserializationTest, DeserializeFromString) {
  std::string jsonStr = R"({
        "code": -32601,
        "message": "Method not found"
    })";

  JsonValue json = JsonValue::parse(jsonStr);
  Error error = JsonDeserializer::deserialize<Error>(json);

  EXPECT_EQ(error.code, -32601);
  EXPECT_EQ(error.message, "Method not found");
}

// ============= Unified Container Serialization Tests =============

TEST(UnifiedContainerTest, SerializeDeserializeVector) {
  std::vector<std::string> original = {"hello", "world", "test"};

  // Direct serialization of vector
  JsonValue json = JsonSerializer::serialize(original);
  ASSERT_TRUE(json.isArray());
  EXPECT_EQ(json.size(), 3u);

  // Direct deserialization of vector
  std::vector<std::string> result =
      JsonDeserializer::deserialize<std::vector<std::string>>(json);
  EXPECT_EQ(result, original);
}

TEST(UnifiedContainerTest, SerializeDeserializeMap) {
  std::map<std::string, int> original = {{"one", 1}, {"two", 2}, {"three", 3}};

  JsonValue json = JsonSerializer::serialize(original);
  ASSERT_TRUE(json.isObject());
  EXPECT_EQ(json["one"].getInt(), 1);
  EXPECT_EQ(json["two"].getInt(), 2);
  EXPECT_EQ(json["three"].getInt(), 3);

  std::map<std::string, int> result =
      JsonDeserializer::deserialize<std::map<std::string, int>>(json);
  EXPECT_EQ(result, original);
}

TEST(UnifiedContainerTest, NestedContainers) {
  std::vector<optional<std::string>> original = {"first", nullopt, "third"};

  JsonValue json = JsonSerializer::serialize(original);
  ASSERT_TRUE(json.isArray());
  ASSERT_EQ(json.size(), 3u);
  EXPECT_FALSE(json[0].isNull());
  EXPECT_TRUE(json[1].isNull());
  EXPECT_FALSE(json[2].isNull());

  auto result =
      JsonDeserializer::deserialize<std::vector<optional<std::string>>>(json);
  ASSERT_EQ(result.size(), 3u);
  ASSERT_TRUE(result[0].has_value());
  EXPECT_EQ(result[0].value(), "first");
  EXPECT_FALSE(result[1].has_value());
  ASSERT_TRUE(result[2].has_value());
  EXPECT_EQ(result[2].value(), "third");
}

TEST(UnifiedContainerTest, VectorOfMcpTypes) {
  std::vector<Error> errors = {{-32601, "Method not found"},
                               {-32700, "Parse error"}};

  JsonValue json = JsonSerializer::serialize(errors);
  ASSERT_TRUE(json.isArray());
  EXPECT_EQ(json.size(), 2u);

  auto result = JsonDeserializer::deserialize<std::vector<Error>>(json);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].code, -32601);
  EXPECT_EQ(result[0].message, "Method not found");
  EXPECT_EQ(result[1].code, -32700);
  EXPECT_EQ(result[1].message, "Parse error");
}

TEST(UnifiedContainerTest, ComplexNestedStructure) {
  std::map<std::string, std::vector<optional<int>>> complex = {
      {"numbers", {1, nullopt, 3}}, {"empty", {}}, {"more", {42, 100}}};

  JsonValue json = JsonSerializer::serialize(complex);
  ASSERT_TRUE(json.isObject());

  // Check structure
  ASSERT_TRUE(json.contains("numbers"));
  ASSERT_TRUE(json["numbers"].isArray());
  EXPECT_EQ(json["numbers"].size(), 3u);
  EXPECT_FALSE(json["numbers"][0].isNull());
  EXPECT_TRUE(json["numbers"][1].isNull());

  ASSERT_TRUE(json.contains("empty"));
  ASSERT_TRUE(json["empty"].isArray());
  EXPECT_EQ(json["empty"].size(), 0u);

  auto result = JsonDeserializer::deserialize<
      std::map<std::string, std::vector<optional<int>>>>(json);
  EXPECT_EQ(result, complex);
}

TEST(UnifiedContainerTest, OptionalMcpTypes) {
  // Test with value
  optional<Error> error = Error{-32601, "Method not found"};

  JsonValue json = JsonSerializer::serialize(error);
  ASSERT_FALSE(json.isNull());
  EXPECT_EQ(json["code"].getInt(), -32601);
  EXPECT_EQ(json["message"].getString(), "Method not found");

  optional<Error> result = JsonDeserializer::deserialize<optional<Error>>(json);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().code, -32601);
  EXPECT_EQ(result.value().message, "Method not found");

  // Test empty optional
  optional<Error> empty;
  json = JsonSerializer::serialize(empty);
  EXPECT_TRUE(json.isNull());

  result = JsonDeserializer::deserialize<optional<Error>>(json);
  EXPECT_FALSE(result.has_value());
}

// ============= Edge Cases Tests =============

TEST(EdgeCasesTest, EmptyContainers) {
  // Empty vector
  std::vector<int> emptyVec;
  JsonValue json = JsonSerializer::serialize(emptyVec);
  ASSERT_TRUE(json.isArray());
  EXPECT_EQ(json.size(), 0u);

  auto resultVec = JsonDeserializer::deserialize<std::vector<int>>(json);
  EXPECT_EQ(resultVec.size(), 0u);

  // Empty map
  std::map<std::string, std::string> emptyMap;
  json = JsonSerializer::serialize(emptyMap);
  ASSERT_TRUE(json.isObject());
  EXPECT_EQ(json.keys().size(), 0u);

  auto resultMap =
      JsonDeserializer::deserialize<std::map<std::string, std::string>>(json);
  EXPECT_EQ(resultMap.size(), 0u);
}

TEST(EdgeCasesTest, VectorOfEmptyOptionals) {
  std::vector<optional<int>> vec = {nullopt, nullopt, nullopt};

  JsonValue json = JsonSerializer::serialize(vec);
  ASSERT_TRUE(json.isArray());
  EXPECT_EQ(json.size(), 3u);
  EXPECT_TRUE(json[0].isNull());
  EXPECT_TRUE(json[1].isNull());
  EXPECT_TRUE(json[2].isNull());

  auto result = JsonDeserializer::deserialize<std::vector<optional<int>>>(json);
  ASSERT_EQ(result.size(), 3u);
  EXPECT_FALSE(result[0].has_value());
  EXPECT_FALSE(result[1].has_value());
  EXPECT_FALSE(result[2].has_value());
}

TEST(EdgeCasesTest, MapWithMixedTypes) {
  // Map with vector values
  std::map<std::string, std::vector<int>> mapOfVectors = {
      {"a", {1, 2, 3}}, {"b", {4, 5}}, {"c", {}}};

  JsonValue json = JsonSerializer::serialize(mapOfVectors);
  ASSERT_TRUE(json.isObject());
  EXPECT_EQ(json["a"].size(), 3u);
  EXPECT_EQ(json["b"].size(), 2u);
  EXPECT_EQ(json["c"].size(), 0u);

  auto result =
      JsonDeserializer::deserialize<std::map<std::string, std::vector<int>>>(
          json);
  EXPECT_EQ(result, mapOfVectors);
}