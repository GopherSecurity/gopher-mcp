#include <gtest/gtest.h>
#include <mcp/types.h>

using namespace mcp;

// Test complex nested object building
TEST(ExtensiveBuildersTest, ComplexNestedInitializeRequest) {
  // Build a complex InitializeRequest with nested capabilities
  auto clientCaps = make<ClientCapabilities>()
                        .resources(true)
                        .tools(true)
                        .build();

  auto request = make<InitializeRequest>("2.0", clientCaps)
                     .clientInfo("test-client", "1.0.0")
                     .build();

  EXPECT_EQ(request.protocolVersion, "2.0");
  ASSERT_TRUE(request.clientInfo.has_value());
  EXPECT_EQ(request.clientInfo->name, "test-client");
  EXPECT_EQ(request.clientInfo->version, "1.0.0");
  ASSERT_TRUE(request.capabilities.experimental.has_value());
}

TEST(ExtensiveBuildersTest, ComplexCreateMessageRequest) {
  // Build complex nested CreateMessageRequest with all options
  auto modelPrefs = make<ModelPreferences>()
                        .add_hint("gpt-4")
                        .add_hint("claude-3")
                        .cost_priority(0.3)
                        .speed_priority(0.5)
                        .intelligence_priority(0.2)
                        .build();

  auto samplingParams = make<SamplingParams>()
                            .temperature(0.7)
                            .maxTokens(2000)
                            .stopSequence("</response>")
                            .stopSequence("END")
                            .build();

  auto request = make<CreateMessageRequest>()
                     .add_user_message("Complex message with annotations")
                     .modelPreferences(modelPrefs)
                     .systemPrompt("You are a helpful assistant")
                     .temperature(0.8)
                     .maxTokens(1500)
                     .build();

  EXPECT_EQ(request.messages.size(), 1);
  EXPECT_EQ(request.messages[0].role, enums::Role::USER);
  auto* msgContent = std::get_if<TextContent>(&request.messages[0].content);
  ASSERT_NE(msgContent, nullptr);
  EXPECT_EQ(msgContent->text, "Complex message with annotations");
  ASSERT_TRUE(request.modelPreferences.has_value());
  ASSERT_TRUE(request.modelPreferences->hints.has_value());
  EXPECT_EQ(request.modelPreferences->hints->size(), 2);
  ASSERT_TRUE(request.systemPrompt.has_value());
  EXPECT_EQ(request.systemPrompt.value(), "You are a helpful assistant");
  ASSERT_TRUE(request.temperature.has_value());
  EXPECT_EQ(request.temperature.value(), 0.8);
}

TEST(ExtensiveBuildersTest, RecursiveResourceBuilding) {
  // Build resources with embedded resources (recursive structure)
  auto baseResource = make<Resource>("file:///base.txt", "base")
                          .description("Base resource")
                          .mimeType("text/plain")
                          .build();

  auto embeddedResource1 = make<EmbeddedResource>(baseResource)
                               .build();

  // Create a resource with a link to another resource
  auto linkedResource = make<ResourceLink>("file:///linked.txt", "linked-file")
                            .description("A linked resource")
                            .mimeType("application/json")
                            .build();

  EXPECT_EQ(embeddedResource1.resource.uri, "file:///base.txt");
  EXPECT_EQ(embeddedResource1.resource.name, "base");
  EXPECT_EQ(linkedResource.uri, "file:///linked.txt");
  EXPECT_EQ(linkedResource.name, "linked-file");
}

TEST(ExtensiveBuildersTest, ComplexToolWithNestedSchemas) {
  // Build a complex tool with nested input schemas
  auto numberSchema = make<NumberSchema>()
                          .description("A number between 0 and 100")
                          .minimum(0)
                          .maximum(100)
                          .multipleOf(5)
                          .build();

  auto enumSchema = make<EnumSchema>(std::vector<std::string>{"option1", "option2", "option3"})
                        .description("Select an option")
                        .addValue("option4")
                        .build();

  auto stringSchema = make<StringSchema>()
                          .description("A string field")
                          .minLength(1)
                          .maxLength(100)
                          .pattern("^[a-zA-Z0-9]+$")
                          .build();

  // Create tool with complex parameter structure
  auto tool = make<Tool>("complex-calculator")
                  .description("A calculator with multiple parameter types")
                  .parameter("number_input", "number", "A numeric input", true)
                  .parameter("string_input", "string", "A text input", true)
                  .parameter("enum_input", "string", "Select from options", false)
                  .parameter("optional_param", "boolean", "Optional boolean", false)
                  .build();

  EXPECT_EQ(tool.name, "complex-calculator");
  ASSERT_TRUE(tool.description.has_value());
  ASSERT_TRUE(tool.parameters.has_value());
  EXPECT_EQ(tool.parameters->size(), 4);
  EXPECT_TRUE(tool.parameters->at(0).required);
  EXPECT_FALSE(tool.parameters->at(3).required);
}

TEST(ExtensiveBuildersTest, ComplexPromptWithMultipleMessages) {
  // Build complex prompt with multiple messages and arguments
  auto promptArg1 = PromptArgument{"name", "The user's name", true};
  auto promptArg2 = PromptArgument{"language", "Preferred language", false};
  auto promptArg3 = PromptArgument{"style", "Response style", false};

  auto prompt = make<Prompt>("multi-step-prompt")
                    .description("A complex multi-step prompt")
                    .argument("user_id", "User identifier", true)
                    .argument("context", "Additional context", false)
                    .argument("format", "Output format", false)
                    .build();

  EXPECT_EQ(prompt.name, "multi-step-prompt");
  ASSERT_TRUE(prompt.description.has_value());
  ASSERT_TRUE(prompt.arguments.has_value());
  EXPECT_EQ(prompt.arguments->size(), 3);
}

TEST(ExtensiveBuildersTest, ComplexGetPromptResult) {
  // Build complex GetPromptResult with multiple messages
  auto result = make<GetPromptResult>()
                    .description("Multi-turn conversation prompt")
                    .addUserMessage("Hello, I need help with a complex task")
                    .addAssistantMessage("I'll help you with that. What specific aspect?")
                    .addUserMessage("I need to process multiple files")
                    .addAssistantMessage("Let me help you with file processing")
                    .build();

  ASSERT_TRUE(result.description.has_value());
  EXPECT_EQ(result.description.value(), "Multi-turn conversation prompt");
  EXPECT_EQ(result.messages.size(), 4);
  EXPECT_EQ(result.messages[0].role, enums::Role::USER);
  EXPECT_EQ(result.messages[1].role, enums::Role::ASSISTANT);
}

TEST(ExtensiveBuildersTest, ComplexListResourcesResult) {
  // Build complex paginated result with multiple resources
  std::vector<Resource> resources;
  for (int i = 0; i < 10; ++i) {
    auto resource = make<Resource>(
                        "file:///resource" + std::to_string(i) + ".txt",
                        "resource-" + std::to_string(i))
                        .description("Resource #" + std::to_string(i))
                        .mimeType(i % 2 == 0 ? "text/plain" : "application/json")
                        .build();
    resources.push_back(resource);
  }

  auto result = make<ListResourcesResult>()
                    .nextCursor("page-2-token");
  
  for (const auto& res : resources) {
    result.add(res);
  }
  
  auto finalResult = result.build();

  EXPECT_EQ(finalResult.resources.size(), 10);
  ASSERT_TRUE(finalResult.nextCursor.has_value());
  EXPECT_EQ(finalResult.nextCursor.value(), "page-2-token");
  
  // Verify alternating mime types
  for (size_t i = 0; i < finalResult.resources.size(); ++i) {
    ASSERT_TRUE(finalResult.resources[i].mimeType.has_value());
    if (i % 2 == 0) {
      EXPECT_EQ(finalResult.resources[i].mimeType.value(), "text/plain");
    } else {
      EXPECT_EQ(finalResult.resources[i].mimeType.value(), "application/json");
    }
  }
}

TEST(ExtensiveBuildersTest, ComplexCallToolResult) {
  // Build complex tool result with multiple content types
  auto result = make<CallToolResult>()
                    .addText("Processing started...")
                    .addText("Step 1: Analyzing input")
                    .addImage("resultImageData", "image/jpeg")
                    .addText("Step 2: Generated visualization")
                    .addAudio("audioData", "audio/mp3")
                    .addText("Processing complete!")
                    .isError(false)
                    .build();

  EXPECT_EQ(result.content.size(), 6);
  EXPECT_FALSE(result.isError);

  // Verify we have multiple content items
  auto* text1 = std::get_if<TextContent>(&result.content[0]);
  ASSERT_NE(text1, nullptr);
  EXPECT_EQ(text1->text, "Processing started...");
}

TEST(ExtensiveBuildersTest, ComplexReadResourceResult) {
  // Build complex resource read result with mixed content types
  auto result = make<ReadResourceResult>();

  // Add multiple text contents
  for (int i = 0; i < 3; ++i) {
    result.addText("Text content #" + std::to_string(i));
  }

  // Add blob contents
  for (int i = 0; i < 2; ++i) {
    result.addBlob("Binary data #" + std::to_string(i));
  }

  auto finalResult = result.build();

  EXPECT_EQ(finalResult.contents.size(), 5);

  // Verify first 3 are text
  for (int i = 0; i < 3; ++i) {
    auto* textContent = std::get_if<TextResourceContents>(&finalResult.contents[i]);
    ASSERT_NE(textContent, nullptr);
    EXPECT_EQ(textContent->text, "Text content #" + std::to_string(i));
  }

  // Verify last 2 are blobs
  for (int i = 0; i < 2; ++i) {
    auto* blobContent = std::get_if<BlobResourceContents>(&finalResult.contents[3 + i]);
    ASSERT_NE(blobContent, nullptr);
    EXPECT_EQ(blobContent->blob, "Binary data #" + std::to_string(i));
  }
}

TEST(ExtensiveBuildersTest, ComplexCompleteResult) {
  // Build complex completion result with many options
  std::vector<std::string> completions = {
      "completion_option_1",
      "completion_option_2",
      "completion_option_3",
      "advanced_option_1",
      "advanced_option_2",
      "super_advanced_option"
  };

  auto result = make<CompleteResult>()
                    .total(100.0)
                    .hasMore(true);

  for (const auto& completion : completions) {
    result.addValue(completion);
  }

  auto finalResult = result.build();

  EXPECT_EQ(finalResult.completion.values.size(), 6);
  ASSERT_TRUE(finalResult.completion.total.has_value());
  EXPECT_EQ(finalResult.completion.total.value(), 100.0);
  EXPECT_TRUE(finalResult.completion.hasMore);

  // Verify all completions are present
  for (size_t i = 0; i < completions.size(); ++i) {
    EXPECT_EQ(finalResult.completion.values[i], completions[i]);
  }
}

TEST(ExtensiveBuildersTest, ComplexInitializeResult) {
  // Build complex server initialization result
  auto serverCaps = make<ServerCapabilities>()
                        .resources(true)
                        .tools(true)
                        .prompts(true)
                        .logging(true)
                        .build();

  auto result = make<InitializeResult>("2.0", serverCaps)
                    .serverInfo("super-server", "3.0.0")
                    .instructions("Welcome to Super Server! Available commands: /help, /status")
                    .build();

  EXPECT_EQ(result.protocolVersion, "2.0");
  ASSERT_TRUE(result.serverInfo.has_value());
  EXPECT_EQ(result.serverInfo->name, "super-server");
  EXPECT_EQ(result.serverInfo->version, "3.0.0");
  ASSERT_TRUE(result.instructions.has_value());
  EXPECT_EQ(result.instructions.value(), 
            "Welcome to Super Server! Available commands: /help, /status");
  
  // Verify capabilities
  ASSERT_TRUE(result.capabilities.resources.has_value());
  ASSERT_TRUE(result.capabilities.tools.has_value());
  ASSERT_TRUE(result.capabilities.prompts.has_value());
  ASSERT_TRUE(result.capabilities.logging.has_value());
}

TEST(ExtensiveBuildersTest, ComplexNestedAnnotations) {
  // Build complex content with nested annotations
  auto annotations = make<Annotations>()
                         .audience({enums::Role::USER, 
                                   enums::Role::ASSISTANT})
                         .priority(0.95)
                         .build();

  auto textContent = make<TextContent>("Critical system message")
                         .annotations(annotations)
                         .build();

  EXPECT_EQ(textContent.text, "Critical system message");
  ASSERT_TRUE(textContent.annotations.has_value());
  ASSERT_TRUE(textContent.annotations->audience.has_value());
  EXPECT_EQ(textContent.annotations->audience->size(), 2);
  ASSERT_TRUE(textContent.annotations->priority.has_value());
  EXPECT_EQ(textContent.annotations->priority.value(), 0.95);
}

TEST(ExtensiveBuildersTest, ComplexResourceTemplate) {
  // Build complex resource template with all fields
  auto tmpl = make<ResourceTemplate>("file:///template/{param}", 
                                     "template-{param}")
                  .description("A parameterized resource template")
                  .mimeType("text/plain")
                  .build();

  EXPECT_EQ(tmpl.uriTemplate, "file:///template/{param}");
  EXPECT_EQ(tmpl.name, "template-{param}");
  ASSERT_TRUE(tmpl.description.has_value());
  EXPECT_EQ(tmpl.description.value(), "A parameterized resource template");
  ASSERT_TRUE(tmpl.mimeType.has_value());
  EXPECT_EQ(tmpl.mimeType.value(), "text/plain");
}

TEST(ExtensiveBuildersTest, ComplexListPromptsResult) {
  // Build complex prompts list with multiple prompts
  auto prompt1 = make<Prompt>("greeting")
                     .description("Greeting prompt")
                     .argument("name", "User's name", true)
                     .argument("language", "Language", false)
                     .build();

  auto prompt2 = make<Prompt>("farewell")
                     .description("Farewell prompt")
                     .argument("name", "User's name", true)
                     .build();

  auto prompt3 = make<Prompt>("help")
                     .description("Help prompt")
                     .build();

  auto result = make<ListPromptsResult>()
                    .add(prompt1)
                    .add(prompt2)
                    .add(prompt3)
                    .nextCursor("more-prompts-token")
                    .build();

  EXPECT_EQ(result.prompts.size(), 3);
  EXPECT_EQ(result.prompts[0].name, "greeting");
  EXPECT_EQ(result.prompts[1].name, "farewell");
  EXPECT_EQ(result.prompts[2].name, "help");
  ASSERT_TRUE(result.nextCursor.has_value());
  EXPECT_EQ(result.nextCursor.value(), "more-prompts-token");
}

TEST(ExtensiveBuildersTest, ComplexLogMessage) {
  // Build complex logging message with metadata
  Metadata logData;
  logData["timestamp"] = "2024-01-01T12:00:00Z";
  logData["module"] = "core";
  logData["line"] = int64_t(42);
  logData["severity"] = 0.8;
  logData["error"] = false;

  auto notification = make<LoggingMessageNotification>(enums::LoggingLevel::ERROR)
                          .logger("system.core")
                          .data(logData)
                          .build();

  EXPECT_EQ(notification.level, enums::LoggingLevel::ERROR);
  ASSERT_TRUE(notification.logger.has_value());
  EXPECT_EQ(notification.logger.value(), "system.core");
  
  auto* metadata = std::get_if<Metadata>(&notification.data);
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->size(), 5);
  
  auto* timestamp = std::get_if<std::string>(&(*metadata)["timestamp"]);
  ASSERT_NE(timestamp, nullptr);
  EXPECT_EQ(*timestamp, "2024-01-01T12:00:00Z");
  
  auto* line = std::get_if<int64_t>(&(*metadata)["line"]);
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(*line, 42);
}

TEST(ExtensiveBuildersTest, ComplexProgressNotification) {
  // Build complex progress notification
  auto notification = make<ProgressNotification>("task-12345", 0.75)
                          .total(1.0)
                          .build();

  auto* token = std::get_if<std::string>(&notification.progressToken);
  ASSERT_NE(token, nullptr);
  EXPECT_EQ(*token, "task-12345");
  EXPECT_EQ(notification.progress, 0.75);
  ASSERT_TRUE(notification.total.has_value());
  EXPECT_EQ(notification.total.value(), 1.0);
}

TEST(ExtensiveBuildersTest, ComplexElicitRequest) {
  // Build complex elicit request with nested schema
  auto schema = PrimitiveSchemaDefinition(
      make<StringSchema>()
          .description("Enter your email")
          .minLength(5)
          .maxLength(100)
          .pattern("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$")
          .build()
  );

  auto request = make<ElicitRequest>("email-input", schema)
                     .prompt("Please enter your email address:")
                     .build();

  EXPECT_EQ(request.name, "email-input");
  ASSERT_TRUE(request.prompt.has_value());
  EXPECT_EQ(request.prompt.value(), "Please enter your email address:");
  
  auto* stringSchema = std::get_if<StringSchema>(&request.schema);
  ASSERT_NE(stringSchema, nullptr);
  ASSERT_TRUE(stringSchema->pattern.has_value());
}

TEST(ExtensiveBuildersTest, ChainedBuilderOperations) {
  // Test extensive chaining of builder operations
  auto tool = make<Tool>("mega-tool")
                  .description("A tool with many parameters")
                  .parameter("p1", "string", "Parameter 1", true)
                  .parameter("p2", "number", "Parameter 2", true)
                  .parameter("p3", "boolean", "Parameter 3", false)
                  .parameter("p4", "array", "Parameter 4", false)
                  .parameter("p5", "object", "Parameter 5", false)
                  .parameter("p6", "string", "Parameter 6", true)
                  .parameter("p7", "number", "Parameter 7", false)
                  .parameter("p8", "boolean", "Parameter 8", false)
                  .parameter("p9", "string", "Parameter 9", true)
                  .parameter("p10", "any", "Parameter 10", false)
                  .build();

  EXPECT_EQ(tool.name, "mega-tool");
  ASSERT_TRUE(tool.parameters.has_value());
  EXPECT_EQ(tool.parameters->size(), 10);
  
  // Count required vs optional
  int requiredCount = 0;
  for (const auto& param : *tool.parameters) {
    if (param.required) requiredCount++;
  }
  EXPECT_EQ(requiredCount, 4); // p1, p2, p6, p9 are required
}

TEST(ExtensiveBuildersTest, BuilderCopyAndMoveSemantics) {
  // Test that builders properly handle copy and move semantics
  auto builder1 = make<Resource>("file:///test.txt", "test");
  builder1.description("Original description");
  
  // Copy builder
  auto builder2 = builder1;
  builder2.description("Modified description");
  
  // Build from both
  auto resource1 = builder1.build();
  auto resource2 = builder2.build();
  
  // Original should keep its description
  ASSERT_TRUE(resource1.description.has_value());
  EXPECT_EQ(resource1.description.value(), "Original description");
  
  // Copy should have modified description
  ASSERT_TRUE(resource2.description.has_value());
  EXPECT_EQ(resource2.description.value(), "Modified description");
  
  // Test move semantics
  auto builder3 = make<Tool>("tool1");
  builder3.description("Tool description")
           .parameter("param1", "string", "Parameter 1", true);
  
  auto tool = std::move(builder3).build();
  EXPECT_EQ(tool.name, "tool1");
  ASSERT_TRUE(tool.description.has_value());
  EXPECT_EQ(tool.description.value(), "Tool description");
}

TEST(ExtensiveBuildersTest, MaximallyComplexMessage) {
  // Create the most complex message structure possible
  auto annotations = make<Annotations>()
                         .audience({enums::Role::USER, enums::Role::ASSISTANT})
                         .priority(1.0)
                         .build();

  auto textContent = make<TextContent>("Complex message with all features")
                         .annotations(annotations)
                         .build();

  auto message = make<Message>(enums::Role::ASSISTANT)
                     .content(textContent)
                     .build();

  EXPECT_EQ(message.role, enums::Role::ASSISTANT);
  
  auto* text = std::get_if<TextContent>(&message.content);
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text->text, "Complex message with all features");
  ASSERT_TRUE(text->annotations.has_value());
  ASSERT_TRUE(text->annotations->priority.has_value());
  EXPECT_EQ(text->annotations->priority.value(), 1.0);
}