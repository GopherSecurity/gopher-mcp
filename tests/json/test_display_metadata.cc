// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * What tools, prompts, resources, resource templates and implementations
 * say about themselves for people: titles, icons, and the rest.
 *
 *   icon            {"src", "mimeType"?, "sizes"?, "theme"?}
 *   prompt          {"name", "title"?, "description"?, "arguments"?,
 *                    "icons"?, "_meta"?}
 *   resource        {"uri", "name", "title"?, "description"?, "mimeType"?,
 *                    "icons"?, "size"?, "annotations"?, "_meta"?}
 *   implementation  {"name", "version", "title"?, "description"?,
 *                    "websiteUrl"?, "icons"?, "_meta"?}
 *
 * Each field is written only when set and read from any peer, a field of
 * the wrong type passed over rather than allowed to cost the message. These
 * compare against the JSON another implementation sends and expects.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::JsonValue;

JsonValue parse(const std::string& text) { return JsonValue::parse(text); }

// The same JSON, whatever order its keys were written in.
#define EXPECT_SAME_JSON(a, b) EXPECT_EQ((a).toString(), (b).toString())

Icon reviewIcon() {
  return make<Icon>("https://example.com/review.svg")
      .mimeType("image/svg+xml")
      .size("any")
      .build();
}

// ── Icons ──────────────────────────────────────────────────────────────

TEST(DisplayMetadata, AnIconIsWrittenWithWhatIsSet) {
  EXPECT_SAME_JSON(json::to_json(Icon("https://example.com/a.png")),
                   parse(R"({"src":"https://example.com/a.png"})"));
  const Icon full = make<Icon>("https://example.com/a.png")
                        .mimeType("image/png")
                        .size("48x48")
                        .size("96x96")
                        .theme("dark")
                        .build();
  EXPECT_SAME_JSON(json::to_json(full),
                   parse(R"({"src":"https://example.com/a.png",
      "mimeType":"image/png","sizes":["48x48","96x96"],"theme":"dark"})"));
}

TEST(DisplayMetadata, AnIconIsReadBack) {
  const auto icon = json::from_json<Icon>(parse(
      R"({"src":"data:image/png;base64,AA==","mimeType":"image/png",
          "sizes":["48x48"],"theme":"light"})"));
  EXPECT_EQ(icon.src, "data:image/png;base64,AA==");
  EXPECT_EQ(icon.mimeType, mcp::make_optional(std::string("image/png")));
  ASSERT_TRUE(icon.sizes.has_value());
  EXPECT_EQ(icon.sizes.value(), std::vector<std::string>({"48x48"}));
  EXPECT_EQ(icon.theme, mcp::make_optional(std::string("light")));
}

// A theme the spec doesn't name, or fields of the wrong type, are passed
// over; the icon is kept.
TEST(DisplayMetadata, AnIconsBadFieldsArePassedOver) {
  const auto icon = json::from_json<Icon>(
      parse(R"({"src":"https://example.com/a.png","mimeType":7,
                "sizes":"48x48","theme":"sepia"})"));
  EXPECT_EQ(icon.src, "https://example.com/a.png");
  EXPECT_FALSE(icon.mimeType.has_value());
  EXPECT_FALSE(icon.sizes.has_value());
  EXPECT_FALSE(icon.theme.has_value());
}

// ── Prompts ────────────────────────────────────────────────────────────

TEST(DisplayMetadata, APromptCarriesItsTitleIconsAndMeta) {
  PromptArgument code{"code", mcp::make_optional(std::string("The code")),
                      true};
  code.title = "Code to review";
  const Prompt prompt = make<Prompt>("code_review")
                            .title("Request Code Review")
                            .description("Reviews code")
                            .argument(code)
                            .icon(reviewIcon())
                            .meta(parse(R"({"team":{"owner":"docs"}})"))
                            .build();
  const JsonValue expected = parse(R"({
      "name":"code_review","title":"Request Code Review",
      "description":"Reviews code",
      "arguments":[{"name":"code","title":"Code to review",
                    "description":"The code","required":true}],
      "icons":[{"src":"https://example.com/review.svg",
                "mimeType":"image/svg+xml","sizes":["any"]}],
      "_meta":{"team":{"owner":"docs"}}})");
  EXPECT_SAME_JSON(json::to_json(prompt), expected);

  const auto back = json::from_json<Prompt>(expected);
  EXPECT_EQ(back.title, mcp::make_optional(std::string("Request Code Review")));
  ASSERT_TRUE(back.arguments.has_value());
  EXPECT_EQ(back.arguments->at(0).title,
            mcp::make_optional(std::string("Code to review")));
  ASSERT_TRUE(back.icons.has_value());
  EXPECT_EQ(back.icons->size(), 1u);
  ASSERT_TRUE(back._meta.has_value());
  EXPECT_SAME_JSON(back._meta.value(), parse(R"({"team":{"owner":"docs"}})"));
  EXPECT_SAME_JSON(json::to_json(back), expected);
}

TEST(DisplayMetadata, APromptWithNoneOfThemWritesNone) {
  const JsonValue written = json::to_json(Prompt("plain"));
  EXPECT_SAME_JSON(written, parse(R"({"name":"plain"})"));
}

// ── Resources ──────────────────────────────────────────────────────────

TEST(DisplayMetadata, AResourceCarriesItsDisplayFields) {
  const Resource resource =
      make<Resource>("file:///project/README.md", "README.md")
          .title("Project Documentation")
          .mimeType("text/markdown")
          .icon(make<Icon>("https://example.com/md.png").size("48x48").build())
          .size(1024)
          .annotations(make<Annotations>().priority(0.8).build())
          .meta(parse(R"({"source":{"repo":"x"}})"))
          .build();
  const JsonValue expected = parse(R"({
      "uri":"file:///project/README.md","name":"README.md",
      "title":"Project Documentation","mimeType":"text/markdown",
      "icons":[{"src":"https://example.com/md.png","sizes":["48x48"]}],
      "size":1024,"annotations":{"priority":0.8},
      "_meta":{"source":{"repo":"x"}}})");
  EXPECT_SAME_JSON(json::to_json(resource), expected);
  EXPECT_SAME_JSON(json::to_json(json::from_json<Resource>(expected)),
                   expected);
}

TEST(DisplayMetadata, AResourceTemplateAndAToolCarryIcons) {
  const ResourceTemplate tmpl =
      make<ResourceTemplate>("file:///{path}", "files")
          .icon(reviewIcon())
          .build();
  const JsonValue tmpl_json = json::to_json(tmpl);
  ASSERT_TRUE(tmpl_json.contains("icons")) << tmpl_json.toString();
  EXPECT_EQ(tmpl_json["icons"][0]["src"].getString(),
            "https://example.com/review.svg");
  EXPECT_TRUE(json::from_json<ResourceTemplate>(tmpl_json).icons.has_value());

  const Tool tool = make<Tool>("lint").icon(reviewIcon()).build();
  const JsonValue tool_json = json::to_json(tool);
  ASSERT_TRUE(tool_json.contains("icons")) << tool_json.toString();
  EXPECT_SAME_JSON(json::to_json(json::from_json<Tool>(tool_json)), tool_json);
}

// A resource link is a resource with a type, read and written as before,
// and now with icons too.
TEST(DisplayMetadata, AResourceLinkKeepsItsShape) {
  const JsonValue link = parse(R"({"type":"resource_link",
      "uri":"file:///a.rs","name":"a.rs","title":"A","description":"d",
      "mimeType":"text/x-rust","size":12,
      "icons":[{"src":"https://example.com/rs.png"}],
      "annotations":{"audience":["user"]},"_meta":{"k":1}})");
  const auto read = json::from_json<ResourceLink>(link);
  EXPECT_EQ(read.title, mcp::make_optional(std::string("A")));
  EXPECT_EQ(read.size, mcp::make_optional(static_cast<int64_t>(12)));
  EXPECT_SAME_JSON(json::to_json(read), link);
}

// ── Implementations ────────────────────────────────────────────────────

TEST(DisplayMetadata, AnImplementationDescribesItself) {
  const Implementation self = make<Implementation>("example-server", "1.0.0")
                                  .title("Example Server")
                                  .description("Serves examples")
                                  .websiteUrl("https://example.com")
                                  .icon(reviewIcon())
                                  .meta(parse(R"({"build":{"commit":"abc"}})"))
                                  .build();
  const JsonValue expected = parse(R"({
      "name":"example-server","version":"1.0.0","title":"Example Server",
      "description":"Serves examples","websiteUrl":"https://example.com",
      "icons":[{"src":"https://example.com/review.svg",
                "mimeType":"image/svg+xml","sizes":["any"]}],
      "_meta":{"build":{"commit":"abc"}}})");
  EXPECT_SAME_JSON(json::to_json(self), expected);

  const auto back = json::from_json<Implementation>(expected);
  EXPECT_EQ(back.websiteUrl,
            mcp::make_optional(std::string("https://example.com")));
  EXPECT_SAME_JSON(json::to_json(back), expected);

  EXPECT_SAME_JSON(json::to_json(Implementation("bare", "2")),
                   parse(R"({"name":"bare","version":"2"})"));
}

// ── Reading from any peer ──────────────────────────────────────────────

// One prompt's malformed display fields cost only those fields, not the
// prompt and not the list.
TEST(DisplayMetadata, MalformedDisplayFieldsArePassedOver) {
  const auto list = json::from_json<ListPromptsResult>(parse(R"({"prompts":[
      {"name":"a","title":5,"_meta":"x",
       "icons":[{"mimeType":"image/png"},{"src":7},
                {"src":"https://example.com/ok.png"},"not an icon"]},
      {"name":"b","icons":{"src":"https://example.com/b.png"}}]})"));
  ASSERT_EQ(list.prompts.size(), 2u);
  const Prompt& a = list.prompts[0];
  EXPECT_FALSE(a.title.has_value());
  EXPECT_FALSE(a._meta.has_value());
  ASSERT_TRUE(a.icons.has_value());
  ASSERT_EQ(a.icons->size(), 1u);
  EXPECT_EQ(a.icons->at(0).src, "https://example.com/ok.png");
  EXPECT_FALSE(list.prompts[1].icons.has_value());

  const auto who = json::from_json<Implementation>(
      parse(R"({"name":"s","title":[],"websiteUrl":1,"icons":"x"})"));
  EXPECT_EQ(who.name, "s");
  EXPECT_EQ(who.version, "");
  EXPECT_FALSE(who.title.has_value());
  EXPECT_FALSE(who.websiteUrl.has_value());
  EXPECT_FALSE(who.icons.has_value());

  const auto resource = json::from_json<Resource>(parse(
      R"({"uri":"a://b","name":"b","title":false,"size":"big",
          "annotations":{"priority":"high","audience":["user"]}})"));
  EXPECT_FALSE(resource.title.has_value());
  EXPECT_FALSE(resource.size.has_value());
  ASSERT_TRUE(resource.annotations.has_value());
  EXPECT_FALSE(resource.annotations->priority.has_value());
  ASSERT_TRUE(resource.annotations->audience.has_value());
}

// ── The name to show people ────────────────────────────────────────────

TEST(DisplayMetadata, TheDisplayNameIsTheTitleThenTheName) {
  Prompt prompt("p");
  EXPECT_EQ(prompt.displayName(), "p");
  prompt.title = "Prompt";
  EXPECT_EQ(prompt.displayName(), "Prompt");

  PromptArgument arg{"arg", nullopt, false};
  EXPECT_EQ(arg.displayName(), "arg");
  arg.title = "Argument";
  EXPECT_EQ(arg.displayName(), "Argument");

  Resource resource("a://b", "b");
  EXPECT_EQ(resource.displayName(), "b");
  resource.title = "B";
  EXPECT_EQ(resource.displayName(), "B");

  ResourceTemplate tmpl;
  tmpl.name = "t";
  EXPECT_EQ(tmpl.displayName(), "t");
  tmpl.title = "T";
  EXPECT_EQ(tmpl.displayName(), "T");

  Implementation self("s", "1");
  EXPECT_EQ(self.displayName(), "s");
  self.title = "S";
  EXPECT_EQ(self.displayName(), "S");

  // A tool's annotations.title comes between its title and its name.
  Tool tool("t");
  EXPECT_EQ(tool.displayName(), "t");
  tool.annotations = make<ToolAnnotations>().title("Annotated").build();
  EXPECT_EQ(tool.displayName(), "Annotated");
  tool.title = "Titled";
  EXPECT_EQ(tool.displayName(), "Titled");
}

}  // namespace
}  // namespace mcp
