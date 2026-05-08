/**
 * @file test_http_sse_filter_client_sse_sm.cc
 * @brief Integration tests for ClientSseStateMachine wiring in the filter
 *
 * Verifies that the ClientSseStateMachine is correctly wired into
 * HttpSseJsonRpcProtocolFilter by testing the observable behavioral
 * effects of state transitions through real TCP connections.
 *
 * Since HttpSseJsonRpcProtocolFilter is internal to the .cc file, we
 * test through the filter's public interface: onWrite output on the
 * wire, sendHttpPost() calls on callbacks, and onError() propagation.
 *
 * Uses real TCP socketpairs and a real libevent dispatcher following
 * the test_sse_transport_round_trip.cc pattern.
 */

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
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

// Records sendHttpPost(), onError(), and onMessageEndpoint() calls so
// the test can verify the behavioral effects of state transitions.
class TestCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request&) override { request_count_++; }
  void onNotification(const jsonrpc::Notification&) override {}
  void onResponse(const jsonrpc::Response&) override { response_count_++; }
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error& error) override {
    last_error_ = error;
    error_count_++;
  }
  void onMessageEndpoint(const std::string& endpoint) override {
    message_endpoint_ = endpoint;
    endpoint_count_++;
  }
  bool sendHttpPost(const std::string& json_body) override {
    last_post_body_ = json_body;
    post_count_++;
    return true;
  }

  int request_count_{0};
  int response_count_{0};
  int error_count_{0};
  int post_count_{0};
  int endpoint_count_{0};
  std::string message_endpoint_;
  std::string last_post_body_;
  Error last_error_;
};

class ClientSseSmFilterTest : public test::RealIoTestBase {
 protected:
  struct Harness {
    std::shared_ptr<HttpSseFilterChainFactory> factory;
    std::unique_ptr<network::ClientConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  // Build a client-mode ConnectionImpl with the filter chain attached.
  // Must be called from within executeInDispatcher().
  Harness makeClientHarness(TestCallbacks& callbacks, bool use_sse = true) {
    auto factory =
        std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks,
                                                    /*is_server=*/false,
                                                    /*http_path=*/"/sse",
                                                    /*http_host=*/"localhost",
                                                    /*use_sse=*/use_sse);

    auto pair = createSocketPair();
    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto si = std::make_shared<stream_info::StreamInfoImpl>();

    auto conn = network::ConnectionImpl::createClientConnection(
        *dispatcher_, std::move(socket), std::move(transport), *si);

    auto* conn_impl = static_cast<network::ConnectionImpl*>(conn.get());
    EXPECT_TRUE(factory->createFilterChain(conn_impl->filterManager()))
        << "factory declined to build a filter chain";
    conn_impl->filterManager().initializeReadFilters();

    return Harness{std::move(factory), std::move(conn), std::move(pair.second),
                   std::move(si)};
  }

  // Read bytes from the peer socket (test thread, not dispatcher thread).
  // Polls with sleep — does NOT call dispatcher_->run().
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

  // Write raw HTTP bytes from the "server" side into the client
  // connection. Called from the test thread — the raw write into the
  // socket is thread-safe; the dispatcher will pick up the read event.
  void writeServerBytes(network::IoHandle& peer, const std::string& data) {
    OwnedBuffer buf;
    buf.add(data);
    auto r = peer.write(buf);
    ASSERT_TRUE(r.ok()) << "peer.write failed: errno=" << errno;
  }

  // Clean shutdown on the dispatcher thread to satisfy destructor asserts.
  void closeOnDispatcher(std::unique_ptr<network::ClientConnection> conn,
                         std::shared_ptr<HttpSseFilterChainFactory> factory) {
    executeInDispatcher([&]() {
      if (conn) {
        conn->close(network::ConnectionCloseType::NoFlush);
      }
      conn.reset();
      factory.reset();
    });
  }
};

// ---------------------------------------------------------------------------
// onNewConnection wiring
// ---------------------------------------------------------------------------

TEST_F(ClientSseSmFilterTest, OnNewConnection_SseMode_Negotiating) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/true);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // Write a JSON-RPC message. In SSE mode the filter should intercept
    // this: send the GET /sse request first and queue the message.
    OwnedBuffer json_buf;
    json_buf.add(R"({"jsonrpc":"2.0","method":"test","id":1})");
    conn->write(json_buf, false);
  });

  // The first bytes on the wire should be a GET /sse request, confirming
  // the state machine entered the negotiation path.
  std::string wire = drainPeer(*peer, 500ms);
  EXPECT_NE(wire.find("GET /sse"), std::string::npos)
      << "Expected GET /sse in: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(ClientSseSmFilterTest, OnNewConnection_StreamableHttp_NotNegotiating) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // In Streamable HTTP mode, writing should produce an HTTP POST, not
    // a GET /sse.
    OwnedBuffer json_buf;
    json_buf.add(R"({"jsonrpc":"2.0","method":"test","id":1})");
    conn->write(json_buf, false);
  });

  std::string wire = drainPeer(*peer, 500ms);
  // Should NOT contain GET /sse
  EXPECT_EQ(wire.find("GET /sse"), std::string::npos)
      << "Streamable HTTP should not send GET /sse, got: " << wire;
  // Should contain POST
  EXPECT_NE(wire.find("POST"), std::string::npos)
      << "Expected HTTP POST in: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(ClientSseSmFilterTest, OnNewConnection_ServerMode_NoClientSm) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    factory =
        std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks,
                                                    /*is_server=*/true,
                                                    /*http_path=*/"/rpc",
                                                    /*http_host=*/"localhost",
                                                    /*use_sse=*/true);

    auto pair = createSocketPair();
    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto si = std::make_shared<stream_info::StreamInfoImpl>();

    conn = network::ConnectionImpl::createServerConnection(
        *dispatcher_, std::move(socket), std::move(transport), *si);
    peer = std::move(pair.second);
    auto* ci = static_cast<network::ConnectionImpl*>(conn.get());
    EXPECT_TRUE(factory->createFilterChain(ci->filterManager()));
    ci->filterManager().initializeReadFilters();
  });

  // If we get here without crash, the server filter was created
  // without a client state machine (no SSE negotiation path).
  executeInDispatcher([&]() {
    conn->close(network::ConnectionCloseType::NoFlush);
    conn.reset();
    factory.reset();
  });
  SUCCEED();
}

// ---------------------------------------------------------------------------
// onWrite wiring: first write sends GET, second write queues
// ---------------------------------------------------------------------------

TEST_F(ClientSseSmFilterTest, OnWrite_FirstWrite_SendsGetRequest) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/true);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    OwnedBuffer json_buf;
    json_buf.add(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    conn->write(json_buf, false);
  });

  std::string wire = drainPeer(*peer, 500ms);
  EXPECT_NE(wire.find("GET /sse"), std::string::npos)
      << "Expected GET /sse in: " << wire;
  // The JSON-RPC message should NOT be on the wire yet (queued).
  EXPECT_EQ(wire.find("initialize"), std::string::npos)
      << "JSON-RPC message should be queued, not on wire: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(ClientSseSmFilterTest, OnWrite_SecondWrite_QueuesMessage) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/true);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // First write: triggers GET /sse
    OwnedBuffer msg1;
    msg1.add(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    conn->write(msg1, false);
  });

  // Drain the GET request
  drainPeer(*peer, 500ms);

  // Second write from dispatcher thread
  executeInDispatcher([&]() {
    OwnedBuffer msg2;
    msg2.add(R"({"jsonrpc":"2.0","method":"tools/list","id":2})");
    conn->write(msg2, false);
  });

  // Try to read more — queued message should NOT appear on wire
  std::string extra = drainPeer(*peer, 300ms);
  EXPECT_TRUE(extra.empty() || extra.find("tools/list") == std::string::npos)
      << "Message should be queued, not sent: " << extra;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ---------------------------------------------------------------------------
// onEvent("endpoint") wiring + POST routing
// ---------------------------------------------------------------------------

TEST_F(ClientSseSmFilterTest, OnEndpointEvent_EnablesPostRouting) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/true);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // Queue a message (triggers GET + queues the JSON-RPC body)
    OwnedBuffer msg1;
    msg1.add(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    conn->write(msg1, false);
  });

  // Drain the GET request from the wire
  drainPeer(*peer, 500ms);

  // Simulate the server's SSE response with an endpoint event.
  writeServerBytes(*peer,
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Cache-Control: no-cache\r\n"
                   "\r\n"
                   "event: endpoint\n"
                   "data: /callback/test123\n\n");

  // Give the dispatcher time to process the read and fire callbacks.
  std::this_thread::sleep_for(300ms);

  // The filter should have called onMessageEndpoint when it received
  // the "endpoint" SSE event.
  EXPECT_EQ(callbacks.endpoint_count_, 1);
  EXPECT_EQ(callbacks.message_endpoint_, "/callback/test123");

  // The queued message should have been flushed via sendHttpPost().
  EXPECT_GE(callbacks.post_count_, 1)
      << "Queued message should have been sent via POST";

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ---------------------------------------------------------------------------
// onHeaders with SSE Content-Type
// ---------------------------------------------------------------------------

TEST_F(ClientSseSmFilterTest, OnHeaders_SseContentType_TransitionsToActive) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/true);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    OwnedBuffer msg;
    msg.add(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    conn->write(msg, false);
  });

  drainPeer(*peer, 500ms);

  // Simulate SSE response with endpoint + a JSON-RPC message event.
  // The Content-Type: text/event-stream triggers StreamStarted on
  // the state machine, transitioning it to Active.
  writeServerBytes(*peer,
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Cache-Control: no-cache\r\n"
                   "\r\n"
                   "event: endpoint\n"
                   "data: /callback/test456\n\n"
                   "event: message\n"
                   "data: {\"jsonrpc\":\"2.0\",\"result\":{},\"id\":1}\n\n");

  std::this_thread::sleep_for(300ms);

  EXPECT_EQ(callbacks.endpoint_count_, 1);
  // JSON-RPC response should have been parsed from the SSE stream.
  // This only works if the filter reached the Active state and
  // routed the SSE body through the SSE codec and JSON-RPC filter.
  EXPECT_GE(callbacks.response_count_, 1)
      << "JSON-RPC response should have been parsed from SSE stream";

  closeOnDispatcher(std::move(conn), std::move(factory));
}

TEST_F(ClientSseSmFilterTest, OnHeaders_NonSseContentType_NoActiveTransition) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/false);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    OwnedBuffer msg;
    msg.add(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    conn->write(msg, false);
  });

  drainPeer(*peer, 500ms);

  // Simulate a normal JSON-RPC response (not SSE)
  writeServerBytes(*peer,
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: 39\r\n"
                   "\r\n"
                   "{\"jsonrpc\":\"2.0\",\"result\":{},\"id\":1}");

  std::this_thread::sleep_for(300ms);

  EXPECT_GE(callbacks.response_count_, 1)
      << "Streamable HTTP response should be parsed as JSON-RPC";
  // No endpoint negotiation should have occurred
  EXPECT_EQ(callbacks.endpoint_count_, 0);

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// ---------------------------------------------------------------------------
// Error propagation: connection close during negotiation
// ---------------------------------------------------------------------------

TEST_F(ClientSseSmFilterTest, ConnectionClose_DuringNegotiation_NoHang) {
  TestCallbacks callbacks;
  std::unique_ptr<network::ClientConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeClientHarness(callbacks, /*use_sse=*/true);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    OwnedBuffer msg;
    msg.add(R"({"jsonrpc":"2.0","method":"initialize","id":1})");
    conn->write(msg, false);
  });

  drainPeer(*peer, 500ms);

  // Close the peer to simulate a server disconnect during negotiation.
  peer.reset();
  std::this_thread::sleep_for(200ms);

  // The key assertion: no hang, no crash. The state machine handles
  // the connection close gracefully.
  closeOnDispatcher(std::move(conn), std::move(factory));
  SUCCEED();
}

}  // namespace
}  // namespace filter
}  // namespace mcp
