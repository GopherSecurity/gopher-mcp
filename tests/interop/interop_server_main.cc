/**
 * This project's server, with a surface built for somebody else's client
 * to drive.
 *
 * The mirror of the reference server in reference-server-ts: the same
 * tools, so the two directions of interop ask the same questions. Every
 * tool here exists to force one path through the transport rather than to
 * be useful:
 *
 *   add                   an answer small enough to arrive in the response
 *   long_task             progress on the way to an answer, so the answer
 *                         has to arrive on a stream
 *   trigger_notification  something said unprompted, which can only arrive
 *                         on a stream the client is holding
 *   sample_prompt         a question asked of the client, whose answer
 *                         comes back as a request of its own
 *   cut_stream            the stream dropped underneath the client, so
 *                         that coming back for what it missed is a thing
 *                         that can be observed
 *
 * plus a resource and a prompt, so reading and getting are covered as
 * well as calling.
 *
 *   gopher_interop_server --port 8931 [--stateless] [--no-resume]
 *                         [--no-get-stream]
 *
 * All five tools are served by one handler that answers when it can
 * rather than by returning, because sample_prompt cannot answer until the
 * client has: every connection this server accepts is on one dispatcher
 * thread, so a handler that waited for the client would be waiting for
 * the thread that has to accept the client's reply.
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <vector>

#include "mcp/event/event_loop.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace {

using namespace mcp;
using mcp::server::McpServer;
using mcp::server::McpServerConfig;
using mcp::server::SessionContext;

struct Options {
  uint16_t port{0};
  bool stateful{true};
  bool resumable{true};
  bool get_stream{true};
};

bool parseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      options->port = static_cast<uint16_t>(std::atoi(argv[++i]));
    } else if (arg.compare(0, 7, "--port=") == 0) {
      options->port = static_cast<uint16_t>(std::atoi(arg.c_str() + 7));
    } else if (arg == "--stateless") {
      // No session ids are minted, so the client is never given one to
      // echo and every request stands alone.
      options->stateful = false;
    } else if (arg == "--no-resume") {
      // Nothing is retained for replay, so a client that comes back
      // asking where it was gets a fresh stream instead.
      options->resumable = false;
    } else if (arg == "--no-get-stream") {
      // No standalone stream at all, which a client has to cope with by
      // going without anything said unprompted.
      options->get_stream = false;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }
  if (options->port == 0) {
    std::cerr << "--port is required\n";
    return false;
  }
  return true;
}

/** Exposes the loop, which a tool that spreads work over time needs. */
class InteropServer : public McpServer {
 public:
  explicit InteropServer(const McpServerConfig& config) : McpServer(config) {}
  mcp::event::Dispatcher* dispatcher() { return main_dispatcher_; }
};

// ── Reading what a call was given ──────────────────────────────────────

/**
 * A tool call's arguments.
 *
 * Nested JSON survives the trip as its serialized form — Metadata holds
 * scalars — so the object is parsed back out here.
 */
json::JsonValue callArguments(const jsonrpc::Request& request) {
  if (!request.params.has_value()) {
    return json::JsonValue::object();
  }
  const auto& params = request.params.value();
  auto it = params.find("arguments");
  if (it == params.end() || !holds_alternative<std::string>(it->second)) {
    return json::JsonValue::object();
  }
  try {
    auto parsed = json::JsonValue::parse(get<std::string>(it->second));
    return parsed.isObject() ? parsed : json::JsonValue::object();
  } catch (const std::exception&) {
    return json::JsonValue::object();
  }
}

std::string calledTool(const jsonrpc::Request& request) {
  if (!request.params.has_value()) {
    return std::string();
  }
  const auto& params = request.params.value();
  auto it = params.find("name");
  if (it == params.end() || !holds_alternative<std::string>(it->second)) {
    return std::string();
  }
  return get<std::string>(it->second);
}

/**
 * The token a client asked its progress to be reported under.
 *
 * Carried in the request's _meta, which arrives stringified for the same
 * reason the arguments do. Absent when the client did not ask for
 * progress, and reporting it anyway is what the spec forbids.
 */
json::JsonValue progressToken(const jsonrpc::Request& request) {
  if (!request.params.has_value()) {
    return json::JsonValue();
  }
  const auto& params = request.params.value();
  auto it = params.find("_meta");
  if (it == params.end() || !holds_alternative<std::string>(it->second)) {
    return json::JsonValue();
  }
  try {
    auto meta = json::JsonValue::parse(get<std::string>(it->second));
    if (meta.isObject() && meta.contains("progressToken")) {
      return meta["progressToken"];
    }
  } catch (const std::exception&) {
  }
  return json::JsonValue();
}

double numberOr(const json::JsonValue& object,
                const std::string& key,
                double fallback) {
  if (!object.isObject() || !object.contains(key)) {
    return fallback;
  }
  const auto& value = object[key];
  if (value.isInteger()) {
    return static_cast<double>(value.getInt());
  }
  if (value.isFloat()) {
    return value.getFloat();
  }
  return fallback;
}

std::string stringOr(const json::JsonValue& object,
                     const std::string& key,
                     const std::string& fallback) {
  if (!object.isObject() || !object.contains(key) || !object[key].isString()) {
    return fallback;
  }
  return object[key].getString();
}

// ── Answering ──────────────────────────────────────────────────────────

/** A tool's answer: one block of text, as every tool here returns. */
jsonrpc::Response textResult(const RequestId& id, const std::string& text) {
  CallToolResult result;
  result.content.push_back(ExtendedContentBlock(TextContent(text)));
  return jsonrpc::Response::success(
      id, jsonrpc::ResponseResult(json::to_json(result)));
}

jsonrpc::Response toolError(const RequestId& id, const std::string& why) {
  return jsonrpc::Response::make_error(id, Error(jsonrpc::INVALID_PARAMS, why));
}

// ── The tools ──────────────────────────────────────────────────────────

std::vector<Tool> interopTools() {
  std::vector<Tool> tools;

  Tool add("add");
  add.description = mcp::make_optional(std::string("Add two numbers"));
  add.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{"a":{"type":"number"},)"
      R"("b":{"type":"number"}},"required":["a","b"]})"));
  tools.push_back(add);

  Tool long_task("long_task");
  long_task.description = mcp::make_optional(
      std::string("Report progress for a number of steps, then answer"));
  long_task.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{"steps":{"type":"integer"},)"
      R"("delay_ms":{"type":"integer"}},"required":["steps"]})"));
  tools.push_back(long_task);

  Tool trigger("trigger_notification");
  trigger.description = mcp::make_optional(
      std::string("Send an out-of-band message notification"));
  trigger.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{"text":{"type":"string"}}})"));
  tools.push_back(trigger);

  Tool sample("sample_prompt");
  sample.description = mcp::make_optional(
      std::string("Ask the client to sample, and return what it said"));
  sample.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{"prompt":{"type":"string"}},)"
      R"("required":["prompt"]})"));
  tools.push_back(sample);

  Tool cut("cut_stream");
  cut.description = mcp::make_optional(std::string(
      "Drop the stream this session is holding, then say something on it"));
  cut.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"type":"object","properties":{"then_notify":{"type":"integer"}}})"));
  tools.push_back(cut);

  return tools;
}

/** What a client is told when something is said unprompted. */
jsonrpc::Notification loggingMessage(const std::string& text) {
  jsonrpc::Notification notification;
  notification.jsonrpc = "2.0";
  notification.method = "notifications/message";
  Metadata params;
  params["level"] = MetadataValue(std::string("info"));
  params["logger"] = MetadataValue(std::string("interop"));
  params["data"] = MetadataValue(text);
  notification.params = mcp::make_optional(params);
  return notification;
}

/**
 * Progress spread over time, one step per tick.
 *
 * Held together by the shared pointer the timer's own callback captures,
 * so it lives exactly as long as the task does and no longer. A client
 * that has gone is not a reason to stop: the answer it is owed is kept
 * for the stream it may come back on.
 */
struct LongTask : public std::enable_shared_from_this<LongTask> {
  ResponseStreamPtr answer;
  RequestId id;
  json::JsonValue token;
  int steps{0};
  int sent{0};
  std::chrono::milliseconds delay{0};
  mcp::event::TimerPtr timer;

  void reportOneStep() {
    ++sent;
    if (token.isNull()) {
      return;
    }
    jsonrpc::Notification progress;
    progress.jsonrpc = "2.0";
    progress.method = "notifications/progress";
    Metadata params;
    if (token.isString()) {
      params["progressToken"] = MetadataValue(token.getString());
    } else if (token.isInteger()) {
      params["progressToken"] = MetadataValue(token.getInt());
    }
    params["progress"] = MetadataValue(static_cast<int64_t>(sent));
    params["total"] = MetadataValue(static_cast<int64_t>(steps));
    progress.params = mcp::make_optional(params);
    answer->sendNotification(progress);
  }

  void finish() {
    answer->sendResponse(
        textResult(id, "done after " + std::to_string(steps) + " steps"));
  }

  /** Runs the whole task now, which is what a zero delay asks for. */
  void runAtOnce() {
    while (sent < steps) {
      reportOneStep();
    }
    finish();
  }

  void runOverTime(mcp::event::Dispatcher& dispatcher) {
    auto self = shared_from_this();
    timer = dispatcher.createTimer([self]() {
      self->reportOneStep();
      if (self->sent < self->steps) {
        self->timer->enableTimer(self->delay);
        return;
      }
      self->finish();
      // Nothing left to fire, and the timer is inside its own callback:
      // let go of the task only once this has returned.
      self->timer.reset();
    });
    timer->enableTimer(delay);
  }
};

/** The one handler behind every tool, answering whenever it can. */
class ToolCalls {
 public:
  explicit ToolCalls(InteropServer& server) : server_(server) {}

  void operator()(const jsonrpc::Request& request,
                  SessionContext& session,
                  const ResponseStreamPtr& answer) {
    const std::string name = calledTool(request);
    const json::JsonValue arguments = callArguments(request);

    if (name == "add") {
      const double sum =
          numberOr(arguments, "a", 0) + numberOr(arguments, "b", 0);
      answer->sendResponse(textResult(request.id, formatNumber(sum)));
      return;
    }

    if (name == "long_task") {
      startLongTask(request, answer, arguments);
      return;
    }

    if (name == "trigger_notification") {
      const std::string text =
          stringOr(arguments, "text", "hello from the gopher server");
      auto sent =
          server_.sendNotification(session.getId(), loggingMessage(text));
      if (holds_alternative<Error>(sent)) {
        answer->sendResponse(toolError(request.id, get<Error>(sent).message));
        return;
      }
      answer->sendResponse(textResult(request.id, "sent"));
      return;
    }

    if (name == "sample_prompt") {
      askTheClient(request, answer, arguments);
      return;
    }

    if (name == "cut_stream") {
      cutTheStream(request, session, answer, arguments);
      return;
    }

    answer->sendResponse(jsonrpc::Response::make_error(
        request.id, Error(jsonrpc::METHOD_NOT_FOUND, "No such tool: " + name)));
  }

 private:
  static std::string formatNumber(double value) {
    // Whole numbers read as whole numbers: "42", not "42.000000", which
    // is what a client comparing against an expected answer is looking
    // for.
    if (value == static_cast<double>(static_cast<int64_t>(value))) {
      return std::to_string(static_cast<int64_t>(value));
    }
    return std::to_string(value);
  }

  void startLongTask(const jsonrpc::Request& request,
                     const ResponseStreamPtr& answer,
                     const json::JsonValue& arguments) {
    auto task = std::make_shared<LongTask>();
    task->answer = answer;
    task->id = request.id;
    task->token = progressToken(request);
    task->steps = static_cast<int>(numberOr(arguments, "steps", 1));
    task->delay = std::chrono::milliseconds(
        static_cast<int64_t>(numberOr(arguments, "delay_ms", 0)));
    if (task->steps < 1) {
      task->steps = 1;
    }

    if (task->delay.count() <= 0 || server_.dispatcher() == nullptr) {
      task->runAtOnce();
      return;
    }
    task->runOverTime(*server_.dispatcher());
  }

  void askTheClient(const jsonrpc::Request& request,
                    const ResponseStreamPtr& answer,
                    const json::JsonValue& arguments) {
    const std::string prompt = stringOr(arguments, "prompt", "");

    json::JsonValue content = json::JsonValue::object();
    content.set("type", json::JsonValue("text"));
    content.set("text", json::JsonValue(prompt));
    json::JsonValue message = json::JsonValue::object();
    message.set("role", json::JsonValue("user"));
    message.set("content", content);
    json::JsonValue messages = json::JsonValue::array();
    messages.push_back(message);

    jsonrpc::Request question;
    question.jsonrpc = "2.0";
    question.id = RequestId(nextQuestionId());
    question.method = "sampling/createMessage";
    Metadata params;
    // Nested structure travels as its serialized form and is rebuilt on
    // the way out, which is how anything but a scalar is carried here.
    params["messages"] = MetadataValue(messages.toString());
    params["maxTokens"] = MetadataValue(static_cast<int64_t>(64));
    question.params = mcp::make_optional(params);

    const RequestId call_id = request.id;
    auto asked = server_.askClient(
        answer, question,
        [answer, call_id](const jsonrpc::Response& said) {
          answer->sendResponse(textResult(call_id, whatTheClientSaid(said)));
        },
        std::chrono::seconds(10));

    if (holds_alternative<Error>(asked)) {
      answer->sendResponse(toolError(request.id, get<Error>(asked).message));
    }
  }

  /**
   * The text out of a sampling answer.
   *
   * Read out of the raw result rather than a parsed type: what a client
   * sends back is its business, and a client that answered in a shape we
   * did not expect should show up as an empty answer the test can fail
   * on, not as a crash.
   */
  static std::string whatTheClientSaid(const jsonrpc::Response& said) {
    if (said.error.has_value()) {
      return std::string("the client refused: ") + said.error->message;
    }
    if (!said.result.has_value()) {
      return std::string();
    }
    const auto& result = said.result.value();
    json::JsonValue value;
    if (holds_alternative<json::JsonValue>(result)) {
      value = get<json::JsonValue>(result);
    } else if (holds_alternative<Metadata>(result)) {
      value = json::metadataToJson(get<Metadata>(result));
    } else {
      return std::string();
    }
    if (value.isObject() && value.contains("content")) {
      const auto& content = value["content"];
      if (content.isObject() && content.contains("text") &&
          content["text"].isString()) {
        return content["text"].getString();
      }
    }
    return std::string();
  }

  void cutTheStream(const jsonrpc::Request& request,
                    SessionContext& session,
                    const ResponseStreamPtr& answer,
                    const json::JsonValue& arguments) {
    const int notify_after =
        static_cast<int>(numberOr(arguments, "then_notify", 0));
    const std::string session_id = session.getId();
    const RequestId call_id = request.id;
    InteropServer* server = &server_;

    // The answer says which of the two things happened, because "cut"
    // and "there was nothing to cut" look identical from the outside and
    // a driver that cannot tell them apart cannot fail on the second.
    server_.dropSessionStream(session_id, [server, session_id, notify_after,
                                           answer, call_id](bool dropped) {
      if (!dropped) {
        answer->sendResponse(textResult(call_id, "nothing to cut"));
        return;
      }
      // Said while nothing is connected, so it waits for whatever
      // connects next — which is the thing worth observing.
      for (int i = 1; i <= notify_after; ++i) {
        server->sendNotification(
            session_id, loggingMessage("after the cut " + std::to_string(i)));
      }
      answer->sendResponse(textResult(call_id, "cut"));
    });
  }

  std::string nextQuestionId() { return "ask-" + std::to_string(++questions_); }

  InteropServer& server_;
  uint64_t questions_{0};
};

// ── Everything else the client can reach ───────────────────────────────

void registerSurface(InteropServer& server) {
  const std::vector<Tool> tools = interopTools();

  server.registerRequestHandler(
      "tools/list", [tools](const jsonrpc::Request& request, SessionContext&) {
        ListToolsResult result;
        result.tools = tools;
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(json::to_json(result)));
      });

  // Optional rather than Required: tools/call is both kinds of tool at
  // once here, and a client that cannot read a stream is still owed the
  // answer to `add`.
  server.registerAsyncRequestHandler("tools/call", ToolCalls(server),
                                     StreamingMode::Optional);

  Resource greeting("interop://greeting", "greeting");
  greeting.description = mcp::make_optional(std::string("A fixed greeting"));
  greeting.mimeType = mcp::make_optional(std::string("text/plain"));
  server.registerResource(
      greeting, [](const std::string& uri, SessionContext&) {
        ReadResourceResult result;
        TextResourceContents contents;
        contents.uri = mcp::make_optional(uri);
        contents.mimeType = mcp::make_optional(std::string("text/plain"));
        contents.text = "hello from the gopher server";
        result.contents.push_back(contents);
        return result;
      });

  Prompt greet("greet");
  greet.description = mcp::make_optional(std::string("Greet somebody by name"));
  std::vector<PromptArgument> arguments;
  arguments.push_back(
      {"name", mcp::make_optional(std::string("Who to greet")), true});
  greet.arguments = mcp::make_optional(arguments);
  server.registerPrompt(
      greet,
      [](const std::string&, const optional<Metadata>& args, SessionContext&) {
        std::string name = "somebody";
        if (args.has_value()) {
          auto it = args->find("name");
          if (it != args->end() && holds_alternative<std::string>(it->second)) {
            name = get<std::string>(it->second);
          }
        }
        GetPromptResult result;
        result.messages.push_back(PromptMessage(
            enums::Role::USER, TextContent("Say hello to " + name)));
        return result;
      });
}

std::atomic<bool>* stopFlagStorage() {
  static std::atomic<bool> stop{false};
  return &stop;
}

void onSignal(int) { stopFlagStorage()->store(true); }

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parseArgs(argc, argv, &options)) {
    return 2;
  }

  McpServerConfig config;
  config.server_name = "gopher-interop-server";
  config.server_version = "1.0.0";
  config.num_workers = 1;
  config.streamable_http.mcp_path = "/mcp";
  config.streamable_http.enable_sessions = options.stateful;
  config.streamable_http.enable_resumability = options.resumable;
  config.streamable_http.enable_get_stream = options.get_stream;
  // Short, so a test that drops a stream and comes back is not waiting on
  // a window sized for a deployment.
  config.streamable_http.closed_stream_retention = std::chrono::seconds(30);
  config.capabilities.tools = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(true);
  config.capabilities.logging = mcp::make_optional(true);
  ResourcesCapability resources;
  resources.subscribe = mcp::make_optional(EmptyCapability());
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(resources));

  InteropServer server(config);
  registerSurface(server);

  signal(SIGTERM, onSignal);
  signal(SIGINT, onSignal);

  const std::string address =
      "http://127.0.0.1:" + std::to_string(options.port);
  auto listening = server.listen(address);
  if (!holds_alternative<std::nullptr_t>(listening)) {
    std::cerr << "could not listen on " << address << ": "
              << get<Error>(listening).message << "\n";
    return 1;
  }

  std::cerr << "gopher-interop-server listening on " << address << "/mcp\n";
  server.run();
  return 0;
}
