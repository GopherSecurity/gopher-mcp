// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The C++ client against a server built on the official Python SDK.
 *
 * The TypeScript suite does the same against the TypeScript SDK, which
 * speaks nothing newer than 2025-11-25. The Python SDK is the only released
 * SDK that speaks 2026-07-28, so this is where this project's newest-era
 * client meets an implementation it did not write: discovery instead of a
 * handshake, the revision declared on every request, input_required,
 * subscriptions/listen and caching hints. Every scenario also runs with the
 * client held to the earlier revisions, against the same server.
 *
 * Kept out of `make test` because it needs Python and a package install.
 * `make test-interop` runs it, and it skips rather than fails where those
 * are not present.
 */

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/builders.h"
#include "mcp/client/mcp_client.h"
#include "mcp/json/json_serialization.h"
#include "mcp/protocol/elicitation.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/subscriptions.h"
#include "mcp/types.h"

#include "python_env.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;
using test::PythonServer;

/** Where the Python reference server lives, relative to the source tree. */
std::string serverDir() {
  const char* from_env = std::getenv("GOPHER_INTEROP_PY_SERVER_DIR");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef GOPHER_INTEROP_PY_SERVER_DIR
  return GOPHER_INTEROP_PY_SERVER_DIR;
#else
  return "tests/interop/reference-server-py";
#endif
}

const char* const kGreeting = "interop://greeting";

/** Which revision the client speaks: the newest, or the ones before it. */
enum class Era { Modern, Classic };

std::string eraName(const ::testing::TestParamInfo<Era>& info) {
  return info.param == Era::Modern ? "Modern" : "Classic";
}

/** The text of the first text block of a tool result. */
std::string firstText(const CallToolResult& result) {
  for (const auto& block : result.content) {
    if (holds_alternative<TextContent>(block)) {
      return get<TextContent>(block).text;
    }
  }
  return std::string();
}

class PythonServerInteropTest : public ::testing::TestWithParam<Era> {
 protected:
  void SetUp() override {
    std::string why_not;
    if (!test::pythonAvailable(serverDir() + "/server.py", why_not)) {
      GTEST_SKIP() << "skipping Python interop: " << why_not;
    }
  }

  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  bool modern() const { return GetParam() == Era::Modern; }

  ::testing::AssertionResult startServer(
      const std::vector<std::string>& flags = {}) {
    return server_.start(serverDir(), "server.py", flags);
  }

  void startClient() {
    client::McpClientConfig config;
    config.client_name = "gopher-python-interop-client";
    config.client_version = "1.0.0";
    config.num_workers = 1;
    config.request_timeout = 15000ms;
    config.protocol_initialization_timeout = 15000ms;
    config.protocol_connection_timeout = 15000ms;
    // The newest revision unless held to the earlier ones. The transport is
    // named in both, as an application would; the revision is still found
    // by asking (#296).
    config.preferred_transport = TransportType::StreamableHttp;
    config.streamable_http.enable_modern_era = modern();

    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);

    // Registered before connecting, so the client declares that it can
    // answer an elicitation and the server is willing to ask one.
    client_->registerRequestHandler(
        "elicitation/create",
        [](const jsonrpc::Request& request) -> jsonrpc::ResponseResult {
          const ElicitRequest question =
              protocol::elicitation::fromRequest(request);
          if (question.message != "Which environment?" ||
              question.requestedSchema.properties.count("env") == 0) {
            return protocol::elicitation::toResult(
                ElicitResult(ElicitAction::Decline));
          }
          return protocol::elicitation::toResult(
              make<ElicitResult>(ElicitAction::Accept)
                  .field("env", "staging")
                  .build());
        });

    auto connected = client_->connect(server_.url());
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
        << "could not reach the Python server at " << server_.url();
  }

  InitializeResult handshake() {
    auto init = client_->initializeProtocol();
    EXPECT_EQ(init.wait_for(15s), std::future_status::ready)
        << "the Python server never answered the handshake";
    return init.get();
  }

  /**
   * Call a tool and wait a bounded time. A call that never comes back fails
   * the test with what the server wrote, rather than hanging it.
   */
  CallToolResult call(const std::string& name,
                      const optional<Metadata>& arguments = nullopt) {
    auto called = client_->callTool(name, arguments);
    if (called.wait_for(15s) != std::future_status::ready) {
      ADD_FAILURE() << name << " never came back; the Python server wrote:\n"
                    << server_.output();
      return CallToolResult();
    }
    return called.get();
  }

  PythonServer server_;
  std::unique_ptr<client::McpClient> client_;
};

// Who the server is, and which revision was settled: discovery in the
// newest, the handshake in the earlier ones.
TEST_P(PythonServerInteropTest, TheServerIsDiscoveredOrIntroduced) {
  ASSERT_TRUE(startServer());
  startClient();

  InitializeResult result;
  ASSERT_NO_THROW(result = handshake());
  ASSERT_TRUE(result.serverInfo.has_value());
  EXPECT_EQ(result.serverInfo->name, "gopher-interop-python-reference");
  ASSERT_TRUE(result.capabilities.tools.has_value());

  if (modern()) {
    EXPECT_EQ(result.protocolVersion, "2026-07-28");
    // Discovery is itself a cacheable result.
    EXPECT_TRUE(result.ttlMs.has_value()) << "discovery carried no ttlMs";
    EXPECT_TRUE(result.cacheScope.has_value());
  } else {
    EXPECT_FALSE(result.protocolVersion.empty());
    EXPECT_NE(result.protocolVersion, "2026-07-28");
  }
}

TEST_P(PythonServerInteropTest, AToolIsCalledAndAnswersExactly) {
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  Metadata arguments;
  arguments["a"] = static_cast<int64_t>(20);
  arguments["b"] = static_cast<int64_t>(22);
  CallToolResult result;
  ASSERT_NO_THROW(result = call("add", arguments));
  EXPECT_FALSE(result.isError);
  EXPECT_EQ(firstText(result), "42");
}

// What a tool says about itself, as the official SDK writes it.
TEST_P(PythonServerInteropTest, AToolsTitleAndHintsAreRead) {
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto listed = client_->listTools();
  ASSERT_EQ(listed.wait_for(15s), std::future_status::ready);
  ListToolsResult tools;
  ASSERT_NO_THROW(tools = listed.get());
  const Tool* add = nullptr;
  for (const auto& tool : tools.tools) {
    if (tool.name == "add") {
      add = &tool;
    }
  }
  ASSERT_NE(add, nullptr) << "add was not listed";
  EXPECT_EQ(add->displayName(), "Add");
  ASSERT_TRUE(add->annotations.has_value());
  EXPECT_EQ(add->annotations->readOnlyHint, mcp::make_optional(true));
  EXPECT_EQ(add->annotations->idempotentHint, mcp::make_optional(true));
  EXPECT_EQ(add->annotations->openWorldHint, mcp::make_optional(false));
  EXPECT_FALSE(add->annotations->destructiveHint.has_value());
  ASSERT_TRUE(add->_meta.has_value());
  EXPECT_EQ((*add->_meta)["interop"]["kind"].getString(), "arithmetic");
}

// A declared result shape, from the listing, and the data, from the call;
// in the newest revision the listing also says how long it may be cached.
TEST_P(PythonServerInteropTest, AStructuredResultIsListedAndRead) {
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto listed = client_->listTools();
  ASSERT_EQ(listed.wait_for(15s), std::future_status::ready);
  ListToolsResult tools;
  ASSERT_NO_THROW(tools = listed.get());
  const Tool* weather = nullptr;
  for (const auto& tool : tools.tools) {
    if (tool.name == "get_weather") {
      weather = &tool;
    }
  }
  ASSERT_NE(weather, nullptr) << "get_weather was not listed";
  ASSERT_TRUE(weather->outputSchema.has_value());
  EXPECT_TRUE((*weather->outputSchema)["properties"].contains("temp"));

  if (modern()) {
    ASSERT_TRUE(tools.ttlMs.has_value()) << "tools/list carried no ttlMs";
    EXPECT_EQ(tools.ttlMs.value(), 60000);
    ASSERT_TRUE(tools.cacheScope.has_value());
    EXPECT_EQ(tools.cacheScope.value(), "public");
  } else {
    EXPECT_FALSE(tools.ttlMs.has_value())
        << "an earlier revision was given caching hints";
  }

  CallToolResult result;
  ASSERT_NO_THROW(result = call("get_weather"));
  ASSERT_TRUE(result.structuredContent.has_value());
  EXPECT_EQ((*result.structuredContent)["temp"].getFloat(), 22.5);
  EXPECT_EQ((*result.structuredContent)["conditions"].getString(), "Cloudy");
}

// The server asks the user which environment: a request of its own in the
// earlier revisions, input_required in the newest. Either way the typed
// handler answers, and the tool returns what it was told.
TEST_P(PythonServerInteropTest, AnElicitationIsAnsweredInEitherRevision) {
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  CallToolResult result;
  ASSERT_NO_THROW(result = call("elicit_prompt"));
  EXPECT_FALSE(result.isError) << firstText(result);
  EXPECT_EQ(firstText(result), "accept:staging");
}

TEST_P(PythonServerInteropTest, AResourceIsReadAndAMissingOneRefused) {
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto read = client_->readResource(kGreeting);
  ASSERT_EQ(read.wait_for(15s), std::future_status::ready);
  ReadResourceResult greeting;
  ASSERT_NO_THROW(greeting = read.get());
  ASSERT_EQ(greeting.contents.size(), 1u);
  ASSERT_TRUE(holds_alternative<TextResourceContents>(greeting.contents[0]));
  EXPECT_EQ(get<TextResourceContents>(greeting.contents[0]).text,
            "hello from the python reference server");

  Metadata params;
  params["uri"] = std::string("interop://missing");
  auto missing =
      client_->sendRequest("resources/read", mcp::make_optional(params));
  ASSERT_EQ(missing.wait_for(15s), std::future_status::ready);
  const jsonrpc::Response response = missing.get();
  ASSERT_TRUE(response.error.has_value())
      << "reading a resource that does not exist succeeded";
}

// What the server said about the missing resource, not just that it said
// no. This server sends it with an HTTP 400, as 2026-07-28 servers may.
TEST_P(PythonServerInteropTest, AMissingResourceKeepsTheServersError) {
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  Metadata params;
  params["uri"] = std::string("interop://missing");
  auto missing =
      client_->sendRequest("resources/read", mcp::make_optional(params));
  ASSERT_EQ(missing.wait_for(15s), std::future_status::ready);
  const jsonrpc::Response response = missing.get();
  ASSERT_TRUE(response.error.has_value());
  // The Python SDK answers -32602 in every revision; 2025-06-18 and
  // 2025-11-25 name -32002 for this. Either way the uri must come back.
  EXPECT_TRUE(response.error->code == jsonrpc::INVALID_PARAMS ||
              response.error->code == jsonrpc::RESOURCE_NOT_FOUND)
      << response.error->code << ": " << response.error->message;
  if (modern()) {
    // Sent with HTTP 400 in 2026-07-28; the client keeps the body's error.
    EXPECT_EQ(response.error->code, jsonrpc::INVALID_PARAMS);
  }
  ASSERT_TRUE(response.error->data.has_value()) << response.error->message;
  const auto* data =
      get_if<std::map<std::string, std::string>>(&response.error->data.value());
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->count("uri") ? data->at("uri") : std::string(),
            "interop://missing");
}

// Listening, which only the newest revision has: a subscription to the
// greeting hears it change when a tool says it did.
TEST_P(PythonServerInteropTest, AListenerHearsAResourceChange) {
  if (!modern()) {
    GTEST_SKIP() << "subscriptions/listen is new in 2026-07-28";
  }
  ASSERT_TRUE(startServer());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto heard = std::make_shared<std::promise<std::string>>();
  auto once = std::make_shared<std::atomic<bool>>(false);
  protocol::modern::NotificationFilter filter;
  filter.resource_uris.push_back(kGreeting);
  const int64_t subscription = client_->listen(
      filter, [heard, once](const jsonrpc::Notification& notification) {
        if (notification.method ==
                protocol::modern::kNotificationResourcesUpdated &&
            !once->exchange(true)) {
          heard->set_value(notification.method);
        }
      });
  ASSERT_NE(subscription, 0) << "no subscription could be opened";

  // Given a moment to be held open before anything is said on it.
  std::this_thread::sleep_for(500ms);
  ASSERT_NO_THROW(call("touch_greeting"));

  auto future = heard->get_future();
  EXPECT_EQ(future.wait_for(10s), std::future_status::ready)
      << "the change was never heard";
  client_->stopListening(subscription);
}

// A server keeping no sessions serves the same answers.
TEST_P(PythonServerInteropTest, AServerKeepingNoSessionsStillWorks) {
  ASSERT_TRUE(startServer({"--stateless"}));
  startClient();
  ASSERT_NO_THROW(handshake());

  Metadata arguments;
  arguments["a"] = static_cast<int64_t>(1);
  arguments["b"] = static_cast<int64_t>(41);
  CallToolResult result;
  ASSERT_NO_THROW(result = call("add", arguments));
  EXPECT_EQ(firstText(result), "42");
}

// Listed so it is not forgotten: nothing pages yet on either side.
// A server listing a page at a time, read to the end by following its
// cursors. Its cursors look like JSON, so they only work if they go back
// exactly as given.
TEST_P(PythonServerInteropTest, AToolListingIsPaged) {
  ASSERT_TRUE(startServer({"--page-size", "2"}));
  startClient();
  ASSERT_NO_THROW(handshake());

  std::vector<std::string> names;
  optional<Cursor> cursor;
  size_t pages = 0;
  do {
    auto listed = client_->listTools(cursor);
    ASSERT_EQ(listed.wait_for(15s), std::future_status::ready);
    ListToolsResult page;
    ASSERT_NO_THROW(page = listed.get());
    EXPECT_LE(page.tools.size(), 2u);
    for (const auto& tool : page.tools) {
      names.push_back(tool.name);
    }
    cursor = page.nextCursor;
    ASSERT_LT(++pages, 20u) << "the cursors never reached the end";
  } while (cursor.has_value());

  EXPECT_GT(pages, 1u) << "the server never paged";
  std::set<std::string> distinct(names.begin(), names.end());
  EXPECT_EQ(distinct.size(), names.size()) << "a tool was listed twice";
  for (const char* expected :
       {"add", "get_weather", "elicit_prompt", "touch_greeting"}) {
    EXPECT_TRUE(distinct.count(expected)) << expected << " was never listed";
  }
}

INSTANTIATE_TEST_SUITE_P(BothRevisions,
                         PythonServerInteropTest,
                         ::testing::Values(Era::Modern, Era::Classic),
                         eraName);

}  // namespace
}  // namespace mcp
