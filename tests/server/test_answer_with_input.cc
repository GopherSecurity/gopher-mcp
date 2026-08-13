/**
 * A handler that cannot finish, answering with what it still needs.
 *
 * Servers initiate nothing in the newest revision, so this is the only
 * way one asks: the answer to the request *is* the question, the client
 * makes the whole request again with what was asked for, and the handler
 * runs a second time with it in hand.
 *
 * The rule that carries the most weight is the one about capabilities. A
 * question a caller cannot answer sits unanswerable — the caller has no
 * way to say "I can't" — so a server that asks anyway has hung the
 * request. It is refused instead, and the refusal names what the caller
 * would have had to declare.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using protocol::modern::InputRequest;
using protocol::modern::NeedsInput;

/** A stream that keeps what was written, refusals included. */
class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(const jsonrpc::Notification&) override {
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  VoidResult sendRefusal(int http_status,
                         const Error& error,
                         const json::JsonValue& data) override {
    refusal_status = http_status;
    refusal = mcp::make_optional(error);
    refusal_data = data;
    return makeVoidSuccess();
  }
  bool alive() const override { return true; }

  std::vector<jsonrpc::Response> answered;
  int refusal_status{0};
  optional<Error> refusal;
  json::JsonValue refusal_data;
};

class AskingServer : public McpServer {
 public:
  explicit AskingServer(const McpServerConfig& config) : McpServer(config) {}
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "answer-with-input-test";
  config.server_version = "0.0.1";
  return config;
}

jsonrpc::Request call(const std::string& method = "tools/call") {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  return request;
}

InputRequest elicit() {
  InputRequest request;
  request.method = protocol::modern::kMethodElicitation;
  request.params = json::JsonValue::object();
  request.params.set("message", json::JsonValue("Who are you?"));
  return request;
}

/** A session whose caller declared these capabilities, or none. */
std::unique_ptr<SessionContext> callerDeclaring(
    const std::string& capabilities_json) {
  std::unique_ptr<SessionContext> session(new SessionContext("s-1", nullptr));
  if (!capabilities_json.empty()) {
    session->setRequestMeta(mcp::make_optional(
        std::string("{\"") + protocol::modern::kMetaClientCapabilities +
        "\":" + capabilities_json + "}"));
  }
  return session;
}

TEST(AnswerWithInput, TheAnswerIsTheQuestion) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  auto session = callerDeclaring(R"({"elicitation":{}})");

  NeedsInput needed;
  needed.requests["who"] = elicit();
  needed.request_state = mcp::make_optional(std::string("round-1"));

  auto sent = server.answerWithInput(stream, call(), *session, needed);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(sent))
      << get<Error>(sent).message;

  ASSERT_EQ(stream->answered.size(), 1u);
  ASSERT_TRUE(stream->answered[0].result.has_value());
  const auto& result = stream->answered[0].result.value();
  ASSERT_TRUE(holds_alternative<json::JsonValue>(result));
  const auto& body = get<json::JsonValue>(result);

  EXPECT_EQ(body[protocol::modern::kResultTypeField].getString(),
            protocol::modern::kResultTypeInputRequired)
      << "a question was sent wearing the shape of an answer";
  EXPECT_TRUE(body[protocol::modern::kInputRequestsField].contains("who"));
  EXPECT_EQ(body[protocol::modern::kRequestStateField].getString(), "round-1");
  EXPECT_EQ(stream->refusal_status, 0) << "a permitted question was refused";
}

// The rule with the most weight: a caller that cannot answer must not be
// asked, because it has no way to say so and the request would hang.
TEST(AnswerWithInput, ACallerIsNeverAskedWhatItCannotAnswer) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  auto session = callerDeclaring(R"({"sampling":{}})");

  NeedsInput needed;
  needed.requests["who"] = elicit();

  auto sent = server.answerWithInput(stream, call(), *session, needed);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(sent));

  EXPECT_TRUE(stream->answered.empty())
      << "a caller was asked something it said it cannot do";
  EXPECT_EQ(stream->refusal_status, 400);
  ASSERT_TRUE(stream->refusal.has_value());
  EXPECT_EQ(stream->refusal->code,
            protocol::modern::kMissingRequiredClientCapability);
  ASSERT_TRUE(stream->refusal_data.contains(
      protocol::modern::kRequiredCapabilitiesField));
  EXPECT_TRUE(stream->refusal_data[protocol::modern::kRequiredCapabilitiesField]
                  .contains("elicitation"))
      << "the refusal did not say what the caller would have had to declare";
}

// A caller that declared nothing at all is a caller that cannot be asked
// for anything. Reading silence as consent is the wrong way to be wrong.
TEST(AnswerWithInput, ACallerThatDeclaredNothingIsAskedNothing) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  auto session = callerDeclaring(std::string());

  NeedsInput needed;
  needed.requests["who"] = elicit();

  server.answerWithInput(stream, call(), *session, needed);
  EXPECT_TRUE(stream->answered.empty());
  EXPECT_EQ(stream->refusal_status, 400);
}

// Carrying state forward without asking for anything needs no capability
// at all — there is nothing for the caller to do.
TEST(AnswerWithInput, CarryingStateAloneNeedsNothingOfTheCaller) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  auto session = callerDeclaring(std::string());

  NeedsInput needed;
  needed.request_state = mcp::make_optional(std::string("come-back"));

  auto sent = server.answerWithInput(stream, call(), *session, needed);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(sent));
  EXPECT_EQ(stream->answered.size(), 1u);
  EXPECT_EQ(stream->refusal_status, 0);
}

// Only three requests may be answered this way. Answering anything else
// would ask a client to retry something it has no reason to think is
// retriable.
TEST(AnswerWithInput, OnlySomeRequestsMayBeAnsweredThisWay) {
  AskingServer server(testConfig());
  auto session = callerDeclaring(R"({"elicitation":{}})");

  NeedsInput needed;
  needed.requests["who"] = elicit();

  for (const char* method : {"tools/call", "resources/read", "prompts/get"}) {
    auto stream = std::make_shared<RecordingStream>();
    EXPECT_TRUE(holds_alternative<std::nullptr_t>(
        server.answerWithInput(stream, call(method), *session, needed)))
        << method << " should have been answerable this way";
  }

  for (const char* method : {"tools/list", "server/discover", "ping"}) {
    auto stream = std::make_shared<RecordingStream>();
    auto sent = server.answerWithInput(stream, call(method), *session, needed);
    EXPECT_TRUE(holds_alternative<Error>(sent))
        << method << " was answered by asking for input";
    EXPECT_TRUE(stream->answered.empty());
  }
}

// Asking for nothing and carrying nothing tells a caller to come back
// with no reason and nothing to come back with.
TEST(AnswerWithInput, AskingForNothingIsNotAQuestion) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  auto session = callerDeclaring(R"({"elicitation":{}})");

  auto sent = server.answerWithInput(stream, call(), *session, NeedsInput());
  EXPECT_TRUE(holds_alternative<Error>(sent));
  EXPECT_TRUE(stream->answered.empty());
}

}  // namespace
}  // namespace server
}  // namespace mcp
