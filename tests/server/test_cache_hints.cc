// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Caching hints on the results the 2026-07-28 revision lets a client cache.
 *
 * server/discover, tools/list, prompts/list, resources/list,
 * resources/templates/list and resources/read answer with
 *
 *   {..., "ttlMs": <how long it stays valid>, "cacheScope": "public" |
 * "private"}
 *
 * so a client or a shared gateway knows how long it may keep an answer and
 * whether it may hand one user's answer to another. Nothing else carries
 * them, no older caller is given them, and a result that is really a
 * question back is not an answer anything could cache.
 */

#include <memory>
#include <string>
#include <vector>

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
using namespace std::chrono_literals;

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

/** A return path that also holds a stream, for answers given later. */
class StreamingContext : public NullMessageDispatchContext {
 public:
  class Stream : public ResponseStream {
   public:
    VoidResult sendNotification(const jsonrpc::Notification&) override {
      return makeVoidSuccess();
    }
    VoidResult sendResponse(const jsonrpc::Response& response) override {
      responses.push_back(response);
      return makeVoidSuccess();
    }
    bool alive() const override { return true; }

    std::vector<jsonrpc::Response> responses;
  };

  ResponseStreamPtr beginResponseStream() override {
    if (!stream) {
      stream = std::make_shared<Stream>();
    }
    return stream;
  }

  std::shared_ptr<Stream> stream;
};

const char* const kModern = "2026-07-28";

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "cache-hint-test";
  config.server_version = "0.0.1";
  config.capabilities.tools = mcp::make_optional(true);
  config.capabilities.prompts = mcp::make_optional(true);
  config.capabilities.resources =
      mcp::make_optional(variant<bool, ResourcesCapability>(true));
  return config;
}

/** A request, declaring a revision in _meta when given one. */
jsonrpc::Request request(const std::string& method,
                         const std::string& revision,
                         const std::string& uri = std::string()) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  Metadata params;
  if (!uri.empty()) {
    params["uri"] = MetadataValue(uri);
  }
  if (method == "initialize") {
    params["protocolVersion"] = MetadataValue(std::string("2025-06-18"));
  }
  if (!revision.empty()) {
    JsonValue meta = JsonValue::object();
    meta.set(protocol::modern::kMetaProtocolVersion, JsonValue(revision));
    params["_meta"] = MetadataValue(meta.toString());
  }
  request.params = mcp::make_optional(params);
  return request;
}

/** The answer, as a peer would read it. */
JsonValue answerTo(DispatchTestServer& server, const jsonrpc::Request& req) {
  CapturingContext context;
  server.onRequestWithContext(req, context);
  if (!context.captured.has_value()) {
    ADD_FAILURE() << req.method << " went unanswered";
    return JsonValue::object();
  }
  return json::to_json(context.captured.value());
}

/** A server with one readable resource, so resources/read can succeed. */
void addResource(DispatchTestServer& server) {
  Resource resource;
  resource.uri = "cache://doc";
  resource.name = "doc";
  server.registerResource(resource,
                          [](const std::string& uri, SessionContext&) {
                            ReadResourceResult result;
                            TextResourceContents contents("text");
                            contents.uri = mcp::make_optional(uri);
                            result.contents.push_back(contents);
                            return result;
                          });
}

/** A resources/templates/list answer, since the server has none built in. */
void addTemplateListing(DispatchTestServer& server) {
  server.registerRequestHandler(
      "resources/templates/list",
      [](const jsonrpc::Request& req, SessionContext&) {
        return jsonrpc::Response::success(
            req.id, jsonrpc::ResponseResult(
                        JsonValue::parse(R"({"resourceTemplates": []})")));
      });
}

// Every cacheable result carries the hints, and with nothing configured
// they are the safe ones: always refetch, never share.
TEST(CacheHints, EveryCacheableResultCarriesTheSafeDefaults) {
  DispatchTestServer server(testConfig());
  addResource(server);
  addTemplateListing(server);

  for (const std::string& method :
       {std::string(protocol::modern::kMethodServerDiscover),
        std::string("tools/list"), std::string("prompts/list"),
        std::string("resources/list"), std::string("resources/templates/list"),
        std::string("resources/read")}) {
    const JsonValue answer = answerTo(
        server, request(method, kModern,
                        method == "resources/read" ? "cache://doc" : ""));
    SCOPED_TRACE(method + ": " + answer.toString());

    ASSERT_TRUE(answer.contains("result"));
    const auto& result = answer["result"];
    ASSERT_TRUE(result.contains("ttlMs"));
    ASSERT_TRUE(result["ttlMs"].isInteger());
    EXPECT_EQ(result["ttlMs"].getInt64(), 0);
    ASSERT_TRUE(result.contains("cacheScope"));
    EXPECT_EQ(result["cacheScope"].getString(), "private");
  }
}

// Each kind of result is configured on its own; one that is not keeps the
// defaults.
TEST(CacheHints, EachKindOfResultIsConfiguredOnItsOwn) {
  McpServerConfig config = testConfig();
  McpServerConfig::CacheHint tools;
  tools.ttl = 300000ms;
  tools.scope = McpServerConfig::CacheScope::Public;
  config.cache_hints["tools/list"] = tools;
  DispatchTestServer server(config);

  const JsonValue listed = answerTo(server, request("tools/list", kModern));
  EXPECT_EQ(listed["result"]["ttlMs"].getInt64(), 300000);
  EXPECT_EQ(listed["result"]["cacheScope"].getString(), "public");

  const JsonValue prompts = answerTo(server, request("prompts/list", kModern));
  EXPECT_EQ(prompts["result"]["ttlMs"].getInt64(), 0);
  EXPECT_EQ(prompts["result"]["cacheScope"].getString(), "private");
}

// An older caller has no such fields, and is given none.
TEST(CacheHints, AnOlderCallerIsGivenNone) {
  DispatchTestServer server(testConfig());
  addResource(server);

  for (const std::string& method :
       {std::string("tools/list"), std::string("prompts/list"),
        std::string("resources/list"), std::string("resources/read")}) {
    const JsonValue answer = answerTo(
        server, request(method, std::string(),
                        method == "resources/read" ? "cache://doc" : ""));
    SCOPED_TRACE(method + ": " + answer.toString());
    ASSERT_TRUE(answer.contains("result"));
    EXPECT_FALSE(answer["result"].contains("ttlMs"));
    EXPECT_FALSE(answer["result"].contains("cacheScope"));
  }
}

// Only the cacheable results carry them, and never an error.
TEST(CacheHints, NothingElseCarriesThem) {
  DispatchTestServer server(testConfig());

  const JsonValue ping = answerTo(server, request("ping", kModern));
  ASSERT_TRUE(ping.contains("result")) << ping.toString();
  EXPECT_FALSE(ping["result"].contains("ttlMs")) << ping.toString();

  const JsonValue missing =
      answerTo(server, request("resources/read", kModern, "cache://missing"));
  ASSERT_TRUE(missing.contains("error")) << missing.toString();
  EXPECT_FALSE(missing.contains("result"));
  EXPECT_FALSE(missing["error"].contains("ttlMs"));
}

// A result that is a question back is not an answer, and a cache that kept
// it would hand the question to the next caller.
TEST(CacheHints, AQuestionBackIsNotStamped) {
  DispatchTestServer server(testConfig());
  server.registerRequestHandler(
      "resources/read", [](const jsonrpc::Request& req, SessionContext&) {
        return jsonrpc::Response::success(
            req.id, jsonrpc::ResponseResult(JsonValue::parse(
                        R"({"resultType": "input_required",
                            "inputRequests": {}, "requestState": "s"})")));
      });

  const JsonValue answer =
      answerTo(server, request("resources/read", kModern, "cache://doc"));
  ASSERT_TRUE(answer.contains("result")) << answer.toString();
  EXPECT_FALSE(answer["result"].contains("ttlMs")) << answer.toString();
  EXPECT_FALSE(answer["result"].contains("cacheScope")) << answer.toString();
}

// What a handler set on its own result is its to keep.
TEST(CacheHints, AHandlersOwnValuesAreKept) {
  McpServerConfig config = testConfig();
  McpServerConfig::CacheHint configured;
  configured.ttl = 1000ms;
  config.cache_hints["tools/list"] = configured;
  DispatchTestServer server(config);
  server.registerRequestHandler(
      "tools/list", [](const jsonrpc::Request& req, SessionContext&) {
        return jsonrpc::Response::success(
            req.id, jsonrpc::ResponseResult(JsonValue::parse(
                        R"({"tools": [], "ttlMs": 5,
                            "cacheScope": "public"})")));
      });

  const JsonValue answer = answerTo(server, request("tools/list", kModern));
  EXPECT_EQ(answer["result"]["ttlMs"].getInt64(), 5);
  EXPECT_EQ(answer["result"]["cacheScope"].getString(), "public");
}

// A handler that answers later still answers a cacheable method, and the
// answer carries the hints the same as one given straight away.
TEST(CacheHints, ADeferredAnswerCarriesThem) {
  DispatchTestServer server(testConfig());
  ResponseStreamPtr held;
  jsonrpc::Request asked;
  server.registerAsyncRequestHandler(
      "tools/list",
      [&held, &asked](const jsonrpc::Request& req, SessionContext&,
                      const ResponseStreamPtr& answer) {
        held = answer;
        asked = req;
      });

  StreamingContext context;
  server.onRequestWithContext(request("tools/list", kModern), context);
  ASSERT_TRUE(held) << "the handler was never asked";
  held->sendResponse(jsonrpc::Response::success(
      asked.id, jsonrpc::ResponseResult(JsonValue::parse(R"({"tools": []})"))));

  ASSERT_TRUE(context.stream);
  ASSERT_EQ(context.stream->responses.size(), 1u);
  const JsonValue answer = json::to_json(context.stream->responses[0]);
  EXPECT_EQ(answer["result"]["ttlMs"].getInt64(), 0) << answer.toString();
  EXPECT_EQ(answer["result"]["cacheScope"].getString(), "private");
}

// No hints reach an older caller, even ones a handler put on a typed result
// itself, whether it answers straight away or later.
TEST(CacheHints, AHandlersHintsNeverReachAnOlderCaller) {
  ListToolsResult typed;
  typed.ttlMs = mcp::make_optional(static_cast<int64_t>(5));
  typed.cacheScope = mcp::make_optional(std::string("public"));

  DispatchTestServer direct(testConfig());
  direct.registerRequestHandler(
      "tools/list", [typed](const jsonrpc::Request& req, SessionContext&) {
        return jsonrpc::Response::success(req.id,
                                          jsonrpc::ResponseResult(typed));
      });
  const JsonValue answered =
      answerTo(direct, request("tools/list", std::string()));
  ASSERT_TRUE(answered.contains("result")) << answered.toString();
  EXPECT_FALSE(answered["result"].contains("ttlMs")) << answered.toString();
  EXPECT_FALSE(answered["result"].contains("cacheScope"))
      << answered.toString();

  DispatchTestServer later(testConfig());
  later.registerAsyncRequestHandler(
      "tools/list", [typed](const jsonrpc::Request& req, SessionContext&,
                            const ResponseStreamPtr& answer) {
        answer->sendResponse(
            jsonrpc::Response::success(req.id, jsonrpc::ResponseResult(typed)));
      });
  StreamingContext context;
  later.onRequestWithContext(request("tools/list", std::string()), context);
  ASSERT_TRUE(context.stream);
  ASSERT_EQ(context.stream->responses.size(), 1u);
  const JsonValue deferred = json::to_json(context.stream->responses[0]);
  EXPECT_FALSE(deferred["result"].contains("ttlMs")) << deferred.toString();
  EXPECT_FALSE(deferred["result"].contains("cacheScope"))
      << deferred.toString();
}

// What a client reads: the hints come through on each typed result, and go
// back out unchanged.
TEST(CacheHints, TheClientReadsThemOnEachResult) {
  const auto response = json::from_json<jsonrpc::Response>(JsonValue::parse(
      R"({"jsonrpc": "2.0", "id": 1,
          "result": {"tools": [], "ttlMs": 60000, "cacheScope": "public"}})"));
  ASSERT_TRUE(holds_alternative<ListToolsResult>(response.result.value()));
  const auto& tools = get<ListToolsResult>(response.result.value());
  EXPECT_EQ(tools.ttlMs.value(), 60000);
  EXPECT_EQ(tools.cacheScope.value(), "public");
  const JsonValue again = json::to_json(response);
  EXPECT_EQ(again["result"]["ttlMs"].getInt64(), 60000);
  EXPECT_EQ(again["result"]["cacheScope"].getString(), "public");

  const auto prompts = json::from_json<ListPromptsResult>(JsonValue::parse(
      R"({"prompts": [], "ttlMs": 1, "cacheScope": "private"})"));
  EXPECT_EQ(prompts.ttlMs.value(), 1);
  const auto resources = json::from_json<ListResourcesResult>(
      JsonValue::parse(R"({"resources": [], "ttlMs": 2})"));
  EXPECT_EQ(resources.ttlMs.value(), 2);
  EXPECT_FALSE(resources.cacheScope.has_value());
  const auto read = json::from_json<ReadResourceResult>(JsonValue::parse(
      R"({"contents": [], "ttlMs": 3, "cacheScope": "public"})"));
  EXPECT_EQ(read.cacheScope.value(), "public");
  const auto templates = json::from_json<ListResourceTemplatesResult>(
      JsonValue::parse(R"({"resourceTemplates": [], "ttlMs": 4})"));
  EXPECT_EQ(templates.ttlMs.value(), 4);

  // A result without them has none.
  const auto bare =
      json::from_json<ListToolsResult>(JsonValue::parse(R"({"tools": []})"));
  EXPECT_FALSE(bare.ttlMs.has_value());
  EXPECT_FALSE(bare.cacheScope.has_value());
}

}  // namespace
}  // namespace server
}  // namespace mcp
