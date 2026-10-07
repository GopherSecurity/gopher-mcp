// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Completion: suggestions for a prompt's arguments or a resource template's
 * parameters while a user fills them in.
 *
 *   request  completion/complete
 *            {"ref": {"type": "ref/prompt", "name"} |
 *                    {"type": "ref/resource", "uri"},
 *             "argument": {"name", "value"},
 *             "context"?: {"arguments"?: {...}}}
 *   result   {"completion": {"values": [...], "total"?, "hasMore"?}}
 *
 * A server with any completion handler advertises the completions
 * capability, and answers with at most 100 values. These compare against
 * the JSON another implementation sends and expects.
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/builders.h"
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

#define EXPECT_SAME_JSON(a, b) EXPECT_EQ((a).toString(), (b).toString())

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(const jsonrpc::Notification&) override {
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  bool alive() const override { return true; }
  std::vector<jsonrpc::Response> answered;
};

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  ResponseStreamPtr beginResponseStream() override { return stream; }
  optional<jsonrpc::Response> captured;
  std::shared_ptr<RecordingStream> stream = std::make_shared<RecordingStream>();
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "completion-test";
  config.server_version = "1.0.0";
  config.send_server_info = false;
  return config;
}

/** A request with these params, of 2026-07-28 when modern. */
jsonrpc::Request requestFor(const std::string& method,
                            JsonValue params,
                            bool modern) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(3);
  request.method = method;
  if (modern) {
    JsonValue meta = JsonValue::object();
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue("2026-07-28"));
    params.set("_meta", meta);
  }
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(json::jsonToMetadata(params));
  return request;
}

JsonValue answerTo(McpServer& server, const jsonrpc::Request& request) {
  CapturingContext context;
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  if (context.captured.has_value()) {
    return json::to_json(context.captured.value());
  }
  if (!context.stream->answered.empty()) {
    return json::to_json(context.stream->answered.back());
  }
  ADD_FAILURE() << request.method << " went unanswered";
  return JsonValue::object();
}

JsonValue complete(McpServer& server,
                   const std::string& params,
                   bool modern = false) {
  return answerTo(server, requestFor("completion/complete",
                                     JsonValue::parse(params), modern));
}

int errorCodeOf(const JsonValue& answer) {
  if (!answer.contains("error")) {
    return 0;
  }
  return static_cast<int>(answer["error"]["code"].getInt64());
}

/** Languages starting with what was typed. */
CompleteResult::Completion languages(const McpServer::CompletionQuery& query) {
  CompleteResult::Completion completion;
  if (query.argument != "language") {
    return completion;
  }
  for (const char* language : {"python", "pytorch", "rust"}) {
    if (std::string(language).compare(0, query.value.size(), query.value) ==
        0) {
      completion.values.push_back(language);
    }
  }
  return completion;
}

const char* const kPromptRequest =
    R"({"ref":{"type":"ref/prompt","name":"code_review"},
        "argument":{"name":"language","value":"py"}})";

}  // namespace

// ── The request and result on the wire ────────────────────────────────

TEST(Completion, ARequestIsWrittenInTheSpecsShape) {
  const CompleteRequest prompt = make<CompleteRequest>("ref/prompt", "review")
                                     .argument("framework", "fla")
                                     .chosen("language", "python")
                                     .build();
  EXPECT_SAME_JSON(json::to_json(prompt), JsonValue::parse(R"({
      "ref":{"type":"ref/prompt","name":"review"},
      "argument":{"name":"framework","value":"fla"},
      "context":{"arguments":{"language":"python"}}})"));

  const CompleteRequest templated =
      make<CompleteRequest>(ResourceTemplateReference("file:///{path}"))
          .argument("path", "src/")
          .build();
  EXPECT_SAME_JSON(json::to_json(templated), JsonValue::parse(R"({
      "ref":{"type":"ref/resource","uri":"file:///{path}"},
      "argument":{"name":"path","value":"src/"}})"));
}

TEST(Completion, ARequestIsReadBack) {
  const auto request = json::from_json<CompleteRequest>(JsonValue::parse(R"({
      "_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"},
      "ref":{"type":"ref/resource","uri":"db://{table}"},
      "argument":{"name":"table","value":"us"},
      "context":{"arguments":{"schema":"public","ignored":7}}})"));
  ASSERT_TRUE(holds_alternative<ResourceTemplateReference>(request.ref));
  EXPECT_EQ(get<ResourceTemplateReference>(request.ref).uri, "db://{table}");
  EXPECT_EQ(request.argument.name, "table");
  EXPECT_EQ(request.argument.value, "us");
  ASSERT_TRUE(request.context.has_value());
  ASSERT_TRUE(request.context->arguments.has_value());
  EXPECT_EQ(request.context->arguments.value(),
            (std::map<std::string, std::string>{{"schema", "public"}}));
  EXPECT_TRUE(request._meta.has_value());
}

// Without something to complete, or an argument of it, a request asks
// nothing.
TEST(Completion, ARequestWithoutARefOrArgumentIsRefused) {
  for (const char* params :
       {R"({"argument":{"name":"a","value":"b"}})",
        R"({"ref":{"type":"ref/tool","name":"x"},
            "argument":{"name":"a","value":"b"}})",
        R"({"ref":{"type":"ref/prompt","name":"x"}})",
        R"({"ref":{"type":"ref/prompt","name":"x"},"argument":{"name":"a"}})",
        R"({"ref":{"type":"ref/prompt","name":"x"},"argument":"a"})"}) {
    SCOPED_TRACE(params);
    EXPECT_THROW(json::from_json<CompleteRequest>(JsonValue::parse(params)),
                 json::JsonException);
  }
}

TEST(Completion, AResultIsWrittenWithAWholeTotal) {
  CompleteResult result;
  result.completion.values = {"python", "pytorch"};
  result.completion.total = 10;
  result.completion.hasMore = true;
  EXPECT_SAME_JSON(json::to_json(result), JsonValue::parse(R"({
      "completion":{"values":["python","pytorch"],"total":10,
                    "hasMore":true}})"));
  const auto back = json::from_json<CompleteResult>(json::to_json(result));
  EXPECT_EQ(back.completion.total, mcp::make_optional(10.0));
  EXPECT_TRUE(back.completion.hasMore);
}

// ── The server ─────────────────────────────────────────────────────────

TEST(Completion, AServerWithNoHandlersOffersNone) {
  DispatchTestServer server(testConfig());
  EXPECT_EQ(errorCodeOf(complete(server, kPromptRequest)),
            jsonrpc::METHOD_NOT_FOUND);

  const JsonValue initialized =
      answerTo(server, requestFor("initialize", JsonValue::object(), false));
  EXPECT_FALSE(initialized["result"]["capabilities"].contains("completions"))
      << initialized.toString();
}

TEST(Completion, AServerWithAHandlerAdvertisesIt) {
  DispatchTestServer server(testConfig());
  server.registerPromptCompletion("code_review", languages);

  const JsonValue initialized =
      answerTo(server, requestFor("initialize", JsonValue::object(), false));
  ASSERT_TRUE(initialized.contains("result")) << initialized.toString();
  EXPECT_SAME_JSON(initialized["result"]["capabilities"]["completions"],
                   JsonValue::object());

  const JsonValue discovered = answerTo(
      server, requestFor("server/discover", JsonValue::object(), true));
  ASSERT_TRUE(discovered.contains("result")) << discovered.toString();
  EXPECT_SAME_JSON(discovered["result"]["capabilities"]["completions"],
                   JsonValue::object());
}

// A prompt's handler is given the argument, what was typed and the other
// arguments chosen, in either revision.
TEST(Completion, APromptsArgumentIsCompleted) {
  for (const bool modern : {false, true}) {
    SCOPED_TRACE(modern ? "2026-07-28" : "earlier");
    DispatchTestServer server(testConfig());
    McpServer::CompletionQuery seen;
    server.registerPromptCompletion(
        "code_review", [&seen](const McpServer::CompletionQuery& query) {
          seen = query;
          return languages(query);
        });

    const JsonValue answer = complete(server, R"({
        "ref":{"type":"ref/prompt","name":"code_review"},
        "argument":{"name":"language","value":"py"},
        "context":{"arguments":{"style":"strict"}}})",
                                      modern);
    ASSERT_TRUE(answer.contains("result")) << answer.toString();
    EXPECT_SAME_JSON(answer["result"]["completion"], JsonValue::parse(R"({
        "values":["python","pytorch"],"hasMore":false})"));
    EXPECT_EQ(seen.argument, "language");
    EXPECT_EQ(seen.value, "py");
    EXPECT_EQ(seen.arguments,
              (std::map<std::string, std::string>{{"style", "strict"}}));
  }
}

TEST(Completion, AResourceTemplatesParameterIsCompleted) {
  DispatchTestServer server(testConfig());
  server.registerResourceTemplateCompletion(
      "file:///{path}", [](const McpServer::CompletionQuery& query) {
        CompleteResult::Completion completion;
        completion.values = {query.value + "main.rs", query.value + "lib.rs"};
        completion.total = 2;
        return completion;
      });

  const JsonValue answer = complete(server, R"({
      "ref":{"type":"ref/resource","uri":"file:///{path}"},
      "argument":{"name":"path","value":"src/"}})");
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  EXPECT_SAME_JSON(answer["result"]["completion"], JsonValue::parse(R"({
      "values":["src/main.rs","src/lib.rs"],"total":2,"hasMore":false})"));
}

// Something with no completions, or an argument the handler doesn't know,
// has no suggestions: an answer, not an error.
TEST(Completion, NothingToSuggestIsAnEmptyAnswer) {
  DispatchTestServer server(testConfig());
  server.registerPromptCompletion("code_review", languages);
  for (const char* params :
       {R"({"ref":{"type":"ref/prompt","name":"other"},
            "argument":{"name":"language","value":"py"}})",
        R"({"ref":{"type":"ref/resource","uri":"file:///{path}"},
            "argument":{"name":"path","value":""}})",
        R"({"ref":{"type":"ref/prompt","name":"code_review"},
            "argument":{"name":"unknown","value":"x"}})"}) {
    SCOPED_TRACE(params);
    const JsonValue answer = complete(server, params);
    ASSERT_TRUE(answer.contains("result")) << answer.toString();
    EXPECT_SAME_JSON(answer["result"]["completion"]["values"],
                     JsonValue::array());
  }
}

TEST(Completion, AMalformedRequestIsInvalidParams) {
  DispatchTestServer server(testConfig());
  server.registerPromptCompletion("code_review", languages);
  for (const char* params :
       {R"({"argument":{"name":"language","value":"py"}})",
        R"({"ref":{"type":"ref/tool","name":"code_review"},
            "argument":{"name":"language","value":"py"}})",
        R"({"ref":{"type":"ref/prompt","name":"code_review"}})"}) {
    SCOPED_TRACE(params);
    EXPECT_EQ(errorCodeOf(complete(server, params)), jsonrpc::INVALID_PARAMS);
  }
}

// At most 100 values go out; when there were more, hasMore says so, and
// the handler's own total is kept.
TEST(Completion, AtMostAHundredValuesAreSent) {
  DispatchTestServer server(testConfig());
  server.registerPromptCompletion(
      "code_review", [](const McpServer::CompletionQuery&) {
        CompleteResult::Completion completion;
        for (int i = 0; i < 150; ++i) {
          completion.values.push_back("v" + std::to_string(i));
        }
        completion.total = 150;
        return completion;
      });

  const JsonValue answer = complete(server, kPromptRequest);
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  const auto& completion = answer["result"]["completion"];
  EXPECT_EQ(completion["values"].size(), 100u);
  EXPECT_EQ(completion["values"][99].getString(), "v99");
  EXPECT_TRUE(completion["hasMore"].getBool());
  EXPECT_EQ(completion["total"].getInt64(), 150);
}

TEST(Completion, AHandlerThatFailsIsAnInternalError) {
  DispatchTestServer server(testConfig());
  server.registerPromptCompletion(
      "code_review",
      [](const McpServer::CompletionQuery&) -> CompleteResult::Completion {
        throw std::runtime_error("index unavailable");
      });
  EXPECT_EQ(errorCodeOf(complete(server, kPromptRequest)),
            jsonrpc::INTERNAL_ERROR);
}

}  // namespace server
}  // namespace mcp
