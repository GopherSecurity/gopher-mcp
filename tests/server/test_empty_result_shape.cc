/**
 * What an answer with nothing to say looks like on the wire.
 *
 * Several methods succeed without returning anything: subscribing to a
 * resource, unsubscribing from one. "Nothing" in JSON-RPC is an empty
 * result object, and the distinction from a null one is not cosmetic —
 * a peer validating messages against the schema cannot parse a response
 * whose result is null, so it does not report a bad result. It reports
 * that the message was not a response at all, and the reason it gives
 * names whatever the first branch of its union happened to be.
 *
 * These tests assert the serialized bytes rather than the C++ value,
 * because that is the only place the difference exists.
 */

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

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

  /** The answer as a peer would read it. */
  std::string wire() const {
    if (!captured.has_value()) {
      return std::string();
    }
    return json::to_json(captured.value()).toString();
  }

  optional<jsonrpc::Response> captured;
};

jsonrpc::Request withUri(int64_t id, const std::string& method) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(id);
  request.method = method;
  Metadata params;
  params["uri"] = MetadataValue(std::string("test://resource"));
  request.params = mcp::make_optional(params);
  return request;
}

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "empty-result-test";
  config.server_version = "0.0.1";
  return config;
}

TEST(EmptyResultShape, SubscribingAnswersWithAnEmptyObject) {
  DispatchTestServer server(testConfig());
  CapturingContext context;

  server.onRequestWithContext(withUri(1, "resources/subscribe"), context);

  ASSERT_TRUE(context.captured.has_value()) << "subscribe went unanswered";
  const std::string wire = context.wire();
  EXPECT_NE(wire.find("\"result\":{}"), std::string::npos)
      << "an empty result is {}, not anything else: " << wire;
  EXPECT_EQ(wire.find("\"result\":null"), std::string::npos)
      << "a null result is not a result: " << wire;
}

TEST(EmptyResultShape, UnsubscribingAnswersWithAnEmptyObject) {
  DispatchTestServer server(testConfig());
  CapturingContext context;

  server.onRequestWithContext(withUri(1, "resources/subscribe"), context);
  context.captured.reset();
  server.onRequestWithContext(withUri(2, "resources/unsubscribe"), context);

  ASSERT_TRUE(context.captured.has_value()) << "unsubscribe went unanswered";
  const std::string wire = context.wire();
  EXPECT_NE(wire.find("\"result\":{}"), std::string::npos)
      << "an empty result is {}, not anything else: " << wire;
  EXPECT_EQ(wire.find("\"result\":null"), std::string::npos)
      << "a null result is not a result: " << wire;
}

// The other half of the same rule: an answer must carry exactly one of
// result and error, so neither may be a failure wearing a success's
// shape.
TEST(EmptyResultShape, AnEmptyAnswerStillCarriesNoError) {
  DispatchTestServer server(testConfig());
  CapturingContext context;

  server.onRequestWithContext(withUri(1, "resources/subscribe"), context);

  ASSERT_TRUE(context.captured.has_value());
  EXPECT_TRUE(context.captured->result.has_value());
  EXPECT_FALSE(context.captured->error.has_value());
  EXPECT_EQ(context.wire().find("\"error\""), std::string::npos)
      << "a success must not also carry an error: " << context.wire();
}

// With no handshake there is no other moment to ask what a server is, so
// this is the one method every server must answer. Served in both eras: a
// method being new does not make it modern-only, and a classic client is
// entitled to the same answer.
TEST(ServerDiscover, AServerSaysWhatItIsAndWhatItSpeaks) {
  McpServerConfig config = testConfig();
  config.server_name = "discoverable";
  config.server_version = "2.0.0";
  config.instructions = "how to use this";
  config.streamable_http.enable_modern_era = true;
  DispatchTestServer server(config);
  CapturingContext context;

  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = "server/discover";
  server.onRequestWithContext(request, context);

  ASSERT_TRUE(context.captured.has_value())
      << "server/discover went unanswered";
  const std::string wire = context.wire();

  EXPECT_NE(wire.find("\"supportedVersions\""), std::string::npos) << wire;
  EXPECT_NE(wire.find("2026-07-28"), std::string::npos)
      << "a server serving the newest revision did not say so: " << wire;
  EXPECT_NE(wire.find("io.modelcontextprotocol/serverInfo"), std::string::npos)
      << "a server that cannot be introduced to did not name itself: " << wire;
  EXPECT_NE(wire.find("discoverable"), std::string::npos) << wire;
  EXPECT_NE(wire.find("how to use this"), std::string::npos) << wire;
}

// And it never claims a revision this server was told not to serve.
TEST(ServerDiscover, ItNamesOnlyWhatIsActuallyServed) {
  auto config = testConfig();
  config.streamable_http.enable_modern_era = false;
  DispatchTestServer server(config);
  CapturingContext context;

  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = "server/discover";
  server.onRequestWithContext(request, context);

  ASSERT_TRUE(context.captured.has_value());
  EXPECT_EQ(context.wire().find("2026-07-28"), std::string::npos)
      << "a revision this server was told not to serve was advertised: "
      << context.wire();
}

// A tool whose designations both ends cannot resolve identically is
// refused, and the rest of the registry is unaffected: every call to it
// would be rejected for a mismatch neither end introduced, and one bad
// definition must not cost the others.
TEST(ToolDesignations, AnUnusableDefinitionIsRefusedAndTheRestAreServed) {
  DispatchTestServer server(testConfig());

  Tool good("add");
  good.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{"a":{"type":"integer"}}})"));
  EXPECT_TRUE(server.registerTool(
      good, [](const std::string&, const optional<Metadata>&, SessionContext&) {
        return CallToolResult();
      }));

  Tool bad("execute_sql");
  bad.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{
          "n":{"type":"number","x-mcp-header":"N"}}})"));
  EXPECT_FALSE(server.registerTool(bad, [](const std::string&,
                                           const optional<Metadata>&,
                                           SessionContext&) {
    return CallToolResult();
  })) << "a tool nobody could call correctly was registered";

  CapturingContext context;
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = "tools/list";
  server.onRequestWithContext(request, context);

  ASSERT_TRUE(context.captured.has_value());
  const std::string wire = context.wire();
  EXPECT_NE(wire.find("\"add\""), std::string::npos)
      << "a usable tool was lost with the unusable one: " << wire;
  EXPECT_EQ(wire.find("execute_sql"), std::string::npos)
      << "a refused tool was listed anyway: " << wire;
}

// A server whose tool set is fixed says so, and one that discovers tools later
// says the opposite. Asserted through a real initialize rather than a
// hand-built JSON blob, so that the capability stays wired to the config: an
// aggregator that stopped advertising listChanged would leave every client
// showing whatever existed at connect time, with nothing failing to say so.
jsonrpc::Request initializeRequest(int64_t id) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(id);
  request.method = "initialize";
  Metadata params;
  params["protocolVersion"] = MetadataValue(std::string("2025-03-26"));
  request.params = mcp::make_optional(params);
  return request;
}

TEST(InitializeCapabilities, AFixedToolSetSaysItDoesNotChange) {
  McpServerConfig config = testConfig();
  config.capabilities.tools = mcp::make_optional(true);
  DispatchTestServer server(config);
  CapturingContext context;

  server.onRequestWithContext(initializeRequest(1), context);

  ASSERT_TRUE(context.captured.has_value()) << "initialize went unanswered";
  const std::string wire = context.wire();
  EXPECT_NE(wire.find("\"listChanged\":false"), std::string::npos)
      << "a server that never gains tools claimed it might: " << wire;
}

TEST(InitializeCapabilities, AServerThatGainsToolsLaterSaysSo) {
  McpServerConfig config = testConfig();
  config.capabilities.tools = mcp::make_optional(true);
  config.tools_list_changed = true;
  DispatchTestServer server(config);
  CapturingContext context;

  server.onRequestWithContext(initializeRequest(1), context);

  ASSERT_TRUE(context.captured.has_value()) << "initialize went unanswered";
  const std::string wire = context.wire();
  EXPECT_NE(wire.find("\"listChanged\":true"), std::string::npos)
      << "a server configured to gain tools did not advertise it, so no client "
         "will ever re-list: "
      << wire;
}

}  // namespace
}  // namespace server
}  // namespace mcp
