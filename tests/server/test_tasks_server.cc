// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * A server running tool calls as tasks (io.modelcontextprotocol/tasks).
 *
 *   tools/call    ─► {resultType: "task", taskId, status, ...}
 *   tasks/get     ─► {resultType: "complete", ...the task}
 *   tasks/update  ─► {resultType: "complete"}
 *   tasks/cancel  ─► {resultType: "complete"}
 *
 * Only for a 2026-07-28 request that declared the extension; anyone else
 * is refused with -32021 naming it. A task belongs to the caller that made
 * it, and no other caller can tell it exists.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/tasks.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using json::JsonValue;
namespace tasks = protocol::tasks;

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(
      const jsonrpc::Notification& notification) override {
    notified.push_back(json::to_json(notification));
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  bool alive() const override { return true; }
  std::vector<JsonValue> notified;
  std::vector<jsonrpc::Response> answered;
};

/** A request's return path, from a caller the transport named. */
class CallerContext : public NullMessageDispatchContext {
 public:
  explicit CallerContext(std::string principal = "alice")
      : principal_(std::move(principal)) {}
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  ResponseStreamPtr beginResponseStream() override { return stream; }
  const std::string& principal() const override { return principal_; }
  optional<jsonrpc::Response> captured;
  std::shared_ptr<RecordingStream> stream = std::make_shared<RecordingStream>();

 private:
  std::string principal_;
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "tasks-test";
  config.server_version = "1.0.0";
  config.send_server_info = false;
  return config;
}

JsonValue meta(bool declares_tasks) {
  JsonValue meta = JsonValue::object();
  meta.set(protocol::modern::kMetaProtocolVersion, JsonValue("2026-07-28"));
  meta.set(protocol::modern::kMetaClientCapabilities,
           declares_tasks
               ? JsonValue::parse(
                     R"({"extensions":{"io.modelcontextprotocol/tasks":{}}})")
               : JsonValue::object());
  return meta;
}

/** A request; modern carries 2026-07-28 metadata, declaring tasks or not. */
jsonrpc::Request requestFor(const std::string& method,
                            JsonValue params,
                            bool modern = true,
                            bool declares_tasks = true) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(11);
  request.method = method;
  if (modern) {
    params.set("_meta", meta(declares_tasks));
  }
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(json::jsonToMetadata(params));
  return request;
}

JsonValue answerTo(McpServer& server,
                   const jsonrpc::Request& request,
                   CallerContext& context) {
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  if (context.captured.has_value()) {
    return json::to_json(context.captured.value());
  }
  if (!context.stream->answered.empty()) {
    return json::to_json(context.stream->answered.back());
  }
  return JsonValue::object();
}

JsonValue answerTo(McpServer& server,
                   const jsonrpc::Request& request,
                   const std::string& principal = "alice") {
  CallerContext context(principal);
  return answerTo(server, request, context);
}

int errorCodeOf(const JsonValue& answer) {
  return answer.contains("error")
             ? static_cast<int>(answer["error"]["code"].getInt64())
             : 0;
}

JsonValue call(const std::string& tool) {
  JsonValue params = JsonValue::object();
  params.set("name", JsonValue(tool));
  params.set("arguments", JsonValue::parse(R"({"city":"New York"})"));
  return params;
}

JsonValue aboutTask(const std::string& id) {
  JsonValue params = JsonValue::object();
  params.set("taskId", JsonValue(id));
  return params;
}

/** A server with one task tool whose handle the test keeps. */
struct TaskServer {
  explicit TaskServer(McpServerConfig config = testConfig()) : server(config) {
    server.registerTaskTool(Tool("weather"),
                            [this](const JsonValue& arguments,
                                   const std::shared_ptr<TaskHandle>& task) {
                              last_arguments = arguments;
                              handles.push_back(task);
                            });
  }

  /** Call the tool as alice, returning the task id. */
  std::string start(const std::string& principal = "alice") {
    const JsonValue answer =
        answerTo(server, requestFor("tools/call", call("weather")), principal);
    EXPECT_TRUE(answer.contains("result")) << answer.toString();
    return answer["result"]["taskId"].getString();
  }

  DispatchTestServer server;
  std::vector<std::shared_ptr<TaskHandle>> handles;
  JsonValue last_arguments;
};

}  // namespace

// ── Negotiation ────────────────────────────────────────────────────────

TEST(TasksServer, ATaskToolAdvertisesTheExtension) {
  DispatchTestServer plain(testConfig());
  EXPECT_FALSE(
      answerTo(plain, requestFor("initialize", JsonValue::object(),
                                 /*modern=*/false))["result"]["capabilities"]
          .contains("extensions"));

  TaskServer tasks;
  const JsonValue discovered = answerTo(
      tasks.server, requestFor("server/discover", JsonValue::object()));
  EXPECT_TRUE(discovered["result"]["capabilities"]["extensions"].contains(
      tasks::kExtensionId))
      << discovered.toString();
}

TEST(TasksServer, ACallBecomesATaskThatIsAlreadyThere) {
  TaskServer tasks;
  const JsonValue answer =
      answerTo(tasks.server, requestFor("tools/call", call("weather")));
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  const JsonValue& result = answer["result"];
  EXPECT_EQ(result["resultType"].getString(), "task");
  EXPECT_EQ(result["status"].getString(), "working");
  EXPECT_EQ(result["ttlMs"].getInt64(), 3600000);
  EXPECT_EQ(result["pollIntervalMs"].getInt64(), 1000);
  EXPECT_EQ(tasks.last_arguments.toString(),
            JsonValue::parse(R"({"city":"New York"})").toString());

  // Retrievable the moment its id is known.
  const JsonValue polled = answerTo(
      tasks.server,
      requestFor(tasks::kMethodGet, aboutTask(result["taskId"].getString())));
  ASSERT_TRUE(polled.contains("result")) << polled.toString();
  EXPECT_EQ(polled["result"]["resultType"].getString(), "complete");
  EXPECT_EQ(polled["result"]["taskId"].getString(),
            result["taskId"].getString());
}

// A task tool has no other way to answer, so a caller that didn't declare
// the extension, or isn't of 2026-07-28, is refused naming it.
TEST(TasksServer, ACallerWithoutTheExtensionIsRefused) {
  TaskServer tasks;
  for (const bool modern : {true, false}) {
    SCOPED_TRACE(modern ? "2026-07-28 without it" : "earlier revision");
    const JsonValue answer =
        answerTo(tasks.server, requestFor("tools/call", call("weather"), modern,
                                          /*declares_tasks=*/false));
    EXPECT_EQ(errorCodeOf(answer),
              protocol::modern::kMissingRequiredClientCapability);
    EXPECT_TRUE(
        answer["error"]["data"]["requiredCapabilities"]["extensions"].contains(
            tasks::kExtensionId))
        << answer.toString();
  }
  EXPECT_TRUE(tasks.handles.empty()) << "work started for a refused call";
}

// An ordinary tool is answered as ever, whatever the caller declares.
TEST(TasksServer, AnOrdinaryToolIsUnchanged) {
  TaskServer tasks;
  tasks.server.registerTool(
      Tool("echo"),
      [](const std::string&, const optional<Metadata>&, SessionContext&) {
        CallToolResult result;
        result.content.push_back(ExtendedContentBlock(TextContent("hi")));
        return result;
      });
  const JsonValue answer =
      answerTo(tasks.server, requestFor("tools/call", call("echo")));
  EXPECT_FALSE(answer["result"].contains("taskId")) << answer.toString();
}

// ── Ownership ──────────────────────────────────────────────────────────

// A caller the transport authenticated as no one cannot be told from any
// other, so by default no task is made for one.
TEST(TasksServer, AnAnonymousCallerGetsNoTaskByDefault) {
  TaskServer tasks;
  const JsonValue refused =
      answerTo(tasks.server, requestFor("tools/call", call("weather")), "");
  EXPECT_EQ(errorCodeOf(refused), jsonrpc::INVALID_REQUEST)
      << refused.toString();
  EXPECT_TRUE(tasks.handles.empty());

  McpServerConfig allowed = testConfig();
  allowed.allow_tasks_without_caller = true;
  TaskServer open(allowed);
  const std::string id = open.start("");
  EXPECT_FALSE(id.empty());
  EXPECT_EQ(errorCodeOf(answerTo(
                open.server, requestFor(tasks::kMethodGet, aboutTask(id)), "")),
            0);
  // An authenticated caller is not the same as no caller.
  EXPECT_EQ(
      errorCodeOf(answerTo(
          open.server, requestFor(tasks::kMethodGet, aboutTask(id)), "alice")),
      jsonrpc::INVALID_PARAMS);
}

// Another caller's task, an expired one and one that never existed are
// answered alike.
TEST(TasksServer, AForeignOrUnknownOrExpiredTaskLooksTheSame) {
  McpServerConfig config = testConfig();
  config.task_ttl = std::chrono::milliseconds(30);
  TaskServer tasks(config);
  const std::string mine = tasks.start("alice");
  const std::string expired = tasks.start("alice");
  std::this_thread::sleep_for(std::chrono::milliseconds(60));

  std::vector<std::string> answers;
  for (const auto& probe :
       {std::make_pair(std::string("mallory"), mine),
        std::make_pair(std::string("alice"), std::string("no-such-task")),
        std::make_pair(std::string("alice"), expired)}) {
    for (const char* method :
         {tasks::kMethodGet, tasks::kMethodUpdate, tasks::kMethodCancel}) {
      JsonValue params = aboutTask(probe.second);
      if (std::string(method) == tasks::kMethodUpdate) {
        params.set("inputResponses", JsonValue::object());
      }
      const JsonValue answer =
          answerTo(tasks.server, requestFor(method, params), probe.first);
      EXPECT_EQ(errorCodeOf(answer), jsonrpc::INVALID_PARAMS) << method;
      answers.push_back(answer["error"]["message"].getString());
    }
  }
  for (const auto& message : answers) {
    EXPECT_EQ(message, answers.front()) << "the answers told the cases apart";
  }
}

TEST(TasksServer, OneCallerMayHoldOnlySoManyTasks) {
  McpServerConfig config = testConfig();
  config.max_tasks_per_caller = 1;
  TaskServer tasks(config);
  tasks.start("alice");
  EXPECT_EQ(
      errorCodeOf(answerTo(tasks.server,
                           requestFor("tools/call", call("weather")), "alice")),
      jsonrpc::INTERNAL_ERROR);
  EXPECT_FALSE(tasks.start("bob").empty()) << "another caller was limited";
}

// ── The task methods ───────────────────────────────────────────────────

TEST(TasksServer, ATaskThatCompletesOrFailsSaysSo) {
  TaskServer tasks;
  const std::string done = tasks.start();
  const std::string broke = tasks.start();
  CallToolResult result;
  result.isError = true;
  result.content.push_back(ExtendedContentBlock(TextContent("bad input")));
  tasks.handles[0]->complete(result);
  tasks.handles[1]->fail(Error(jsonrpc::INTERNAL_ERROR, "rate limited"));

  const JsonValue completed =
      answerTo(tasks.server, requestFor(tasks::kMethodGet, aboutTask(done)));
  EXPECT_EQ(completed["result"]["status"].getString(), "completed");
  EXPECT_TRUE(completed["result"]["result"]["isError"].getBool())
      << "a tool error is a completion, not a failure";

  const JsonValue failed =
      answerTo(tasks.server, requestFor(tasks::kMethodGet, aboutTask(broke)));
  EXPECT_EQ(failed["result"]["status"].getString(), "failed");
  EXPECT_EQ(failed["result"]["error"]["message"].getString(), "rate limited");
}

TEST(TasksServer, InputIsAskedForAndAnsweredThroughUpdate) {
  TaskServer tasks;
  const std::string id = tasks.start();
  protocol::modern::InputRequest ask;
  ask.method = protocol::modern::kMethodElicitation;
  ask.params = JsonValue::parse(R"({"mode":"form","message":"Name?",
      "requestedSchema":{"type":"object","properties":{"name":{"type":"string"}}}})");
  protocol::modern::InputRequests requests;
  requests["name"] = ask;
  TaskHandle::Answers answers;
  ASSERT_TRUE(tasks.handles[0]->askForInput(
      requests, [&answers](const TaskHandle::Answers& a) { answers = a; }));

  const JsonValue asking =
      answerTo(tasks.server, requestFor(tasks::kMethodGet, aboutTask(id)));
  EXPECT_EQ(asking["result"]["status"].getString(), "input_required");
  EXPECT_EQ(asking["result"]["inputRequests"]["name"]["method"].getString(),
            "elicitation/create");

  JsonValue update = aboutTask(id);
  update.set("inputResponses",
             JsonValue::parse(
                 R"({"name":{"action":"accept","content":{"name":"Luca"}}})"));
  const JsonValue ack =
      answerTo(tasks.server, requestFor(tasks::kMethodUpdate, update));
  EXPECT_EQ(ack["result"].toString(),
            JsonValue::parse(R"({"resultType":"complete"})").toString());
  ASSERT_EQ(answers.count("name"), 1u);
  EXPECT_EQ(answers["name"]["content"]["name"].getString(), "Luca");
  EXPECT_EQ(
      answerTo(tasks.server,
               requestFor(tasks::kMethodGet, aboutTask(id)))["result"]["status"]
          .getString(),
      "working");
}

TEST(TasksServer, CancellingReachesTheWork) {
  TaskServer tasks;
  const std::string id = tasks.start();
  bool told = false;
  tasks.handles[0]->onCancelled([&told]() { told = true; });

  const JsonValue ack =
      answerTo(tasks.server, requestFor(tasks::kMethodCancel, aboutTask(id)));
  EXPECT_EQ(ack["result"].toString(),
            JsonValue::parse(R"({"resultType":"complete"})").toString());
  EXPECT_TRUE(told);
  EXPECT_TRUE(tasks.handles[0]->isCancelled());

  // Work finishing afterwards is too late.
  tasks.handles[0]->complete(JsonValue::object());
  EXPECT_EQ(
      answerTo(tasks.server,
               requestFor(tasks::kMethodGet, aboutTask(id)))["result"]["status"]
          .getString(),
      "cancelled");
}

TEST(TasksServer, TheMethodsNeedTheExtensionAndTheirEra) {
  TaskServer tasks;
  const std::string id = tasks.start();
  EXPECT_EQ(errorCodeOf(answerTo(
                tasks.server, requestFor(tasks::kMethodGet, aboutTask(id), true,
                                         /*declares_tasks=*/false))),
            protocol::modern::kMissingRequiredClientCapability);
  EXPECT_EQ(errorCodeOf(answerTo(tasks.server,
                                 requestFor(tasks::kMethodGet, aboutTask(id),
                                            /*modern=*/false))),
            jsonrpc::METHOD_NOT_FOUND);

  DispatchTestServer plain(testConfig());
  EXPECT_EQ(errorCodeOf(
                answerTo(plain, requestFor(tasks::kMethodGet, aboutTask(id)))),
            jsonrpc::METHOD_NOT_FOUND)
      << "a server without tasks answered a task method";
}

// ── Subscriptions ──────────────────────────────────────────────────────

TEST(TasksServer, AListenerHearsItsOwnTasks) {
  TaskServer tasks;
  const std::string mine = tasks.start("alice");
  const std::string theirs = tasks.start("bob");

  JsonValue listen = JsonValue::object();
  JsonValue ids = JsonValue::array();
  ids.push_back(JsonValue(mine));
  ids.push_back(JsonValue(theirs));
  JsonValue filter = JsonValue::object();
  filter.set("taskIds", ids);
  listen.set("notifications", filter);

  CallerContext alice("alice");
  answerTo(tasks.server, requestFor("subscriptions/listen", listen), alice);
  ASSERT_FALSE(alice.stream->notified.empty()) << "no acknowledgement";
  const JsonValue ack = alice.stream->notified.front();
  const JsonValue& accepted = ack["params"]["notifications"]["taskIds"];
  ASSERT_EQ(accepted.size(), 1u) << ack.toString();
  EXPECT_EQ(accepted[0].getString(), mine);

  // A change to each: only alice's own reaches her, as the whole task.
  CallToolResult result;
  result.content.push_back(ExtendedContentBlock(TextContent("sunny")));
  tasks.handles[1]->complete(result);
  tasks.handles[0]->complete(result);
  std::vector<JsonValue> changes;
  for (const auto& sent : alice.stream->notified) {
    if (sent["method"].getString() == tasks::kNotificationTasks) {
      changes.push_back(sent);
    }
  }
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0]["params"]["taskId"].getString(), mine);
  EXPECT_EQ(changes[0]["params"]["status"].getString(), "completed");
  EXPECT_EQ(changes[0]["params"]["result"]["content"][0]["text"].getString(),
            "sunny");

  // Without the extension, task changes are refused.
  CallerContext undeclared("alice");
  answerTo(tasks.server,
           requestFor("subscriptions/listen", listen, true, false), undeclared);
  ASSERT_FALSE(undeclared.stream->answered.empty());
  ASSERT_TRUE(undeclared.stream->answered[0].error.has_value());
  EXPECT_EQ(undeclared.stream->answered[0].error->code,
            protocol::modern::kMissingRequiredClientCapability);
}

}  // namespace server
}  // namespace mcp
