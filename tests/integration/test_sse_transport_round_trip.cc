/**
 * Real-IO integration test for the SSE server transport.
 *
 * Verifies the full round-trip promised in PR #215's test plan:
 *
 *   1. GET on the configured SSE path returns an HTTP 200 stream that
 *      opens with an `event: endpoint` frame carrying a /callback/{id}
 *      URL the client can POST to.
 *   2. POST on that /callback/{id} returns 202 Accepted, and a JSON-RPC
 *      response produced on the POST connection is rerouted through the
 *      SseSessionRegistry onto the original SSE stream (instead of being
 *      framed as HTTP bytes on the POST socket).
 *
 * Covered wire paths:
 *   - HttpSseFilterChainFactory server-mode chain construction
 *   - HttpSseJsonRpcProtocolFilter's `onHeaders` handshake for both
 *     GET /sse (writes HTTP prelude + endpoint event inline via
 *     connection().write()) and POST /callback/{id} (writes 202 inline
 *     and tags the filter instance with sse_callback_session_id_)
 *   - HttpSseJsonRpcProtocolFilter's `onWrite` interception that pulls
 *     JSON-RPC bytes off the POST connection's write chain and hands
 *     them to SseSessionRegistry::sendResponse on the matching SSE
 *     connection
 *
 * Design:
 *   - No McpServer bootstrap; two ConnectionImpls over real TCP
 *     socketpairs sharing a single HttpSseFilterChainFactory is the
 *     smallest harness that exercises the real routing path (not a mock)
 *     while keeping the test self-contained. The shared factory is the
 *     key: it owns the SseSessionRegistry so the POST-connection's
 *     filter instance can find the SSE-connection's filter instance.
 *   - Dispatcher-thread invariant: every mutation of the connection and
 *     filter chain runs inside executeInDispatcher() because
 *     ConnectionImpl's lifecycle methods assert isThreadSafe().
 *   - Cleanup closes both connections and drops the factory on the
 *     dispatcher thread — tearing down ConnectionImpl from the test
 *     thread would trip its destructor assert, and the factory's
 *     transitively-owned SSE filters call registry.removeSession() from
 *     their destructors which also asserts dispatcher-thread.
 */

#include <chrono>
#include <regex>
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
namespace integration {
namespace {

using namespace std::chrono_literals;

// Minimal McpProtocolCallbacks that just records what it saw. The SSE
// handshake path never reaches these methods (onHeaders writes the
// endpoint event before any JSON-RPC parsing happens), so they're here
// only to satisfy the interface.
class RecordingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests_.push_back(request);
  }
  void onNotification(const jsonrpc::Notification& n) override {
    notifications_.push_back(n);
  }
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  std::vector<jsonrpc::Request> requests_;
  std::vector<jsonrpc::Notification> notifications_;
};

class SseTransportRoundTripTest : public test::RealIoTestBase {
 protected:
  // Build a server-side ConnectionImpl wrapped around one end of a real
  // TCP socketpair, with the factory's full HTTP+SSE filter chain
  // attached. The test holds the peer IoHandle so it can simulate a raw
  // HTTP client: write GET bytes in, read the server response out.
  struct Harness {
    std::shared_ptr<filter::HttpSseFilterChainFactory> factory;
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  Harness makeHarness(McpProtocolCallbacks& callbacks,
                      const std::string& sse_path = "/sse",
                      const std::string& rpc_path = "/mcp",
                      const std::string& external_url = "") {
    auto factory = std::make_shared<filter::HttpSseFilterChainFactory>(
        *dispatcher_, callbacks,
        /*is_server=*/true,
        /*http_path=*/rpc_path,
        /*http_host=*/"localhost",
        /*use_sse=*/true,
        /*sse_path=*/sse_path,
        /*rpc_path=*/rpc_path,
        /*external_url=*/external_url);

    auto inner = makeConnectionForFactory(factory);
    return Harness{std::move(factory), std::move(inner.conn),
                   std::move(inner.peer), std::move(inner.stream_info)};
  }

  // Lightweight shape for attaching additional connections to an existing
  // factory. The POST /callback leg of the round-trip needs two live
  // server connections sharing the same factory — both so their filter
  // instances share the factory's SseSessionRegistry, and so tearing the
  // factory down cleans up both filter chains at once.
  struct ExtraConnection {
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  ExtraConnection makeConnectionForFactory(
      const std::shared_ptr<filter::HttpSseFilterChainFactory>& factory) {
    auto pair = createSocketPair();
    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto stream_info = std::make_shared<stream_info::StreamInfoImpl>();

    auto conn = network::ConnectionImpl::createServerConnection(
        *dispatcher_, std::move(socket), std::move(transport), *stream_info);

    auto* conn_impl = static_cast<network::ConnectionImpl*>(conn.get());
    // createFilterChain() returning false would mean the factory couldn't
    // assemble the HTTP+SSE chain at all — nothing downstream is meaningful
    // if that happens, so fail loudly right here.
    EXPECT_TRUE(factory->createFilterChain(conn_impl->filterManager()))
        << "factory declined to build a filter chain";
    conn_impl->filterManager().initializeReadFilters();

    return ExtraConnection{std::move(conn), std::move(pair.second),
                           std::move(stream_info)};
  }

  // Simulate the HTTP client: push bytes onto the peer IoHandle so they
  // arrive on the server connection's read path.
  void writeClientBytes(network::IoHandle& peer, const std::string& data) {
    OwnedBuffer buf;
    buf.add(data);
    auto r = peer.write(buf);
    ASSERT_TRUE(r.ok()) << "peer.write failed: errno=" << errno;
  }

  // Read whatever the server has written back onto the peer socket,
  // polling up to `budget` milliseconds. Returns as soon as we've got
  // something and nothing more is buffered — the loopback pair is
  // effectively instant once the dispatcher pumps the write event.
  std::string drainPeer(network::IoHandle& peer,
                        std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = peer.read(buf, /*max_length=*/4096);
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

  // Tear down the connection and factory on the dispatcher thread.
  // ConnectionImpl's destructor asserts isThreadSafe(), and the factory's
  // shared filter vector transitively owns the SSE-codec filter whose
  // destructor calls SseSessionRegistry::removeSession — that assert also
  // fires if it runs off the dispatcher thread.
  void closeOnDispatcher(
      std::unique_ptr<network::ServerConnection> conn,
      std::shared_ptr<filter::HttpSseFilterChainFactory> factory) {
    executeInDispatcher([&]() {
      conn->close(network::ConnectionCloseType::NoFlush);
      conn.reset();
      factory.reset();
    });
  }
};

// The SSE handshake: client GETs the configured SSE path, server writes
// HTTP 200 + `event: endpoint\ndata: <callback_url>\n\n`. Verifies both
// the HTTP status line and the event framing, and that the callback URL
// is shaped the way PR #215 promised (has `/callback/` plus a non-empty
// session ID).
TEST_F(SseTransportRoundTripTest, SseGetAnnouncesCallbackEndpoint) {
  RecordingCallbacks callbacks;

  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<filter::HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeHarness(callbacks);
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // Push the raw GET request onto the peer socket. The server
    // dispatcher will see a read event and drive it through the filter
    // chain; onHeaders writes the handshake bytes back.
    writeClientBytes(
        *peer,
        "GET /sse HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept: text/event-stream\r\n"
        "\r\n");
  });

  const std::string wire = drainPeer(*peer);

  // HTTP status line: the filter writes a 200 OK prelude before the
  // endpoint event. If this fails, the handshake didn't run at all.
  EXPECT_NE(wire.find("HTTP/1.1 200"), std::string::npos)
      << "expected HTTP 200 status line, got: " << wire;
  EXPECT_NE(wire.find("Content-Type: text/event-stream"), std::string::npos)
      << "expected SSE content-type header, got: " << wire;

  // Endpoint frame: `event: endpoint\ndata: <url>\n\n`. Parse the URL
  // out of it rather than hard-coding — the session ID is generated by
  // SseSessionRegistry so we can only check its shape.
  std::smatch m;
  std::regex endpoint_re(R"(event:\s*endpoint\s*\ndata:\s*([^\r\n]+))");
  ASSERT_TRUE(std::regex_search(wire, m, endpoint_re))
      << "no endpoint event in handshake bytes: " << wire;

  // No external_url set → factory advertises a relative URL of the
  // form `callback/<id>`. With external_url set, it's absolute and
  // contains `/callback/<id>`. Accept either shape.
  const std::string callback_url = m[1].str();
  EXPECT_NE(callback_url.find("callback/"), std::string::npos)
      << "endpoint URL should contain callback/, got: " << callback_url;

  // Session ID is the tail after the final "callback/" — should be
  // non-empty regardless of whether the URL is relative or absolute.
  const std::string callback_marker = "callback/";
  auto cb_pos = callback_url.rfind(callback_marker);
  ASSERT_NE(cb_pos, std::string::npos);
  const std::string session_id =
      callback_url.substr(cb_pos + callback_marker.size());
  EXPECT_FALSE(session_id.empty())
      << "session ID in callback URL is empty: " << callback_url;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// When `external_url` is configured (reverse-proxy deployment), the
// callback URL advertised in the endpoint event should use that base
// instead of being derived from the Host header. This exercises the
// McpServerConfig → factory wiring landed in PR #215.
TEST_F(SseTransportRoundTripTest, ExternalUrlIsAdvertisedInEndpointEvent) {
  RecordingCallbacks callbacks;

  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<filter::HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeHarness(callbacks, /*sse_path=*/"/sse", /*rpc_path=*/"/mcp",
                         /*external_url=*/"https://proxy.example.com/mcp");
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    // Host header intentionally set to something different from the
    // external URL — if the factory fell back to Host-derived URLs,
    // we'd see "localhost" in the callback URL instead of the proxy.
    writeClientBytes(
        *peer,
        "GET /sse HTTP/1.1\r\n"
        "Host: internal-host:8080\r\n"
        "\r\n");
  });

  const std::string wire = drainPeer(*peer);

  std::smatch m;
  std::regex endpoint_re(R"(event:\s*endpoint\s*\ndata:\s*([^\r\n]+))");
  ASSERT_TRUE(std::regex_search(wire, m, endpoint_re))
      << "no endpoint event: " << wire;

  const std::string callback_url = m[1].str();
  EXPECT_NE(callback_url.find("proxy.example.com"), std::string::npos)
      << "external_url should be in advertised callback URL, got: "
      << callback_url;
  EXPECT_EQ(callback_url.find("internal-host"), std::string::npos)
      << "Host header leaked into callback URL instead of external_url: "
      << callback_url;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// Configuring a non-default SSE path (e.g. /events for a legacy client)
// should change only where the GET is accepted, not the /callback/{id}
// shape of the announced endpoint. Guards against a regression where
// the handshake path got hardcoded alongside the config field.
TEST_F(SseTransportRoundTripTest, ConfiguredSsePathIsHonored) {
  RecordingCallbacks callbacks;

  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<filter::HttpSseFilterChainFactory> factory;

  executeInDispatcher([&]() {
    auto h = makeHarness(callbacks, /*sse_path=*/"/events", /*rpc_path=*/"/rpc",
                         /*external_url=*/"");
    conn = std::move(h.conn);
    peer = std::move(h.peer);
    factory = std::move(h.factory);

    writeClientBytes(
        *peer,
        "GET /events HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
  });

  const std::string wire = drainPeer(*peer);
  EXPECT_NE(wire.find("HTTP/1.1 200"), std::string::npos)
      << "configured SSE path /events did not return 200, got: " << wire;
  EXPECT_NE(wire.find("event: endpoint"), std::string::npos)
      << "no endpoint event on configured /events path, got: " << wire;

  closeOnDispatcher(std::move(conn), std::move(factory));
}

// Full round-trip: GET /sse registers a session, POST /callback/{id}
// gets 202 Accepted, and a JSON-RPC response produced on the POST
// connection is rerouted through the SseSessionRegistry onto the SSE
// stream. This is the contract PR #215's test plan called out as
// pending — if the onWrite interception regresses, the response will
// either leak back onto the POST connection as HTTP bytes or disappear
// entirely.
TEST_F(SseTransportRoundTripTest, PostCallbackRoutesResponseThroughSseStream) {
  // Test-only callbacks that synthesize a server response the moment a
  // JSON-RPC request is parsed off the POST /callback body. The real
  // McpServer emits responses via the JSON-RPC filter's encoder; a raw
  // connection().write() reaches the same write chain and therefore
  // the same HttpSseJsonRpcProtocolFilter::onWrite that does the
  // rerouting. The write must happen on the dispatcher thread (ConnImpl
  // asserts it), which is already the case — filter.onRequest fires
  // from inside the dispatcher's read path.
  class EchoingCallbacks : public McpProtocolCallbacks {
   public:
    void onRequest(const jsonrpc::Request& request) override {
      ++requests_seen;
      if (callback_conn) {
        OwnedBuffer b;
        // A minimal JSON-RPC response; the exact shape doesn't matter,
        // only that it's recognizable on the SSE peer.
        std::string resp =
            R"({"jsonrpc":"2.0","id":)" +
            (holds_alternative<int64_t>(request.id)
                 ? std::to_string(get<int64_t>(request.id))
                 : std::string("\"") + get<std::string>(request.id) + "\"") +
            R"(,"result":{"echoed":true}})" + "\n";
        b.add(resp);
        callback_conn->write(b, false);
      }
    }
    void onNotification(const jsonrpc::Notification&) override {}
    void onResponse(const jsonrpc::Response&) override {}
    void onConnectionEvent(network::ConnectionEvent) override {}
    void onError(const Error&) override {}

    network::Connection* callback_conn = nullptr;
    std::atomic<int> requests_seen{0};
  } callbacks;

  std::shared_ptr<filter::HttpSseFilterChainFactory> factory;
  std::unique_ptr<network::ServerConnection> sse_conn;
  network::IoHandlePtr sse_peer;
  std::unique_ptr<network::ServerConnection> cb_conn;
  network::IoHandlePtr cb_peer;

  // Bring up the SSE stream connection first so the session id the
  // factory generates is registered before any POST can land.
  executeInDispatcher([&]() {
    auto h = makeHarness(callbacks);
    factory = std::move(h.factory);
    sse_conn = std::move(h.conn);
    sse_peer = std::move(h.peer);

    writeClientBytes(*sse_peer,
                     "GET /sse HTTP/1.1\r\n"
                     "Host: localhost\r\n"
                     "\r\n");
  });

  const std::string handshake = drainPeer(*sse_peer);
  std::smatch m;
  std::regex endpoint_re(R"(event:\s*endpoint\s*\ndata:\s*([^\r\n]+))");
  ASSERT_TRUE(std::regex_search(handshake, m, endpoint_re))
      << "no endpoint event on SSE stream: " << handshake;
  const std::string callback_url = m[1].str();
  const std::string marker = "callback/";
  const auto cb_pos = callback_url.rfind(marker);
  ASSERT_NE(cb_pos, std::string::npos);
  const std::string session_id = callback_url.substr(cb_pos + marker.size());
  ASSERT_FALSE(session_id.empty());

  // Now bring up the POST /callback connection on the SAME factory and
  // wire its connection pointer into the echoing callbacks. Without
  // that pointer the callbacks can't emit a write, and the routing
  // assertion below would be trivially true of any config.
  executeInDispatcher([&]() {
    auto extra = makeConnectionForFactory(factory);
    cb_conn = std::move(extra.conn);
    cb_peer = std::move(extra.peer);
    callbacks.callback_conn = cb_conn.get();

    const std::string body =
        R"({"jsonrpc":"2.0","id":42,"method":"ping"})";
    std::string req;
    req += "POST /callback/" + session_id + " HTTP/1.1\r\n";
    req += "Host: localhost\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n";
    req += body;
    writeClientBytes(*cb_peer, req);
  });

  // The POST connection gets back only the 202 Accepted handshake;
  // the response body is rerouted off this socket.
  const std::string cb_peer_bytes = drainPeer(*cb_peer);
  EXPECT_NE(cb_peer_bytes.find("HTTP/1.1 202 Accepted"), std::string::npos)
      << "expected 202 Accepted on POST connection, got: " << cb_peer_bytes;
  EXPECT_EQ(cb_peer_bytes.find("\"echoed\":true"), std::string::npos)
      << "response leaked back onto POST connection instead of SSE stream: "
      << cb_peer_bytes;

  EXPECT_EQ(callbacks.requests_seen.load(), 1)
      << "JSON-RPC filter never dispatched the POSTed request";

  // The actual routed-response assertion: after the 202, the JSON-RPC
  // response bytes produced by EchoingCallbacks must appear on the SSE
  // peer — that's the registry-based reroute working end-to-end.
  const std::string routed = drainPeer(*sse_peer);
  EXPECT_NE(routed.find("\"echoed\":true"), std::string::npos)
      << "JSON-RPC response was not routed through SSE stream, got: "
      << routed;

  // Tear everything down on the dispatcher thread. The factory outlives
  // both connections via its filters_ vector, so it must be dropped
  // last — dropping it off-thread would trip SseSessionRegistry asserts
  // as the SSE-codec filter destructors run.
  executeInDispatcher([&]() {
    // Null the side-channel connection pointer before destroying cb_conn
    // so any stray read event on the socket doesn't race our teardown
    // through EchoingCallbacks.
    callbacks.callback_conn = nullptr;
    cb_conn->close(network::ConnectionCloseType::NoFlush);
    cb_conn.reset();
    sse_conn->close(network::ConnectionCloseType::NoFlush);
    sse_conn.reset();
    factory.reset();
  });
}

}  // namespace
}  // namespace integration
}  // namespace mcp
