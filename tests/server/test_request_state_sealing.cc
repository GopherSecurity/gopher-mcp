// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * A server holding request-state keys seals what its handlers carry across
 * a round, and refuses a retry whose state does not open before any
 * handler sees it. Without keys, state passes through as it always has.
 */

#include <memory>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/protocol/request_state_sealer.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using protocol::modern::NeedsInput;
using protocol::modern::RequestStateSealer;

/** A stream that keeps what was written. */
class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(const jsonrpc::Notification&) override {
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }
  VoidResult sendRefusal(int, const Error&, const json::JsonValue&) override {
    return makeVoidSuccess();
  }
  bool alive() const override { return true; }

  std::vector<jsonrpc::Response> answered;
};

/** A return path that keeps the answer, from a caller the transport named. */
class CallerContext : public NullMessageDispatchContext {
 public:
  explicit CallerContext(std::string principal)
      : principal_(std::move(principal)) {}
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  const std::string& principal() const override { return principal_; }

  optional<jsonrpc::Response> captured;

 private:
  std::string principal_;
};

class SealingServer : public McpServer {
 public:
  explicit SealingServer(const McpServerConfig& config) : McpServer(config) {
    // Stands in for a tool: records what the retry carried, as a handler
    // reads it, and answers.
    registerRequestHandler(
        "tools/call", [this](const jsonrpc::Request& request, SessionContext&) {
          ++handled;
          const json::JsonValue params = request.params_json.has_value()
                                             ? request.params_json.value()
                                             : json::JsonValue::object();
          seen = protocol::modern::carriedInputOf(params).request_state;
          return jsonrpc::Response::success(
              request.id, jsonrpc::ResponseResult(json::JsonValue::object()));
        });
  }
  using McpServer::onRequestWithContext;

  int handled{0};
  optional<std::string> seen;
};

const RequestStateSealer::Key kKey{"k1", std::string(32, 's')};

McpServerConfig sealingConfig() {
  McpServerConfig config;
  config.server_name = "request-state-sealing-test";
  config.server_version = "0.0.1";
  config.request_state_keys = {kKey};
  return config;
}

jsonrpc::Request deploy(const std::string& env = "production") {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = "tools/call";
  json::JsonValue params = json::JsonValue::object();
  params.set("name", json::JsonValue("deploy"));
  json::JsonValue arguments = json::JsonValue::object();
  arguments.set("env", json::JsonValue(env));
  params.set("arguments", arguments);
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(json::jsonToMetadata(params));
  return request;
}

/** The same request, retried carrying this state. */
jsonrpc::Request retryOf(jsonrpc::Request request, const std::string& state) {
  json::JsonValue params = request.params_json.value();
  params.set("requestState", json::JsonValue(state));
  request.params_json = mcp::make_optional(params);
  (*request.params)["requestState"] = state;
  return request;
}

/** What answerWithInput sends as the state, for a caller with this name. */
std::string stateSentTo(SealingServer& server,
                        const std::string& principal,
                        const std::string& state) {
  auto stream = std::make_shared<RecordingStream>();
  SessionContext session("s-1", nullptr);
  session.setPrincipal(principal);
  NeedsInput needed;
  needed.request_state = mcp::make_optional(state);
  auto sent = server.answerWithInput(stream, deploy(), session, needed);
  EXPECT_TRUE(holds_alternative<std::nullptr_t>(sent));
  if (stream->answered.size() != 1) {
    ADD_FAILURE() << "nothing was asked";
    return std::string();
  }
  const auto body = json::to_json(stream->answered[0].result.value());
  return body[protocol::modern::kRequestStateField].getString();
}

/** A retry, as the given caller; true when a handler got to see it. */
bool retried(SealingServer& server,
             const jsonrpc::Request& retry,
             const std::string& principal,
             optional<Error>* refused = nullptr) {
  const int before = server.handled;
  CallerContext caller(principal);
  server.onRequestWithContext(retry, caller);
  if (refused != nullptr && caller.captured.has_value() &&
      caller.captured->error.has_value()) {
    *refused = caller.captured->error;
  }
  return server.handled > before;
}

}  // namespace

// What a handler carries goes out sealed, not as written.
TEST(RequestStateSealing, StateGoesOutSealed) {
  SealingServer server(sealingConfig());
  const std::string sent = stateSentTo(server, "alice", "approved:deploy");
  EXPECT_NE(sent, "approved:deploy");
  EXPECT_EQ(sent.find("approved"), std::string::npos)
      << "the state went out readable at a glance";
}

// The retry that brings it back reaches the handler with the state as the
// handler wrote it.
TEST(RequestStateSealing, ASealedStateComesBackAsWritten) {
  SealingServer server(sealingConfig());
  const std::string sent = stateSentTo(server, "alice", "approved:deploy");

  ASSERT_TRUE(retried(server, retryOf(deploy(), sent), "alice"));
  ASSERT_TRUE(server.seen.has_value());
  EXPECT_EQ(server.seen.value(), "approved:deploy");
}

// Edited, unsealed, someone else's, or for another call: refused with
// -32602, and no handler runs.
TEST(RequestStateSealing, AStateThatDoesNotOpenIsRefusedBeforeAnyHandler) {
  SealingServer server(sealingConfig());
  const std::string sent = stateSentTo(server, "alice", "approved:deploy");
  std::string edited = sent;
  edited[edited.size() / 2] = edited[edited.size() / 2] == 'A' ? 'B' : 'A';

  struct Case {
    const char* what;
    jsonrpc::Request retry;
    std::string principal;
  };
  const Case cases[] = {
      {"an edited state", retryOf(deploy(), edited), "alice"},
      {"a state written by the client", retryOf(deploy(), "approved:deploy"),
       "alice"},
      {"another caller's state", retryOf(deploy(), sent), "mallory"},
      {"a state from another call", retryOf(deploy("staging"), sent), "alice"},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.what);
    optional<Error> refused;
    EXPECT_FALSE(retried(server, c.retry, c.principal, &refused))
        << "a handler saw state that did not open";
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, jsonrpc::INVALID_PARAMS);
    EXPECT_EQ(refused->message, "Invalid requestState")
        << "the refusal said why, which helps whoever is guessing";
  }
}

// A request carrying no state is untouched by sealing.
TEST(RequestStateSealing, ARequestWithoutStateIsServedAsBefore) {
  SealingServer server(sealingConfig());
  EXPECT_TRUE(retried(server, deploy(), "alice"));
  EXPECT_FALSE(server.seen.has_value());
}

// Without keys, state goes out as written and comes back as sent.
TEST(RequestStateSealing, WithoutKeysStatePassesThrough) {
  McpServerConfig config = sealingConfig();
  config.request_state_keys.clear();
  SealingServer server(config);

  EXPECT_EQ(stateSentTo(server, "alice", "approved:deploy"), "approved:deploy");
  ASSERT_TRUE(retried(server, retryOf(deploy(), "anything"), "alice"));
  EXPECT_EQ(server.seen.value(), "anything");
}

// A key that could not protect anything stops the server at startup.
TEST(RequestStateSealing, AnUnusableKeyIsFoundAtStartup) {
  McpServerConfig config = sealingConfig();
  config.request_state_keys = {{"k1", "too short"}};
  EXPECT_THROW(SealingServer server(config), std::invalid_argument);
}

}  // namespace server
}  // namespace mcp
