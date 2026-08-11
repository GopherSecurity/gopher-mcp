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

}  // namespace
}  // namespace server
}  // namespace mcp
