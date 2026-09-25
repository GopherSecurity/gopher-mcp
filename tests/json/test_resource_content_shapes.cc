// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Resource links and embedded resources, in the shapes the spec gives them.
 *
 *   resource_link  a pointer; the client fetches it later with resources/read
 *     {"type": "resource_link", "uri", "name", "title"?, "description"?,
 *      "mimeType"?, "size"?, "annotations"?}
 *
 *   resource       the contents themselves, inline
 *     {"type": "resource", "resource": {"uri", "mimeType"?, "text" | "blob"}}
 *
 * A round trip through this SDK alone would pass with any shape at all, so
 * these compare against the JSON another implementation would send or
 * expect.
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

TEST(ResourceContentShapes, ALinkIsWrittenAsAResourceLink) {
  ResourceLink link(Resource("file:///src/main.rs", "main.rs"));
  link.title = std::string("Main");
  link.description = std::string("The entry point");
  link.mimeType = std::string("text/x-rust");
  link.size = 1024;
  Annotations annotations;
  annotations.priority = mcp::make_optional(0.5);
  link.annotations = annotations;

  const JsonValue wire = to_json(ExtendedContentBlock(link));

  EXPECT_EQ(wire["type"].getString(), "resource_link");
  EXPECT_EQ(wire["uri"].getString(), "file:///src/main.rs");
  EXPECT_EQ(wire["name"].getString(), "main.rs");
  EXPECT_EQ(wire["title"].getString(), "Main");
  EXPECT_EQ(wire["description"].getString(), "The entry point");
  EXPECT_EQ(wire["mimeType"].getString(), "text/x-rust");
  EXPECT_EQ(wire["size"].getInt64(), 1024);
  EXPECT_EQ(wire["annotations"]["priority"].getFloat(), 0.5);
  EXPECT_FALSE(wire.contains("resource"));
}

TEST(ResourceContentShapes, AnEmbeddedResourceIsWrittenWithItsContents) {
  TextResourceContents contents("fn main() {}");
  contents.uri = std::string("file:///src/main.rs");
  contents.mimeType = std::string("text/x-rust");

  const JsonValue wire = to_json(make_embedded_resource(contents));

  EXPECT_EQ(wire["type"].getString(), "resource");
  ASSERT_TRUE(wire["resource"].isObject());
  EXPECT_EQ(wire["resource"]["uri"].getString(), "file:///src/main.rs");
  EXPECT_EQ(wire["resource"]["mimeType"].getString(), "text/x-rust");
  EXPECT_EQ(wire["resource"]["text"].getString(), "fn main() {}");
  EXPECT_FALSE(wire["resource"].contains("name"));
  EXPECT_FALSE(wire.contains("content"));
}

TEST(ResourceContentShapes, AnEmbeddedBlobIsWrittenWithItsBytes) {
  BlobResourceContents contents("iVBORw0KGgo=");
  contents.uri = std::string("file:///logo.png");
  contents.mimeType = std::string("image/png");

  const JsonValue wire = to_json(make_embedded_resource(contents));

  EXPECT_EQ(wire["type"].getString(), "resource");
  EXPECT_EQ(wire["resource"]["blob"].getString(), "iVBORw0KGgo=");
  EXPECT_FALSE(wire["resource"].contains("text"));
}

// The older ContentBlock variant has only a link form, and it goes out the
// same way a ResourceLink does.
TEST(ResourceContentShapes, TheOlderContentBlockLinkIsAResourceLinkToo) {
  Resource resource("file:///notes.txt", "notes.txt");
  resource.mimeType = std::string("text/plain");

  const JsonValue wire = to_json(make_resource_content(resource));

  EXPECT_EQ(wire["type"].getString(), "resource_link");
  EXPECT_EQ(wire["uri"].getString(), "file:///notes.txt");
  EXPECT_EQ(wire["name"].getString(), "notes.txt");
  EXPECT_FALSE(wire.contains("resource"));
}

// What another implementation sends, read into the right type.
TEST(ResourceContentShapes, AToolResultFromAnotherSdkIsRead) {
  const auto result = from_json<CallToolResult>(JsonValue::parse(R"({
    "content": [
      {"type": "resource_link", "uri": "file:///a.txt", "name": "a.txt",
       "title": "A", "size": 12},
      {"type": "resource",
       "resource": {"uri": "file:///b.txt", "mimeType": "text/plain",
                    "text": "bee"}},
      {"type": "resource",
       "resource": {"uri": "file:///c.bin", "blob": "Y2Vl"}}
    ]
  })"));

  ASSERT_EQ(result.content.size(), 3u);

  ASSERT_TRUE(holds_alternative<ResourceLink>(result.content[0]));
  const auto& link = get<ResourceLink>(result.content[0]);
  EXPECT_EQ(link.uri, "file:///a.txt");
  EXPECT_EQ(link.name, "a.txt");
  EXPECT_EQ(link.title.value(), "A");
  EXPECT_EQ(link.size.value(), 12);

  ASSERT_TRUE(holds_alternative<EmbeddedResource>(result.content[1]));
  const auto& text = get<EmbeddedResource>(result.content[1]);
  ASSERT_TRUE(holds_alternative<TextResourceContents>(text.resource));
  EXPECT_EQ(get<TextResourceContents>(text.resource).text, "bee");
  EXPECT_EQ(get<TextResourceContents>(text.resource).uri.value(),
            "file:///b.txt");

  ASSERT_TRUE(holds_alternative<EmbeddedResource>(result.content[2]));
  const auto& blob = get<EmbeddedResource>(result.content[2]);
  ASSERT_TRUE(holds_alternative<BlobResourceContents>(blob.resource));
  EXPECT_EQ(get<BlobResourceContents>(blob.resource).blob, "Y2Vl");
}

TEST(ResourceContentShapes, APromptMessageFromAnotherSdkIsRead) {
  const auto embedded = from_json<PromptMessage>(JsonValue::parse(R"({
    "role": "user",
    "content": {"type": "resource",
                "resource": {"uri": "file:///b.txt", "text": "bee"}}
  })"));
  ASSERT_TRUE(holds_alternative<EmbeddedResource>(embedded.content));
  EXPECT_EQ(get<TextResourceContents>(
                get<EmbeddedResource>(embedded.content).resource)
                .text,
            "bee");

  const auto link = from_json<PromptMessage>(JsonValue::parse(R"({
    "role": "assistant",
    "content": {"type": "resource_link", "uri": "file:///a.txt",
                "name": "a.txt"}
  })"));
  ASSERT_TRUE(holds_alternative<ResourceLink>(link.content));
  EXPECT_EQ(get<ResourceLink>(link.content).uri, "file:///a.txt");
}

TEST(ResourceContentShapes, APromptMessageIsWrittenInTheSpecShapes) {
  TextResourceContents contents("bee");
  contents.uri = std::string("file:///b.txt");
  PromptMessage message;
  message.role = enums::Role::USER;
  message.content = EmbeddedResource(contents);

  const JsonValue wire = to_json(message);
  EXPECT_EQ(wire["content"]["type"].getString(), "resource");
  EXPECT_EQ(wire["content"]["resource"]["text"].getString(), "bee");

  message.content = ResourceLink(Resource("file:///a.txt", "a.txt"));
  EXPECT_EQ(to_json(message)["content"]["type"].getString(), "resource_link");
}

// A peer on an older version of this SDK wrote links as "resource", either
// flat or with the resource nested. Both are still read as links.
TEST(ResourceContentShapes, LinksInTheOldShapeAreStillRead) {
  const auto flat = from_json<ExtendedContentBlock>(JsonValue::parse(
      R"({"type": "resource", "uri": "file:///a.txt", "name": "a.txt"})"));
  ASSERT_TRUE(holds_alternative<ResourceLink>(flat));
  EXPECT_EQ(get<ResourceLink>(flat).uri, "file:///a.txt");

  const auto nested = from_json<ContentBlock>(JsonValue::parse(
      R"({"type": "resource",
          "resource": {"uri": "file:///a.txt", "name": "a.txt"}})"));
  ASSERT_TRUE(holds_alternative<ResourceContent>(nested));
  EXPECT_EQ(get<ResourceContent>(nested).resource.name, "a.txt");
}

// A result the client receives with no type to decode it into: a list of
// blocks the older ContentBlock can hold becomes one, and a list with an
// embedded resource in it is kept whole rather than losing that block.
TEST(ResourceContentShapes, UntypedResultsKeepEveryBlock) {
  const auto links = from_json<jsonrpc::Response>(JsonValue::parse(R"({
    "jsonrpc": "2.0", "id": 1,
    "result": [{"type": "text", "text": "see"},
               {"type": "resource_link", "uri": "file:///a.txt",
                "name": "a.txt"}]
  })"));
  ASSERT_TRUE(
      holds_alternative<std::vector<ContentBlock>>(links.result.value()));
  const auto& blocks = get<std::vector<ContentBlock>>(links.result.value());
  ASSERT_EQ(blocks.size(), 2u);
  EXPECT_TRUE(holds_alternative<ResourceContent>(blocks[1]));

  const auto embedded = from_json<jsonrpc::Response>(JsonValue::parse(R"({
    "jsonrpc": "2.0", "id": 1,
    "result": [{"type": "text", "text": "see"},
               {"type": "resource",
                "resource": {"uri": "file:///b.txt", "text": "bee"}}]
  })"));
  ASSERT_TRUE(holds_alternative<JsonValue>(embedded.result.value()));
  const auto& kept = get<JsonValue>(embedded.result.value());
  EXPECT_EQ(kept[1]["resource"]["text"].getString(), "bee");
}

// A link with a title, a size or annotations has more than the older
// ContentBlock can hold, so a result carrying one stays JSON and loses
// none of it.
TEST(ResourceContentShapes, ARichLinkInAnUntypedResultKeepsItsFields) {
  const char* body = R"({
    "jsonrpc": "2.0", "id": 1,
    "result": [{"type": "resource_link", "uri": "file:///a.txt",
                "name": "a.txt", "title": "A", "size": 12,
                "annotations": {"priority": 0.5}}]
  })";
  const auto response = from_json<jsonrpc::Response>(JsonValue::parse(body));

  ASSERT_TRUE(holds_alternative<JsonValue>(response.result.value()));
  const auto& kept = get<JsonValue>(response.result.value());
  EXPECT_EQ(kept[0]["title"].getString(), "A");
  EXPECT_EQ(kept[0]["size"].getInt64(), 12);

  // And out again unchanged.
  EXPECT_EQ(to_json(response).toString(), JsonValue::parse(body).toString());

  // The same on its own, not in a list.
  const auto single = from_json<jsonrpc::Response>(JsonValue::parse(R"({
    "jsonrpc": "2.0", "id": 1,
    "result": {"type": "resource_link", "uri": "file:///a.txt",
               "name": "a.txt", "title": "A"}
  })"));
  ASSERT_TRUE(holds_alternative<JsonValue>(single.result.value()));
  EXPECT_EQ(get<JsonValue>(single.result.value())["title"].getString(), "A");
}

// Embedded contents name their resource and carry exactly one of text or
// blob. Anything else is refused rather than read as something it is not.
TEST(ResourceContentShapes, EmbeddedContentsAreHeldToTheSpec) {
  EXPECT_THROW(from_json<EmbeddedResource>(JsonValue::parse(
                   R"({"type": "resource", "resource": {"text": "no uri"}})")),
               json::JsonException);
  EXPECT_THROW(from_json<EmbeddedResource>(JsonValue::parse(
                   R"({"type": "resource",
              "resource": {"uri": "file:///a", "text": "t", "blob": "Yg=="}})")),
               json::JsonException);
  EXPECT_THROW(
      from_json<EmbeddedResource>(JsonValue::parse(
          R"({"type": "resource", "resource": {"uri": "file:///a"}})")),
      json::JsonException);

  // Both in a tool result, where the block is chosen by its contents.
  EXPECT_THROW(from_json<CallToolResult>(JsonValue::parse(
                   R"({"content": [{"type": "resource",
              "resource": {"uri": "file:///a", "text": "t", "blob": "Yg=="}}]})")),
               json::JsonException);
}

// The same malformed contents inside a tool result. With no text or blob
// they look a little like a link in the old shape, but a link has a name,
// and these are refused rather than read as a link to nothing.
TEST(ResourceContentShapes, MalformedContentsInAToolResultAreRefused) {
  EXPECT_THROW(from_json<CallToolResult>(JsonValue::parse(
                   R"({"content": [{"type": "resource",
                       "resource": {"uri": "file:///a"}}]})")),
               json::JsonException);
  EXPECT_THROW(from_json<CallToolResult>(JsonValue::parse(
                   R"({"content": [{"type": "resource",
                       "resource": {"text": "no uri"}}]})")),
               json::JsonException);

  // A link in either shape, spec or old, still needs its name.
  EXPECT_THROW(from_json<CallToolResult>(JsonValue::parse(
                   R"({"content": [{"type": "resource_link",
                       "uri": "file:///a"}]})")),
               json::JsonException);
  EXPECT_THROW(from_json<ContentBlock>(JsonValue::parse(
                   R"({"type": "resource_link", "uri": "file:///a"})")),
               json::JsonException);

  // While an old-style link with its name is still read as one.
  const auto old = from_json<CallToolResult>(JsonValue::parse(
      R"({"content": [{"type": "resource",
          "resource": {"uri": "file:///a", "name": "a"}}]})"));
  ASSERT_EQ(old.content.size(), 1u);
  ASSERT_TRUE(holds_alternative<ResourceLink>(old.content[0]));
  EXPECT_EQ(get<ResourceLink>(old.content[0]).name, "a");
}

TEST(ResourceContentShapes, AnEmbeddedResourceWithNoUriIsNotWritten) {
  EXPECT_THROW(to_json(make_embedded_resource(TextResourceContents("t"))),
               json::JsonException);
  EXPECT_THROW(to_json(make_embedded_resource(BlobResourceContents("Yg=="))),
               json::JsonException);
}

}  // namespace
}  // namespace mcp
