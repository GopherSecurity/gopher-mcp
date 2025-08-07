#include <gtest/gtest.h>
#include <mcp/types.h>  // This includes builders.h at the end

using namespace mcp;

TEST(BuildersTest, ResourceBuilder) {
  auto resource = make<Resource>("file:///path/to/resource")
                      .name("my-resource")
                      .description("A test resource")
                      .mimeType("text/plain")
                      .build();

  EXPECT_EQ(resource.uri, "file:///path/to/resource");
  EXPECT_EQ(resource.name, "my-resource");
  ASSERT_TRUE(resource.description.has_value());
  EXPECT_EQ(resource.description.value(), "A test resource");
  ASSERT_TRUE(resource.mimeType.has_value());
  EXPECT_EQ(resource.mimeType.value(), "text/plain");
}

TEST(BuildersTest, ResourceBuilderWithNameConstructor) {
  auto resource = ResourceBuilder("file:///test.txt", "test-file").build();

  EXPECT_EQ(resource.uri, "file:///test.txt");
  EXPECT_EQ(resource.name, "test-file");
}

TEST(BuildersTest, ToolBuilder) {
  ToolInputSchema inputSchema;
  auto tool = make<Tool>("calculator")
                  .description("Performs calculations")
                  .inputSchema(inputSchema)
                  .parameter("x", "number", "First operand", true)
                  .parameter("y", "number", "Second operand", true)
                  .build();

  EXPECT_EQ(tool.name, "calculator");
  ASSERT_TRUE(tool.description.has_value());
  EXPECT_EQ(tool.description.value(), "Performs calculations");
  ASSERT_TRUE(tool.inputSchema.has_value());
  ASSERT_TRUE(tool.parameters.has_value());
  EXPECT_EQ(tool.parameters->size(), 2);
}

TEST(BuildersTest, PromptBuilder) {
  auto prompt = make<Prompt>("greeting")
                    .description("A friendly greeting")
                    .argument("name", "The person's name", true)
                    .argument("language", "Language for greeting", false)
                    .build();

  EXPECT_EQ(prompt.name, "greeting");
  ASSERT_TRUE(prompt.description.has_value());
  EXPECT_EQ(prompt.description.value(), "A friendly greeting");
  ASSERT_TRUE(prompt.arguments.has_value());
  EXPECT_EQ(prompt.arguments->size(), 2);
  EXPECT_EQ(prompt.arguments->at(0).name, "name");
  EXPECT_TRUE(prompt.arguments->at(0).required);
  EXPECT_EQ(prompt.arguments->at(1).name, "language");
  EXPECT_FALSE(prompt.arguments->at(1).required);
}

TEST(BuildersTest, TextContentBuilder) {
  auto content = make<TextContent>("Hello, World!")
                     .audience({enums::Role::USER, enums::Role::ASSISTANT})
                     .priority(0.8)
                     .build();

  EXPECT_EQ(content.text, "Hello, World!");
  ASSERT_TRUE(content.annotations.has_value());
  ASSERT_TRUE(content.annotations->audience.has_value());
  EXPECT_EQ(content.annotations->audience->size(), 2);
  ASSERT_TRUE(content.annotations->priority.has_value());
  EXPECT_EQ(content.annotations->priority.value(), 0.8);
}

TEST(BuildersTest, ImageContentBuilder) {
  auto content = make<ImageContent>("base64data", "image/png").build();

  EXPECT_EQ(content.data, "base64data");
  EXPECT_EQ(content.mimeType, "image/png");
}

TEST(BuildersTest, AudioContentBuilder) {
  auto content = make<AudioContent>("audiodata", "audio/wav").build();

  EXPECT_EQ(content.data, "audiodata");
  EXPECT_EQ(content.mimeType, "audio/wav");
}

TEST(BuildersTest, MessageBuilder) {
  auto message = make<Message>(enums::Role::USER).text("Hello").build();

  EXPECT_EQ(message.role, enums::Role::USER);
  auto* textContent = mcp::get_if<TextContent>(&message.content);
  ASSERT_NE(textContent, nullptr);
  EXPECT_EQ(textContent->text, "Hello");
}

TEST(BuildersTest, MessageBuilderWithImage) {
  auto message =
      make<Message>(enums::Role::ASSISTANT).image("data", "image/jpeg").build();

  EXPECT_EQ(message.role, enums::Role::ASSISTANT);
  auto* imageContent = mcp::get_if<ImageContent>(&message.content);
  ASSERT_NE(imageContent, nullptr);
  EXPECT_EQ(imageContent->data, "data");
  EXPECT_EQ(imageContent->mimeType, "image/jpeg");
}

TEST(BuildersTest, ErrorBuilder) {
  auto error = make<Error>(404, "Not Found").data("resource-id-123").build();

  EXPECT_EQ(error.code, 404);
  EXPECT_EQ(error.message, "Not Found");
  ASSERT_TRUE(error.data.has_value());
  auto* stringData = mcp::get_if<std::string>(&error.data.value());
  ASSERT_NE(stringData, nullptr);
  EXPECT_EQ(*stringData, "resource-id-123");
}

TEST(BuildersTest, InitializeRequestBuilder) {
  ClientCapabilities capabilities;
  auto request = make<InitializeRequest>("1.0", capabilities)
                     .clientInfo("test-client", "1.0.0")
                     .build();

  EXPECT_EQ(request.protocolVersion, "1.0");
  ASSERT_TRUE(request.clientInfo.has_value());
  EXPECT_EQ(request.clientInfo->name, "test-client");
  EXPECT_EQ(request.clientInfo->version, "1.0.0");
}

TEST(BuildersTest, InitializeResultBuilder) {
  ServerCapabilities capabilities;
  auto result = make<InitializeResult>("1.0", capabilities)
                    .serverInfo("test-server", "2.0.0")
                    .instructions("Welcome to the server")
                    .build();

  EXPECT_EQ(result.protocolVersion, "1.0");
  ASSERT_TRUE(result.serverInfo.has_value());
  EXPECT_EQ(result.serverInfo->name, "test-server");
  EXPECT_EQ(result.serverInfo->version, "2.0.0");
  ASSERT_TRUE(result.instructions.has_value());
  EXPECT_EQ(result.instructions.value(), "Welcome to the server");
}

TEST(BuildersTest, CallToolRequestBuilder) {
  auto request = make<CallToolRequest>("calculator")
                     .argument("operation", "add")
                     .argument("x", 5)
                     .argument("y", 3)
                     .build();

  EXPECT_EQ(request.name, "calculator");
  ASSERT_TRUE(request.arguments.has_value());
}

TEST(BuildersTest, CallToolResultBuilder) {
  auto result = make<CallToolResult>()
                    .addText("Result: 42")
                    .isError(false)
                    .build();

  EXPECT_EQ(result.content.size(), 1);
  EXPECT_FALSE(result.isError);
  auto* textContent = mcp::get_if<TextContent>(&result.content[0]);
  ASSERT_NE(textContent, nullptr);
  EXPECT_EQ(textContent->text, "Result: 42");
}

TEST(BuildersTest, GetPromptRequestBuilder) {
  auto request = make<GetPromptRequest>("greeting")
                     .argument("name", "Alice")
                     .argument("language", "en")
                     .build();

  EXPECT_EQ(request.name, "greeting");
  ASSERT_TRUE(request.arguments.has_value());
}

TEST(BuildersTest, GetPromptResultBuilder) {
  auto result = make<GetPromptResult>()
                    .description("A greeting prompt")
                    .addUserMessage("Hello")
                    .addAssistantMessage("Hi there!")
                    .build();

  ASSERT_TRUE(result.description.has_value());
  EXPECT_EQ(result.description.value(), "A greeting prompt");
  EXPECT_EQ(result.messages.size(), 2);
  EXPECT_EQ(result.messages[0].role, enums::Role::USER);
  EXPECT_EQ(result.messages[1].role, enums::Role::ASSISTANT);
}

TEST(BuildersTest, CreateMessageResultBuilder) {
  auto result = make<CreateMessageResult>(enums::Role::ASSISTANT, "gpt-4")
                    .text("Generated response")
                    .stopReason("end_turn")
                    .build();

  EXPECT_EQ(result.role, enums::Role::ASSISTANT);
  EXPECT_EQ(result.model, "gpt-4");
  auto* textContent = mcp::get_if<TextContent>(&result.content);
  ASSERT_NE(textContent, nullptr);
  EXPECT_EQ(textContent->text, "Generated response");
  ASSERT_TRUE(result.stopReason.has_value());
  EXPECT_EQ(result.stopReason.value(), "end_turn");
}

TEST(BuildersTest, ListResourcesResultBuilder) {
  auto resource1 = make<Resource>("file:///1.txt").name("file1").build();
  auto resource2 = make<Resource>("file:///2.txt").name("file2").build();
  
  auto result = make<ListResourcesResult>()
                    .add(resource1)
                    .add(resource2)
                    .nextCursor("next-page")
                    .build();

  EXPECT_EQ(result.resources.size(), 2);
  EXPECT_EQ(result.resources[0].uri, "file:///1.txt");
  EXPECT_EQ(result.resources[1].uri, "file:///2.txt");
  ASSERT_TRUE(result.nextCursor.has_value());
  EXPECT_EQ(result.nextCursor.value(), "next-page");
}

TEST(BuildersTest, ListToolsResultBuilder) {
  auto tool1 = make<Tool>("tool1").build();
  auto tool2 = make<Tool>("tool2").build();
  
  auto result = make<ListToolsResult>()
                    .add(tool1)
                    .add(tool2)
                    .build();

  EXPECT_EQ(result.tools.size(), 2);
  EXPECT_EQ(result.tools[0].name, "tool1");
  EXPECT_EQ(result.tools[1].name, "tool2");
}

TEST(BuildersTest, ReadResourceResultBuilder) {
  auto result = make<ReadResourceResult>()
                    .addText("Text content")
                    .addBlob("Binary content")
                    .build();

  EXPECT_EQ(result.contents.size(), 2);
  auto* textContent = mcp::get_if<TextResourceContents>(&result.contents[0]);
  ASSERT_NE(textContent, nullptr);
  EXPECT_EQ(textContent->text, "Text content");
  auto* blobContent = mcp::get_if<BlobResourceContents>(&result.contents[1]);
  ASSERT_NE(blobContent, nullptr);
  EXPECT_EQ(blobContent->blob, "Binary content");
}

TEST(BuildersTest, CompleteRequestBuilder) {
  auto request = make<CompleteRequest>("prompt", "greeting")
                     .argument("partial-input")
                     .build();

  EXPECT_EQ(request.ref.type, "prompt");
  EXPECT_EQ(request.ref.name, "greeting");
  ASSERT_TRUE(request.argument.has_value());
  EXPECT_EQ(request.argument.value(), "partial-input");
}

TEST(BuildersTest, CompleteResultBuilder) {
  auto result = make<CompleteResult>()
                    .addValue("option1")
                    .addValue("option2")
                    .total(10.0)
                    .hasMore(true)
                    .build();

  EXPECT_EQ(result.completion.values.size(), 2);
  EXPECT_EQ(result.completion.values[0], "option1");
  EXPECT_EQ(result.completion.values[1], "option2");
  ASSERT_TRUE(result.completion.total.has_value());
  EXPECT_EQ(result.completion.total.value(), 10.0);
  EXPECT_TRUE(result.completion.hasMore);
}

TEST(BuildersTest, LoggingMessageNotificationBuilder) {
  auto notification = make<LoggingMessageNotification>(enums::LoggingLevel::INFO)
                          .logger("test-logger")
                          .data("Log message")
                          .build();

  EXPECT_EQ(notification.level, enums::LoggingLevel::INFO);
  ASSERT_TRUE(notification.logger.has_value());
  EXPECT_EQ(notification.logger.value(), "test-logger");
  auto* stringData = mcp::get_if<std::string>(&notification.data);
  ASSERT_NE(stringData, nullptr);
  EXPECT_EQ(*stringData, "Log message");
}

TEST(BuildersTest, ProgressNotificationBuilder) {
  auto notification = make<ProgressNotification>("token-123", 0.5)
                          .total(1.0)
                          .build();

  auto* stringToken = mcp::get_if<std::string>(&notification.progressToken);
  ASSERT_NE(stringToken, nullptr);
  EXPECT_EQ(*stringToken, "token-123");
  EXPECT_EQ(notification.progress, 0.5);
  ASSERT_TRUE(notification.total.has_value());
  EXPECT_EQ(notification.total.value(), 1.0);
}

TEST(BuildersTest, CancelledNotificationBuilder) {
  auto notification = make<CancelledNotification>("request-123")
                          .reason("User cancelled")
                          .build();

  auto* stringId = mcp::get_if<std::string>(&notification.requestId);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(*stringId, "request-123");
  ASSERT_TRUE(notification.reason.has_value());
  EXPECT_EQ(notification.reason.value(), "User cancelled");
}

TEST(BuildersTest, ResourceUpdatedNotificationBuilder) {
  auto notification =
      make<ResourceUpdatedNotification>("file:///updated.txt").build();

  EXPECT_EQ(notification.uri, "file:///updated.txt");
}

TEST(BuildersTest, NumberSchemaBuilder) {
  auto schema = make<NumberSchema>()
                    .description("A number field")
                    .minimum(0)
                    .maximum(100)
                    .multipleOf(5)
                    .build();

  ASSERT_TRUE(schema.description.has_value());
  EXPECT_EQ(schema.description.value(), "A number field");
  ASSERT_TRUE(schema.minimum.has_value());
  EXPECT_EQ(schema.minimum.value(), 0);
  ASSERT_TRUE(schema.maximum.has_value());
  EXPECT_EQ(schema.maximum.value(), 100);
  ASSERT_TRUE(schema.multipleOf.has_value());
  EXPECT_EQ(schema.multipleOf.value(), 5);
}

TEST(BuildersTest, BooleanSchemaBuilder) {
  auto schema = make<BooleanSchema>()
                    .description("A boolean field")
                    .build();

  ASSERT_TRUE(schema.description.has_value());
  EXPECT_EQ(schema.description.value(), "A boolean field");
}

TEST(BuildersTest, EnumSchemaBuilder) {
  auto schema = make<EnumSchema>(std::vector<std::string>{"option1", "option2"})
                    .description("An enum field")
                    .addValue("option3")
                    .build();

  ASSERT_TRUE(schema.description.has_value());
  EXPECT_EQ(schema.description.value(), "An enum field");
  EXPECT_EQ(schema.values.size(), 3);
  EXPECT_EQ(schema.values[0], "option1");
  EXPECT_EQ(schema.values[1], "option2");
  EXPECT_EQ(schema.values[2], "option3");
}

TEST(BuildersTest, ElicitRequestBuilder) {
  auto schema = PrimitiveSchemaDefinition(StringSchema());
  auto request = make<ElicitRequest>("user-input", schema)
                     .prompt("Please enter your name:")
                     .build();

  EXPECT_EQ(request.name, "user-input");
  ASSERT_TRUE(request.prompt.has_value());
  EXPECT_EQ(request.prompt.value(), "Please enter your name:");
}

TEST(BuildersTest, ElicitResultBuilder) {
  auto result = make<ElicitResult>().value("user response").build();

  auto* stringValue = mcp::get_if<std::string>(&result.value);
  ASSERT_NE(stringValue, nullptr);
  EXPECT_EQ(*stringValue, "user response");
}

TEST(BuildersTest, BuilderImplicitConversion) {
  TextContent content = make<TextContent>("Hello").build();
  EXPECT_EQ(content.text, "Hello");

  Resource resource = make<Resource>("file:///test.txt")
                          .name("test")
                          .build();
  EXPECT_EQ(resource.uri, "file:///test.txt");
  EXPECT_EQ(resource.name, "test");
}

TEST(BuildersTest, BuilderMoveSemantics) {
  auto builder = make<Resource>("file:///test.txt");
  builder.name("test").description("A test file");
  
  Resource resource = std::move(builder).build();
  
  EXPECT_EQ(resource.uri, "file:///test.txt");
  EXPECT_EQ(resource.name, "test");
  ASSERT_TRUE(resource.description.has_value());
  EXPECT_EQ(resource.description.value(), "A test file");
}

TEST(BuildersTest, BuilderCopySemantics) {
  auto builder = make<Resource>("file:///test.txt");
  builder.name("test");
  
  Resource resource1 = builder.build();
  Resource resource2 = builder.build();
  
  EXPECT_EQ(resource1.uri, resource2.uri);
  EXPECT_EQ(resource1.name, resource2.name);
}

TEST(BuildersTest, SamplingParamsBuilder) {
  auto params = make<SamplingParams>()
                    .temperature(0.7)
                    .maxTokens(100)
                    .stopSequence("\\n")
                    .build();
  
  ASSERT_TRUE(params.temperature.has_value());
  EXPECT_EQ(params.temperature.value(), 0.7);
  ASSERT_TRUE(params.maxTokens.has_value());
  EXPECT_EQ(params.maxTokens.value(), 100);
  ASSERT_TRUE(params.stopSequences.has_value());
  EXPECT_EQ(params.stopSequences->size(), 1);
  EXPECT_EQ(params.stopSequences->at(0), "\\n");
}

TEST(BuildersTest, ModelPreferencesBuilder) {
  auto prefs = make<ModelPreferences>()
                   .add_hint("gpt-4")
                   .add_hint("claude-3")
                   .cost_priority(0.3)
                   .speed_priority(0.5)
                   .intelligence_priority(0.2)
                   .build();
  
  ASSERT_TRUE(prefs.hints.has_value());
  EXPECT_EQ(prefs.hints->size(), 2);
  ASSERT_TRUE(prefs.costPriority.has_value());
  EXPECT_EQ(prefs.costPriority.value(), 0.3);
  ASSERT_TRUE(prefs.speedPriority.has_value());
  EXPECT_EQ(prefs.speedPriority.value(), 0.5);
  ASSERT_TRUE(prefs.intelligencePriority.has_value());
  EXPECT_EQ(prefs.intelligencePriority.value(), 0.2);
}

TEST(BuildersTest, ClientCapabilitiesBuilder) {
  auto caps = make<ClientCapabilities>()
                  .resources(true)
                  .tools(true)
                  .build();
  
  ASSERT_TRUE(caps.experimental.has_value());
}

TEST(BuildersTest, ServerCapabilitiesBuilder) {
  auto caps = make<ServerCapabilities>()
                  .resources(true)
                  .tools(true)
                  .prompts(true)
                  .logging(false)
                  .build();
  
  ASSERT_TRUE(caps.resources.has_value());
  ASSERT_TRUE(caps.tools.has_value());
  EXPECT_TRUE(caps.tools.value());
  ASSERT_TRUE(caps.prompts.has_value());
  EXPECT_TRUE(caps.prompts.value());
  ASSERT_TRUE(caps.logging.has_value());
  EXPECT_FALSE(caps.logging.value());
}

TEST(BuildersTest, AnnotationsBuilder) {
  auto annotations = make<Annotations>()
                         .audience({enums::Role::USER, enums::Role::ASSISTANT})
                         .priority(0.9)
                         .build();
  
  ASSERT_TRUE(annotations.audience.has_value());
  EXPECT_EQ(annotations.audience->size(), 2);
  ASSERT_TRUE(annotations.priority.has_value());
  EXPECT_EQ(annotations.priority.value(), 0.9);
}

TEST(BuildersTest, ToolParameterBuilder) {
  auto param = make<ToolParameter>("input", "string")
                   .description("Input parameter")
                   .required(true)
                   .build();
  
  EXPECT_EQ(param.name, "input");
  EXPECT_EQ(param.type, "string");
  ASSERT_TRUE(param.description.has_value());
  EXPECT_EQ(param.description.value(), "Input parameter");
  EXPECT_TRUE(param.required);
}

TEST(BuildersTest, ResourceLinkBuilder) {
  auto link = make<ResourceLink>("file:///path/to/file", "my-file")
                  .description("A linked file")
                  .mimeType("text/plain")
                  .build();
  
  EXPECT_EQ(link.uri, "file:///path/to/file");
  EXPECT_EQ(link.name, "my-file");
  ASSERT_TRUE(link.description.has_value());
  EXPECT_EQ(link.description.value(), "A linked file");
  ASSERT_TRUE(link.mimeType.has_value());
  EXPECT_EQ(link.mimeType.value(), "text/plain");
}

TEST(BuildersTest, ModelHintBuilder) {
  auto hint = make<ModelHint>("gpt-4").build();
  
  EXPECT_EQ(hint.name, "gpt-4");
}

TEST(BuildersTest, TextResourceContentsBuilder) {
  auto contents = make<TextResourceContents>("file:///test.txt")
                      .text("File contents")
                      .mimeType("text/plain")
                      .build();
  
  EXPECT_EQ(contents.uri, "file:///test.txt");
  EXPECT_EQ(contents.text, "File contents");
  ASSERT_TRUE(contents.mimeType.has_value());
  EXPECT_EQ(contents.mimeType.value(), "text/plain");
}

TEST(BuildersTest, BlobResourceContentsBuilder) {
  auto contents = make<BlobResourceContents>("file:///binary.dat")
                      .blob("binary data")
                      .mimeType("application/octet-stream")
                      .build();
  
  EXPECT_EQ(contents.uri, "file:///binary.dat");
  EXPECT_EQ(contents.blob, "binary data");
  ASSERT_TRUE(contents.mimeType.has_value());
  EXPECT_EQ(contents.mimeType.value(), "application/octet-stream");
}

TEST(BuildersTest, PingRequestBuilder) {
  auto request = make<PingRequest>()
                     .id("ping-123")
                     .build();
  
  EXPECT_EQ(request.method, "ping");
  auto* stringId = mcp::get_if<std::string>(&request.id);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(*stringId, "ping-123");
}

TEST(BuildersTest, ListResourcesRequestBuilder) {
  auto request = make<ListResourcesRequest>()
                     .id("req-123")
                     .cursor("next-page")
                     .build();
  
  EXPECT_EQ(request.method, "resources/list");
  auto* stringId = mcp::get_if<std::string>(&request.id);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(*stringId, "req-123");
  ASSERT_TRUE(request.cursor.has_value());
  EXPECT_EQ(request.cursor.value(), "next-page");
}

TEST(BuildersTest, ReadResourceRequestBuilder) {
  auto request = make<ReadResourceRequest>("file:///test.txt")
                     .id("read-123")
                     .build();
  
  EXPECT_EQ(request.method, "resources/read");
  EXPECT_EQ(request.uri, "file:///test.txt");
  auto* stringId = mcp::get_if<std::string>(&request.id);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(*stringId, "read-123");
}

TEST(BuildersTest, SubscribeRequestBuilder) {
  auto request = make<SubscribeRequest>("file:///monitored.txt")
                     .id("sub-123")
                     .build();
  
  EXPECT_EQ(request.method, "resources/subscribe");
  EXPECT_EQ(request.uri, "file:///monitored.txt");
  auto* stringId = mcp::get_if<std::string>(&request.id);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(*stringId, "sub-123");
}

TEST(BuildersTest, SetLevelRequestBuilder) {
  auto request = make<SetLevelRequest>(enums::LoggingLevel::DEBUG)
                     .id("log-123")
                     .build();
  
  EXPECT_EQ(request.method, "logging/setLevel");
  EXPECT_EQ(request.level, enums::LoggingLevel::DEBUG);
  auto* stringId = mcp::get_if<std::string>(&request.id);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(*stringId, "log-123");
}

TEST(BuildersTest, EmptyResultBuilder) {
  auto result = make<EmptyResult>().build();
  
  // EmptyResult has no fields to test, just ensure it compiles
  (void)result;
}