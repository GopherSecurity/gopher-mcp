/**
 * Real-IO integration test for the SSE server transport.
 *
 * Verifies the first leg of the round-trip promised in PR #215's test
 * plan: a GET on the configured SSE path returns an HTTP 200 stream that
 * opens with an `event: endpoint` frame carrying a /callback/{id} URL
 * the client can POST to. That exercises:
 *
 *   - HttpSseFilterChainFactory server-mode chain construction
 *   - HttpSseJsonRpcProtocolFilter's `onHeaders` handshake path, which
 *     writes the HTTP prelude + the endpoint event inline via
 *     connection().write()
 *   - SseSessionRegistry::registerSession being called against the live
 *     server connection
 *
 * Design:
 *   - No McpServer bootstrap; the factory + a ConnectionImpl around a
 *     real TCP socketpair is the smallest harness that exercises the
 *     real write path (not a mock) while keeping the test self-contained.
 *   - Dispatcher-thread invariant: every mutation of the connection and
 *     filter chain runs inside executeInDispatcher() because
 *     ConnectionImpl's lifecycle methods assert isThreadSafe().
 *   - Cleanup closes the connection on the dispatcher thread too — tearing
 *     down ConnectionImpl from the test thread would trip its destructor
 *     assert.
 *
 * The POST /callback/{id} → 202 → response-routed-through-SSE leg of the
 * round-trip requires either a full McpServer bootstrap or a test-only
 * connection-tracking filter and is tracked as a follow-up.
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

  Harness makeHarness(RecordingCallbacks& callbacks,
                      const std::string& sse_path = "/sse",
                      const std::string& rpc_path = "/mcp",
                      const std::string& external_url = "") {
    auto pair = createSocketPair();

    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto stream_info = std::make_shared<stream_info::StreamInfoImpl>();

    auto conn = network::ConnectionImpl::createServerConnection(
        *dispatcher_, std::move(socket), std::move(transport), *stream_info);

    auto factory = std::make_shared<filter::HttpSseFilterChainFactory>(
        *dispatcher_, callbacks,
        /*is_server=*/true,
        /*http_path=*/rpc_path,
        /*http_host=*/"localhost",
        /*use_sse=*/true,
        /*sse_path=*/sse_path,
        /*rpc_path=*/rpc_path,
        /*external_url=*/external_url);

    // Attach the factory's chain to the server connection and arm reads.
    // Equivalent to what TcpActiveListener::createConnection does for
    // real accepted sockets.
    auto* conn_impl = static_cast<network::ConnectionImpl*>(conn.get());
    // createFilterChain() returning false would mean the factory couldn't
    // assemble the HTTP+SSE chain at all — nothing downstream is meaningful
    // if that happens, so fail loudly right here.
    EXPECT_TRUE(factory->createFilterChain(conn_impl->filterManager()))
        << "factory declined to build a filter chain";
    conn_impl->filterManager().initializeReadFilters();

    return Harness{std::move(factory), std::move(conn), std::move(pair.second),
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

}  // namespace
}  // namespace integration
}  // namespace mcp
