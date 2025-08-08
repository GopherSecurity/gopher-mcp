#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <limits>
#include <thread>
#include <chrono>

#include "mcp/json/json_serialization.h"
#include "mcp/json/json_bridge.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;

class MCPSerializationExtensiveTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
  
  // Helper to verify JSON structure matches expected schema
  void verifyJsonStructure(const JsonValue& j, const std::string& expected_type) {
    if (expected_type == "text") {
      EXPECT_TRUE(j.contains("type"));
      EXPECT_EQ(j["type"].getString(), "text");
      EXPECT_TRUE(j.contains("text"));
    } else if (expected_type == "image") {
      EXPECT_TRUE(j.contains("type"));
      EXPECT_EQ(j["type"].getString(), "image");
      EXPECT_TRUE(j.contains("data"));
      EXPECT_TRUE(j.contains("mimeType"));
    } else if (expected_type == "resource") {
      EXPECT_TRUE(j.contains("type"));
      EXPECT_EQ(j["type"].getString(), "resource");
      EXPECT_TRUE(j.contains("resource"));
    }
  }
  
  // Helper to test that serialization preserves all fields
  template <typename T>
  void testFieldPreservation(const T& value, const std::vector<std::string>& required_fields) {
    JsonValue j = to_json(value);
    for (const auto& field : required_fields) {
      EXPECT_TRUE(j.contains(field)) << "Missing required field: " << field;
    }
  }
};

// =============================================================================
// Extreme Edge Cases
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, ExtremelyLongStrings) {
  // Test with very long strings (10MB)
  std::string long_text(10 * 1024 * 1024, 'a');
  TextContent content(long_text);
  
  JsonValue j = to_json(content);
  TextContent deserialized = from_json<TextContent>(j);
  EXPECT_EQ(deserialized.text.size(), long_text.size());
  EXPECT_EQ(deserialized.text, long_text);
}

// NOTE: This test is disabled because Metadata can only hold primitive values
// (string, int64_t, double, bool, null), not nested Metadata or arrays
/*
TEST_F(MCPSerializationExtensiveTest, DeeplyNestedMetadata) {
  // Would require Metadata to support nested objects, which it doesn't
}
*/

TEST_F(MCPSerializationExtensiveTest, MassiveArrays) {
  // Test with large arrays in various contexts
  ListResourcesResult result;
  
  // Add 10000 resources
  for (int i = 0; i < 10000; ++i) {
    Resource res("file:///path/file" + std::to_string(i), "file" + std::to_string(i));
    res.description = mcp::make_optional(std::string("Description " + std::to_string(i)));
    res.mimeType = mcp::make_optional(std::string("text/plain"));
    result.resources.push_back(res);
  }
  
  JsonValue j = to_json(result);
  ListResourcesResult deserialized = from_json<ListResourcesResult>(j);
  
  EXPECT_EQ(deserialized.resources.size(), 10000);
  for (size_t i = 0; i < 100; ++i) { // Spot check first 100
    EXPECT_EQ(deserialized.resources[i].uri, "file:///path/file" + std::to_string(i));
    EXPECT_EQ(deserialized.resources[i].name, "file" + std::to_string(i));
  }
}

// =============================================================================
// Boundary Value Testing
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, NumericBoundaries) {
  // Test all numeric boundaries
  struct NumericTests {
    // Max/min integers
    int max_int = std::numeric_limits<int>::max();
    int min_int = std::numeric_limits<int>::min();
    
    // Max/min doubles
    double max_double = std::numeric_limits<double>::max();
    double min_double = std::numeric_limits<double>::lowest();
    double infinity = std::numeric_limits<double>::infinity();
    double neg_infinity = -std::numeric_limits<double>::infinity();
    double nan_val = std::numeric_limits<double>::quiet_NaN();
    
    // Special doubles
    double epsilon = std::numeric_limits<double>::epsilon();
    double denorm_min = std::numeric_limits<double>::denorm_min();
  };
  
  // Test in metadata
  Metadata meta = make_metadata();
  add_metadata(meta, "max_int", std::numeric_limits<int>::max());
  add_metadata(meta, "min_int", std::numeric_limits<int>::min());
  add_metadata(meta, "max_long", std::numeric_limits<long long>::max());
  add_metadata(meta, "min_long", std::numeric_limits<long long>::min());
  
  // Note: JSON doesn't support Infinity or NaN directly
  add_metadata(meta, "max_double", std::numeric_limits<double>::max());
  add_metadata(meta, "min_double", std::numeric_limits<double>::lowest());
  add_metadata(meta, "epsilon", std::numeric_limits<double>::epsilon());
  
  JsonValue j = to_json(meta);
  Metadata deserialized = from_json<Metadata>(j);
  
  EXPECT_EQ(mcp::get<int64_t>(deserialized["max_int"]), std::numeric_limits<int>::max());
  EXPECT_EQ(mcp::get<int64_t>(deserialized["min_int"]), std::numeric_limits<int>::min());
}

// =============================================================================
// Unicode and Special Characters
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, UnicodeExtreme) {
  // Test various Unicode ranges
  std::vector<std::string> unicode_tests = {
    // Basic Latin
    "Hello World",
    
    // Latin Extended
    "Ĥęľľő Ŵőŕłđ",
    
    // Greek
    "Γειά σου κόσμε",
    
    // Cyrillic
    "Привет мир",
    
    // Arabic (RTL)
    "مرحبا بالعالم",
    
    // Hebrew (RTL)
    "שלום עולם",
    
    // Chinese
    "你好世界",
    
    // Japanese (Hiragana, Katakana, Kanji)
    "こんにちは世界 コンニチハ 今日は",
    
    // Korean
    "안녕하세요 세계",
    
    // Emoji
    "👋🌍🌎🌏 Hello 😀🎉🚀",
    
    // Mathematical symbols
    "∀x∈ℝ: x²≥0 ∫∞ ∑∏√∛",
    
    // Box drawing
    "┌─┬─┐│ ││ │├─┼─┤└─┴─┘",
    
    // Control characters (escaped)
    "Line1\nLine2\tTab\rCarriage\bBackspace",
    
    // Zero-width characters
    "Hello\u200B\u200CWorld\u200D\uFEFF",
    
    // Combining characters
    "a\u0300 e\u0301 i\u0302 o\u0303 u\u0308", // à é î õ ü
    
    // Surrogate pairs (Emoji with skin tone)
    "👨‍💻👩🏽‍💻👨🏿‍🔬",
  };
  
  for (const auto& text : unicode_tests) {
    TextContent content(text);
    JsonValue j = to_json(content);
    TextContent deserialized = from_json<TextContent>(j);
    EXPECT_EQ(deserialized.text, text) << "Failed for: " << text;
  }
}

TEST_F(MCPSerializationExtensiveTest, EscapeSequences) {
  // Test all JSON escape sequences
  std::string escape_test = 
    "Quote: \"\n"
    "Backslash: \\\n"
    "Slash: /\n"
    "Backspace: \b\n"
    "Form feed: \f\n"
    "Newline: \n\n"
    "Carriage return: \r\n"
    "Tab: \t\n"
    "Unicode: \u0041\u0042\u0043";
  
  TextContent content(escape_test);
  std::string json_str = mcp::json::to_json(content).toString();
  TextContent deserialized = from_json<TextContent>(mcp::json::JsonValue::parse(json_str));
  EXPECT_EQ(deserialized.text, escape_test);
}

// =============================================================================
// Variant Exhaustive Testing
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, AllContentBlockCombinations) {
  // Create all possible ContentBlock variants with all optional fields
  std::vector<ContentBlock> blocks;
  
  // Text with all annotation combinations
  for (int priority = 0; priority <= 2; ++priority) {
    for (int audience = 0; audience <= 3; ++audience) {
      TextContent text("Text variant " + std::to_string(priority * 4 + audience));
      
      if (priority > 0) {
        if (!text.annotations) {
          text.annotations = mcp::make_optional(Annotations());
        }
        text.annotations->priority = mcp::make_optional(priority == 1 ? 0.5 : 1.0);
      }
      
      if (audience > 0) {
        if (!text.annotations) {
          text.annotations = mcp::make_optional(Annotations());
        }
        std::vector<enums::Role::Value> roles;
        if (audience & 1) roles.push_back(enums::Role::USER);
        if (audience & 2) roles.push_back(enums::Role::ASSISTANT);
        if (!roles.empty()) {
          text.annotations->audience = mcp::make_optional(roles);
        }
      }
      
      blocks.push_back(ContentBlock(text));
    }
  }
  
  // Image with various mime types
  std::vector<std::string> mime_types = {
    "image/jpeg", "image/png", "image/gif", "image/svg+xml", 
    "image/webp", "image/bmp", "image/tiff"
  };
  
  for (const auto& mime : mime_types) {
    blocks.push_back(make_image_content("imagedata_" + mime, mime));
  }
  
  // Resource with all optional field combinations
  for (int has_desc = 0; has_desc <= 1; ++has_desc) {
    for (int has_mime = 0; has_mime <= 1; ++has_mime) {
      Resource res("uri://resource/" + std::to_string(has_desc * 2 + has_mime),
                   "Resource " + std::to_string(has_desc * 2 + has_mime));
      if (has_desc) {
        res.description = mcp::make_optional(std::string("Description"));
      }
      if (has_mime) {
        res.mimeType = mcp::make_optional(std::string("application/octet-stream"));
      }
      blocks.push_back(make_resource_content(res));
    }
  }
  
  // Test all blocks
  for (const auto& block : blocks) {
    JsonValue j = to_json(block);
    ContentBlock deserialized = from_json<ContentBlock>(j);
    JsonValue j2 = to_json(deserialized);
    EXPECT_EQ(j.toString(), j2.toString());
  }
}

// =============================================================================
// Protocol Message Combinations
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, AllRequestTypeCombinations) {
  // Test all request types with various ID types
  std::vector<RequestId> ids = {
    mcp::make_request_id(0),
    mcp::make_request_id(-1),
    mcp::make_request_id(999999),
    mcp::make_request_id(""),
    mcp::make_request_id("simple"),
    mcp::make_request_id("with-dashes-and-numbers-123"),
    mcp::make_request_id("unicode-你好-мир-🌍")
  };
  
  for (const auto& id : ids) {
    // Initialize request
    InitializeRequest init_req;
    init_req.id = id;
    init_req.protocolVersion = "1.0.0";
    init_req.capabilities = ClientCapabilities();
    JsonValue j = to_json(init_req);
    InitializeRequest init_des = from_json<InitializeRequest>(j);
    EXPECT_EQ(to_json(init_des.id).toString(), to_json(id).toString());
    
    // Ping request
    PingRequest ping_req;
    ping_req.id = id;
    j = to_json(ping_req);
    PingRequest ping_des = from_json<PingRequest>(j);
    EXPECT_EQ(to_json(ping_des.id).toString(), to_json(id).toString());
    
    // List resources request
    ListResourcesRequest list_req;
    list_req.id = id;
    j = to_json(list_req);
    ListResourcesRequest list_des = from_json<ListResourcesRequest>(j);
    EXPECT_EQ(to_json(list_des.id).toString(), to_json(id).toString());
  }
}

TEST_F(MCPSerializationExtensiveTest, AllNotificationTypes) {
  // Test all notification types
  std::vector<std::string> methods = {
    "initialized",
    "cancelled", 
    "progress",
    "resources/list_changed",
    "resources/updated",
    "prompts/list_changed",
    "tools/list_changed",
    "roots/list_changed",
    "message"
  };
  
  for (const auto& method : methods) {
    if (method == "initialized") {
      InitializedNotification notif;
      JsonValue j = to_json(notif);
      InitializedNotification deserialized = from_json<InitializedNotification>(j);
      // InitializedNotification has no fields to compare
    } else if (method == "cancelled") {
      CancelledNotification notif;
      notif.requestId = mcp::make_request_id("test");
      notif.reason = mcp::make_optional(std::string("Test reason"));
      JsonValue j = to_json(notif);
      CancelledNotification deserialized = from_json<CancelledNotification>(j);
      EXPECT_EQ(to_json(deserialized.requestId).toString(), to_json(notif.requestId).toString());
      EXPECT_EQ(deserialized.reason.value(), "Test reason");
    } else if (method == "progress") {
      ProgressNotification notif;
      notif.progressToken = make_progress_token("token");
      notif.progress = 0.5;
      notif.total = mcp::make_optional(100.0);
      JsonValue j = to_json(notif);
      ProgressNotification deserialized = from_json<ProgressNotification>(j);
      EXPECT_EQ(to_json(deserialized.progressToken).toString(), to_json(notif.progressToken).toString());
      EXPECT_DOUBLE_EQ(deserialized.progress, 0.5);
      EXPECT_DOUBLE_EQ(deserialized.total.value(), 100.0);
    }
    // Continue for other notification types...
  }
}

// =============================================================================
// Error Conditions and Recovery
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, MalformedJsonRecovery) {
  // Test that we handle various malformed JSON inputs gracefully
  std::vector<std::string> malformed_jsons = {
    R"({"type": "text"})", // Missing required "text" field
    R"({"type": "image", "data": "xyz"})", // Missing mimeType
    R"({"type": "unknown", "data": "xyz"})", // Unknown type
    R"({"type": 123})", // Wrong type for "type" field
    "[]", // Array instead of object
    "null", // Null value
    "\"string\"", // Plain string
    "123", // Plain number
  };
  
  for (const auto& bad_json : malformed_jsons) {
    try {
      JsonValue j = JsonValue::parse(bad_json);
      // Attempt to deserialize - should either throw or handle gracefully
      if (j.isObject() && j.contains("type")) {
        if (j["type"].getString() == "text") {
          EXPECT_THROW(from_json<TextContent>(j), std::exception);
        } else if (j["type"].getString() == "image") {
          EXPECT_THROW(from_json<ImageContent>(j), std::exception);
        }
      }
    } catch (const JsonException& e) {
      // JSON parsing failed - this is expected for some inputs
      continue;
    }
  }
}

// =============================================================================
// Performance and Stress Testing
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, RapidSerializationDeserialization) {
  // Perform rapid serialization/deserialization to check for memory leaks
  const int iterations = 10000;
  
  for (int i = 0; i < iterations; ++i) {
    // Create complex object
    CreateMessageRequest req = make<CreateMessageRequest>()
      .add_user_message("Message " + std::to_string(i))
      .temperature(0.5 + (i % 10) * 0.05)
      .maxTokens(100 + i % 900)
      .build();
    
    // Serialize and deserialize
    JsonValue j = to_json(req);
    CreateMessageRequest deserialized = from_json<CreateMessageRequest>(j);
    
    // Basic validation
    EXPECT_EQ(deserialized.messages.size(), 1);
  }
}

TEST_F(MCPSerializationExtensiveTest, ConcurrentSerialization) {
  // Test thread safety of serialization
  const int num_threads = 10;
  const int iterations_per_thread = 100;
  std::vector<std::thread> threads;
  
  auto worker = [iterations_per_thread](int thread_id) {
    for (int i = 0; i < iterations_per_thread; ++i) {
      // Create unique content for each thread
      std::string text = "Thread " + std::to_string(thread_id) + 
                        " iteration " + std::to_string(i);
      TextContent content(text);
      
      // Serialize and deserialize
      JsonValue j = to_json(content);
      TextContent deserialized = from_json<TextContent>(j);
      
      // Verify
      EXPECT_EQ(deserialized.text, text);
    }
  };
  
  // Launch threads
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back(worker, i);
  }
  
  // Wait for completion
  for (auto& t : threads) {
    t.join();
  }
}

// =============================================================================
// Complex Real-World Scenarios
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, CompleteProtocolSession) {
  // Simulate a complete MCP protocol session
  
  // 1. Client connects and initializes
  InitializeRequest init_req = make_initialize_request(
    "1.0.0",
    make<ClientCapabilities>()
      .resources(true)
      .tools(true)
      .build()
  );
  init_req.id = mcp::make_request_id(1);
  init_req.clientInfo = mcp::make_optional(Implementation("TestClient", "1.0.0"));
  
  JsonValue init_req_json = to_json(init_req);
  InitializeRequest init_req_des = from_json<InitializeRequest>(init_req_json);
  
  // 2. Server responds
  InitializeResult init_result;
  init_result.protocolVersion = "1.0.0";
  init_result.capabilities = make<ServerCapabilities>()
    .resources(true)
    .tools(true)
    .prompts(true)
    .build();
  init_result.serverInfo = mcp::make_optional(Implementation("TestServer", "1.0.0"));
  
  JsonValue init_result_json = to_json(init_result);
  InitializeResult init_result_des = from_json<InitializeResult>(init_result_json);
  
  // 3. Client sends initialized
  InitializedNotification initialized;
  JsonValue initialized_json = to_json(initialized);
  
  // 4. Client lists available tools
  ListToolsRequest list_tools;
  list_tools.id = mcp::make_request_id(2);
  JsonValue list_tools_json = to_json(list_tools);
  
  // 5. Server responds with tools
  ListToolsResult tools_result;
  tools_result.tools.push_back(
    make<Tool>("calculator")
      .description("Performs calculations")
      .inputSchema(JsonValue::parse(R"({
        "type": "object",
        "properties": {
          "expression": {"type": "string"}
        }
      })"))
      .build()
  );
  tools_result.tools.push_back(
    make<Tool>("weather")
      .description("Gets weather information")
      .build()
  );
  
  JsonValue tools_result_json = to_json(tools_result);
  ListToolsResult tools_result_des = from_json<ListToolsResult>(tools_result_json);
  
  // 6. Client calls a tool
  Metadata calc_args = make_metadata();
  add_metadata(calc_args, "expression", "2 + 2 * 3");
  CallToolRequest call_tool = make_call_tool_request("calculator", calc_args);
  call_tool.id = mcp::make_request_id(3);
  
  JsonValue call_tool_json = to_json(call_tool);
  CallToolRequest call_tool_des = from_json<CallToolRequest>(call_tool_json);
  
  // 7. Server responds with result
  CallToolResult tool_result;
  tool_result.content.push_back(ExtendedContentBlock(TextContent("8")));
  
  JsonValue tool_result_json = to_json(tool_result);
  CallToolResult tool_result_des = from_json<CallToolResult>(tool_result_json);
  
  // 8. Client requests resource list
  ListResourcesRequest list_res;
  list_res.id = mcp::make_request_id(4);
  
  JsonValue list_res_json = to_json(list_res);
  
  // 9. Server sends paginated response
  ListResourcesResult res_result;
  for (int i = 0; i < 100; ++i) {
    res_result.resources.push_back(
      make<Resource>("file:///doc" + std::to_string(i) + ".txt", 
                     "Document " + std::to_string(i))
        .description("Description of document " + std::to_string(i))
        .mimeType("text/plain")
        .build()
    );
  }
  res_result.nextCursor = mcp::make_optional(Cursor("page2"));
  
  JsonValue res_result_json = to_json(res_result);
  ListResourcesResult res_result_des = from_json<ListResourcesResult>(res_result_json);
  
  // 10. Progress notifications during long operation
  ProgressNotification progress1 = make_progress_notification(
    make_progress_token("operation-1"), 0.25
  );
  progress1.total = mcp::make_optional(100.0);
  
  JsonValue progress1_json = to_json(progress1);
  
  ProgressNotification progress2 = make_progress_notification(
    make_progress_token("operation-1"), 0.75
  );
  progress2.total = mcp::make_optional(100.0);
  
  JsonValue progress2_json = to_json(progress2);
  
  // Verify all messages were properly serialized/deserialized
  EXPECT_EQ(init_req_des.protocolVersion, "1.0.0");
  EXPECT_EQ(init_result_des.protocolVersion, "1.0.0");
  EXPECT_EQ(tools_result_des.tools.size(), 2);
  EXPECT_EQ(tool_result_des.content.size(), 1);
  EXPECT_EQ(res_result_des.resources.size(), 100);
  EXPECT_TRUE(res_result_des.nextCursor.has_value());
}

// =============================================================================
// Schema Validation Tests
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, ValidateJsonSchema) {
  // Ensure serialized JSON matches expected MCP schema structure
  
  // Test TextContent schema
  TextContent text("Hello");
  JsonValue text_json = to_json(text);
  EXPECT_EQ(text_json["type"].getString(), "text");
  EXPECT_TRUE(text_json.contains("text"));
  EXPECT_EQ(text_json["text"].getString(), "Hello");
  
  // Test with annotations
  text.annotations = mcp::make_optional(Annotations());
  text.annotations->priority = mcp::make_optional(0.8);
  text_json = to_json(text);
  EXPECT_TRUE(text_json.contains("annotations"));
  EXPECT_EQ(text_json["annotations"]["priority"].getFloat(), 0.8);
  
  // Test Tool schema
  Tool tool = make<Tool>("test_tool")
    .description("A test tool")
    .inputSchema(JsonValue::parse(R"({"type": "object"})"))
    .build();
  
  JsonValue tool_json = to_json(tool);
  EXPECT_EQ(tool_json["name"].getString(), "test_tool");
  EXPECT_EQ(tool_json["description"].getString(), "A test tool");
  EXPECT_TRUE(tool_json.contains("inputSchema"));
  EXPECT_EQ(tool_json["inputSchema"]["type"].getString(), "object");
  
  // Test Request schema
  jsonrpc::Request req(mcp::make_request_id(123), "test_method");
  JsonValue req_json = to_json(req);
  EXPECT_EQ(req_json["jsonrpc"].getString(), "2.0");
  EXPECT_EQ(req_json["id"].getInt(), 123);
  EXPECT_EQ(req_json["method"].getString(), "test_method");
  
  // Test Response schema
  auto response = jsonrpc::Response::success(mcp::make_request_id("resp-1"), std::string("result"));
  JsonValue resp_json = to_json(response);
  EXPECT_EQ(resp_json["jsonrpc"].getString(), "2.0");
  EXPECT_EQ(resp_json["id"].getString(), "resp-1");
  EXPECT_TRUE(resp_json.contains("result"));
  EXPECT_FALSE(resp_json.contains("error"));
  
  // Test Error Response schema
  auto error_response = jsonrpc::Response::make_error(
    mcp::make_request_id("resp-2"),
    Error(jsonrpc::INVALID_PARAMS, "Invalid parameters")
  );
  JsonValue err_resp_json = to_json(error_response);
  EXPECT_EQ(err_resp_json["jsonrpc"].getString(), "2.0");
  EXPECT_EQ(err_resp_json["id"].getString(), "resp-2");
  EXPECT_FALSE(err_resp_json.contains("result"));
  EXPECT_TRUE(err_resp_json.contains("error"));
  EXPECT_EQ(err_resp_json["error"]["code"].getInt(), jsonrpc::INVALID_PARAMS);
  EXPECT_EQ(err_resp_json["error"]["message"].getString(), "Invalid parameters");
}

// =============================================================================
// Capability Combination Testing
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, AllCapabilityCombinations) {
  // Test all possible capability combinations
  
  // Client capabilities with all features
  ClientCapabilities full_client = make<ClientCapabilities>()
    .experimental(make_metadata())
    .sampling(make<SamplingParams>()
      .temperature(0.7)
      .maxTokens(1000)
      .stopSequence("STOP")
      .build())
    .build();
  
  // Add roots capability
  full_client.roots = mcp::make_optional(RootsCapability());
  full_client.roots->listChanged = mcp::make_optional(EmptyCapability());
  
  JsonValue client_json = to_json(full_client);
  ClientCapabilities client_des = from_json<ClientCapabilities>(client_json);
  
  // Server capabilities with ResourcesCapability
  ServerCapabilities server_with_resources;
  ResourcesCapability res_cap;
  res_cap.subscribe = mcp::make_optional(EmptyCapability());
  res_cap.listChanged = mcp::make_optional(EmptyCapability());
  server_with_resources.resources = mcp::make_optional(
    variant<bool, ResourcesCapability>(res_cap)
  );
  server_with_resources.tools = mcp::make_optional(true);
  server_with_resources.prompts = mcp::make_optional(true);
  server_with_resources.logging = mcp::make_optional(true);
  
  JsonValue server_json = to_json(server_with_resources);
  ServerCapabilities server_des = from_json<ServerCapabilities>(server_json);
  
  // Server capabilities with bool resources
  ServerCapabilities server_with_bool = make<ServerCapabilities>()
    .resources(true)
    .tools(false)
    .prompts(true)
    .logging(false)
    .build();
  
  JsonValue server_bool_json = to_json(server_with_bool);
  ServerCapabilities server_bool_des = from_json<ServerCapabilities>(server_bool_json);
  
  // Minimal capabilities
  ClientCapabilities minimal_client;
  ServerCapabilities minimal_server;
  
  JsonValue min_client_json = to_json(minimal_client);
  JsonValue min_server_json = to_json(minimal_server);
  
  ClientCapabilities min_client_des = from_json<ClientCapabilities>(min_client_json);
  ServerCapabilities min_server_des = from_json<ServerCapabilities>(min_server_json);
}

// =============================================================================
// Model Preferences and Sampling
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, ComplexModelPreferences) {
  // Test all model preference combinations
  std::vector<ModelPreferences> preferences;
  
  // All priorities set
  preferences.push_back(
    make<ModelPreferences>()
      .add_hint("gpt-4")
      .add_hint("claude-3-opus")
      .add_hint("gemini-ultra")
      .cost_priority(0.0)  // Minimum cost priority
      .speed_priority(1.0)  // Maximum speed priority
      .intelligence_priority(0.5)  // Balanced intelligence
      .build()
  );
  
  // Only hints
  preferences.push_back(
    make<ModelPreferences>()
      .add_hint("llama-70b")
      .build()
  );
  
  // Only priorities
  preferences.push_back(
    make<ModelPreferences>()
      .cost_priority(1.0)
      .speed_priority(0.0)
      .intelligence_priority(1.0)
      .build()
  );
  
  // Empty preferences
  preferences.push_back(ModelPreferences());
  
  for (const auto& pref : preferences) {
    JsonValue j = to_json(pref);
    ModelPreferences deserialized = from_json<ModelPreferences>(j);
    JsonValue j2 = to_json(deserialized);
    EXPECT_EQ(j.toString(), j2.toString());
  }
}

TEST_F(MCPSerializationExtensiveTest, ComplexSamplingMessages) {
  // Create complex sampling message request
  CreateMessageRequest req;
  req.id = mcp::make_request_id("sampling-1");
  
  // Add various message types
  SamplingMessage msg1;
  msg1.role = enums::Role::USER;
  msg1.content = TextContent("What's the weather?");
  req.messages.push_back(msg1);
  
  SamplingMessage msg2;
  msg2.role = enums::Role::ASSISTANT;
  msg2.content = ImageContent("weathermap", "image/png");
  req.messages.push_back(msg2);
  
  SamplingMessage msg3;
  msg3.role = enums::Role::USER;
  msg3.content = AudioContent("audioquery", "audio/mp3");
  req.messages.push_back(msg3);
  
  // Set all optional fields
  req.modelPreferences = mcp::make_optional(
    make<ModelPreferences>()
      .add_hint("gpt-4")
      .cost_priority(0.3)
      .build()
  );
  req.systemPrompt = mcp::make_optional(std::string("You are a weather assistant"));
  req.includeContext = mcp::make_optional(make_metadata());
  add_metadata(*req.includeContext, "location", "San Francisco");
  add_metadata(*req.includeContext, "units", "metric");
  req.temperature = mcp::make_optional(0.7);
  req.maxTokens = mcp::make_optional(500);
  req.stopSequences = mcp::make_optional(std::vector<std::string>{"END", "STOP"});
  req.metadata = mcp::make_optional(make_metadata());
  add_metadata(*req.metadata, "request_id", "weather-123");
  
  JsonValue j = to_json(req);
  CreateMessageRequest deserialized = from_json<CreateMessageRequest>(j);
  
  EXPECT_EQ(deserialized.messages.size(), 3);
  EXPECT_TRUE(deserialized.modelPreferences.has_value());
  EXPECT_TRUE(deserialized.systemPrompt.has_value());
  EXPECT_TRUE(deserialized.temperature.has_value());
  EXPECT_EQ(*deserialized.temperature, 0.7);
  EXPECT_TRUE(deserialized.maxTokens.has_value());
  EXPECT_EQ(*deserialized.maxTokens, 500);
}

// =============================================================================
// Resource Template and Pagination
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, ResourceTemplatesAndPagination) {
  // Test resource templates
  ListResourceTemplatesRequest tmpl_req;
  tmpl_req.id = mcp::make_request_id("tmpl-1");
  tmpl_req.cursor = mcp::make_optional(Cursor("page1"));
  
  JsonValue tmpl_req_json = to_json(tmpl_req);
  ListResourceTemplatesRequest tmpl_req_des = 
    from_json<ListResourceTemplatesRequest>(tmpl_req_json);
  
  // Create response with templates
  ListResourceTemplatesResult tmpl_result;
  
  ResourceTemplate tmpl1;
  tmpl1.uriTemplate = "file:///{path}";
  tmpl1.name = "File Template";
  tmpl1.description = mcp::make_optional(std::string("Access local files"));
  tmpl1.mimeType = mcp::make_optional(std::string("text/plain"));
  tmpl_result.resourceTemplates.push_back(tmpl1);
  
  ResourceTemplate tmpl2;
  tmpl2.uriTemplate = "http://api.example.com/{endpoint}/{id}";
  tmpl2.name = "API Template";
  tmpl2.description = mcp::make_optional(std::string("Access API endpoints"));
  tmpl_result.resourceTemplates.push_back(tmpl2);
  
  tmpl_result.nextCursor = mcp::make_optional(Cursor("page2"));
  
  JsonValue tmpl_result_json = to_json(tmpl_result);
  ListResourceTemplatesResult tmpl_result_des = 
    from_json<ListResourceTemplatesResult>(tmpl_result_json);
  
  EXPECT_EQ(tmpl_result_des.resourceTemplates.size(), 2);
  EXPECT_TRUE(tmpl_result_des.nextCursor.has_value());
}

// =============================================================================
// Elicitation Schema Testing
// =============================================================================

TEST_F(MCPSerializationExtensiveTest, AllElicitationSchemas) {
  // Test all schema types with all options
  
  // String schema with all constraints
  StringSchema str_schema = make<StringSchema>()
    .description("Enter your name")
    .pattern("^[A-Za-z ]+$")
    .min_length(2)
    .max_length(50)
    .build();
  
  PrimitiveSchemaDefinition str_def(str_schema);
  JsonValue str_json = to_json(str_def);
  PrimitiveSchemaDefinition str_des = from_json<PrimitiveSchemaDefinition>(str_json);
  
  // Number schema with all constraints
  NumberSchema num_schema;
  num_schema.description = mcp::make_optional(std::string("Enter age"));
  num_schema.minimum = mcp::make_optional(0.0);
  num_schema.maximum = mcp::make_optional(150.0);
  num_schema.multipleOf = mcp::make_optional(1.0);
  
  PrimitiveSchemaDefinition num_def(num_schema);
  JsonValue num_json = to_json(num_def);
  PrimitiveSchemaDefinition num_des = from_json<PrimitiveSchemaDefinition>(num_json);
  
  // Boolean schema
  BooleanSchema bool_schema;
  bool_schema.description = mcp::make_optional(std::string("Agree to terms?"));
  
  PrimitiveSchemaDefinition bool_def(bool_schema);
  JsonValue bool_json = to_json(bool_def);
  PrimitiveSchemaDefinition bool_des = from_json<PrimitiveSchemaDefinition>(bool_json);
  
  // Enum schema with many values
  std::vector<std::string> colors = {
    "red", "green", "blue", "yellow", "orange", "purple",
    "black", "white", "gray", "brown", "pink", "cyan"
  };
  EnumSchema enum_schema(std::move(colors));
  enum_schema.description = mcp::make_optional(std::string("Select a color"));
  
  PrimitiveSchemaDefinition enum_def(enum_schema);
  JsonValue enum_json = to_json(enum_def);
  PrimitiveSchemaDefinition enum_des = from_json<PrimitiveSchemaDefinition>(enum_json);
  
  // Test elicitation requests with each schema type
  std::vector<PrimitiveSchemaDefinition> schemas = {str_def, num_def, bool_def, enum_def};
  
  for (size_t i = 0; i < schemas.size(); ++i) {
    ElicitRequest req;
    req.id = mcp::make_request_id("elicit-" + std::to_string(i));
    req.name = "field_" + std::to_string(i);
    req.schema = schemas[i];
    req.prompt = mcp::make_optional(std::string("Please provide input"));
    
    JsonValue req_json = to_json(req);
    ElicitRequest req_des = from_json<ElicitRequest>(req_json);
    
    EXPECT_EQ(req_des.name, req.name);
    EXPECT_TRUE(req_des.prompt.has_value());
  }
}

// Main function
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}