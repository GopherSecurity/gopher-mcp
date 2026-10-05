// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * resources/templates/list, and what a resource template carries.
 *
 * Paging is covered with the other lists in test_list_paging.cc; this is
 * what is particular to templates: that they are served at all, that one
 * registered twice is listed once, what a template says about itself, and
 * the caching hints of the newest revision.
 */

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;
using namespace mcp::server;

namespace {

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  optional<jsonrpc::Response> captured;
};

/** A resources/templates/list, declaring a revision when given one. */
JsonValue listTemplates(McpServer& server,
                        const std::string& revision = std::string()) {
  JsonValue params = JsonValue::object();
  if (!revision.empty()) {
    JsonValue meta = JsonValue::object();
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue(revision));
    params.set("_meta", meta);
  }
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = "resources/templates/list";
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(jsonToMetadata(params));
  CapturingContext context;
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  if (!context.captured.has_value()) {
    ADD_FAILURE() << "resources/templates/list went unanswered";
    return JsonValue::object();
  }
  return json::to_json(context.captured.value());
}

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "resource-template-test";
  config.server_version = "0.0.1";
  return config;
}

ResourceTemplate filesTemplate() {
  return make<ResourceTemplate>("file:///{path}", "files")
      .title("Project files")
      .description("Any file under the project")
      .mimeType("text/plain")
      .meta(JsonValue::parse(R"({"vendor":{"indexed":true}})"))
      .build();
}

}  // namespace

// Registered templates are what the list serves, as the spec shapes them.
TEST(ResourceTemplates, RegisteredTemplatesAreListed) {
  DispatchTestServer server(testConfig());
  server.registerResourceTemplate(filesTemplate());

  const JsonValue answer = listTemplates(server);
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  const JsonValue& listed = answer["result"]["resourceTemplates"];
  ASSERT_EQ(listed.size(), 1u) << answer.toString();
  EXPECT_EQ(listed[0]["uriTemplate"].getString(), "file:///{path}");
  EXPECT_EQ(listed[0]["name"].getString(), "files");
  EXPECT_EQ(listed[0]["title"].getString(), "Project files");
  EXPECT_EQ(listed[0]["mimeType"].getString(), "text/plain");
  EXPECT_TRUE(listed[0]["_meta"]["vendor"]["indexed"].getBool());
  EXPECT_FALSE(answer["result"].contains("nextCursor"));
}

// With none registered, the list is there and empty, not refused.
TEST(ResourceTemplates, NoTemplatesIsAnEmptyList) {
  DispatchTestServer server(testConfig());
  const JsonValue answer = listTemplates(server);
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  EXPECT_EQ(answer["result"]["resourceTemplates"].size(), 0u);
}

// The same uriTemplate registered again replaces the first.
TEST(ResourceTemplates, RegisteringAgainReplaces) {
  DispatchTestServer server(testConfig());
  server.registerResourceTemplate(filesTemplate());
  ResourceTemplate renamed = filesTemplate();
  renamed.name = "files-again";
  server.registerResourceTemplate(renamed);

  const JsonValue answer = listTemplates(server);
  const JsonValue& listed = answer["result"]["resourceTemplates"];
  ASSERT_EQ(listed.size(), 1u) << answer.toString();
  EXPECT_EQ(listed[0]["name"].getString(), "files-again");
}

// The newest revision's caching hints, configured for this method, and
// none for an earlier revision.
TEST(ResourceTemplates, CachingHintsFollowTheRevision) {
  McpServerConfig config = testConfig();
  McpServerConfig::CacheHint hint;
  hint.ttl = std::chrono::milliseconds(30000);
  hint.scope = McpServerConfig::CacheScope::Public;
  config.cache_hints["resources/templates/list"] = hint;
  DispatchTestServer server(config);
  server.registerResourceTemplate(filesTemplate());

  const JsonValue modern = listTemplates(server, "2026-07-28");
  ASSERT_TRUE(modern.contains("result")) << modern.toString();
  EXPECT_EQ(modern["result"]["ttlMs"].getInt64(), 30000);
  EXPECT_EQ(modern["result"]["cacheScope"].getString(), "public");

  const JsonValue older = listTemplates(server, "2025-11-25");
  ASSERT_TRUE(older.contains("result")) << older.toString();
  EXPECT_FALSE(older["result"].contains("ttlMs"));
  EXPECT_FALSE(older["result"].contains("cacheScope"));
}

// A template's fields go out as set and come back as they went.
TEST(ResourceTemplates, AFullTemplateRoundTrips) {
  ResourceTemplate tmpl = filesTemplate();
  Annotations annotations;
  annotations.priority = 0.5;
  tmpl.annotations = annotations;
  const JsonValue json = to_json(tmpl);
  EXPECT_DOUBLE_EQ(json["annotations"]["priority"].getFloat(), 0.5);

  const ResourceTemplate back = from_json<ResourceTemplate>(json);
  EXPECT_EQ(back.uriTemplate, tmpl.uriTemplate);
  EXPECT_EQ(back.title, tmpl.title);
  ASSERT_TRUE(back.annotations.has_value());
  ASSERT_TRUE(back.annotations->priority.has_value());
  EXPECT_DOUBLE_EQ(back.annotations->priority.value(), 0.5);
  ASSERT_TRUE(back._meta.has_value());
  EXPECT_EQ(back._meta->toString(), json["_meta"].toString());
}

// A template that says nothing extra writes nothing extra.
TEST(ResourceTemplates, APlainTemplateWritesOnlyWhatItHas) {
  const JsonValue json =
      to_json(make<ResourceTemplate>("db://{table}", "tables").build());
  EXPECT_FALSE(json.contains("title"));
  EXPECT_FALSE(json.contains("annotations"));
  EXPECT_FALSE(json.contains("_meta"));
  EXPECT_FALSE(json.contains("description"));
}

// Read from any server: a field of the wrong type is passed over, not
// allowed to cost the template.
TEST(ResourceTemplates, ATemplateIsReadForgivingly) {
  const JsonValue json = JsonValue::parse(R"({
    "uriTemplate": "db://{table}", "name": "tables",
    "title": 7, "description": false, "mimeType": ["x"],
    "annotations": "high", "_meta": [1, 2]})");
  ResourceTemplate tmpl;
  ASSERT_NO_THROW(tmpl = from_json<ResourceTemplate>(json));
  EXPECT_EQ(tmpl.uriTemplate, "db://{table}");
  EXPECT_FALSE(tmpl.title.has_value());
  EXPECT_FALSE(tmpl.description.has_value());
  EXPECT_FALSE(tmpl.mimeType.has_value());
  EXPECT_FALSE(tmpl.annotations.has_value());
  EXPECT_FALSE(tmpl._meta.has_value());
}

// _meta must be an object to be sent; the string-entry setter still works.
TEST(ResourceTemplates, TheBuilderKeepsMetaAnObject) {
  EXPECT_THROW(make<ResourceTemplate>("a://{b}", "a").meta(JsonValue(3)),
               std::invalid_argument);
  const ResourceTemplate tmpl =
      make<ResourceTemplate>("a://{b}", "a").metadata("owner", "docs").build();
  ASSERT_TRUE(tmpl._meta.has_value());
  EXPECT_EQ((*tmpl._meta)["owner"].getString(), "docs");
}
