// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Annotations and `_meta` on every content block.
 *
 * Each of text, image, audio, resource_link and resource may carry
 *
 *   "annotations": {"audience": [...], "priority": 0.5,
 *                   "lastModified": "2026-01-12T15:00:58Z"},
 *   "_meta": {...any JSON object...}
 *
 * and a prompt message may hold any of the five. Whatever a peer sends in
 * those places reaches the program, and whatever the program sets reaches
 * the peer; a block with neither goes out as it always has.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::from_json;
using json::JsonValue;
using json::to_json;

const char* const kAnnotations =
    R"({"audience": ["user", "assistant"], "priority": 0.5,
        "lastModified": "2026-01-12T15:00:58Z"})";
const char* const kMeta = R"({"trace": {"id": "t-1", "hops": [1, 2]}})";

Annotations annotations() {
  return from_json<Annotations>(JsonValue::parse(kAnnotations));
}
JsonValue meta() { return JsonValue::parse(kMeta); }

/** The annotations and _meta a block went out with are the ones given. */
void expectCarried(const JsonValue& wire) {
  ASSERT_TRUE(wire.contains("annotations")) << wire.toString();
  EXPECT_EQ(wire["annotations"]["priority"].getFloat(), 0.5);
  EXPECT_EQ(wire["annotations"]["lastModified"].getString(),
            "2026-01-12T15:00:58Z");
  EXPECT_EQ(wire["annotations"]["audience"][1].getString(), "assistant");
  ASSERT_TRUE(wire.contains("_meta")) << wire.toString();
  EXPECT_EQ(wire["_meta"].toString(), meta().toString());
}

TEST(ContentAnnotations, LastModifiedIsWrittenAndRead) {
  const Annotations read = annotations();
  ASSERT_TRUE(read.lastModified.has_value());
  EXPECT_EQ(read.lastModified.value(), "2026-01-12T15:00:58Z");
  EXPECT_EQ(to_json(read)["lastModified"].getString(), "2026-01-12T15:00:58Z");

  const Annotations built =
      make<Annotations>().lastModified("2026-02-01T00:00:00Z").build();
  EXPECT_EQ(to_json(built)["lastModified"].getString(), "2026-02-01T00:00:00Z");
}

// Every one of the five, written with both and read back with both.
TEST(ContentAnnotations, EveryBlockCarriesAnnotationsAndMeta) {
  TextContent text =
      make<TextContent>("hi").annotations(annotations()).meta(meta()).build();
  ImageContent image = make<ImageContent>("aW1n", "image/png")
                           .annotations(annotations())
                           .meta(meta())
                           .build();
  AudioContent audio = make<AudioContent>("YXVk", "audio/wav")
                           .annotations(annotations())
                           .meta(meta())
                           .build();
  ResourceLink link = make<ResourceLink>("file:///a.txt", "a.txt")
                          .annotations(annotations())
                          .meta(meta())
                          .build();
  TextResourceContents contents("bee");
  contents.uri = std::string("file:///b.txt");
  EmbeddedResource embedded = make<EmbeddedResource>(contents)
                                  .annotations(annotations())
                                  .meta(meta())
                                  .build();

  for (const ExtendedContentBlock& block :
       {ExtendedContentBlock(text), ExtendedContentBlock(image),
        ExtendedContentBlock(audio), ExtendedContentBlock(link),
        ExtendedContentBlock(embedded)}) {
    const JsonValue wire = to_json(block);
    SCOPED_TRACE(wire.toString());
    expectCarried(wire);

    // Read back into the same type, and out again unchanged.
    const ExtendedContentBlock read = from_json<ExtendedContentBlock>(wire);
    EXPECT_EQ(to_json(read).toString(), wire.toString());
  }
}

// What another implementation sends, read into the right fields.
TEST(ContentAnnotations, BlocksFromAnotherSdkAreRead) {
  const auto image = from_json<ExtendedContentBlock>(JsonValue::parse(
      std::string(R"({"type": "image", "data": "aW1n", "mimeType": "image/png",
                      "annotations": )") +
      kAnnotations + R"(, "_meta": )" + kMeta + "}"));
  ASSERT_TRUE(holds_alternative<ImageContent>(image));
  const auto& read = get<ImageContent>(image);
  ASSERT_TRUE(read.annotations.has_value());
  EXPECT_EQ(read.annotations->lastModified.value(), "2026-01-12T15:00:58Z");
  ASSERT_TRUE(read._meta.has_value());
  EXPECT_EQ((*read._meta)["trace"]["hops"][1].getInt(), 2);
}

// A block with neither goes out exactly as it did.
TEST(ContentAnnotations, ABlockWithNeitherIsUnchanged) {
  EXPECT_EQ(to_json(ExtendedContentBlock(TextContent("hi"))).toString(),
            JsonValue::parse(R"({"type": "text", "text": "hi"})").toString());
  EXPECT_EQ(to_json(ExtendedContentBlock(AudioContent("YXVk", "audio/wav")))
                .toString(),
            JsonValue::parse(
                R"({"type": "audio", "mimeType": "audio/wav", "data": "YXVk"})")
                .toString());
}

// Resource contents carry _meta too, embedded or read.
TEST(ContentAnnotations, ResourceContentsCarryMeta) {
  BlobResourceContents blob("Yg==");
  blob.uri = std::string("file:///b.bin");
  blob._meta = meta();
  const JsonValue wire = to_json(blob);
  EXPECT_EQ(wire["_meta"].toString(), meta().toString());

  const auto read = from_json<ReadResourceResult>(JsonValue::parse(
      std::string(
          R"({"contents": [{"uri": "file:///a", "text": "t", "_meta": )") +
      kMeta + "}]}"));
  ASSERT_EQ(read.contents.size(), 1u);
  const auto& text = get<TextResourceContents>(read.contents[0]);
  ASSERT_TRUE(text._meta.has_value());
  EXPECT_EQ(text._meta->toString(), meta().toString());
}

// _meta is an object; anything else is refused both ways.
TEST(ContentAnnotations, MetaThatIsNotAnObjectIsRefused) {
  TextContent text("hi");
  text._meta = JsonValue("not an object");
  EXPECT_THROW(to_json(ExtendedContentBlock(text)), json::JsonException);
  EXPECT_THROW(from_json<ExtendedContentBlock>(JsonValue::parse(
                   R"({"type": "text", "text": "hi", "_meta": [1]})")),
               json::JsonException);
}

// A prompt message holds any of the five, audio included.
TEST(ContentAnnotations, APromptMessageHoldsAudio) {
  const PromptMessage built =
      make<PromptMessage>(enums::Role::USER).audio("YXVk", "audio/wav").build();
  const JsonValue wire = to_json(built);
  EXPECT_EQ(wire["content"]["type"].getString(), "audio");

  const auto read = from_json<PromptMessage>(JsonValue::parse(
      std::string(R"({"role": "user", "content": {"type": "audio",
          "data": "YXVk", "mimeType": "audio/wav", "_meta": )") +
      kMeta + "}}"));
  ASSERT_TRUE(holds_alternative<AudioContent>(read.content));
  const auto& audio = get<AudioContent>(read.content);
  EXPECT_EQ(audio.mimeType, "audio/wav");
  ASSERT_TRUE(audio._meta.has_value());
  EXPECT_EQ(audio._meta->toString(), meta().toString());
}

// ---------------------------------------------------------------------------
// Through the server
// ---------------------------------------------------------------------------

class DispatchTestServer : public server::McpServer {
 public:
  explicit DispatchTestServer(const server::McpServerConfig& config)
      : server::McpServer(config) {}
  using server::McpServer::onRequestWithContext;
};

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  optional<jsonrpc::Response> captured;
};

JsonValue answerTo(DispatchTestServer& server,
                   const std::string& method,
                   const std::string& name) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  Metadata params;
  params["name"] = MetadataValue(name);
  request.params = mcp::make_optional(params);
  CapturingContext context;
  server.onRequestWithContext(request, context);
  if (!context.captured.has_value()) {
    ADD_FAILURE() << method << " went unanswered";
    return JsonValue::object();
  }
  return to_json(context.captured.value());
}

server::McpServerConfig testConfig() {
  server::McpServerConfig config;
  config.server_name = "content-annotations-test";
  config.server_version = "0.0.1";
  config.capabilities.tools = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(true);
  return config;
}

TEST(ContentAnnotations, AToolResultKeepsThemThroughTheServer) {
  DispatchTestServer server(testConfig());
  ASSERT_TRUE(server.registerTool(
      make<Tool>("annotated").build(),
      [](const std::string&, const optional<Metadata>&,
         server::SessionContext&) {
        CallToolResult result;
        result.content.push_back(
            ExtendedContentBlock(make<ImageContent>("aW1n", "image/png")
                                     .annotations(annotations())
                                     .meta(meta())
                                     .build()));
        return result;
      }));

  const JsonValue answer = answerTo(server, "tools/call", "annotated");
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  expectCarried(answer["result"]["content"][0]);

  // And the client reads them back out of that answer.
  const auto read = from_json<CallToolResult>(answer["result"]);
  const auto& image = get<ImageContent>(read.content[0]);
  EXPECT_EQ(image.annotations->lastModified.value(), "2026-01-12T15:00:58Z");
  EXPECT_EQ(image._meta->toString(), meta().toString());
}

TEST(ContentAnnotations, APromptMessageKeepsThemThroughTheServer) {
  DispatchTestServer server(testConfig());
  server.registerPrompt(
      Prompt("spoken"), [](const std::string&, const optional<Metadata>&,
                           server::SessionContext&) {
        GetPromptResult result;
        PromptMessage message;
        message.role = enums::Role::USER;
        message.content = make<AudioContent>("YXVk", "audio/wav")
                              .annotations(annotations())
                              .meta(meta())
                              .build();
        result.messages.push_back(message);
        return result;
      });

  const JsonValue answer = answerTo(server, "prompts/get", "spoken");
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  const auto& content = answer["result"]["messages"][0]["content"];
  EXPECT_EQ(content["type"].getString(), "audio");
  expectCarried(content);

  const auto read = from_json<GetPromptResult>(answer["result"]);
  ASSERT_TRUE(holds_alternative<AudioContent>(read.messages[0].content));
}

}  // namespace
}  // namespace mcp
