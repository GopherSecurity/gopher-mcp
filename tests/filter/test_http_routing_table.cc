/**
 * @file test_http_routing_table.cc
 * @brief Wire-level tests for the HTTP route table
 *
 * Drives a real server connection and asserts on the bytes that come
 * back, which is the only way to observe status codes, the Allow header,
 * and the fact that a request is answered at all rather than left to
 * hang until the client times out.
 *
 * Uses real TCP socketpairs following test_http_sse_filter_server_mode.cc.
 */

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/types.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

class RoutingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& req) override {
    requests_.push_back(req);
  }
  void onRequestWithContext(const jsonrpc::Request& req,
                            MessageDispatchContext& context) override {
    requests_.push_back(req);
    transport_sessions_.push_back(context.transportSessionId());
    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = req.id;
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
    context.sendResponse(response);
  }
  void onNotification(const jsonrpc::Notification&) override {}
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}
  void onMessageEndpoint(const std::string&) override {}
  bool sendHttpPost(const std::string&) override { return true; }

  std::vector<jsonrpc::Request> requests_;
  std::vector<std::string> transport_sessions_;
};

class HttpRoutingTableTest : public test::RealIoTestBase {
 protected:
  struct Harness {
    std::shared_ptr<HttpSseFilterChainFactory> factory;
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  // The route registration callback fires at the end of route setup, so
  // the captured filter sees the finished table.
  Harness makeServerHarness(RoutingCallbacks& callbacks,
                            const std::string& rpc_path = "/mcp",
                            const std::string& sse_path = "/sse",
                            bool allow_client_termination = true,
                            bool enable_get_stream = true) {
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, callbacks,
        /*is_server=*/true, rpc_path,
        /*http_host=*/"localhost",
        /*use_sse=*/true, sse_path, rpc_path);
    // What the endpoint serves is what the table admits, and what the
    // table admits is what Allow advertises.
    transport::StreamableHttpConfig config;
    config.allow_client_termination = allow_client_termination;
    config.enable_get_stream = enable_get_stream;
    factory->setSessionConfig(config);
    factory->setRouteRegistrationCallback(
        [this](HttpRoutingFilter* router) { router_ = router; });

    auto pair = createSocketPair();
    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto si = std::make_shared<stream_info::StreamInfoImpl>();

    auto conn = network::ConnectionImpl::createServerConnection(
        *dispatcher_, std::move(socket), std::move(transport), *si);
    auto* ci = static_cast<network::ConnectionImpl*>(conn.get());
    EXPECT_TRUE(factory->createFilterChain(ci->filterManager()));
    ci->filterManager().initializeReadFilters();

    return Harness{std::move(factory), std::move(conn), std::move(pair.second),
                   std::move(si)};
  }

  std::string drainPeer(network::IoHandle& peer,
                        std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = peer.read(buf, 4096);
      if (r.ok() && *r > 0) {
        out.append(buf.toString());
      } else if (!out.empty()) {
        return out;
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }

  void writeClientBytes(network::IoHandle& peer, const std::string& data) {
    OwnedBuffer buf;
    buf.add(data);
    auto r = peer.write(buf);
    ASSERT_TRUE(r.ok()) << "peer.write failed: errno=" << errno;
  }

  void closeOnDispatcher(std::unique_ptr<network::ServerConnection> conn,
                         std::shared_ptr<HttpSseFilterChainFactory> factory) {
    executeInDispatcher([&]() {
      if (conn) {
        conn->close(network::ConnectionCloseType::NoFlush);
      }
      conn.reset();
      factory.reset();
    });
  }

  static std::string request(const std::string& method,
                             const std::string& path,
                             const std::string& body = "",
                             const std::string& origin = "",
                             const std::string& extra_headers = "") {
    std::string out = method + " " + path +
                      " HTTP/1.1\r\n"
                      "Host: localhost\r\n";
    if (!origin.empty()) {
      out += "Origin: " + origin + "\r\n";
    }
    out += extra_headers;
    if (!body.empty()) {
      out += "Content-Type: application/json\r\n";
      out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    out += "\r\n";
    out += body;
    return out;
  }

  static std::string initializeBody() {
    return R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{)"
           R"("protocolVersion":"2025-06-18","capabilities":{},)"
           R"("clientInfo":{"name":"test","version":"1.0"}}})";
  }

  static std::string sessionIdOnTheWire(const std::string& wire) {
    const std::string name = "\r\nMcp-Session-Id: ";
    const size_t at = wire.find(name);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t start = at + name.size();
    const size_t end = wire.find("\r\n", start);
    return wire.substr(start, end - start);
  }

  static size_t countOccurrences(const std::string& haystack,
                                 const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
      ++count;
      pos += needle.size();
    }
    return count;
  }

  HttpRoutingFilter* router_{nullptr};
};

// A GET on the MCP endpoint has no handler yet, but it must still be
// answered here. Left to fall through it reaches a protocol layer with
// nothing to reply and the client waits for its own timeout.
TEST_F(HttpRoutingTableTest, GetMcpIsRejectedWithoutHanging) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    // The standalone event stream turned off, so GET on the endpoint is
    // a route that rejects — which is what this test is about.
    auto h = makeServerHarness(callbacks, "/mcp", "/sse",
                               /*allow_client_termination=*/true,
                               /*enable_get_stream=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("GET", "/mcp"));
  });

  const auto started = std::chrono::steady_clock::now();
  std::string wire = drainPeer(*peer, 2000ms);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_NE(wire.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos)
      << "Expected 405, got: " << wire;
  // Access-Control-Allow-Origin also contains "Allow", so match the
  // header name at the start of a line.
  EXPECT_NE(wire.find("\r\nAllow: DELETE, OPTIONS, POST\r\n"),
            std::string::npos)
      << "405 must advertise the methods the endpoint serves, got: " << wire;
  EXPECT_LT(elapsed, 2000ms) << "Response must not wait out the read budget";
  EXPECT_TRUE(callbacks.requests_.empty())
      << "A rejected request must not reach JSON-RPC dispatch";

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// Ending a session is served or refused according to configuration, and
// Allow follows suit — it is rendered from the table rather than written
// out anywhere, so it cannot advertise something that would not be served.
TEST_F(HttpRoutingTableTest, DeleteMcpIsRejectedWhenTerminationIsOff) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks, "/mcp", "/sse",
                               /*allow_client_termination=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("DELETE", "/mcp"));
  });

  std::string wire = drainPeer(*peer, 2000ms);
  EXPECT_NE(wire.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos)
      << "Expected 405, got: " << wire;
  EXPECT_NE(wire.find("\r\nAllow: GET, OPTIONS, POST\r\n"), std::string::npos)
      << "Expected Allow without DELETE, got: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// A rejection is answered from the headers, so any body has to be
// drained rather than left to be read as the next request.
TEST_F(HttpRoutingTableTest, RejectedRequestWithBodyLeavesConnectionUsable) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    // Termination off, so DELETE is a route that rejects — which is what
    // this test needs: a rejection answered from the headers alone.
    auto h = makeServerHarness(callbacks, "/mcp", "/sse",
                               /*allow_client_termination=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("DELETE", "/mcp", "{\"a\":1}"));
  });

  std::string wire = drainPeer(*peer, 2000ms);
  EXPECT_NE(wire.find("HTTP/1.1 405"), std::string::npos)
      << "Expected 405, got: " << wire;

  // The connection must still be framed for the next request.
  executeInDispatcher([&]() {
    writeClientBytes(*peer, request("POST", "/mcp", initializeBody()));
  });
  std::this_thread::sleep_for(200ms);

  ASSERT_FALSE(callbacks.requests_.empty())
      << "Request after a rejected one must still be dispatched";
  EXPECT_EQ(callbacks.requests_.back().method, "initialize");

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// The Allow header is derived from the table, so it must never name a
// method whose only route is a rejection.
TEST_F(HttpRoutingTableTest, RejectRoutesAreNeverAdvertised) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    // Both optional methods off, so the table actually holds rejections to
    // check the invariant against.
    auto h = makeServerHarness(callbacks, "/mcp", "/sse",
                               /*allow_client_termination=*/false,
                               /*enable_get_stream=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
  });

  ASSERT_NE(router_, nullptr) << "Route registration callback did not fire";

  size_t reject_routes = 0;
  for (const auto& entry : router_->routes()) {
    if (entry.second.kind != HttpRoutingFilter::RouteTarget::Kind::Reject) {
      continue;
    }
    if (entry.second.status_code != 405) {
      continue;
    }
    ++reject_routes;

    const size_t separator = entry.first.find(' ');
    ASSERT_NE(separator, std::string::npos) << entry.first;
    const std::string method = entry.first.substr(0, separator);
    const std::string path = entry.first.substr(separator + 1);

    const std::string allow = router_->allowedMethodsFor(path);
    EXPECT_FALSE(allow.empty())
        << "A 405 on " << path << " must still advertise something";
    EXPECT_EQ(allow.find(method), std::string::npos)
        << method << " answers 405 on " << path
        << " and must not appear in Allow: " << allow;
  }
  EXPECT_GT(reject_routes, 0u) << "Expected the MCP endpoint placeholders";

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(HttpRoutingTableTest, PostMcpPassesThroughToProtocol) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("POST", "/mcp", initializeBody()));
  });

  std::this_thread::sleep_for(200ms);

  ASSERT_FALSE(callbacks.requests_.empty());
  EXPECT_EQ(callbacks.requests_[0].method, "initialize");

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// The historic path keeps working after the endpoint default moved.
TEST_F(HttpRoutingTableTest, PostRpcAliasPassesThroughToProtocol) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("POST", "/rpc", initializeBody()));
  });

  std::this_thread::sleep_for(200ms);

  ASSERT_FALSE(callbacks.requests_.empty());
  EXPECT_EQ(callbacks.requests_[0].method, "initialize");

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(HttpRoutingTableTest, UnknownPathReturnsNotFound) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("GET", "/nope"));
  });

  std::string wire = drainPeer(*peer, 2000ms);
  EXPECT_NE(wire.find("HTTP/1.1 404"), std::string::npos)
      << "Expected 404, got: " << wire;
  EXPECT_NE(wire.find(R"({"error":"not_found"})"), std::string::npos)
      << "Expected the not_found body, got: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(HttpRoutingTableTest, LegacyEventStreamStillOpens) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("GET", "/sse"));
  });

  std::string wire = drainPeer(*peer, 2000ms);
  EXPECT_NE(wire.find("HTTP/1.1 200"), std::string::npos)
      << "Expected the SSE stream to open, got: " << wire;
  EXPECT_NE(wire.find("text/event-stream"), std::string::npos)
      << "Expected an SSE content type, got: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(HttpRoutingTableTest, StreamableGetRpcIsAnsweredOnlyOnce) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("POST", "/mcp", initializeBody()));
  });

  const std::string init = drainPeer(*peer, 2000ms);
  const std::string session_id = sessionIdOnTheWire(init);
  ASSERT_FALSE(session_id.empty()) << init;

  executeInDispatcher([&]() {
    writeClientBytes(*peer, request("GET", "/mcp", /*body=*/"", /*origin=*/"",
                                    "Accept: text/event-stream\r\n"
                                    "Mcp-Session-Id: " +
                                        session_id + "\r\n"));
  });

  const std::string wire = drainPeer(*peer, 2000ms);
  EXPECT_EQ(countOccurrences(wire, "HTTP/1.1 200 OK\r\n"), 1u) << wire;
  EXPECT_NE(wire.find("Content-Type: text/event-stream"), std::string::npos)
      << wire;
  EXPECT_EQ(wire.find("event: endpoint"), std::string::npos) << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(HttpRoutingTableTest, CallbackSessionDoesNotLeakToNextRequest) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer,
                     request("POST", "/callback/client_1", initializeBody()));
  });

  std::string callback_ack = drainPeer(*peer, 2000ms);
  EXPECT_NE(callback_ack.find("HTTP/1.1 202 Accepted"), std::string::npos)
      << callback_ack;
  ASSERT_EQ(callbacks.transport_sessions_.size(), 1u);
  EXPECT_EQ(callbacks.transport_sessions_[0], "client_1");

  executeInDispatcher([&]() {
    writeClientBytes(*peer, request("POST", "/rpc", initializeBody()));
  });

  std::string plain_response = drainPeer(*peer, 2000ms);
  EXPECT_NE(plain_response.find("HTTP/1.1 200 OK"), std::string::npos)
      << plain_response;
  ASSERT_EQ(callbacks.transport_sessions_.size(), 2u);
  EXPECT_TRUE(callbacks.transport_sessions_[1].empty())
      << "plain keep-alive request reused callback session "
      << callbacks.transport_sessions_[1];

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(HttpRoutingTableTest, HealthAndPreflightStillAnswered) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("GET", "/health"));
  });

  std::string health = drainPeer(*peer, 2000ms);
  EXPECT_NE(health.find("HTTP/1.1 200"), std::string::npos)
      << "Expected a health response, got: " << health;
  EXPECT_NE(health.find("\"status\":\"healthy\""), std::string::npos)
      << "Expected the health body, got: " << health;

  // A preflight only ever comes from a browser, and a browser always says
  // which page it is on.
  executeInDispatcher([&]() {
    writeClientBytes(*peer,
                     request("OPTIONS", "/mcp", "", "http://localhost:3000"));
  });

  std::string preflight = drainPeer(*peer, 2000ms);
  EXPECT_NE(preflight.find("HTTP/1.1 204"), std::string::npos)
      << "Expected a preflight response, got: " << preflight;
  EXPECT_NE(preflight.find("Access-Control-Allow-Methods"), std::string::npos)
      << "Expected CORS headers, got: " << preflight;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// The table is built from configuration, not from literal paths.
TEST_F(HttpRoutingTableTest, ConfiguredEndpointPathIsHonored) {
  RoutingCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeServerHarness(callbacks, "/api/mcp", "/sse",
                               /*allow_client_termination=*/true,
                               /*enable_get_stream=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);
    writeClientBytes(*peer, request("GET", "/api/mcp"));
  });

  std::string wire = drainPeer(*peer, 2000ms);
  EXPECT_NE(wire.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos)
      << "Expected 405 on the configured endpoint, got: " << wire;
  EXPECT_NE(wire.find("\r\nAllow: DELETE, OPTIONS, POST\r\n"),
            std::string::npos)
      << "Expected Allow on the configured endpoint, got: " << wire;

  executeInDispatcher([&]() {
    writeClientBytes(*peer, request("POST", "/api/mcp", initializeBody()));
  });
  std::this_thread::sleep_for(200ms);

  ASSERT_FALSE(callbacks.requests_.empty())
      << "POST on the configured endpoint must be dispatched";
  EXPECT_EQ(callbacks.requests_[0].method, "initialize");

  closeOnDispatcher(std::move(conn), std::move(factory));
}

}  // namespace
}  // namespace filter
}  // namespace mcp
