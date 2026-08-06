/**
 * @file test_http_security.cc
 * @brief What a browser and a non-browser client actually read back
 *
 * The policy's own tests say what it decides; these say that the decision
 * reaches the wire on every route, new and legacy, and that a refused
 * request is refused before anything runs. What a served request records
 * about its caller is asserted where it is recorded, in the endpoint
 * filter's own tests — nothing on the wire carries a principal.
 *
 * Real TCP socketpairs, following test_streamable_http_post.cc.
 */

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_security_filter.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/json/json_bridge.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/types.h"

#include "real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

/** Answers every request, so a served POST has something to carry back. */
class EchoingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests.push_back(request);
  }

  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    requests.push_back(request);

    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
    context.sendResponse(response);
  }

  void onNotification(const jsonrpc::Notification& notification) override {
    notifications.push_back(notification);
  }

  void onNotificationWithContext(const jsonrpc::Notification& notification,
                                 MessageDispatchContext&) override {
    notifications.push_back(notification);
  }

  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  std::vector<jsonrpc::Request> requests;
  std::vector<jsonrpc::Notification> notifications;
};

class HttpSecurityTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    executeInDispatcher([&]() {
      if (conn_) {
        conn_->close(network::ConnectionCloseType::NoFlush);
      }
      conn_.reset();
      factory_.reset();
    });
    peer_.reset();
    test::RealIoTestBase::TearDown();
  }

  /** Build a server whose security settings the caller gets to shape. */
  void startServer(const std::function<void(HttpSseFilterChainFactory&)>&
                       configure = nullptr) {
    executeInDispatcher([&]() {
      factory_ =
          std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks_,
                                                      /*is_server=*/true,
                                                      /*http_path=*/"/mcp",
                                                      /*http_host=*/"localhost",
                                                      /*use_sse=*/true,
                                                      /*sse_path=*/"/sse",
                                                      /*rpc_path=*/"/mcp");
      if (configure) {
        configure(*factory_);
      }

      auto pair = createSocketPair();
      auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      auto transport = std::make_unique<network::RawBufferTransportSocket>();
      stream_info_ = std::make_shared<stream_info::StreamInfoImpl>();

      conn_ = network::ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket), std::move(transport), *stream_info_);
      auto* impl = static_cast<network::ConnectionImpl*>(conn_.get());
      ASSERT_TRUE(factory_->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();

      peer_ = std::move(pair.second);
    });
  }

  void send(const std::string& request) {
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(request);
      auto result = peer_->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
  }

  void sendPost(const std::string& body, const std::string& origin) {
    send(
        "POST /mcp HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n" +
        originHeader(origin) +
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
  }

  void sendBodiless(const std::string& method,
                    const std::string& path,
                    const std::string& origin) {
    send(method + " " + path +
         " HTTP/1.1\r\n"
         "Host: localhost\r\n" +
         originHeader(origin) + "\r\n");
  }

  static std::string originHeader(const std::string& origin) {
    return origin.empty() ? std::string() : "Origin: " + origin + "\r\n";
  }

  std::string readResponse(std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = peer_->read(buffer, 4096);
      if (result.ok() && *result > 0) {
        out.append(buffer.toString());
      } else if (!out.empty()) {
        return out;
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }

  EchoingCallbacks callbacks_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::unique_ptr<network::ServerConnection> conn_;
  network::IoHandlePtr peer_;
  std::shared_ptr<stream_info::StreamInfoImpl> stream_info_;
};

const char kInitialize[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";

// ── The endpoint ───────────────────────────────────────────────────────────

TEST_F(HttpSecurityTest, ACallerWithNoOriginGetsAPlainAnswer) {
  startServer();
  sendPost(kInitialize, "");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 200 OK\r\n"), 0u) << response;
  // No browser asked, so there is nothing to tell one.
  EXPECT_EQ(response.find("Access-Control-"), std::string::npos) << response;
  EXPECT_EQ(response.find("Vary:"), std::string::npos) << response;
  EXPECT_EQ(callbacks_.requests.size(), 1u);
}

TEST_F(HttpSecurityTest, ALocalPageIsAnsweredWithItsOwnOriginBack) {
  startServer();
  sendPost(kInitialize, "http://localhost:3000");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 200 OK\r\n"), 0u) << response;
  EXPECT_NE(response.find("Access-Control-Allow-Origin: http://localhost:3000"),
            std::string::npos)
      << response;
  EXPECT_NE(response.find("Vary: Origin"), std::string::npos) << response;
  EXPECT_NE(response.find("Access-Control-Expose-Headers: Mcp-Session-Id"),
            std::string::npos)
      << response;
  EXPECT_EQ(callbacks_.requests.size(), 1u);
}

TEST_F(HttpSecurityTest, APageFromElsewhereIsRefusedBeforeItRuns) {
  startServer();
  sendPost(kInitialize, "http://evil.example");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 403 Forbidden\r\n"), 0u) << response;
  EXPECT_NE(response.find("\"id\":null"), std::string::npos) << response;
  EXPECT_TRUE(callbacks_.requests.empty())
      << "a page this server does not serve must not run a message through it";
  // Answering with the origin would tell the browser it had been allowed.
  EXPECT_EQ(response.find("Access-Control-Allow-Origin"), std::string::npos)
      << response;
}

TEST_F(HttpSecurityTest, AConfiguredListIsWhatDecides) {
  startServer([](HttpSseFilterChainFactory& factory) {
    transport::StreamableHttpConfig config;
    config.allowed_origins = {"https://app.example.com"};
    factory.setSecurityConfig(config);
  });

  sendPost(kInitialize, "http://localhost:3000");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 403 Forbidden\r\n"), 0u)
      << "naming an origin narrows the server to it: " << response;
  EXPECT_TRUE(callbacks_.requests.empty());
}

// ── The auth hook ──────────────────────────────────────────────────────────

TEST_F(HttpSecurityTest, ADeniedCallerGetsTheStatusTheHookChose) {
  startServer([](HttpSseFilterChainFactory& factory) {
    factory.setAuthCallback([](const RequestHeadersView& headers) {
      return headers.get("Authorization") == "letmein"
                 ? AuthResult::allow("alice")
                 : AuthResult::deny(401, "token required");
    });
  });

  sendPost(kInitialize, "http://localhost:3000");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 401 Unauthorized\r\n"), 0u) << response;
  EXPECT_NE(response.find("token required"), std::string::npos) << response;
  EXPECT_TRUE(callbacks_.requests.empty());
  // The origin was fine, so the browser can read why it was turned away.
  EXPECT_NE(response.find("Access-Control-Allow-Origin: http://localhost:3000"),
            std::string::npos)
      << response;
}

TEST_F(HttpSecurityTest, AnAllowedCallerIsServedNormally) {
  startServer([](HttpSseFilterChainFactory& factory) {
    factory.setAuthCallback([](const RequestHeadersView& headers) {
      return headers.get("Authorization") == "letmein"
                 ? AuthResult::allow("alice")
                 : AuthResult::deny(401, "token required");
    });
  });

  send(
      "POST /mcp HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Type: application/json\r\n"
      "Authorization: letmein\r\n"
      "Content-Length: " +
      std::to_string(sizeof(kInitialize) - 1) + "\r\n\r\n" + kInitialize);

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 200 OK\r\n"), 0u) << response;
  EXPECT_EQ(callbacks_.requests.size(), 1u);
}

// ── Preflight ──────────────────────────────────────────────────────────────

TEST_F(HttpSecurityTest, PreflightAdvertisesEverythingTheTransportUses) {
  startServer();
  sendBodiless("OPTIONS", "/mcp", "http://localhost:3000");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 204 No Content\r\n"), 0u) << response;
  EXPECT_NE(response.find("Access-Control-Allow-Methods: POST, GET, DELETE, "
                          "OPTIONS"),
            std::string::npos)
      << "a browser that cannot preflight DELETE cannot end its own session: "
      << response;
  EXPECT_NE(response.find("Access-Control-Expose-Headers: Mcp-Session-Id"),
            std::string::npos)
      << response;
  EXPECT_NE(response.find("Last-Event-ID"), std::string::npos) << response;
  EXPECT_NE(response.find("Vary: Origin"), std::string::npos) << response;
}

TEST_F(HttpSecurityTest, PreflightNamesTheHeadersRegisteredToolsDesignate) {
  // Stands in for a tool registry: the same derivation the server wires,
  // over a list a test can grow between requests.
  auto tools = std::make_shared<std::vector<Tool>>();
  startServer([tools](HttpSseFilterChainFactory& factory) {
    factory.setExtraAllowedHeaders([tools]() {
      std::vector<std::string> names;
      for (const auto& tool : *tools) {
        for (const auto& name : HttpSecurityPolicy::paramHeadersFor(tool)) {
          names.push_back(name);
        }
      }
      return names;
    });
  });

  sendBodiless("OPTIONS", "/mcp", "http://localhost:3000");
  EXPECT_EQ(readResponse().find("Mcp-Param-region"), std::string::npos)
      << "nothing designates it yet";

  Tool search("search");
  search.inputSchema = mcp::make_optional(json::JsonValue::parse(
      R"({"properties":{"region":{"type":"string","x-mcp-header":true}}})"));
  tools->push_back(search);

  // A tool can be registered at any point in a server's life, so the
  // advertised set has to follow rather than be fixed at startup.
  sendBodiless("OPTIONS", "/mcp", "http://localhost:3000");
  const std::string after = readResponse();
  EXPECT_NE(after.find("Mcp-Param-region"), std::string::npos) << after;
}

// ── The rest of the routes ─────────────────────────────────────────────────

TEST_F(HttpSecurityTest, ThePlainHttpRoutesAreJudgedToo) {
  startServer();

  sendBodiless("GET", "/health", "http://evil.example");
  EXPECT_EQ(readResponse().find("HTTP/1.1 403 Forbidden\r\n"), 0u)
      << "every route is reachable from a browser, so every route is judged";
}

TEST_F(HttpSecurityTest, ThePlainHttpRoutesReflectTheOriginToo) {
  startServer();

  sendBodiless("GET", "/health", "http://localhost:3000");
  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 200"), 0u) << response;
  EXPECT_NE(response.find("Access-Control-Allow-Origin: http://localhost:3000"),
            std::string::npos)
      << response;
  EXPECT_NE(response.find("Vary: Origin"), std::string::npos) << response;
}

TEST_F(HttpSecurityTest, TheOlderEventStreamIsJudgedToo) {
  startServer();

  sendBodiless("GET", "/sse", "http://evil.example");
  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 403 Forbidden\r\n"), 0u)
      << "the older transport is as reachable from a browser as the new one: "
      << response;
  EXPECT_EQ(response.find("text/event-stream"), std::string::npos)
      << "and must not have opened a stream: " << response;
}

TEST_F(HttpSecurityTest, TheOlderEventStreamStillOpensForALocalPage) {
  startServer();

  sendBodiless("GET", "/sse", "http://localhost:3000");
  const std::string response = readResponse();

  EXPECT_NE(response.find("text/event-stream"), std::string::npos) << response;
  EXPECT_NE(response.find("Access-Control-Allow-Origin: http://localhost:3000"),
            std::string::npos)
      << response;
}

}  // namespace
}  // namespace filter
}  // namespace mcp
