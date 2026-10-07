// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Which callers a server may ask for elicitation. Forms first appear in
 * 2025-06-18 and URL mode in 2025-11-25; before those, or without the
 * capability declared, a caller is not asked.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {
    // Says which session a request was served in, which is what the
    // question below is asked about.
    registerRequestHandler(
        "test/whoami",
        [this](const jsonrpc::Request& request, SessionContext& session) {
          session_id = session.getId();
          return jsonrpc::Response::success(
              request.id, jsonrpc::ResponseResult(JsonValue::object()));
        });
  }
  using McpServer::onRequestWithContext;
  std::string session_id;
};

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response&) override {
    return makeVoidSuccess();
  }
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "elicitation-gate-test";
  config.server_version = "0.0.1";
  return config;
}

/** Introduce a caller at this revision, declaring these capabilities. */
std::string introduce(DispatchTestServer& server,
                      const std::string& revision,
                      const std::string& capabilities) {
  jsonrpc::Request init;
  init.jsonrpc = "2.0";
  init.id = make_request_id(1);
  init.method = "initialize";
  JsonValue params = JsonValue::object();
  params.set("protocolVersion", JsonValue(revision));
  params.set("capabilities", JsonValue::parse(capabilities));
  JsonValue info = JsonValue::object();
  info.set("name", JsonValue("c"));
  info.set("version", JsonValue("1"));
  params.set("clientInfo", info);
  init.params_json = mcp::make_optional(params);
  init.params = mcp::make_optional(json::jsonToMetadata(params));
  CapturingContext context;
  server.onRequestWithContext(init, context);

  jsonrpc::Request who;
  who.jsonrpc = "2.0";
  who.id = make_request_id(2);
  who.method = "test/whoami";
  server.onRequestWithContext(who, context);
  return server.session_id;
}

}  // namespace

// Forms only from 2025-06-18, when elicitation first appears.
TEST(ElicitationGate, FormsStartIn20250618) {
  const std::string both = R"({"elicitation":{"form":{},"url":{}}})";
  {
    DispatchTestServer server(testConfig());
    const std::string id = introduce(server, "2025-03-26", both);
    EXPECT_FALSE(server.sessionSupportsElicitation(id, "form"))
        << "a 2025-03-26 caller was offered elicitation";
  }
  {
    DispatchTestServer server(testConfig());
    const std::string id = introduce(server, "2025-06-18", both);
    EXPECT_TRUE(server.sessionSupportsElicitation(id, "form"));
    EXPECT_FALSE(server.sessionSupportsElicitation(id, "url"))
        << "URL mode is from 2025-11-25";
  }
  {
    DispatchTestServer server(testConfig());
    const std::string id = introduce(server, "2025-11-25", both);
    EXPECT_TRUE(server.sessionSupportsElicitation(id, "form"));
    EXPECT_TRUE(server.sessionSupportsElicitation(id, "url"));
  }
}

// Each mode only when the caller declared it.
TEST(ElicitationGate, EachModeOnlyWhenDeclared) {
  DispatchTestServer server(testConfig());
  const std::string id =
      introduce(server, "2025-11-25", R"({"elicitation":{"form":{}}})");
  EXPECT_TRUE(server.sessionSupportsElicitation(id, "form"));
  EXPECT_FALSE(server.sessionSupportsElicitation(id, "url"));

  DispatchTestServer silent(testConfig());
  const std::string none = introduce(silent, "2025-11-25", "{}");
  EXPECT_FALSE(silent.sessionSupportsElicitation(none, "form"));
}

}  // namespace server
}  // namespace mcp
