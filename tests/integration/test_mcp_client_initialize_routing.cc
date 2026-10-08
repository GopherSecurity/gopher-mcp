// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Integration test: McpClient::initializeProtocol dispatcher-routing contract.
 *
 * The contract under test (commit that moved state commit onto the
 * dispatcher thread): when a response to the "initialize" request arrives,
 * the detached worker thread that blocks on the response future parses
 * it and then hands the actual mutation of McpClient state — namely
 *
 *   - server_capabilities_
 *   - initialized_
 *   - protocol_state_machine_ transition to INITIALIZED
 *
 * back to the main dispatcher via main_dispatcher_->post(...). The worker
 * only resolves the externally-visible std::future<InitializeResult> from
 * *inside* that post, so by the time a caller's future.get() unblocks, the
 * dispatcher has already published the new state. Writing those fields
 * from the worker thread directly would be a data race with dispatcher
 * readers and is what the fix removes.
 *
 * Verifying that contract end-to-end means driving the full path: spin up
 * a real McpServer, point a real McpClient at it, and walk through:
 *   connect → initializeProtocol → follow-up request.
 *
 * Observable asserts:
 *
 *   1. connect() succeeds against a real listening server.
 *   2. initializeProtocol().get() returns the capabilities that the server
 *      was configured with — exercises the parse-on-worker-thread step.
 *   3. A subsequent ping request succeeds — exercises the post-initialize
 *      state machine, which only reaches a usable state once the
 *      dispatcher-thread commit has run. If the commit had been skipped
 *      or reordered, the state machine would still be in INITIALIZING
 *      and the follow-up request would not round-trip cleanly.
 *
 * The private fields themselves are not publicly accessible, so the
 * asserts are the tightest observable proxies. Running this test under a
 * data-race detector is the second half of the story — the race the fix
 * closed is invisible to ordinary execution.
 *
 * Why a real server rather than a canned HTTP peer: faking enough of the
 * HTTP/JSON-RPC reply path to satisfy McpClient's parser and framing is
 * more code than standing up the real server, and the real server is
 * what the fix has to hold under.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

// Pick a loopback port the kernel believes is free by briefly binding
// ephemeral-port 0. Classic TOCTOU: something else could steal the port
// between here and McpServer::listen(), but on a loopback test bed the
// window is short enough that we accept the small flake risk rather than
// bake a random-port hand-off into McpServer. Done via the MCP
// SocketInterface abstraction rather than raw BSD sockets so we keep the
// project's platform layering honest.
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

  auto bind_addr = network::Address::parseInternetAddress("127.0.0.1", 0);
  auto bind_result = handle->bind(bind_addr);
  if (!bind_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: bind() failed");
  }

  auto local_addr_result = handle->localAddress();
  if (!local_addr_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: localAddress() failed");
  }

  const auto* ip =
      dynamic_cast<const network::Address::Ip*>(local_addr_result->get());
  if (ip == nullptr) {
    throw std::runtime_error("pickEphemeralPort: not an IP address");
  }
  uint16_t port = ip->port();
  handle->close();
  return port;
}

class McpClientInitializeRoutingTest : public ::testing::Test {
 protected:
  void SetUp() override { startServer(/*serve_newest=*/true); }

  // Applied to the server's config by the next startServer.
  std::function<void(server::McpServerConfig&)> tweak_config_;
  // Applied to the client's config by the next connectInitializedClient.
  std::function<void(client::McpClientConfig&)> tweak_client_;

  /**
   * Bring up the server on a fresh port. With serve_newest false it serves
   * only the revisions before 2026-07-28, as an older server would.
   */
  void startServer(bool serve_newest) {
    port_ = pickEphemeralPort();

    // Server: minimal config. One worker, known capability set — the test
    // will cross-check the capability bits echoed back through initialize.
    // ping is a built-in server handler, no explicit registration needed.
    server::McpServerConfig server_config;
    server_config.server_name = "init-routing-test-server";
    server_config.server_version = "0.0.1";
    server_config.supported_transports = {TransportType::HttpSse};
    server_config.num_workers = 1;
    server_config.capabilities.tools = mcp::make_optional(true);
    server_config.capabilities.prompts = mcp::make_optional(true);
    server_config.capabilities.logging = mcp::make_optional(true);
    server_config.streamable_http.enable_modern_era = serve_newest;
    if (tweak_config_) {
      tweak_config_(server_config);
    }

    server_ = server::createMcpServer(server_config);
    ASSERT_NE(server_, nullptr);

    const std::string listen_address =
        "http://127.0.0.1:" + std::to_string(port_);
    auto listen_result = server_->listen(listen_address);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(listen_result))
        << "McpServer::listen failed";

    // run() blocks — keep it on a background thread. performListen() and
    // every per-connection callback run on the server's dispatcher thread
    // inside run(), so this is the right place for it.
    server_thread_ = std::thread([this]() { server_->run(); });

    // Wait until something is accepting on the listen port before handing
    // the port to the client. listen()/performListen() is two-step, and
    // the listener only starts accepting after the dispatcher picks up
    // the work. Polling the port with a connect probe is cheaper than
    // sleeping long enough to cover the slowest machine.
    ASSERT_TRUE(waitForListenerReady(port_, 5s))
        << "Server did not begin accepting on port " << port_;
  }

  /** Stop the running server, so another can be started in its place. */
  void stopServer() {
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  void TearDown() override {
    // Client first so the server sees a RemoteClose on its still-
    // running dispatcher. The server's onConnectionLifecycleEvent
    // takes the active_connections_ erase + deferredDelete path on
    // RemoteClose.
    if (client_) {
      client_->shutdown();
      client_.reset();
    }

    // server_->shutdown() drains active_connections_ on the
    // dispatcher thread inside its cleanup post, so by the time
    // server_.reset() runs on this thread there are no connections
    // left whose destructors would fire on the wrong thread.
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  // Try to open a TCP connection to the given loopback port until it
  // succeeds or the budget elapses. The McpServer exposes no "ready"
  // signal out of listen(), so this is the honest way to synchronize.
  static bool waitForListenerReady(uint16_t port,
                                   std::chrono::milliseconds budget) {
    auto& iface = network::socketInterface();
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      auto fd_result =
          iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                       network::Address::IpVersion::v4);
      if (fd_result.ok()) {
        auto handle = iface.ioHandleForFd(*fd_result, false);
        // Block on connect: on a non-blocking socket, a successful loopback
        // connect still returns EINPROGRESS and we'd have to poll for write
        // readiness. For a readiness probe, a blocking connect is the
        // cheapest honest signal.
        handle->setBlocking(true);
        auto connect_result = handle->connect(addr);
        handle->close();
        if (connect_result.ok()) {
          return true;
        }
      }
      std::this_thread::sleep_for(25ms);
    }
    return false;
  }

  // A client connected to this fixture's server with the handshake done,
  // for tests about what comes after it.
  void connectInitializedClient() {
    client::McpClientConfig client_config;
    client_config.client_name = "init-routing-test-client";
    client_config.client_version = "0.0.1";
    client_config.num_workers = 1;
    client_config.request_timeout = 5000ms;
    client_config.protocol_initialization_timeout = 5000ms;
    client_config.protocol_connection_timeout = 5000ms;
    if (tweak_client_) {
      tweak_client_(client_config);
    }

    client_ = client::createMcpClient(client_config);
    ASSERT_NE(client_, nullptr);

    const std::string uri =
        "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));
    auto init_future = client_->initializeProtocol();
    ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
    ASSERT_NO_THROW(init_future.get());
  }

  // Answer prompts/list with exactly this, in place of the server's own.
  void answerPromptsListWith(const std::string& result_json) {
    server_->registerRequestHandler(
        "prompts/list", [result_json](const jsonrpc::Request& request,
                                      server::SessionContext&) {
          return jsonrpc::Response::success(
              request.id,
              jsonrpc::ResponseResult(json::JsonValue::parse(result_json)));
        });
  }

  uint16_t port_{0};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
  std::unique_ptr<client::McpClient> client_;
};

TEST_F(McpClientInitializeRoutingTest, ReturnsCapabilitiesAndUnblocksFollowUp) {
  client::McpClientConfig client_config;
  client_config.client_name = "init-routing-test-client";
  client_config.client_version = "0.0.1";
  client_config.num_workers = 1;
  // Keep timeouts tight so an accidentally-broken commit (e.g. the promise
  // never being resolved from the dispatcher post) surfaces as a test
  // failure rather than a long hang. 5s is plenty for a loopback
  // HTTP+JSON round trip.
  client_config.request_timeout = 5000ms;
  client_config.protocol_initialization_timeout = 5000ms;
  client_config.protocol_connection_timeout = 5000ms;

  client_ = client::createMcpClient(client_config);
  ASSERT_NE(client_, nullptr);

  // StreamableHttp is the transport McpClient negotiates for a plain
  // http:// URL whose path is neither /sse nor /events. "/rpc" is the
  // historic alias of the server's endpoint, kept routable so older
  // clients still reach it.
  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  auto connect_result = client_->connect(uri);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(connect_result))
      << "McpClient::connect failed against real server";
  EXPECT_TRUE(client_->isConnected());

  // The dispatcher-routing contract: when the future resolves, the
  // worker has already posted state commit through main_dispatcher_
  // and that post has run. Externally we see two things:
  //   - the future resolves in bounded time (no deadlock / lost post)
  //   - the parsed capabilities come out of the same post
  auto init_future = client_->initializeProtocol();
  const auto status = init_future.wait_for(5s);
  ASSERT_EQ(status, std::future_status::ready)
      << "initializeProtocol future never resolved";

  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get())
      << "initializeProtocol resolved with an exception";

  // protocolVersion is always populated: the parser either pulls it
  // from the response metadata or falls back to the client's
  // configured value. Either branch runs inside the dispatcher-thread
  // commit post, so observing a populated protocolVersion here is
  // proof that the post executed before the future resolved.
  // What the nested initialize result says is checked by
  // TheIntroductionIsReadAsTheNestedObjectItIs below.
  // Both ends serve the newest revision unless told otherwise, so this
  // is what they settled on — and it is not the configured version,
  // which is what an introduction would have offered. That era has none,
  // so there was nothing to offer and nothing to negotiate.
  EXPECT_EQ(result.protocolVersion, protocol::kProtocolVersion20260728);

  // A follow-up request rides on the same connection and the same
  // protocol state machine that initializeProtocol just advanced. If
  // the dispatcher-thread commit had been dropped or reordered
  // (e.g. worker set the promise *before* posting, or the post never
  // fired) the state machine would still be mid-transition and the
  // follow-up request would not come back cleanly. ping is a built-in
  // server handler so it doesn't depend on any custom registration.
  auto ping_future = client_->sendRequest("ping");
  const auto ping_status = ping_future.wait_for(5s);
  ASSERT_EQ(ping_status, std::future_status::ready)
      << "ping follow-up never resolved — protocol state likely stuck";

  jsonrpc::Response ping_response;
  ASSERT_NO_THROW(ping_response = ping_future.get());
  EXPECT_FALSE(ping_response.error.has_value())
      << "ping returned error: "
      << (ping_response.error.has_value() ? ping_response.error->message : "");
}

// The same conversation with the newest era switched off at the client:
// it introduces itself as before and settles on what it offered. Which
// is the point of the switch — one end declining the era is enough, and
// nothing about the older path changes when it does.
TEST_F(McpClientInitializeRoutingTest, AClientMayDeclineTheNewestEra) {
  client::McpClientConfig client_config;
  client_config.client_name = "init-routing-test-client";
  client_config.client_version = "0.0.1";
  client_config.num_workers = 1;
  client_config.request_timeout = 5000ms;
  client_config.protocol_initialization_timeout = 5000ms;
  client_config.protocol_connection_timeout = 5000ms;
  client_config.streamable_http.enable_modern_era = false;

  client_ = client::createMcpClient(client_config);
  ASSERT_NE(client_, nullptr);

  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);

  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get());
  EXPECT_EQ(result.protocolVersion, client_config.protocol_version)
      << "a client that declined the newest era was taken into it anyway";

  auto ping_future = client_->sendRequest("ping");
  ASSERT_EQ(ping_future.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping_future.get().error.has_value());
}

// notifications/initialized ends the handshake of the earlier revisions.
// The newest has no handshake, so a client speaking it has nothing to
// end and sends nothing; one speaking an earlier revision still does.
TEST_F(McpClientInitializeRoutingTest, OnlyAHandshakeIsFollowedByInitialized) {
  for (const bool newest : {true, false}) {
    SCOPED_TRACE(newest ? "newest revision" : "earlier revision");
    auto arrived = std::make_shared<std::atomic<int>>(0);
    server_->registerNotificationHandler(
        "notifications/initialized",
        [arrived](const jsonrpc::Notification&, server::SessionContext&) {
          ++*arrived;
        });

    client::McpClientConfig client_config;
    client_config.client_name = "init-routing-test-client";
    client_config.client_version = "0.0.1";
    client_config.num_workers = 1;
    client_config.request_timeout = 5000ms;
    client_config.protocol_initialization_timeout = 5000ms;
    client_config.protocol_connection_timeout = 5000ms;
    client_config.streamable_http.enable_modern_era = newest;
    client_ = client::createMcpClient(client_config);
    ASSERT_NE(client_, nullptr);

    const std::string uri =
        "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));
    auto init_future = client_->initializeProtocol();
    ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
    InitializeResult result;
    ASSERT_NO_THROW(result = init_future.get());
    ASSERT_EQ(result.protocolVersion == protocol::kProtocolVersion20260728,
              newest);

    // Anything sent at the end of the handshake went out before this, on
    // the same connection, and has been read by the time it is answered.
    auto ping = client_->sendRequest("ping");
    ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
    EXPECT_FALSE(ping.get().error.has_value());
    if (!newest) {
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      while (arrived->load() == 0 &&
             std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
      }
    } else {
      std::this_thread::sleep_for(200ms);
    }
    EXPECT_EQ(arrived->load(), newest ? 0 : 1);

    client_->shutdown();
    client_.reset();
  }
}

// The older era's answer to an introduction nests serverInfo and
// capabilities as objects. Read as the flat map it used to be squeezed
// into, the name never arrived and every capability read as absent.
TEST_F(McpClientInitializeRoutingTest,
       TheIntroductionIsReadAsTheNestedObjectItIs) {
  client::McpClientConfig client_config;
  client_config.client_name = "init-routing-test-client";
  client_config.client_version = "0.0.1";
  client_config.num_workers = 1;
  client_config.request_timeout = 5000ms;
  client_config.protocol_initialization_timeout = 5000ms;
  client_config.protocol_connection_timeout = 5000ms;
  client_config.streamable_http.enable_modern_era = false;

  client_ = client::createMcpClient(client_config);
  ASSERT_NE(client_, nullptr);

  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);

  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get());
  ASSERT_TRUE(result.serverInfo.has_value()) << "serverInfo never arrived";
  EXPECT_EQ(result.serverInfo->name, "init-routing-test-server");
  EXPECT_EQ(result.serverInfo->version, "0.0.1");
  ASSERT_TRUE(result.capabilities.tools.has_value());
  EXPECT_TRUE(result.capabilities.tools.value());
  ASSERT_TRUE(result.capabilities.prompts.has_value());
  EXPECT_TRUE(result.capabilities.prompts.value());
}

// A prompt's answer is a description and an array of messages, each with
// nested content. getPrompt() hands back what the server said, and the
// arguments it sent arrive at the handler as the object they were. The
// description looks like JSON on purpose: it is still a string, and has to
// arrive as one.
TEST_F(McpClientInitializeRoutingTest, APromptIsReadAsTheResultItIs) {
  Prompt greet("greet");
  greet.description = mcp::make_optional(std::string("Say hello"));
  server_->registerPrompt(
      greet, [](const std::string&, const optional<Metadata>& arguments,
                server::SessionContext&) {
        std::string who = "nobody";
        if (arguments.has_value()) {
          auto it = arguments->find("who");
          if (it != arguments->end() &&
              holds_alternative<std::string>(it->second)) {
            who = get<std::string>(it->second);
          }
        }
        GetPromptResult result;
        result.description = mcp::make_optional(std::string("{}"));
        result.messages.push_back(
            PromptMessage(enums::Role::USER, TextContent("hello " + who)));
        return result;
      });

  client::McpClientConfig client_config;
  client_config.client_name = "init-routing-test-client";
  client_config.client_version = "0.0.1";
  client_config.num_workers = 1;
  client_config.request_timeout = 5000ms;
  client_config.protocol_initialization_timeout = 5000ms;
  client_config.protocol_connection_timeout = 5000ms;

  client_ = client::createMcpClient(client_config);
  ASSERT_NE(client_, nullptr);

  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));
  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  ASSERT_NO_THROW(init_future.get());

  Metadata arguments;
  arguments["who"] = std::string("gopher");
  auto prompt_future =
      client_->getPrompt("greet", mcp::make_optional(arguments));
  ASSERT_EQ(prompt_future.wait_for(5s), std::future_status::ready);

  GetPromptResult result;
  ASSERT_NO_THROW(result = prompt_future.get());
  ASSERT_TRUE(result.description.has_value());
  EXPECT_EQ(result.description.value(), "{}");
  ASSERT_EQ(result.messages.size(), 1u);
  EXPECT_EQ(result.messages[0].role, enums::Role::USER);
  ASSERT_TRUE(holds_alternative<TextContent>(result.messages[0].content));
  EXPECT_EQ(get<TextContent>(result.messages[0].content).text, "hello gopher");
}

// What a tool sees of the session it was called in, as text: whether the
// client declared sampling and elicitation at the handshake, and whether it
// has said the handshake is over.
std::string describeSession(server::SessionContext& session) {
  const auto& caps = session.getClientCapabilities();
  return std::string("sampling=") + (caps.sampling.has_value() ? "1" : "0") +
         " elicitation=" + (caps.elicitation.has_value() ? "1" : "0") +
         " initialized=" + (session.isInitialized() ? "1" : "0");
}

// The older era's handshake has to say what the client can answer, or a
// server never asks it anything. A handler registered before initialize
// is a capability declared by it; one registered after changes nothing the
// handshake already said. And the handshake is over for the server only
// once notifications/initialized has arrived.
TEST_F(McpClientInitializeRoutingTest,
       TheHandshakeDeclaresWhatTheClientCanAnswer) {
  Tool inspect;
  inspect.name = "inspect";
  ASSERT_TRUE(server_->registerTool(
      inspect, [](const std::string&, const optional<Metadata>&,
                  server::SessionContext& session) {
        CallToolResult result;
        result.content.push_back(TextContent(describeSession(session)));
        return result;
      }));

  // What an application's own handler for the notification sees. It runs
  // while the notification is handled, and the handshake is over by then.
  auto seen_by_handler = std::make_shared<std::promise<bool>>();
  auto seen_by_handler_future = seen_by_handler->get_future();
  auto reported = std::make_shared<std::atomic<bool>>(false);
  server_->registerNotificationHandler(
      "notifications/initialized",
      [seen_by_handler, reported](const jsonrpc::Notification&,
                                  server::SessionContext& session) {
        if (!reported->exchange(true)) {
          seen_by_handler->set_value(session.isInitialized());
        }
      });

  client::McpClientConfig client_config;
  client_config.client_name = "init-routing-test-client";
  client_config.client_version = "0.0.1";
  client_config.num_workers = 1;
  client_config.request_timeout = 5000ms;
  client_config.protocol_initialization_timeout = 5000ms;
  client_config.protocol_connection_timeout = 5000ms;
  client_config.streamable_http.enable_modern_era = false;

  client_ = client::createMcpClient(client_config);
  ASSERT_NE(client_, nullptr);

  client_->registerRequestHandler(
      "sampling/createMessage",
      [](const jsonrpc::Request&) { return jsonrpc::ResponseResult(nullptr); });

  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));
  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  ASSERT_NO_THROW(init_future.get());

  client_->registerRequestHandler(
      "elicitation/create",
      [](const jsonrpc::Request&) { return jsonrpc::ResponseResult(nullptr); });

  // notifications/initialized is sent as the handshake completes, but the
  // server reads it on its own schedule, so it is waited for rather than
  // assumed to be ahead of the next request.
  std::string seen;
  for (int attempt = 0; attempt < 40; ++attempt) {
    auto called = client_->callTool("inspect");
    ASSERT_EQ(called.wait_for(5s), std::future_status::ready);
    CallToolResult result;
    ASSERT_NO_THROW(result = called.get());
    ASSERT_EQ(result.content.size(), 1u);
    ASSERT_TRUE(holds_alternative<TextContent>(result.content[0]));
    seen = get<TextContent>(result.content[0]).text;
    if (seen.find("initialized=1") != std::string::npos) {
      break;
    }
    std::this_thread::sleep_for(50ms);
  }

  EXPECT_NE(seen.find("sampling=1"), std::string::npos)
      << "a handler registered before the handshake was not declared: " << seen;
  EXPECT_NE(seen.find("elicitation=0"), std::string::npos)
      << "a handler registered after the handshake changed what it said: "
      << seen;
  EXPECT_NE(seen.find("initialized=1"), std::string::npos)
      << "notifications/initialized never marked the session: " << seen;

  ASSERT_EQ(seen_by_handler_future.wait_for(5s), std::future_status::ready)
      << "the application's handler for notifications/initialized never ran";
  EXPECT_TRUE(seen_by_handler_future.get())
      << "the handler for notifications/initialized saw the session as not "
         "yet initialized";
}

// The listing comes back as an object with the prompts under "prompts",
// which is what this SDK's own server sends. Read as anything else, every
// prompt the server has is silently dropped.
TEST_F(McpClientInitializeRoutingTest, ThePromptsTheServerListsAreReturned) {
  Prompt greet("greet");
  greet.description = mcp::make_optional(std::string("Say hello"));
  PromptArgument who;
  who.name = "who";
  who.description = mcp::make_optional(std::string("Whom to greet"));
  who.required = true;
  greet.arguments = mcp::make_optional(std::vector<PromptArgument>{who});
  server_->registerPrompt(
      greet, [](const std::string&, const optional<Metadata>&,
                server::SessionContext&) { return GetPromptResult(); });
  server_->registerPrompt(
      Prompt("farewell"),
      [](const std::string&, const optional<Metadata>&,
         server::SessionContext&) { return GetPromptResult(); });

  connectInitializedClient();
  auto listed = client_->listPrompts();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);

  ListPromptsResult result;
  ASSERT_NO_THROW(result = listed.get());
  ASSERT_EQ(result.prompts.size(), 2u);

  const Prompt* found = nullptr;
  for (const auto& prompt : result.prompts) {
    if (prompt.name == "greet") {
      found = &prompt;
    }
  }
  ASSERT_NE(found, nullptr) << "greet was not listed";
  ASSERT_TRUE(found->description.has_value());
  EXPECT_EQ(found->description.value(), "Say hello");
  ASSERT_TRUE(found->arguments.has_value());
  ASSERT_EQ(found->arguments->size(), 1u);
  EXPECT_EQ(found->arguments->at(0).name, "who");
  EXPECT_TRUE(found->arguments->at(0).required);
}

// A listing with more to come says where the next page starts, and the
// caller cannot ask for it without being told.
TEST_F(McpClientInitializeRoutingTest, APagedListingCarriesItsCursor) {
  answerPromptsListWith(
      R"({"prompts":[{"name":"first"}],"nextCursor":"page-2"})");

  connectInitializedClient();
  auto listed = client_->listPrompts();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);

  ListPromptsResult result;
  ASSERT_NO_THROW(result = listed.get());
  ASSERT_EQ(result.prompts.size(), 1u);
  EXPECT_EQ(result.prompts[0].name, "first");
  ASSERT_TRUE(result.nextCursor.has_value());
  EXPECT_EQ(result.nextCursor.value(), "page-2");
}

// Older servers answered with the bare array. One of prompts with no
// arguments looks exactly like a list of tools, and is still read.
TEST_F(McpClientInitializeRoutingTest, AnOlderServersBareArrayIsStillRead) {
  answerPromptsListWith(R"([{"name":"first","description":"The first"}])");

  connectInitializedClient();
  auto listed = client_->listPrompts();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);

  ListPromptsResult result;
  ASSERT_NO_THROW(result = listed.get());
  ASSERT_EQ(result.prompts.size(), 1u);
  EXPECT_EQ(result.prompts[0].name, "first");
  ASSERT_TRUE(result.prompts[0].description.has_value());
  EXPECT_EQ(result.prompts[0].description.value(), "The first");
}

// An answer that is no listing at all fails the call. An empty list in its
// place would say the server has no prompts, which nothing said.
TEST_F(McpClientInitializeRoutingTest, AnAnswerThatIsNoListingFailsTheCall) {
  answerPromptsListWith(R"({"tools":"not prompts"})");

  connectInitializedClient();
  auto listed = client_->listPrompts();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);
  EXPECT_THROW(listed.get(), std::runtime_error);
}

// A tool whose result cannot go on the wire as the spec shapes it gets an
// error back, not a malformed answer and not a server that falls over.
TEST_F(McpClientInitializeRoutingTest, AResultThatCannotBeEncodedIsAnError) {
  Tool broken("broken");
  ASSERT_TRUE(server_->registerTool(
      broken, [](const std::string&, const optional<Metadata>&,
                 server::SessionContext&) {
        CallToolResult result;
        // Embedded contents with no uri, which the spec does not allow.
        result.content.push_back(
            make_embedded_resource(TextResourceContents("orphan")));
        return result;
      }));

  connectInitializedClient();
  auto called = client_->callTool("broken");
  ASSERT_EQ(called.wait_for(5s), std::future_status::ready);
  EXPECT_THROW(called.get(), std::runtime_error);

  // And the server is still answering.
  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

// A client speaking the newest revision is told how long it may cache what
// it discovered and listed, and whether it may share it. With nothing
// configured on the server, that is the safe answer: refetch, and keep it
// to yourself.
TEST_F(McpClientInitializeRoutingTest, CachingHintsReachTheClient) {
  connectInitializedClient();

  // connectInitializedClient() already ran the handshake, which in this
  // revision is a server/discover; run it again to look at the answer.
  auto discovered = client_->initializeProtocol();
  ASSERT_EQ(discovered.wait_for(5s), std::future_status::ready);
  InitializeResult discovery;
  ASSERT_NO_THROW(discovery = discovered.get());
  ASSERT_TRUE(discovery.ttlMs.has_value()) << "discovery carried no ttlMs";
  EXPECT_EQ(discovery.ttlMs.value(), 0);
  ASSERT_TRUE(discovery.cacheScope.has_value());
  EXPECT_EQ(discovery.cacheScope.value(), "private");

  auto listed = client_->listTools();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);
  ListToolsResult tools;
  ASSERT_NO_THROW(tools = listed.get());
  ASSERT_TRUE(tools.ttlMs.has_value()) << "tools/list carried no ttlMs";
  EXPECT_EQ(tools.ttlMs.value(), 0);
  ASSERT_TRUE(tools.cacheScope.has_value());
  EXPECT_EQ(tools.cacheScope.value(), "private");
}

/** A client that names Streamable HTTP as its transport. */
client::McpClientConfig namedTransportConfig() {
  client::McpClientConfig config;
  config.client_name = "init-routing-test-client";
  config.client_version = "0.0.1";
  config.num_workers = 1;
  config.request_timeout = 5000ms;
  config.protocol_initialization_timeout = 5000ms;
  config.protocol_connection_timeout = 5000ms;
  config.preferred_transport = TransportType::StreamableHttp;
  return config;
}

// What the server received as a call's params, read back from its answer.
// The handlers below replace the server's own and echo the params exactly
// as they came off the wire.
json::JsonValue echoedParams(const std::string& text) {
  return json::JsonValue::parse(text);
}

// A tool's arguments reach the server as they were given. Through the flat
// map they used to be rewritten on the way: a string that looks like JSON
// became an object, and an integer wider than int was cut down.
TEST_F(McpClientInitializeRoutingTest, ToolArgumentsReachTheServerUnchanged) {
  server_->registerRequestHandler(
      "tools/call",
      [](const jsonrpc::Request& request, server::SessionContext&) {
        const std::string received = request.params_json.has_value()
                                         ? request.params_json->toString()
                                         : std::string("null");
        CallToolResult result;
        result.content.push_back(TextContent(received));
        return jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(json::to_json(result)));
      });
  connectInitializedClient();

  auto textOf = [](std::future<CallToolResult> call) {
    EXPECT_EQ(call.wait_for(5s), std::future_status::ready);
    CallToolResult result = call.get();
    return get<TextContent>(result.content.at(0)).text;
  };

  Metadata flat;
  flat["literal"] = std::string("{}");
  flat["list"] = std::string("[1,2]");
  flat["wide"] = static_cast<int64_t>(5000000000LL);
  flat["flag"] = true;
  auto sent = echoedParams(textOf(client_->callTool("echo", flat)));
  EXPECT_EQ(sent["name"].getString(), "echo");
  ASSERT_TRUE(sent["arguments"]["literal"].isString())
      << "a string argument was sent as something else: " << sent.toString();
  EXPECT_EQ(sent["arguments"]["literal"].getString(), "{}");
  EXPECT_TRUE(sent["arguments"]["list"].isString());
  EXPECT_EQ(sent["arguments"]["wide"].getInt64(), 5000000000LL)
      << "a wide integer argument was narrowed";
  EXPECT_TRUE(sent["arguments"]["flag"].getBool());

  // Nested arguments, given as the JSON they are.
  auto nested = json::JsonValue::parse(
      R"({"filter":{"tags":["a","b"],"limit":9007199254740993},)"
      R"("raw":"{\"not\":\"parsed\"}"})");
  sent = echoedParams(textOf(client_->callTool("echo", nested)));
  EXPECT_EQ(sent["arguments"].toString(), nested.toString());
}

// A prompt's arguments go the same way.
TEST_F(McpClientInitializeRoutingTest, PromptArgumentsReachTheServerUnchanged) {
  server_->registerRequestHandler(
      "prompts/get",
      [](const jsonrpc::Request& request, server::SessionContext&) {
        const std::string received = request.params_json.has_value()
                                         ? request.params_json->toString()
                                         : std::string("null");
        auto result = json::JsonValue::object();
        auto message = json::JsonValue::object();
        message.set("role", json::JsonValue("user"));
        auto content = json::JsonValue::object();
        content.set("type", json::JsonValue("text"));
        content.set("text", json::JsonValue(received));
        message.set("content", content);
        auto messages = json::JsonValue::array();
        messages.push_back(message);
        result.set("messages", messages);
        return jsonrpc::Response::success(request.id,
                                          jsonrpc::ResponseResult(result));
      });
  connectInitializedClient();

  Metadata flat;
  flat["topic"] = std::string("[draft]");
  auto got = client_->getPrompt("greet", flat);
  ASSERT_EQ(got.wait_for(5s), std::future_status::ready);
  GetPromptResult prompt = got.get();
  ASSERT_EQ(prompt.messages.size(), 1u);
  auto sent = echoedParams(get<TextContent>(prompt.messages[0].content).text);
  EXPECT_EQ(sent["name"].getString(), "greet");
  ASSERT_TRUE(sent["arguments"]["topic"].isString()) << sent.toString();
  EXPECT_EQ(sent["arguments"]["topic"].getString(), "[draft]");

  auto given = json::JsonValue::parse(R"({"topic":"{draft}"})");
  got = client_->getPrompt("greet", given);
  ASSERT_EQ(got.wait_for(5s), std::future_status::ready);
  prompt = got.get();
  sent = echoedParams(get<TextContent>(prompt.messages[0].content).text);
  EXPECT_EQ(sent["arguments"].toString(), given.toString());
}

// {} has always meant "no arguments", and still does next to the overloads
// that take JSON: this must compile, and send none.
TEST_F(McpClientInitializeRoutingTest, EmptyBracesStillMeanNoArguments) {
  auto echo = [](const jsonrpc::Request& request, server::SessionContext&) {
    const std::string received = request.params_json.has_value()
                                     ? request.params_json->toString()
                                     : std::string("null");
    CallToolResult result;
    result.content.push_back(TextContent(received));
    return jsonrpc::Response::success(
        request.id, jsonrpc::ResponseResult(json::to_json(result)));
  };
  server_->registerRequestHandler("tools/call", echo);
  connectInitializedClient();

  auto expectNoArguments = [](std::future<CallToolResult> call) {
    ASSERT_EQ(call.wait_for(5s), std::future_status::ready);
    CallToolResult result = call.get();
    auto sent = echoedParams(get<TextContent>(result.content.at(0)).text);
    EXPECT_EQ(sent["name"].getString(), "echo");
    EXPECT_FALSE(sent.contains("arguments")) << sent.toString();
  };
  expectNoArguments(client_->callTool("echo", {}));
  expectNoArguments(client_->callTool("echo", {}, {}));

  // getPrompt takes {} the same way; what it sends is covered above, so
  // here it only has to be accepted.
  auto prompt = client_->getPrompt("greet", {});
  EXPECT_EQ(prompt.wait_for(5s), std::future_status::ready);
}

// What a tool says about itself reaches the client that lists it.
TEST_F(McpClientInitializeRoutingTest, AToolsTitleAndHintsAreListed) {
  Tool tool = make<Tool>("drop_table")
                  .title("Drop a table")
                  .destructiveHint(true)
                  .openWorldHint(false)
                  .meta(json::JsonValue::parse(R"({"owner":{"team":"db"}})"))
                  .build();
  ASSERT_TRUE(server_->registerTool(
      tool, [](const std::string&, const optional<Metadata>&,
               server::SessionContext&) { return CallToolResult(); }));
  connectInitializedClient();

  auto listed = client_->listTools();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);
  ListToolsResult result = listed.get();
  const Tool* found = nullptr;
  for (const auto& each : result.tools) {
    if (each.name == "drop_table") {
      found = &each;
    }
  }
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->displayName(), "Drop a table");
  ASSERT_TRUE(found->annotations.has_value());
  EXPECT_EQ(found->annotations->destructiveHint, mcp::make_optional(true));
  EXPECT_EQ(found->annotations->openWorldHint, mcp::make_optional(false));
  EXPECT_FALSE(found->annotations->readOnlyHint.has_value());
  ASSERT_TRUE(found->_meta.has_value());
  EXPECT_EQ((*found->_meta)["owner"]["team"].getString(), "db");
}

// A cursor is opaque: whatever the server gave, the client hands back
// exactly, even one that looks like JSON. Each handler here answers with
// the cursor it received, so what comes back is what went out.
TEST_F(McpClientInitializeRoutingTest, ACursorGoesBackExactlyAsGiven) {
  auto echoCursor = [](const char* items) {
    return [items](const jsonrpc::Request& request, server::SessionContext&) {
      json::JsonValue result = json::JsonValue::object();
      result.set(items, json::JsonValue::array());
      const bool carried = request.params_json.has_value() &&
                           request.params_json->contains("cursor");
      result.set("nextCursor", carried ? (*request.params_json)["cursor"]
                                       : json::JsonValue("<none>"));
      return jsonrpc::Response::success(request.id,
                                        jsonrpc::ResponseResult(result));
    };
  };
  server_->registerRequestHandler("tools/list", echoCursor("tools"));
  server_->registerRequestHandler("prompts/list", echoCursor("prompts"));
  server_->registerRequestHandler("resources/list", echoCursor("resources"));
  server_->registerRequestHandler("resources/templates/list",
                                  echoCursor("resourceTemplates"));
  connectInitializedClient();

  const std::string looks_like_json = R"({"offset":2})";
  for (const std::string& cursor : {looks_like_json, std::string("")}) {
    SCOPED_TRACE(cursor);
    auto tools = client_->listTools(mcp::make_optional(cursor));
    ASSERT_EQ(tools.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(tools.get().nextCursor, mcp::make_optional(cursor));

    auto prompts = client_->listPrompts(mcp::make_optional(cursor));
    ASSERT_EQ(prompts.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(prompts.get().nextCursor, mcp::make_optional(cursor));

    auto resources = client_->listResources(mcp::make_optional(cursor));
    ASSERT_EQ(resources.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(resources.get().nextCursor, mcp::make_optional(cursor));

    auto templates = client_->listResourceTemplates(mcp::make_optional(cursor));
    ASSERT_EQ(templates.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(templates.get().nextCursor, mcp::make_optional(cursor));
  }

  // And none at all is no cursor, not an empty one.
  auto first = client_->listTools();
  ASSERT_EQ(first.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(first.get().nextCursor, mcp::make_optional(std::string("<none>")));
}

// A server's resource templates reach the client that lists them, with
// what each says about itself.
TEST_F(McpClientInitializeRoutingTest, ResourceTemplatesAreListed) {
  server_->registerResourceTemplate(
      make<ResourceTemplate>("file:///{path}", "files")
          .title("Project files")
          .mimeType("text/plain")
          .meta(json::JsonValue::parse(R"({"vendor":{"indexed":true}})"))
          .build());
  server_->registerResourceTemplate(
      make<ResourceTemplate>("db://{table}", "tables").build());
  connectInitializedClient();

  auto listed = client_->listResourceTemplates();
  ASSERT_EQ(listed.wait_for(5s), std::future_status::ready);
  ListResourceTemplatesResult result;
  ASSERT_NO_THROW(result = listed.get());
  ASSERT_EQ(result.resourceTemplates.size(), 2u);
  // In uriTemplate order.
  EXPECT_EQ(result.resourceTemplates[0].uriTemplate, "db://{table}");
  const ResourceTemplate& files = result.resourceTemplates[1];
  EXPECT_EQ(files.uriTemplate, "file:///{path}");
  EXPECT_EQ(files.title, mcp::make_optional(std::string("Project files")));
  EXPECT_EQ(files.mimeType, mcp::make_optional(std::string("text/plain")));
  ASSERT_TRUE(files._meta.has_value());
  EXPECT_TRUE((*files._meta)["vendor"]["indexed"].getBool());
  EXPECT_FALSE(result.nextCursor.has_value());
}

// A call to a tool the server does not have fails as an error the caller
// can read the code of, not as a result from a tool that ran.
TEST_F(McpClientInitializeRoutingTest, AnUnknownToolIsReportedAsAnError) {
  connectInitializedClient();

  auto call = client_->callTool("no_such_tool");
  ASSERT_EQ(call.wait_for(5s), std::future_status::ready);
  try {
    call.get();
    FAIL() << "a call to an unknown tool succeeded";
  } catch (const client::RequestError& refused) {
    EXPECT_EQ(refused.code(), jsonrpc::INVALID_PARAMS);
    EXPECT_NE(std::string(refused.what()).find("no_such_tool"),
              std::string::npos)
        << refused.what();
  }
}

// Arguments are an object or nothing; anything else is refused before it
// is sent rather than left for the server to make sense of.
TEST_F(McpClientInitializeRoutingTest, ArgumentsThatAreNoObjectAreRefused) {
  connectInitializedClient();
  auto call = client_->callTool("echo", json::JsonValue("not an object"));
  ASSERT_EQ(call.wait_for(5s), std::future_status::ready);
  EXPECT_THROW(call.get(), std::invalid_argument);
  auto prompt = client_->getPrompt("greet", json::JsonValue::array());
  ASSERT_EQ(prompt.wait_for(5s), std::future_status::ready);
  EXPECT_THROW(prompt.get(), std::invalid_argument);
}

// In the newest revision the server names itself on every result, and the
// client keeps the latest for display and logging.
TEST_F(McpClientInitializeRoutingTest, TheClientKnowsWhoAnswered) {
  connectInitializedClient();
  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  ASSERT_FALSE(ping.get().error.has_value());

  const auto who = client_->serverInfo();
  ASSERT_TRUE(who.has_value()) << "no result named the server";
  EXPECT_EQ(who->name, "init-routing-test-server");
  EXPECT_EQ(who->version, "0.0.1");
}

// A client asks for completions of a prompt's argument or a resource
// template's parameter, in either revision, and gets the server's values.
TEST_F(McpClientInitializeRoutingTest, ArgumentsAreCompleted) {
  for (const bool newest : {true, false}) {
    SCOPED_TRACE(newest ? "2026-07-28" : "earlier");
    if (!newest) {
      client_->shutdown();
      client_.reset();
      stopServer();
      startServer(/*serve_newest=*/false);
    }
    server_->registerPromptCompletion(
        "code_review", [](const server::McpServer::CompletionQuery& query) {
          CompleteResult::Completion completion;
          const auto language = query.arguments.find("language");
          if (query.argument == "framework" &&
              language != query.arguments.end() &&
              language->second == "python") {
            for (const char* framework : {"flask", "fastapi", "django"}) {
              if (std::string(framework).compare(0, query.value.size(),
                                                 query.value) == 0) {
                completion.values.push_back(framework);
              }
            }
          }
          completion.total = static_cast<double>(completion.values.size());
          return completion;
        });
    server_->registerResourceTemplateCompletion(
        "file:///{path}", [](const server::McpServer::CompletionQuery& query) {
          CompleteResult::Completion completion;
          completion.values = {query.value + "main.rs"};
          completion.hasMore = true;
          return completion;
        });
    connectInitializedClient();

    auto prompt = client_->completePromptArgument(
        "code_review", "framework", "f", {{"language", "python"}});
    ASSERT_EQ(prompt.wait_for(5s), std::future_status::ready);
    const CompleteResult frameworks = prompt.get();
    EXPECT_EQ(frameworks.completion.values,
              (std::vector<std::string>{"flask", "fastapi"}));
    EXPECT_EQ(frameworks.completion.total, mcp::make_optional(2.0));
    EXPECT_FALSE(frameworks.completion.hasMore);

    auto templated = client_->completeResourceTemplateArgument("file:///{path}",
                                                               "path", "src/");
    ASSERT_EQ(templated.wait_for(5s), std::future_status::ready);
    const CompleteResult paths = templated.get();
    EXPECT_EQ(paths.completion.values,
              (std::vector<std::string>{"src/main.rs"}));
    EXPECT_TRUE(paths.completion.hasMore);
  }
}

// The client's trace context reaches the server's handler: from a scope
// around the call, else from the provider, in either revision. Each
// request the client sends is a span ended with its outcome.
TEST_F(McpClientInitializeRoutingTest, TheTraceContextReachesTheServer) {
  const std::string from_provider =
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  const std::string from_scope =
      "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01";
  for (const bool newest : {true, false}) {
    SCOPED_TRACE(newest ? "2026-07-28" : "earlier");
    if (!newest) {
      client_->shutdown();
      client_.reset();
      stopServer();
      startServer(/*serve_newest=*/false);
    }
    server_->registerRequestHandler(
        "tools/call",
        [](const jsonrpc::Request& request, server::SessionContext&) {
          const auto& seen = protocol::trace::current();
          CallToolResult result;
          result.content.push_back(
              TextContent(seen.traceparent.value_or("none") + " " +
                          seen.baggage.value_or("none")));
          return jsonrpc::Response::success(
              request.id, jsonrpc::ResponseResult(json::to_json(result)));
        });

    auto spans = std::make_shared<std::vector<std::string>>();
    auto spans_mutex = std::make_shared<std::mutex>();
    tweak_client_ = [&](client::McpClientConfig& config) {
      config.trace_context_provider = [from_provider]() {
        protocol::trace::TraceContext context;
        context.traceparent = from_provider;
        context.baggage = std::string("from=provider");
        return context;
      };
      config.span_hook = [spans,
                          spans_mutex](const protocol::trace::SpanStart& start)
          -> protocol::trace::SpanEnd {
        return [spans, spans_mutex, start](const optional<Error>& error) {
          std::lock_guard<std::mutex> lock(*spans_mutex);
          spans->push_back(start.method + " " +
                           start.context.traceparent.value_or("none") +
                           (error.has_value() ? " failed" : " ok"));
        };
      };
    };
    connectInitializedClient();

    auto textOf = [](std::future<CallToolResult> call) {
      EXPECT_EQ(call.wait_for(5s), std::future_status::ready);
      return get<TextContent>(call.get().content.at(0)).text;
    };

    EXPECT_EQ(textOf(client_->callTool("echo", json::JsonValue::object())),
              from_provider + " from=provider");

    {
      protocol::trace::TraceContext scoped;
      scoped.traceparent = from_scope;
      protocol::trace::TraceScope scope(scoped);
      EXPECT_EQ(textOf(client_->callTool("echo", json::JsonValue::object())),
                from_scope + " none");
    }

    // An empty scope wins too: it is how a caller sends no trace whatever
    // the provider would say.
    {
      protocol::trace::TraceScope untraced{protocol::trace::TraceContext()};
      EXPECT_EQ(textOf(client_->callTool("echo", json::JsonValue::object())),
                "none none");
    }

    std::lock_guard<std::mutex> lock(*spans_mutex);
    EXPECT_NE(std::find(spans->begin(), spans->end(),
                        "tools/call " + from_provider + " ok"),
              spans->end());
    EXPECT_NE(std::find(spans->begin(), spans->end(),
                        "tools/call " + from_scope + " ok"),
              spans->end());
    tweak_client_ = nullptr;
  }
}

// A tracer that throws costs the caller nothing: the request still
// resolves with its answer.
TEST_F(McpClientInitializeRoutingTest, AThrowingTracerStillAnswers) {
  tweak_client_ = [](client::McpClientConfig& config) {
    config.span_hook =
        [](const protocol::trace::SpanStart&) -> protocol::trace::SpanEnd {
      return [](const optional<Error>&) {
        throw std::runtime_error("exporter down");
      };
    };
  };
  connectInitializedClient();
  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

// A server that offers no completions refuses, and the call fails with
// what it said.
TEST_F(McpClientInitializeRoutingTest, NoCompletionsIsARefusal) {
  connectInitializedClient();
  auto call = client_->completePromptArgument("code_review", "language", "p");
  ASSERT_EQ(call.wait_for(5s), std::future_status::ready);
  try {
    call.get();
    FAIL() << "a server with no completions answered";
  } catch (const client::RequestError& e) {
    EXPECT_EQ(e.error().code, jsonrpc::METHOD_NOT_FOUND);
  }
}

// A client of an earlier revision hears a list change through its own
// notification handlers, and only from a server that said it announces them.
TEST_F(McpClientInitializeRoutingTest, AnEarlierRevisionHearsListChanges) {
  for (const bool announces : {true, false}) {
    SCOPED_TRACE(announces ? "announced" : "not announced");
    stopServer();
    tweak_config_ = [announces](server::McpServerConfig& config) {
      config.tools_list_changed = announces;
    };
    startServer(/*serve_newest=*/false);

    auto heard = std::make_shared<std::atomic<int>>(0);
    client::McpClientConfig config = namedTransportConfig();
    config.streamable_http.enable_modern_era = false;
    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);
    client_->registerNotificationHandler(
        "notifications/tools/list_changed",
        [heard](const jsonrpc::Notification&) { ++*heard; });
    const std::string uri =
        "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));
    auto init = client_->initializeProtocol();
    ASSERT_EQ(init.wait_for(5s), std::future_status::ready);
    ASSERT_NO_THROW(init.get());
    // The stream it is told on, which opens once the handshake is done.
    std::this_thread::sleep_for(300ms);

    server_->notifyToolsListChanged();
    const auto deadline = std::chrono::steady_clock::now() +
                          (announces ? std::chrono::milliseconds(5000)
                                     : std::chrono::milliseconds(500));
    while (heard->load() == 0 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(heard->load(), announces ? 1 : 0);

    client_->shutdown();
    client_.reset();
  }
}

// The flags are advertised, in the handshake and in discovery alike, and a
// capability that says so itself wins.
TEST_F(McpClientInitializeRoutingTest, ListChangeFlagsAreAdvertised) {
  stopServer();
  tweak_config_ = [](server::McpServerConfig& config) {
    config.prompts_list_changed = true;
    config.resources_list_changed = true;
    config.resources_subscribe = true;
    config.capabilities.resources =
        mcp::make_optional(variant<bool, ResourcesCapability>(true));
    config.tools_list_changed = true;
    ToolsCapability tools;
    tools.listChanged = mcp::make_optional(false);
    config.capabilities.tools = mcp::make_optional(tools);
  };
  startServer(/*serve_newest=*/true);

  for (const bool newest : {true, false}) {
    SCOPED_TRACE(newest ? "server/discover" : "initialize");
    client::McpClientConfig config = namedTransportConfig();
    config.streamable_http.enable_modern_era = newest;
    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);
    const std::string uri =
        "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));
    auto init = client_->initializeProtocol();
    ASSERT_EQ(init.wait_for(5s), std::future_status::ready);
    InitializeResult result;
    ASSERT_NO_THROW(result = init.get());

    const ServerCapabilities& caps = result.capabilities;
    ASSERT_TRUE(caps.prompts.has_value());
    EXPECT_EQ(caps.prompts->listChanged, mcp::make_optional(true));
    ASSERT_TRUE(caps.resources.has_value());
    ASSERT_TRUE(holds_alternative<ResourcesCapability>(caps.resources.value()));
    const auto& resources = get<ResourcesCapability>(caps.resources.value());
    EXPECT_EQ(resources.listChanged, mcp::make_optional(true));
    EXPECT_EQ(resources.subscribe, mcp::make_optional(true));
    ASSERT_TRUE(caps.tools.has_value());
    EXPECT_EQ(caps.tools->listChanged, mcp::make_optional(false))
        << "the capability's own listChanged was overridden";

    client_->shutdown();
    client_.reset();
  }
}

// Naming the transport says which transport, not which revision: the
// client still asks, and speaks the newest revision to a server serving it.
TEST_F(McpClientInitializeRoutingTest, NamingTheTransportStillFindsTheNewest) {
  client_ = client::createMcpClient(namedTransportConfig());
  ASSERT_NE(client_, nullptr);
  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get());
  EXPECT_EQ(result.protocolVersion, protocol::kProtocolVersion20260728)
      << "a client that named its transport never spoke the newest revision";

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

// A client that declines the newest revision is not asked to find it.
TEST_F(McpClientInitializeRoutingTest,
       NamingTheTransportAndDecliningStaysOlder) {
  client::McpClientConfig config = namedTransportConfig();
  config.streamable_http.enable_modern_era = false;
  client_ = client::createMcpClient(config);
  ASSERT_NE(client_, nullptr);
  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get());
  EXPECT_EQ(result.protocolVersion, config.protocol_version);
}

// At the endpoint that holds sessions, everything after initialize has to
// carry the session the server issued, notifications/initialized first.
// A server refusing what comes without one would otherwise refuse the end
// of the handshake itself.
TEST_F(McpClientInitializeRoutingTest, AHandshakeAtTheSessionEndpointHolds) {
  auto arrived = std::make_shared<std::atomic<int>>(0);
  server_->registerNotificationHandler(
      "notifications/initialized",
      [arrived](const jsonrpc::Notification&, server::SessionContext&) {
        ++*arrived;
      });

  client::McpClientConfig config = namedTransportConfig();
  config.streamable_http.enable_modern_era = false;
  client_ = client::createMcpClient(config);
  ASSERT_NE(client_, nullptr);
  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get());
  EXPECT_EQ(result.protocolVersion, config.protocol_version);

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  const auto answer = ping.get();
  EXPECT_FALSE(answer.error.has_value())
      << (answer.error.has_value() ? answer.error->message : std::string());

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (arrived->load() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_EQ(arrived->load(), 1)
      << "the server never took notifications/initialized";
}

// A client that gives its session back and goes at once leaves the server
// answering on a connection that is already gone. That is the server's
// problem to absorb, not a reason for it to stop.
TEST_F(McpClientInitializeRoutingTest, AClientLeavingAtOnceLeavesTheServerUp) {
  auto arrived = std::make_shared<std::atomic<int>>(0);
  server_->registerNotificationHandler(
      "notifications/initialized",
      [arrived](const jsonrpc::Notification&, server::SessionContext&) {
        ++*arrived;
      });

  client::McpClientConfig config = namedTransportConfig();
  config.streamable_http.enable_modern_era = false;
  client_ = client::createMcpClient(config);
  ASSERT_NE(client_, nullptr);
  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  ASSERT_NO_THROW(init_future.get());
  client_->shutdown();
  client_.reset();

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (arrived->load() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_EQ(arrived->load(), 1);
}

// A server that serves only the older revisions is met through the
// handshake, even by a client that would rather speak the newest.
TEST_F(McpClientInitializeRoutingTest, NamingTheTransportMeetsAnOlderServer) {
  stopServer();
  startServer(/*serve_newest=*/false);

  client::McpClientConfig config = namedTransportConfig();
  client_ = client::createMcpClient(config);
  ASSERT_NE(client_, nullptr);
  const std::string uri = "http://127.0.0.1:" + std::to_string(port_) + "/rpc";
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(client_->connect(uri)));

  auto init_future = client_->initializeProtocol();
  ASSERT_EQ(init_future.wait_for(5s), std::future_status::ready);
  InitializeResult result;
  ASSERT_NO_THROW(result = init_future.get());
  EXPECT_FALSE(result.protocolVersion.empty());
  EXPECT_NE(result.protocolVersion, protocol::kProtocolVersion20260728);

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

}  // namespace
}  // namespace mcp
