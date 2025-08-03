#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mcp/json.h"
#include "mcp/types.h"

using namespace mcp;
using json = nlohmann::json;

class JsonSerializationTest : public ::testing::Test {
 protected:
  // Helper to test round-trip serialization
  template <typename T>
  void testRoundTrip(const T& value) {
    json j;
    to_json(j, value);
    
    T deserialized;
    from_json(j, deserialized);
    
    json j2;
    to_json(j2, deserialized);
    
    EXPECT_EQ(j, j2);
  }
};

// Basic types tests
TEST_F(JsonSerializationTest, Role) {
  json j = "user";
  enums::Role::Value role;
  from_json(j, role);
  EXPECT_EQ(role, enums::Role::USER);
  
  json j2;
  to_json(j2, role);
  EXPECT_EQ(j2, "user");
}

TEST_F(JsonSerializationTest, LoggingLevel) {
  std::vector<std::string> levels = {
    "debug", "info", "notice", "warning", 
    "error", "critical", "alert", "emergency"
  };
  
  for (const auto& level_str : levels) {
    json j = level_str;
    enums::LoggingLevel::Value level;
    from_json(j, level);
    
    json j2;
    to_json(j2, level);
    EXPECT_EQ(j2, level_str);
  }
}

// Content types tests
TEST_F(JsonSerializationTest, TextContent) {
  TextContent content;
  content.text = "Hello, World!";
  content.annotations = Annotations();
  content.annotations.value().priority = 0.8;
  
  testRoundTrip(content);
}

TEST_F(JsonSerializationTest, ImageContent) {
  ImageContent content;
  content.data = "base64encodeddata==";
  content.mimeType = "image/png";
  
  testRoundTrip(content);
}

TEST_F(JsonSerializationTest, AudioContent) {
  AudioContent content;
  content.data = "base64audiodata==";
  content.mimeType = "audio/mp3";
  
  json j;
  to_json(j, content);
  EXPECT_EQ(j["type"], "audio");
  EXPECT_EQ(j["data"], "base64audiodata==");
  EXPECT_EQ(j["mimeType"], "audio/mp3");
  
  AudioContent deserialized;
  from_json(j, deserialized);
  EXPECT_EQ(deserialized.data, content.data);
  EXPECT_EQ(deserialized.mimeType, content.mimeType);
}

TEST_F(JsonSerializationTest, Resource) {
  Resource resource;
  resource.uri = "file:///path/to/resource.txt";
  resource.name = "Test Resource";
  resource.description = "A test resource";
  resource.mimeType = "text/plain";
  
  testRoundTrip(resource);
}

TEST_F(JsonSerializationTest, ResourceContent) {
  ResourceContent content;
  content.resource.uri = "file:///test.txt";
  content.resource.name = "Test";
  
  testRoundTrip(content);
}

TEST_F(JsonSerializationTest, ContentBlock) {
  // Text variant
  ContentBlock text_block = make_text_content("Hello");
  json j;
  to_json(j, text_block);
  EXPECT_EQ(j["type"], "text");
  EXPECT_EQ(j["text"], "Hello");
  
  ContentBlock deserialized;
  from_json(j, deserialized);
  EXPECT_TRUE(holds_alternative<TextContent>(deserialized));
  
  // Image variant
  ContentBlock image_block = make_image_content("data", "image/png");
  to_json(j, image_block);
  EXPECT_EQ(j["type"], "image");
  EXPECT_EQ(j["data"], "data");
  EXPECT_EQ(j["mimeType"], "image/png");
}

// Tool types tests
TEST_F(JsonSerializationTest, Tool) {
  Tool tool;
  tool.name = "calculator";
  tool.description = "Performs calculations";
  tool.inputSchema = json::parse(R"({
    "type": "object",
    "properties": {
      "expression": {"type": "string"}
    }
  })");
  
  testRoundTrip(tool);
}

// Prompt types tests
TEST_F(JsonSerializationTest, Prompt) {
  Prompt prompt;
  prompt.name = "code_review";
  prompt.description = "Reviews code";
  
  PromptArgument arg;
  arg.name = "code";
  arg.required = true;
  prompt.arguments = std::vector<PromptArgument>{arg};
  
  testRoundTrip(prompt);
}

// Server/Client info tests
TEST_F(JsonSerializationTest, Implementation) {
  Implementation impl;
  impl.name = "test-server";
  impl.version = "1.0.0";
  
  testRoundTrip(impl);
}

// Capabilities tests
TEST_F(JsonSerializationTest, ServerCapabilities) {
  ServerCapabilities caps;
  caps.tools = true;
  ResourcesCapability res_cap;
  res_cap.subscribe = EmptyCapability();
  caps.resources = res_cap;
  caps.prompts = true;
  caps.logging = true;
  
  json j;
  to_json(j, caps);
  
  EXPECT_TRUE(j["tools"].is_boolean());
  EXPECT_TRUE(j["resources"].is_object());
  EXPECT_TRUE(j["resources"]["subscribe"].is_object());
  EXPECT_TRUE(j["prompts"].is_boolean());
  EXPECT_TRUE(j["logging"].is_boolean());
  
  ServerCapabilities deserialized;
  from_json(j, deserialized);
  
  EXPECT_TRUE(deserialized.tools.has_value());
  EXPECT_TRUE(deserialized.resources.has_value());
  EXPECT_TRUE(holds_alternative<ResourcesCapability>(deserialized.resources.value()));
  auto& res = get<ResourcesCapability>(deserialized.resources.value());
  EXPECT_TRUE(res.subscribe.has_value());
  EXPECT_TRUE(deserialized.prompts.has_value());
  EXPECT_TRUE(deserialized.logging.has_value());
}

TEST_F(JsonSerializationTest, ClientCapabilities) {
  ClientCapabilities caps;
  RootsCapability roots;
  roots.listChanged = EmptyCapability();
  caps.roots = roots;
  SamplingParams sampling;
  sampling.temperature = 0.7;
  caps.sampling = sampling;
  
  testRoundTrip(caps);
}

// Request/Response tests
TEST_F(JsonSerializationTest, InitializeRequest) {
  InitializeRequest req;
  req.jsonrpc = "2.0";
  req.method = "initialize";
  req.id = make_request_id("init-123");
  req.protocolVersion = "1.0";
  Implementation clientInfo;
  clientInfo.name = "Test Client";
  clientInfo.version = "1.0.0";
  req.clientInfo = clientInfo;
  req.capabilities = ClientCapabilities();
  
  testRoundTrip(req);
}

TEST_F(JsonSerializationTest, InitializeResult) {
  InitializeResult result;
  result.protocolVersion = "1.0";
  Implementation serverInfo;
  serverInfo.name = "Test Server";
  serverInfo.version = "1.0.0";
  result.serverInfo = serverInfo;
  ServerCapabilities caps;
  caps.tools = true;
  result.capabilities = caps;
  
  testRoundTrip(result);
}

TEST_F(JsonSerializationTest, CallToolResult) {
  CallToolResult result;
  TextContent tc;
  tc.type = "text";
  tc.text = "Result: 42";
  result.content.push_back(tc);
  result.isError = false;
  
  json j;
  to_json(j, result);
  
  EXPECT_TRUE(j["content"].is_array());
  EXPECT_EQ(j["content"][0]["type"], "text");
  EXPECT_EQ(j["content"][0]["text"], "Result: 42");
  EXPECT_FALSE(j.contains("isError"));  // Default false not serialized
  
  CallToolResult deserialized;
  from_json(j, deserialized);
  
  EXPECT_EQ(deserialized.content.size(), 1u);
  EXPECT_FALSE(deserialized.isError);
}

// Metadata tests - Metadata is a map<string, MetadataValue> where MetadataValue is a variant
TEST_F(JsonSerializationTest, Metadata) {
  Metadata meta;
  meta["string"] = std::string("value");
  meta["number"] = int64_t(42);
  meta["float"] = 3.14;
  meta["bool"] = true;
  meta["null"] = nullptr;
  // Note: Metadata only supports basic types, not arrays or objects
  
  json j;
  to_json(j, meta);
  
  EXPECT_EQ(j["string"], "value");
  EXPECT_EQ(j["number"], 42);
  EXPECT_DOUBLE_EQ(j["float"].get<double>(), 3.14);
  EXPECT_EQ(j["bool"], true);
  EXPECT_TRUE(j["null"].is_null());
  
  Metadata deserialized;
  from_json(j, deserialized);
  
  // Check the variant values
  EXPECT_TRUE(holds_alternative<std::string>(deserialized["string"]));
  EXPECT_EQ(get<std::string>(deserialized["string"]), "value");
  EXPECT_TRUE(holds_alternative<int64_t>(deserialized["number"]));
  EXPECT_EQ(get<int64_t>(deserialized["number"]), 42);
  EXPECT_TRUE(holds_alternative<double>(deserialized["float"]));
  EXPECT_DOUBLE_EQ(get<double>(deserialized["float"]), 3.14);
  EXPECT_TRUE(holds_alternative<bool>(deserialized["bool"]));
  EXPECT_EQ(get<bool>(deserialized["bool"]), true);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(deserialized["null"]));
}

TEST_F(JsonSerializationTest, PromptMessage) {
  // Test PromptMessage structure
  PromptMessage msg;
  msg.role = enums::Role::USER;
  TextContent tc;
  tc.type = "text";
  tc.text = "Hello";
  msg.content = tc;
  
  json j;
  to_json(j, msg);
  
  EXPECT_EQ(j["role"], "user");
  EXPECT_EQ(j["content"]["type"], "text");
  EXPECT_EQ(j["content"]["text"], "Hello");
  
  PromptMessage deserialized;
  from_json(j, deserialized);
  
  EXPECT_EQ(deserialized.role, enums::Role::USER);
  EXPECT_TRUE(holds_alternative<TextContent>(deserialized.content));
}

// Edge cases tests
TEST_F(JsonSerializationTest, OptionalFields) {
  // Test with all optional fields missing
  Resource resource;
  resource.uri = "file:///required.txt";
  resource.name = "Required name";  // name is required
  
  json j;
  to_json(j, resource);
  
  EXPECT_EQ(j["uri"], "file:///required.txt");
  EXPECT_EQ(j["name"], "Required name");
  EXPECT_FALSE(j.contains("description"));
  EXPECT_FALSE(j.contains("mimeType"));
  
  Resource deserialized;
  from_json(j, deserialized);
  
  EXPECT_EQ(deserialized.uri, resource.uri);
  EXPECT_EQ(deserialized.name, resource.name);
  EXPECT_FALSE(deserialized.description.has_value());
  EXPECT_FALSE(deserialized.mimeType.has_value());
}

// Error handling tests
TEST_F(JsonSerializationTest, InvalidJsonThrows) {
  // Missing required field
  json j = json::object();
  
  EXPECT_THROW({
    Resource resource;
    from_json(j, resource);
  }, std::exception);
  
  // Invalid type field
  j = json{{"type", "invalid_type"}};
  
  EXPECT_THROW({
    ContentBlock block;
    from_json(j, block);
  }, std::runtime_error);
  
  // Invalid enum value
  j = "invalid_level";
  EXPECT_THROW({
    enums::LoggingLevel::Value level;
    from_json(j, level);
  }, std::runtime_error);
}

TEST_F(JsonSerializationTest, ExtendedContentBlock) {
  // Test ResourceLink variant
  ResourceLink link;
  link.uri = "https://example.com/resource";
  link.name = "Example Resource";
  link.type = "resource";
  
  ExtendedContentBlock block = link;
  json j;
  to_json(j, block);
  
  EXPECT_EQ(j["type"], "resource");
  EXPECT_EQ(j["uri"], "https://example.com/resource");
  EXPECT_EQ(j["name"], "Example Resource");
  
  ExtendedContentBlock deserialized;
  from_json(j, deserialized);
  EXPECT_TRUE(holds_alternative<ResourceLink>(deserialized));
}

TEST_F(JsonSerializationTest, ResourceTemplate) {
  ResourceTemplate tmpl;
  tmpl.uriTemplate = "/files/{path}";
  tmpl.name = "File Browser";
  tmpl.description = "Browse files in the system";
  
  testRoundTrip(tmpl);
}

TEST_F(JsonSerializationTest, Root) {
  Root root;
  root.uri = "file:///home/user/project";
  root.name = "My Project";
  
  testRoundTrip(root);
}

TEST_F(JsonSerializationTest, ModelPreferences) {
  ModelPreferences prefs;
  prefs.costPriority = 0.3;
  prefs.speedPriority = 0.5;
  prefs.intelligencePriority = 0.2;
  
  ModelHint hint;
  hint.name = "gpt-4";
  prefs.hints = std::vector<ModelHint>{hint};
  
  testRoundTrip(prefs);
}

TEST_F(JsonSerializationTest, SamplingMessage) {
  SamplingMessage msg;
  msg.role = enums::Role::ASSISTANT;
  TextContent tc;
  tc.type = "text";
  tc.text = "Here's the answer";
  msg.content = tc;
  
  testRoundTrip(msg);
}

TEST_F(JsonSerializationTest, CreateMessageResult) {
  CreateMessageResult result;
  result.role = enums::Role::ASSISTANT;
  TextContent tc;
  tc.type = "text";
  tc.text = "Generated response";
  result.content = tc;
  result.model = "claude-3";
  result.stopReason = "max_tokens";
  
  testRoundTrip(result);
}

TEST_F(JsonSerializationTest, Error) {
  Error err;
  err.code = -32600;
  err.message = "Invalid Request";
  err.data = std::string("Missing required field");
  
  json j;
  to_json(j, err);
  
  EXPECT_EQ(j["code"], -32600);
  EXPECT_EQ(j["message"], "Invalid Request");
  EXPECT_EQ(j["data"], "Missing required field");
  
  Error deserialized;
  from_json(j, deserialized);
  
  EXPECT_EQ(deserialized.code, err.code);
  EXPECT_EQ(deserialized.message, err.message);
  EXPECT_TRUE(deserialized.data.has_value());
  EXPECT_TRUE(holds_alternative<std::string>(deserialized.data.value()));
}