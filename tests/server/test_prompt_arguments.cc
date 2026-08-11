/**
 * What a prompt is given when a client asks for one.
 *
 * A prompt without its arguments is a template with nothing filled in,
 * which is not a smaller version of the right answer — it is a different
 * answer, returned confidently. The arguments do not travel as an
 * ordinary field: nested objects do not fit in the flat map a request's
 * params are held in, so they arrive as serialized JSON and have to be
 * parsed back before the handler sees them.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  optional<jsonrpc::Response> captured;
};

class PromptServer : public McpServer {
 public:
  explicit PromptServer(const McpServerConfig& config) : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "prompt-arguments-test";
  config.server_version = "0.0.1";
  return config;
}

/** A prompt whose answer is whatever it was given. */
void registerEcho(PromptServer& server, optional<Metadata>* seen) {
  Prompt greet("greet");
  greet.description = mcp::make_optional(std::string("Greet somebody"));
  server.registerPrompt(
      greet, [seen](const std::string&, const optional<Metadata>& arguments,
                    SessionContext&) {
        *seen = arguments;
        std::string name = "<nobody>";
        if (arguments.has_value()) {
          auto it = arguments->find("name");
          if (it != arguments->end() &&
              holds_alternative<std::string>(it->second)) {
            name = get<std::string>(it->second);
          }
        }
        GetPromptResult result;
        result.messages.push_back(
            PromptMessage(enums::Role::USER, TextContent("Hello, " + name)));
        return result;
      });
}

jsonrpc::Request getPrompt(const std::string& arguments_json) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = "prompts/get";
  Metadata params;
  params["name"] = MetadataValue(std::string("greet"));
  if (!arguments_json.empty()) {
    params["arguments"] = MetadataValue(arguments_json);
  }
  request.params = mcp::make_optional(params);
  return request;
}

TEST(PromptArguments, TheyReachTheHandler) {
  PromptServer server(testConfig());
  optional<Metadata> seen;
  registerEcho(server, &seen);
  CapturingContext context;

  server.onRequestWithContext(getPrompt(R"({"name":"Ada"})"), context);

  ASSERT_TRUE(seen.has_value()) << "the handler was given no arguments at all";
  auto it = seen->find("name");
  ASSERT_NE(it, seen->end()) << "the argument the client sent never arrived";
  ASSERT_TRUE(holds_alternative<std::string>(it->second));
  EXPECT_EQ(get<std::string>(it->second), "Ada");

  ASSERT_TRUE(context.captured.has_value());
  const std::string wire = json::to_json(context.captured.value()).toString();
  EXPECT_NE(wire.find("Hello, Ada"), std::string::npos) << wire;
}

// A prompt asked for without arguments is not the same as one asked for
// with unreadable ones, and neither is a reason to fail the request.
TEST(PromptArguments, NoneIsNotTheSameAsEmpty) {
  PromptServer server(testConfig());
  optional<Metadata> seen;
  registerEcho(server, &seen);
  CapturingContext context;

  server.onRequestWithContext(getPrompt(std::string()), context);

  EXPECT_FALSE(seen.has_value())
      << "a prompt asked for without arguments was given some";
  ASSERT_TRUE(context.captured.has_value());
  EXPECT_TRUE(context.captured->result.has_value());
}

TEST(PromptArguments, UnreadableOnesAreNoneRatherThanAFailure) {
  PromptServer server(testConfig());
  optional<Metadata> seen;
  registerEcho(server, &seen);
  CapturingContext context;

  server.onRequestWithContext(getPrompt("not json at all"), context);

  ASSERT_TRUE(context.captured.has_value());
  EXPECT_TRUE(context.captured->result.has_value())
      << "the request was failed over arguments the prompt may not need";
  ASSERT_TRUE(seen.has_value());
  EXPECT_TRUE(seen->empty());
}

}  // namespace
}  // namespace server
}  // namespace mcp
