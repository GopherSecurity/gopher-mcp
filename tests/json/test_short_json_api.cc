#include <gtest/gtest.h>
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;

// Demonstrate the clean, short API
TEST(ShortJsonApi, BasicUsage) {
    // Serialization - super clean!
    Tool tool;
    tool.name = "calculator";
    tool.description = "A simple calculator";
    
    JsonValue json = to_json(tool);
    
    // Deserialization - equally clean!
    Tool tool2 = from_json<Tool>(json);
    
    EXPECT_EQ(tool.name, tool2.name);
    EXPECT_EQ(tool.description, tool2.description);
}

TEST(ShortJsonApi, Containers) {
    // Works seamlessly with containers
    std::vector<std::string> items = {"apple", "banana", "cherry"};
    
    // So simple!
    JsonValue json = to_json(items);
    auto result = from_json<std::vector<std::string>>(json);
    
    EXPECT_EQ(items, result);
}

TEST(ShortJsonApi, OptionalTypes) {
    // Works with optionals
    optional<int> maybe_number = 42;
    
    JsonValue json = to_json(maybe_number);
    auto result = from_json<optional<int>>(json);
    
    EXPECT_EQ(maybe_number, result);
    
    // Empty optional
    optional<int> empty;
    json = to_json(empty);
    result = from_json<optional<int>>(json);
    
    EXPECT_FALSE(result.has_value());
}

TEST(ShortJsonApi, ComplexTypes) {
    // Even complex MCP types are clean
    InitializeRequest request;
    request.protocolVersion = "1.0.0";
    request.capabilities.experimental = Metadata{{"feature", MetadataValue("enabled")}};
    
    // Serialize
    JsonValue json = to_json(request);
    
    // Deserialize
    InitializeRequest result = from_json<InitializeRequest>(json);
    
    EXPECT_EQ(request.protocolVersion, result.protocolVersion);
}

TEST(ShortJsonApi, RoundTrip) {
    // Round-trip with nested structures
    Prompt prompt;
    prompt.name = "code_review";
    prompt.description = "Reviews code for quality";
    prompt.arguments = std::vector<PromptArgument>{
        {"file", "The file to review", true},
        {"style", "The style guide to use", false}
    };
    
    // Clean round-trip
    auto json = to_json(prompt);
    auto result = from_json<Prompt>(json);
    
    EXPECT_EQ(prompt.name, result.name);
    EXPECT_EQ(prompt.description, result.description);
    EXPECT_EQ(prompt.arguments->size(), result.arguments->size());
}

// Show both APIs work together
TEST(ShortJsonApi, BothApisWork) {
    TextContent content;
    content.text = "Hello World";
    
    // All these work:
    JsonValue json1 = to_json(content);                          // Short & sweet
    JsonValue json2 = JsonSerializer::serialize(content);        // Explicit class method
    
    TextContent result1 = from_json<TextContent>(json1);        // Short & sweet
    TextContent result2 = JsonDeserializer::deserialize<TextContent>(json2); // Explicit
    
    EXPECT_EQ(result1.text, result2.text);
}