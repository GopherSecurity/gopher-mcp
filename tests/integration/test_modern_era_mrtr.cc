/**
 * A server asking its client for something, without sending it a request.
 *
 * A real client and a real server, both in the revision where servers
 * never initiate. A handler that cannot finish answers with what it still
 * needs; the client puts those questions to its own handlers and sends
 * the whole request again with the answers attached; the handler runs a
 * second time with them in hand.
 *
 * What is worth testing here is what the two ends have to agree on and
 * neither can check alone: that the second round carries an id of its
 * own, that the state comes back byte for byte, and that a server which
 * only ever asks cannot keep one request going forever.
 *
 * Built on the harness in test_mcp_client_initialize_routing.cc — a real
 * server on a loopback port, a real client pointed at it.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;
namespace modern = protocol::modern;

uint16_t pickEphemeralPort() {
  auto& iface = network::socketInterface();
  auto fd_result =
      iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                   network::Address::IpVersion::v4);
  if (!fd_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: socket() failed");
  }
  auto handle = iface.ioHandleForFd(*fd_result, /*socket_v6only=*/false);
  handle->setBlocking(false);
  auto bind_result =
      handle->bind(network::Address::parseInternetAddress("127.0.0.1", 0));
  if (!bind_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: bind() failed");
  }
  auto local = handle->localAddress();
  if (!local.ok()) {
    throw std::runtime_error("pickEphemeralPort: localAddress() failed");
  }
  const auto* ip = dynamic_cast<const network::Address::Ip*>(local->get());
  if (ip == nullptr) {
    throw std::runtime_error("pickEphemeralPort: not an IP address");
  }
  const uint16_t port = ip->port();
  handle->close();
  return port;
}

bool waitForListenerReady(uint16_t port, std::chrono::milliseconds budget) {
  auto& iface = network::socketInterface();
  auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    auto fd_result =
        iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                     network::Address::IpVersion::v4);
    if (fd_result.ok()) {
      auto handle = iface.ioHandleForFd(*fd_result, false);
      handle->setBlocking(true);
      auto connected = handle->connect(addr);
      handle->close();
      if (connected.ok()) {
        return true;
      }
    }
    std::this_thread::sleep_for(25ms);
  }
  return false;
}

/**
 * What the handler under test saw, round by round.
 *
 * Guarded because the handler runs on the server's dispatcher thread and
 * the assertions read it from the test's.
 */
struct RoundsSeen {
  std::mutex mutex;
  std::vector<std::string> states;
  std::vector<std::string> answers;
  std::set<std::string> ids;

  void record(const std::string& id,
              const optional<std::string>& state,
              const std::string& answer) {
    std::lock_guard<std::mutex> lock(mutex);
    ids.insert(id);
    states.push_back(state.has_value() ? state.value() : std::string());
    answers.push_back(answer);
  }

  size_t count() {
    std::lock_guard<std::mutex> lock(mutex);
    return states.size();
  }
};

std::string idText(const RequestId& id) {
  return holds_alternative<std::string>(id) ? get<std::string>(id)
                                            : std::to_string(get<int64_t>(id));
}

class ModernEraMrtrTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = pickEphemeralPort();

    server::McpServerConfig config;
    config.server_name = "mrtr-test-server";
    config.server_version = "0.0.1";
    config.supported_transports = {TransportType::HttpSse};
    config.num_workers = 1;
    config.capabilities.tools = mcp::make_optional(true);
    config.streamable_http.enable_modern_era = true;

    server_ = server::createMcpServer(config);
    ASSERT_NE(server_, nullptr);
  }

  /** Start serving, once the handlers under test are registered. */
  void startServing() {
    auto listening =
        server_->listen("http://127.0.0.1:" + std::to_string(port_));
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(listening));
    server_thread_ = std::thread([this]() { server_->run(); });
    ASSERT_TRUE(waitForListenerReady(port_, 5s));
  }

  /** A client of the same era, with somewhere to put what it is asked. */
  void startClient(size_t max_rounds = 5) {
    client::McpClientConfig config;
    config.client_name = "mrtr-test-client";
    config.client_version = "0.0.1";
    config.num_workers = 1;
    config.request_timeout = 5000ms;
    config.protocol_initialization_timeout = 5000ms;
    config.protocol_connection_timeout = 5000ms;
    config.streamable_http.enable_modern_era = true;
    config.streamable_http.mrtr_max_rounds = max_rounds;
    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);

    client_->registerRequestHandler(
        modern::kMethodElicitation,
        [this](const jsonrpc::Request&) -> jsonrpc::ResponseResult {
          ++asked_of_client_;
          Metadata answer;
          answer["action"] = MetadataValue(std::string("accept"));
          return jsonrpc::ResponseResult(answer);
        });

    const std::string uri =
        "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
    auto connected = client_->connect(uri);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
        << "the client could not reach a server of its own era";
  }

  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  uint16_t port_{0};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
  std::unique_ptr<client::McpClient> client_;
  std::atomic<int> asked_of_client_{0};
  RoundsSeen rounds_;
};

/**
 * A handler that asks once and then finishes.
 *
 * @param asks How many rounds it asks for before answering. A handler
 *             that never stops asking is what the round bound is for.
 */
server::McpServer::AsyncRequestHandler askingHandler(server::McpServer* server,
                                                     RoundsSeen* rounds,
                                                     const std::string& state,
                                                     int asks) {
  return [server, rounds, state, asks](const jsonrpc::Request& request,
                                       server::SessionContext& session,
                                       const ResponseStreamPtr& stream) {
    json::JsonValue params = request.params.has_value()
                                 ? json::metadataToJson(request.params.value())
                                 : json::JsonValue::object();
    const auto carried = modern::carriedInputOf(params);

    std::string answered;
    if (carried.responses.isObject() &&
        carried.responses.contains("confirmation")) {
      answered = carried.responses["confirmation"].toString();
    }
    rounds->record(idText(request.id), carried.request_state, answered);

    if (static_cast<int>(rounds->count()) <= asks) {
      modern::NeedsInput needed;
      modern::InputRequest ask;
      ask.method = modern::kMethodElicitation;
      ask.params = json::JsonValue::object();
      ask.params.set("message", json::JsonValue("Are you sure?"));
      needed.requests["confirmation"] = ask;
      needed.request_state = mcp::make_optional(state);
      server->answerWithInput(stream, request, session, needed);
      return;
    }

    json::JsonValue done = json::JsonValue::object();
    done.set("content", json::JsonValue::array());
    stream->sendResponse(
        jsonrpc::Response::success(request.id, jsonrpc::ResponseResult(done)));
  };
}

// One round trip, and the three things the two ends have to agree on and
// neither can check alone: the second request is a different request,
// the state comes back untouched, and what the client was asked reached
// its own handler and came back under the name it was asked under.
TEST_F(ModernEraMrtrTest, AQuestionIsAnsweredAndTheRequestComesBackAgain) {
  const std::string state = "round-one-opaque";
  server_->registerAsyncRequestHandler(
      modern::kMethodToolsCall,
      askingHandler(server_.get(), &rounds_, state, /*asks=*/1),
      StreamingMode::Optional);
  startServing();
  startClient();

  Metadata args;
  args["name"] = MetadataValue(std::string("confirm"));
  auto called =
      client_->sendRequest(modern::kMethodToolsCall, mcp::make_optional(args));
  ASSERT_EQ(called.wait_for(10s), std::future_status::ready)
      << "the call never came back";
  auto answer = called.get();
  EXPECT_FALSE(answer.error.has_value())
      << "the call failed: "
      << (answer.error.has_value() ? answer.error->message : "");

  std::lock_guard<std::mutex> lock(rounds_.mutex);
  ASSERT_EQ(rounds_.states.size(), 2u)
      << "the handler did not run a second time with what it asked for";

  // The two rounds are independent requests. A server must never be able
  // to read a repeated id as one conversation it is expected to remember.
  EXPECT_EQ(rounds_.ids.size(), 2u)
      << "the second round arrived under the first round's id";

  EXPECT_TRUE(rounds_.states[0].empty())
      << "a first attempt carried state nobody had given it";
  EXPECT_EQ(rounds_.states[1], state)
      << "the state did not come back as it was sent";

  EXPECT_EQ(asked_of_client_.load(), 1)
      << "the client's own handler was never asked";
  EXPECT_NE(rounds_.answers[1].find("accept"), std::string::npos)
      << "what the client answered did not reach the handler: "
      << rounds_.answers[1];
}

// A server that answers every round by asking for something else must
// not be able to keep one request going forever.
TEST_F(ModernEraMrtrTest, AServerThatOnlyEverAsksIsGivenUpOn) {
  server_->registerAsyncRequestHandler(
      modern::kMethodToolsCall,
      askingHandler(server_.get(), &rounds_, "never-ends", /*asks=*/1000),
      StreamingMode::Optional);
  startServing();
  startClient(/*max_rounds=*/3);

  Metadata args;
  args["name"] = MetadataValue(std::string("confirm"));
  auto called =
      client_->sendRequest(modern::kMethodToolsCall, mcp::make_optional(args));
  ASSERT_EQ(called.wait_for(20s), std::future_status::ready)
      << "a request nobody was ever going to answer never came back";

  auto answer = called.get();
  EXPECT_TRUE(answer.error.has_value())
      << "a request that was never answered was reported as answered";

  std::lock_guard<std::mutex> lock(rounds_.mutex);
  EXPECT_EQ(rounds_.states.size(), 4u)
      << "the first attempt plus three rounds of asking, and no more";
  EXPECT_EQ(rounds_.ids.size(), rounds_.states.size())
      << "two rounds shared an id";
}

}  // namespace
}  // namespace mcp
