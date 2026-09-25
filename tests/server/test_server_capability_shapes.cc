// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Server capabilities, in the shape the spec gives them.
 *
 * Every capability is an object with boolean flags inside:
 *
 *   {"tools": {"listChanged": true}, "prompts": {}, "logging": {},
 *    "resources": {"subscribe": true, "listChanged": true}}
 *
 * and initialize and server/discover must say the same thing, since both
 * answer the one question of what this server can do. A strict client
 * rejects `"tools": true` outright, and with it everything else the answer
 * said.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;

/** Widens the dispatch entry so a request can be handed straight in. */
class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

/** A return path that keeps what was sent instead of writing it. */
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
  config.server_name = "capability-shape-test";
  config.server_version = "0.0.1";
  return config;
}

/** The capabilities a request to this server answers with, as sent. */
JsonValue capabilitiesFrom(const McpServerConfig& config,
                           const std::string& method) {
  DispatchTestServer server(config);
  CapturingContext context;

  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  if (method == "initialize") {
    Metadata params;
    params["protocolVersion"] = MetadataValue(std::string("2025-06-18"));
    request.params = mcp::make_optional(params);
  }
  server.onRequestWithContext(request, context);

  if (!context.captured.has_value()) {
    ADD_FAILURE() << method << " went unanswered";
    return JsonValue::object();
  }
  const JsonValue wire = json::to_json(context.captured.value());
  if (!wire.contains("result") || !wire["result"].contains("capabilities")) {
    ADD_FAILURE() << method
                  << " answered with no capabilities: " << wire.toString();
    return JsonValue::object();
  }
  return wire["result"]["capabilities"];
}

const char* const kMethods[] = {"initialize",
                                protocol::modern::kMethodServerDiscover};

// Declared the way existing configuration does it, with a bare true, and
// written as the objects the spec requires, by both methods.
TEST(ServerCapabilityShapes, DeclaredWithTrueIsWrittenAsObjects) {
  McpServerConfig config = testConfig();
  config.capabilities.tools = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(true);
  config.capabilities.logging = mcp::make_optional(true);
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(true));

  for (const char* method : kMethods) {
    const JsonValue caps = capabilitiesFrom(config, method);
    SCOPED_TRACE(std::string(method) + ": " + caps.toString());

    ASSERT_TRUE(caps["tools"].isObject());
    EXPECT_TRUE(caps["prompts"].isObject());
    EXPECT_EQ(caps["prompts"].toString(), "{}");
    EXPECT_TRUE(caps["logging"].isObject());
    EXPECT_EQ(caps["logging"].toString(), "{}");
    EXPECT_TRUE(caps["resources"].isObject());
    EXPECT_EQ(caps["resources"].toString(), "{}");
  }
}

// A capability declared false is not declared, and is left out rather than
// written as false.
TEST(ServerCapabilityShapes, AnUndeclaredCapabilityIsLeftOut) {
  McpServerConfig config = testConfig();
  config.capabilities.tools = mcp::make_optional(false);
  config.capabilities.prompts = mcp::make_optional(false);
  config.capabilities.logging = mcp::make_optional(false);
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(false));

  for (const char* method : kMethods) {
    const JsonValue caps = capabilitiesFrom(config, method);
    SCOPED_TRACE(std::string(method) + ": " + caps.toString());

    EXPECT_FALSE(caps.contains("tools"));
    EXPECT_FALSE(caps.contains("prompts"));
    EXPECT_FALSE(caps.contains("logging"));
    EXPECT_FALSE(caps.contains("resources"));
  }
}

// The flags are booleans, and only what was configured is claimed. A server
// that can be subscribed to but never announces list changes says exactly
// that, not both.
TEST(ServerCapabilityShapes, FlagsAreBooleansAndOnlyWhatWasConfigured) {
  McpServerConfig config = testConfig();
  ResourcesCapability resources;
  resources.subscribe = mcp::make_optional(true);
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(resources));
  PromptsCapability prompts;
  prompts.listChanged = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(prompts);

  for (const char* method : kMethods) {
    const JsonValue caps = capabilitiesFrom(config, method);
    SCOPED_TRACE(std::string(method) + ": " + caps.toString());

    ASSERT_TRUE(caps["resources"].isObject());
    ASSERT_TRUE(caps["resources"]["subscribe"].isBoolean());
    EXPECT_TRUE(caps["resources"]["subscribe"].getBool());
    EXPECT_FALSE(caps["resources"].contains("listChanged"))
        << "list changes were claimed without being configured";

    ASSERT_TRUE(caps["prompts"]["listChanged"].isBoolean());
    EXPECT_TRUE(caps["prompts"]["listChanged"].getBool());
  }
}

// Both methods answer the same question, so they give the same answer, the
// tools listChanged setting included.
TEST(ServerCapabilityShapes, InitializeAndDiscoverAgree) {
  McpServerConfig config = testConfig();
  config.capabilities.tools = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(true);
  config.capabilities.logging = mcp::make_optional(true);
  ResourcesCapability resources;
  resources.subscribe = mcp::make_optional(true);
  resources.listChanged = mcp::make_optional(true);
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(resources));
  config.tools_list_changed = true;

  const JsonValue introduced = capabilitiesFrom(config, "initialize");
  const JsonValue discovered =
      capabilitiesFrom(config, protocol::modern::kMethodServerDiscover);

  EXPECT_EQ(introduced.toString(), discovered.toString());
  ASSERT_TRUE(discovered["tools"]["listChanged"].isBoolean());
  EXPECT_TRUE(discovered["tools"]["listChanged"].getBool());
}

// What a client reads: the objects and their flags, and the bare booleans
// and object-valued flags that older servers of this SDK sent.
TEST(ServerCapabilityShapes, TheClientReadsObjectsAndOlderShapes) {
  const auto current = json::from_json<ServerCapabilities>(JsonValue::parse(
      R"({"tools": {"listChanged": true}, "prompts": {"listChanged": false},
          "logging": {}, "resources": {"subscribe": true}})"));
  ASSERT_TRUE(current.tools.has_value());
  EXPECT_TRUE(static_cast<bool>(current.tools.value()));
  EXPECT_TRUE(current.tools->listChanged.value());
  ASSERT_TRUE(current.prompts.has_value());
  EXPECT_FALSE(current.prompts->listChanged.value());
  ASSERT_TRUE(current.logging.has_value());
  EXPECT_TRUE(static_cast<bool>(current.logging.value()));
  const auto& resources = get<ResourcesCapability>(current.resources.value());
  EXPECT_TRUE(resources.subscribe.value());
  EXPECT_FALSE(resources.listChanged.has_value());

  const auto older = json::from_json<ServerCapabilities>(JsonValue::parse(
      R"({"tools": true, "prompts": false, "logging": true,
          "resources": {"subscribe": {}, "listChanged": {}}})"));
  ASSERT_TRUE(older.tools.has_value());
  EXPECT_TRUE(static_cast<bool>(older.tools.value()));
  EXPECT_FALSE(older.tools->listChanged.has_value());
  ASSERT_TRUE(older.prompts.has_value());
  EXPECT_FALSE(static_cast<bool>(older.prompts.value()))
      << "a capability sent as false was read as declared";
  EXPECT_TRUE(static_cast<bool>(older.logging.value()));
  const auto& old_resources = get<ResourcesCapability>(older.resources.value());
  EXPECT_TRUE(old_resources.subscribe.value());
  EXPECT_TRUE(old_resources.listChanged.value());
}

}  // namespace
}  // namespace server
}  // namespace mcp
