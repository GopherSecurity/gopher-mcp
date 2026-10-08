// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Extensions in capabilities, and built-in methods gated on what a server
 * advertises.
 *
 *   capabilities  {"extensions": {"io.modelcontextprotocol/tasks": {}},
 *                  "experimental": {"name": {...}}, ...}
 *
 * An extension identifier has a required prefix of dot-separated labels,
 * a slash, and a name. Each side advertises its extensions and can ask
 * whether the other did. A built-in method whose capability a server
 * doesn't advertise is not found; a server advertises what it registers.
 */

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/extensions.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;
namespace extensions = protocol::extensions;

#define EXPECT_SAME_JSON(a, b) EXPECT_EQ((a).toString(), (b).toString())

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

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "extensions-test";
  config.server_version = "1.0.0";
  config.send_server_info = false;
  return config;
}

jsonrpc::Request requestFor(const std::string& method,
                            JsonValue params = JsonValue::object(),
                            optional<JsonValue> meta = nullopt) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(4);
  request.method = method;
  if (meta.has_value()) {
    params.set("_meta", meta.value());
  }
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(json::jsonToMetadata(params));
  return request;
}

JsonValue answerTo(McpServer& server, const jsonrpc::Request& request) {
  CapturingContext context;
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  EXPECT_TRUE(context.captured.has_value()) << request.method;
  return context.captured.has_value() ? json::to_json(context.captured.value())
                                      : JsonValue::object();
}

int errorCodeOf(const JsonValue& answer) {
  return answer.contains("error")
             ? static_cast<int>(answer["error"]["code"].getInt64())
             : 0;
}

JsonValue capabilitiesIn(const JsonValue& answer) {
  return answer["result"]["capabilities"];
}

JsonValue modernMeta() {
  JsonValue meta = JsonValue::object();
  meta.set(protocol::modern::kMetaProtocolVersion, JsonValue("2026-07-28"));
  meta.set(protocol::modern::kMetaClientCapabilities, JsonValue::object());
  return meta;
}

}  // namespace

// ── Identifiers ────────────────────────────────────────────────────────

TEST(ExtensionsCapabilities, IdentifiersFollowTheMetaKeyRules) {
  for (const char* good : {"io.modelcontextprotocol/tasks", "com.example/ui",
                           "a/b", "x-1.y2/n_a.m-e", "example/0name9"}) {
    EXPECT_TRUE(extensions::isValidId(good)) << good;
  }
  for (const char* bad :
       {"", "tasks", "/tasks", "io.modelcontextprotocol/", "1io.example/x",
        "io-.example/x", "io..example/x", "io.example/-x", "io.example/x-",
        "io.example/x y", "io_example/x", "io.example/x/y"}) {
    EXPECT_FALSE(extensions::isValidId(bad)) << bad;
  }
}

// ── On the wire ────────────────────────────────────────────────────────

TEST(ExtensionsCapabilities, ExtensionsAndExperimentalRoundTrip) {
  const JsonValue wire = JsonValue::parse(R"({
      "extensions":{"io.modelcontextprotocol/tasks":{},
                    "com.example/ui":{"theme":{"mode":"dark"}}},
      "experimental":{"preview":{"level":2}},
      "tools":{}})");
  const auto server = json::from_json<ServerCapabilities>(wire);
  ASSERT_TRUE(server.extensions.has_value());
  EXPECT_SAME_JSON(json::to_json(server), wire);

  const JsonValue client_wire = JsonValue::parse(R"({
      "extensions":{"io.modelcontextprotocol/ui":{"mimeTypes":["text/html"]}},
      "experimental":{"preview":{}}})");
  EXPECT_SAME_JSON(
      json::to_json(json::from_json<ClientCapabilities>(client_wire)),
      client_wire);
}

// What a peer sends of the wrong shape is passed over, not allowed to
// cost the handshake.
TEST(ExtensionsCapabilities, MalformedEntriesArePassedOver) {
  const auto read = json::from_json<ServerCapabilities>(JsonValue::parse(R"({
      "extensions":{"io.modelcontextprotocol/tasks":{},"not-an-id":{},
                    "com.example/flag":true},
      "experimental":{"ok":{},"flag":true,"text":"x"}})"));
  EXPECT_SAME_JSON(read.extensions.value(),
                   JsonValue::parse(R"({"io.modelcontextprotocol/tasks":{}})"));
  EXPECT_SAME_JSON(read.experimental.value(), JsonValue::parse(R"({"ok":{}})"));

  const auto none = json::from_json<ClientCapabilities>(
      JsonValue::parse(R"({"extensions":[],"experimental":"x"})"));
  EXPECT_FALSE(none.extensions.has_value());
  EXPECT_FALSE(none.experimental.has_value());
}

// The flat-map form still builds experimental, reading JSON text as the
// object it spells.
TEST(ExtensionsCapabilities, BuildersSetBoth) {
  Metadata flat;
  flat["preview"] = std::string(R"({"level":2})");
  flat["ignored"] = std::string("plain text");
  const ServerCapabilities caps =
      make<ServerCapabilities>()
          .experimental(flat)
          .extension("io.modelcontextprotocol/tasks")
          .build();
  EXPECT_SAME_JSON(caps.experimental.value(),
                   JsonValue::parse(R"({"preview":{"level":2}})"));
  EXPECT_SAME_JSON(json::to_json(caps)["extensions"],
                   JsonValue::parse(R"({"io.modelcontextprotocol/tasks":{}})"));
}

// ── Advertising ────────────────────────────────────────────────────────

TEST(ExtensionsCapabilities, AServerAdvertisesItsExtensions) {
  McpServerConfig config = testConfig();
  config.extensions["io.modelcontextprotocol/tasks"] = JsonValue::object();
  config.extensions["com.example/ui"] =
      JsonValue::parse(R"({"theme":"configured"})");
  config.capabilities.extensions =
      JsonValue::parse(R"({"com.example/ui":{"theme":"own"}})");
  DispatchTestServer server(config);

  const JsonValue expected = JsonValue::parse(R"({
      "com.example/ui":{"theme":"own"},
      "io.modelcontextprotocol/tasks":{}})");
  EXPECT_SAME_JSON(
      capabilitiesIn(answerTo(server, requestFor("initialize")))["extensions"],
      expected);
  EXPECT_SAME_JSON(
      capabilitiesIn(
          answerTo(server, requestFor("server/discover", JsonValue::object(),
                                      modernMeta())))["extensions"],
      expected);
}

TEST(ExtensionsCapabilities, AServerWithNoExtensionsAdvertisesNone) {
  DispatchTestServer server(testConfig());
  EXPECT_FALSE(capabilitiesIn(answerTo(server, requestFor("initialize")))
                   .contains("extensions"));
}

TEST(ExtensionsCapabilities, AnInvalidConfiguredExtensionIsRefused) {
  McpServerConfig bad_id = testConfig();
  bad_id.extensions["tasks"] = JsonValue::object();
  EXPECT_THROW(DispatchTestServer server(bad_id), std::invalid_argument);

  McpServerConfig bad_settings = testConfig();
  bad_settings.extensions["com.example/ui"] = JsonValue(true);
  EXPECT_THROW(DispatchTestServer server(bad_settings), std::invalid_argument);

  McpServerConfig bad_declared = testConfig();
  bad_declared.capabilities.extensions =
      JsonValue::parse(R"({"no-prefix":{}})");
  EXPECT_THROW(DispatchTestServer server(bad_declared), std::invalid_argument);
}

// ── What the client advertised ─────────────────────────────────────────

TEST(ExtensionsCapabilities, AServerCanAskWhatTheClientAdvertised) {
  DispatchTestServer server(testConfig());
  bool earlier = false;
  JsonValue settings;
  bool modern = false;
  server.registerRequestHandler(
      "example/ask",
      [&](const jsonrpc::Request& request, SessionContext& session) {
        if (session.getRequestMeta().has_value()) {
          modern = session.clientHasExtension("com.example/ui");
          auto found = session.clientExtension("com.example/ui");
          if (found.has_value()) {
            settings = found.value();
          }
        } else {
          earlier = session.clientHasExtension("io.modelcontextprotocol/tasks");
        }
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      });

  // An earlier revision says it once, in initialize.
  JsonValue init = JsonValue::object();
  init.set("protocolVersion", JsonValue("2025-11-25"));
  init.set("capabilities",
           JsonValue::parse(
               R"({"extensions":{"io.modelcontextprotocol/tasks":{}}})"));
  init.set("clientInfo", JsonValue::parse(R"({"name":"c","version":"1"})"));
  answerTo(server, requestFor("initialize", init));
  answerTo(server, requestFor("example/ask"));
  EXPECT_TRUE(earlier);

  // 2026-07-28 says it on every request.
  JsonValue meta = modernMeta();
  meta.set(protocol::modern::kMetaClientCapabilities,
           JsonValue::parse(
               R"({"extensions":{"com.example/ui":{"theme":"dark"}}})"));
  answerTo(server, requestFor("example/ask", JsonValue::object(), meta));
  EXPECT_TRUE(modern);
  EXPECT_SAME_JSON(settings, JsonValue::parse(R"({"theme":"dark"})"));
}

// ── Gating built-in methods ────────────────────────────────────────────

// A server that offers nothing finds none of the built-in feature methods.
TEST(ExtensionsCapabilities, UnadvertisedMethodsAreNotFound) {
  DispatchTestServer server(testConfig());
  for (const char* method : {"tools/list", "tools/call", "prompts/list",
                             "prompts/get", "resources/list", "resources/read",
                             "resources/templates/list", "logging/setLevel"}) {
    SCOPED_TRACE(method);
    EXPECT_EQ(errorCodeOf(answerTo(server, requestFor(method))),
              jsonrpc::METHOD_NOT_FOUND);
  }
  // What has no capability is always there.
  EXPECT_EQ(errorCodeOf(answerTo(server, requestFor("ping"))), 0);
}

// A server advertises what it registers, so it never refuses its own
// features.
TEST(ExtensionsCapabilities, WhatIsRegisteredIsAdvertisedAndServed) {
  DispatchTestServer server(testConfig());
  server.registerTool(Tool("echo"),
                      [](const std::string&, const optional<Metadata>&,
                         SessionContext&) { return CallToolResult(); });
  server.registerPrompt(Prompt("greet"),
                        [](const std::string&, const optional<Metadata>&,
                           SessionContext&) { return GetPromptResult(); });
  server.registerResource(Resource("file:///a", "a"));

  const JsonValue caps =
      capabilitiesIn(answerTo(server, requestFor("initialize")));
  EXPECT_TRUE(caps.contains("tools")) << caps.toString();
  EXPECT_TRUE(caps.contains("prompts")) << caps.toString();
  EXPECT_TRUE(caps.contains("resources")) << caps.toString();
  for (const char* method : {"tools/list", "prompts/list", "resources/list"}) {
    SCOPED_TRACE(method);
    EXPECT_EQ(errorCodeOf(answerTo(server, requestFor(method))), 0);
  }
  // Still nothing for logging, which nothing here offers.
  EXPECT_EQ(errorCodeOf(answerTo(server, requestFor("logging/setLevel"))),
            jsonrpc::METHOD_NOT_FOUND);
}

// A handler registered for a method counts as offering it.
TEST(ExtensionsCapabilities, AHandlerForAMethodOffersIt) {
  DispatchTestServer server(testConfig());
  server.registerRequestHandler(
      "tools/list", [](const jsonrpc::Request& request, SessionContext&) {
        return jsonrpc::Response::success(
            request.id,
            jsonrpc::ResponseResult(JsonValue::parse(R"({"tools":[]})")));
      });
  EXPECT_EQ(errorCodeOf(answerTo(server, requestFor("tools/list"))), 0);
  EXPECT_TRUE(capabilitiesIn(answerTo(server, requestFor("initialize")))
                  .contains("tools"));
}

// One configured either way stays as configured: a server that says it
// has no tools has none, whatever is registered.
TEST(ExtensionsCapabilities, AConfiguredCapabilityIsKept) {
  McpServerConfig config = testConfig();
  config.capabilities.tools = mcp::make_optional(ToolsCapability(false));
  config.capabilities.logging = mcp::make_optional(LoggingCapability(true));
  DispatchTestServer server(config);
  server.registerTool(Tool("echo"),
                      [](const std::string&, const optional<Metadata>&,
                         SessionContext&) { return CallToolResult(); });
  bool level_set = false;
  server.registerRequestHandler(
      "logging/setLevel",
      [&level_set](const jsonrpc::Request& request, SessionContext&) {
        level_set = true;
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(JsonValue::object()));
      });

  EXPECT_EQ(errorCodeOf(answerTo(server, requestFor("tools/list"))),
            jsonrpc::METHOD_NOT_FOUND);
  EXPECT_EQ(errorCodeOf(answerTo(server, requestFor("logging/setLevel"))), 0);
  EXPECT_TRUE(level_set);
}

}  // namespace server
}  // namespace mcp
