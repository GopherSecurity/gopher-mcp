// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/http_security_filter.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"

#include "real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

struct Reply {
  std::map<std::string, std::string> headers;
  std::string body;
  bool complete{false};

  std::string header(const std::string& name) const {
    const auto it = headers.find(name);
    return it == headers.end() ? std::string() : it->second;
  }

  int status() const { return std::atoi(header(":status").c_str()); }
  std::string session() const { return header("mcp-session-id"); }
};

// Parse actual wire replies through the SDK codec, including chunked SSE.
class ReplyCapture : public HttpCodecFilter::MessageCallbacks {
 public:
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool) override {
    replies.emplace_back();
    replies.back().headers = headers;
  }
  void onBody(const std::string& data, bool) override {
    if (!replies.empty()) {
      replies.back().body += data;
    }
  }
  void onMessageComplete() override { replies.back().complete = true; }
  void onError(const std::string& error) override { errors.push_back(error); }

  std::vector<Reply> replies;
  std::vector<std::string> errors;
};

class IsolationCallbacks : public McpProtocolCallbacks {
 public:
  struct Pending {
    ResponseStreamPtr stream;
    jsonrpc::Response response;
    std::string session;
    std::string principal;
  };

  void onRequest(const jsonrpc::Request&) override {}
  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    json::JsonValue result = json::JsonValue::object();
    result["protocolVersion"] = version;
    result["principal"] = context.principal();
    result["session"] = context.transportSessionId();
    jsonrpc::Response response;
    response.id = request.id;
    response.result = mcp::make_optional(jsonrpc::ResponseResult(result));
    if (request.method == "test/delayed") {
      pending.push_back({context.beginResponseStream(), response,
                         context.transportSessionId(), context.principal()});
      return;
    }
    EXPECT_FALSE(holds_alternative<Error>(context.sendResponse(response)));
  }
  StreamingMode streamingFor(const jsonrpc::Request& request) const override {
    return request.method == "test/delayed" ? StreamingMode::Optional
                                            : StreamingMode::None;
  }
  void onNotification(const jsonrpc::Notification&) override {}
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  std::vector<Pending> pending;
  std::string version;
};

struct IsolationConfig {
  std::string endpoint;
  std::string version;
  bool sessions;
};

class HttpSessionResponseIsolationTest
    : public test::RealIoTestBase,
      public ::testing::WithParamInterface<IsolationConfig> {
 protected:
  struct Client {
    std::shared_ptr<stream_info::StreamInfoImpl> info;
    std::unique_ptr<network::ServerConnection> server;
    network::IoHandlePtr peer;
    ReplyCapture capture;
    std::unique_ptr<HttpCodecFilter> decoder;
    size_t consumed{0};
    std::string principal;
    std::string session;
  };

  void SetUp() override {
    test::RealIoTestBase::SetUp();
    executeInDispatcher([&]() {
      const auto& config = GetParam();
      callbacks_.version = config.version;
      factory_ = std::make_shared<HttpSseFilterChainFactory>(
          *dispatcher_, callbacks_, true, config.endpoint, "localhost", true,
          "/sse", config.endpoint);
      transport::StreamableHttpConfig sessions;
      sessions.enable_sessions = config.sessions;
      sessions.enable_modern_era = true;
      factory_->setSessionConfig(sessions);
      factory_->setSecurityConfig(sessions);
      factory_->setAuthCallback([](const RequestHeadersView& headers) {
        const auto path = headers.get("url");
        if (path == "/health" || path == "/info" || path == "/ready" ||
            path == "/.well-known/oauth-protected-resource" ||
            headers.get(":method") == "OPTIONS") {
          return AuthResult::allow("anonymous");
        }
        const auto auth = headers.get("authorization");
        if (auth.compare(0, 7, "Bearer ") != 0) {
          return AuthResult::deny(401, "authentication required");
        }
        return AuthResult::allow(auth.substr(7));
      });
      factory_->setRouteRegistrationCallback([](HttpRoutingFilter* routes) {
        for (const char* path :
             {"/ready", "/.well-known/oauth-protected-resource"}) {
          routes->registerHandler("GET", path, [](const auto&) {
            HttpRoutingFilter::Response response;
            response.body = "{}";
            response.headers["Content-Length"] = "2";
            return response;
          });
        }
        routes->addRoute("POST", "/blocked",
                         HttpRoutingFilter::RouteTarget::reject(429));
      });
    });
  }

  void TearDown() override {
    executeInDispatcher([&]() {
      for (auto& client : clients_) {
        if (client->server) {
          client->server->close(network::ConnectionCloseType::NoFlush);
        }
        client->decoder.reset();
        client->server.reset();
      }
      callbacks_.pending.clear();
      clients_.clear();
      factory_.reset();
    });
    test::RealIoTestBase::TearDown();
  }

  Client& connect(const std::string& principal) {
    auto client = std::make_unique<Client>();
    client->principal = principal;
    executeInDispatcher([&]() {
      auto pair = createSocketPair();
      auto address = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), address, address);
      client->info = std::make_shared<stream_info::StreamInfoImpl>();
      client->server = network::ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket),
          std::make_unique<network::RawBufferTransportSocket>(), *client->info);
      auto* impl = static_cast<network::ConnectionImpl*>(client->server.get());
      EXPECT_TRUE(factory_->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();
      client->peer = std::move(pair.second);
      client->decoder = std::make_unique<HttpCodecFilter>(client->capture,
                                                          *dispatcher_, false);
      client->decoder->onNewConnection();
    });
    clients_.push_back(std::move(client));
    return *clients_.back();
  }

  std::string message(const std::string& method, int id = 2) const {
    json::JsonValue request = json::JsonValue::object();
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = method;
    if (method == "initialize") {
      json::JsonValue params = json::JsonValue::object();
      params["protocolVersion"] = GetParam().version;
      params["capabilities"] = json::JsonValue::object();
      params["clientInfo"] = json::JsonValue::object();
      request["params"] = params;
    }
    return request.toString();
  }

  std::string auth(const Client& client) const {
    return "Authorization: Bearer " + client.principal + "\r\n";
  }

  std::string session(const Client& client) const {
    return client.session.empty()
               ? std::string()
               : "Mcp-Session-Id: " + client.session + "\r\n";
  }

  void send(Client& client,
            const std::string& method,
            const std::string& path,
            const std::string& body = "",
            const std::string& headers = "") {
    const auto protocol =
        path == GetParam().endpoint
            ? "MCP-Protocol-Version: " + GetParam().version + "\r\n"
            : std::string();
    const auto bytes = method + " " + path +
                       " HTTP/1.1\r\nHost: localhost\r\n"
                       "Content-Type: application/json\r\n" +
                       headers + protocol +
                       "Content-Length: " + std::to_string(body.size()) +
                       "\r\n\r\n" + body;
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(bytes);
      auto written = client.peer->write(buffer);
      ASSERT_TRUE(written.ok());
      ASSERT_EQ(buffer.length(), 0u);
    });
  }

  Reply read(Client& client,
             bool complete = true,
             const std::string& marker = "") {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer bytes;
      auto read = client.peer->read(bytes, 65536);
      if (read.ok() && *read > 0) {
        executeInDispatcher([&]() { client.decoder->onData(bytes, false); });
      }
      Reply reply;
      bool ready = executeInDispatcher([&]() {
        EXPECT_TRUE(client.capture.errors.empty());
        if (client.consumed >= client.capture.replies.size()) {
          return false;
        }
        reply = client.capture.replies[client.consumed];
        if (complete && !reply.complete) {
          return false;
        }
        if (!marker.empty() && reply.body.find(marker) == std::string::npos) {
          return false;
        }
        if (reply.complete) {
          ++client.consumed;
        }
        return true;
      });
      if (ready) {
        return reply;
      }
      std::this_thread::sleep_for(2ms);
    }
    ADD_FAILURE() << "HTTP response did not arrive for " << client.principal;
    return Reply();
  }

  void initialize(Client& client) {
    send(client, "POST", GetParam().endpoint, message("initialize", 1),
         auth(client));
    const auto reply = read(client);
    ASSERT_EQ(reply.status(), 200);
    client.session = reply.session();
    EXPECT_EQ(!client.session.empty(), GetParam().sessions);
  }

  void prime(Client& client) {
    send(client, "POST", GetParam().endpoint, message("tools/list"),
         auth(client) + session(client));
    const auto reply = read(client);
    ASSERT_EQ(reply.status(), 200);
    EXPECT_EQ(reply.session(), client.session);
  }

  bool awaitPending(size_t count) {
    return waitFor(
        [&]() {
          return executeInDispatcher(
              [&]() { return callbacks_.pending.size() >= count; });
        },
        2s);
  }

  IsolationCallbacks callbacks_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::vector<std::unique_ptr<Client>> clients_;
};

TEST_P(HttpSessionResponseIsolationTest,
       PublicRepliesAfterMcpDoNotInheritSession) {
  auto& client = connect("alice");
  initialize(client);
  for (const char* path : {"/health", "/info", "/ready",
                           "/.well-known/oauth-protected-resource"}) {
    SCOPED_TRACE(path);
    prime(client);
    send(client, "GET", path);
    const auto reply = read(client);
    EXPECT_EQ(reply.status(), 200);
    EXPECT_TRUE(reply.session().empty()) << path << ": " << reply.session();
  }
}

TEST_P(HttpSessionResponseIsolationTest, RejectedRepliesDoNotInheritSession) {
  auto& client = connect("alice");
  initialize(client);
  prime(client);
  send(client, "POST", GetParam().endpoint, message("tools/list"));
  auto reply = read(client);
  EXPECT_EQ(reply.status(), 401);
  EXPECT_TRUE(reply.session().empty());

  prime(client);
  send(client, "POST", "/blocked", "{}", auth(client));
  reply = read(client);
  EXPECT_EQ(reply.status(), 429);
  EXPECT_TRUE(reply.session().empty());

  prime(client);
  send(client, "POST", GetParam().endpoint, "{",
       auth(client) + session(client));
  reply = read(client);
  EXPECT_EQ(reply.status(), 400);
  EXPECT_TRUE(reply.session().empty());

  if (GetParam().sessions) {
    prime(client);
    send(client, "POST", GetParam().endpoint, message("tools/list"),
         "Authorization: Bearer mallory\r\n" + session(client));
    reply = read(client);
    EXPECT_EQ(reply.status(), 403);
    EXPECT_TRUE(reply.session().empty());
  }
  prime(client);
}

TEST_P(HttpSessionResponseIsolationTest, ControlPlaneRepliesStaySessionless) {
  auto& client = connect("alice");
  initialize(client);
  prime(client);
  send(client, "OPTIONS", GetParam().endpoint, "",
       "Origin: http://localhost:3000\r\n"
       "Access-Control-Request-Method: POST\r\n" +
           session(client));
  auto reply = read(client);
  EXPECT_EQ(reply.status(), 204);
  EXPECT_TRUE(reply.session().empty());

  prime(client);
  send(client, "POST", GetParam().endpoint, message("server/discover"),
       auth(client));
  reply = read(client);
  EXPECT_EQ(reply.status(), 200);
  EXPECT_TRUE(reply.session().empty());

  prime(client);
  send(client, "DELETE", GetParam().endpoint, "",
       auth(client) + session(client));
  reply = read(client);
  EXPECT_EQ(reply.status(), GetParam().sessions ? 204 : 405);
  EXPECT_TRUE(reply.session().empty());

  send(client, "GET", "/health");
  reply = read(client);
  EXPECT_EQ(reply.status(), 200);
  EXPECT_TRUE(reply.session().empty());
}

TEST_P(HttpSessionResponseIsolationTest,
       DelayedRepliesKeepTheirOwnSessionAfterAnotherRequestArrives) {
  auto& alice = connect("alice");
  auto& bob = connect("bob");
  initialize(alice);
  initialize(bob);

  send(alice, "POST", GetParam().endpoint, message("test/delayed", 31),
       auth(alice) + session(alice));
  ASSERT_TRUE(awaitPending(1));
  // Pipeline another caller's request, but answer in HTTP request order.
  // Each answer must remember its own headers even after both dispatches end.
  send(alice, "POST", GetParam().endpoint, message("test/delayed", 32),
       auth(bob) + (GetParam().sessions
                        ? session(bob)
                        : "Mcp-Session-Id: untrusted-session\r\n"));
  ASSERT_TRUE(awaitPending(2));
  executeInDispatcher([&]() {
    for (auto& pending : callbacks_.pending) {
      ASSERT_NE(pending.stream, nullptr);
      EXPECT_FALSE(holds_alternative<Error>(
          pending.stream->sendResponse(pending.response)));
    }
  });

  const auto first = read(alice);
  const auto second = read(alice);
  EXPECT_EQ(first.status(), 200);
  EXPECT_EQ(second.status(), 200);
  EXPECT_EQ(first.session(), alice.session);
  EXPECT_EQ(second.session(), bob.session);
  const auto first_body = json::JsonValue::parse(first.body);
  const auto second_body = json::JsonValue::parse(second.body);
  EXPECT_EQ(first_body["id"].getInt(), 31);
  EXPECT_EQ(second_body["id"].getInt(), 32);
  EXPECT_EQ(first_body["result"]["principal"].getString(), "alice");
  EXPECT_EQ(second_body["result"]["principal"].getString(), "bob");
  send(alice, "GET", "/health");
  EXPECT_TRUE(read(alice).session().empty());
}

TEST_P(HttpSessionResponseIsolationTest,
       StreamHeadersBelongToTheDelayedRequest) {
  auto& alice = connect("alice");
  auto& bob = connect("bob");
  initialize(alice);
  initialize(bob);
  send(alice, "POST", GetParam().endpoint, message("test/delayed"),
       auth(alice) + session(alice) + "Accept: text/event-stream\r\n");
  ASSERT_TRUE(awaitPending(1));
  prime(bob);
  executeInDispatcher([&]() {
    auto& pending = callbacks_.pending.front();
    ASSERT_NE(pending.stream, nullptr);
    jsonrpc::Notification progress;
    progress.method = "notifications/progress";
    EXPECT_FALSE(
        holds_alternative<Error>(pending.stream->sendNotification(progress)));
  });
  const auto opening = read(alice, false, "notifications/progress");
  EXPECT_EQ(opening.status(), 200);
  EXPECT_EQ(opening.session(), alice.session);
  EXPECT_EQ(opening.header("content-type"), "text/event-stream");

  send(bob, "GET", "/health");
  EXPECT_TRUE(read(bob).session().empty());
  executeInDispatcher([&]() {
    auto& pending = callbacks_.pending.front();
    EXPECT_FALSE(holds_alternative<Error>(
        pending.stream->sendResponse(pending.response)));
  });
  const auto finished = read(alice);
  EXPECT_EQ(finished.session(), alice.session);
  EXPECT_NE(finished.body.find("\"principal\":\"alice\""), std::string::npos);
  prime(alice);
}

TEST_P(HttpSessionResponseIsolationTest,
       ParallelPersistentClientsStayIsolated) {
  std::vector<Client*> clients;
  for (int i = 0; i < 4; ++i) {
    clients.push_back(&connect("caller-" + std::to_string(i)));
  }
  // All four requests are outstanding before any replies are consumed.
  for (auto* client : clients) {
    send(*client, "POST", GetParam().endpoint, message("initialize", 1),
         auth(*client));
  }
  std::set<std::string> issued;
  for (auto* client : clients) {
    const auto reply = read(*client);
    ASSERT_EQ(reply.status(), 200);
    client->session = reply.session();
    EXPECT_EQ(!client->session.empty(), GetParam().sessions);
    if (GetParam().sessions) {
      EXPECT_TRUE(issued.insert(client->session).second);
    }
  }
  for (const char* path : {"/health", "/ready", "/info",
                           "/.well-known/oauth-protected-resource"}) {
    for (auto* client : clients) {
      send(*client, "POST", GetParam().endpoint, message("tools/list"),
           auth(*client) + session(*client));
    }
    for (auto* client : clients) {
      const auto reply = read(*client);
      EXPECT_EQ(reply.status(), 200);
      EXPECT_EQ(reply.session(), client->session);
      const auto body = json::JsonValue::parse(reply.body);
      EXPECT_EQ(body["result"]["principal"].getString(), client->principal);
      EXPECT_EQ(body["result"]["session"].getString(), client->session);
    }
    for (auto* client : clients) {
      send(*client, "GET", path);
    }
    for (auto* client : clients) {
      const auto reply = read(*client);
      EXPECT_EQ(reply.status(), 200);
      EXPECT_TRUE(reply.session().empty()) << client->principal << ": " << path;
    }
  }
}

TEST_P(HttpSessionResponseIsolationTest,
       LateReplyAfterDisconnectCannotReachAnotherClient) {
  auto& alice = connect("alice");
  auto& bob = connect("bob");
  initialize(alice);
  initialize(bob);
  send(alice, "POST", GetParam().endpoint, message("test/delayed"),
       auth(alice) + session(alice));
  ASSERT_TRUE(awaitPending(1));
  executeInDispatcher([&]() {
    alice.server->close(network::ConnectionCloseType::NoFlush);
    alice.server.reset();
    auto& pending = callbacks_.pending.front();
    ASSERT_NE(pending.stream, nullptr);
    EXPECT_TRUE(holds_alternative<Error>(
        pending.stream->sendResponse(pending.response)));
  });
  prime(bob);
  send(bob, "GET", "/health");
  EXPECT_TRUE(read(bob).session().empty());
}

TEST_P(HttpSessionResponseIsolationTest,
       CallbackConnectionCanServeAPublicReply) {
  auto& events = connect("alice");
  auto& posts = connect("alice");
  send(events, "GET", "/sse", "", auth(events));
  const auto opening = read(events, false, "data: callback/");
  ASSERT_EQ(opening.status(), 200);
  EXPECT_TRUE(opening.session().empty());
  const std::string marker = "data: ";
  const auto start = opening.body.find(marker) + marker.size();
  const auto end = opening.body.find_first_of("\r\n", start);
  const auto callback = "/" + opening.body.substr(start, end - start);
  send(posts, "POST", callback, message("initialize", 71), auth(posts));
  EXPECT_EQ(read(posts).status(), 202);
  read(events, false, "\"id\":71");

  send(posts, "GET", "/health");
  const auto health = read(posts);
  EXPECT_EQ(health.status(), 200);
  EXPECT_TRUE(health.session().empty());

  send(posts, "POST", callback, message("tools/list", 72), auth(posts));
  EXPECT_EQ(read(posts).status(), 202);
  const auto later = read(events, false, "\"id\":72");
  EXPECT_EQ(later.body.find("HTTP/1.1"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
    EndpointsAndRevisions,
    HttpSessionResponseIsolationTest,
    ::testing::Values(IsolationConfig{"/mcp", "2025-06-18", true},
                      IsolationConfig{"/mcp", "2025-11-25", true},
                      IsolationConfig{"/v1/mcp", "2025-06-18", true},
                      IsolationConfig{"/v1/mcp", "2025-11-25", true},
                      IsolationConfig{"/mcp", "2025-06-18", false},
                      IsolationConfig{"/mcp", "2025-11-25", false},
                      IsolationConfig{"/v1/mcp", "2025-06-18", false},
                      IsolationConfig{"/v1/mcp", "2025-11-25", false}));

}  // namespace
}  // namespace filter
}  // namespace mcp
