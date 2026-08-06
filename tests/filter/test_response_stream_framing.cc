/**
 * @file test_response_stream_framing.cc
 * @brief Wire-level tests for how a server response stream is framed
 *
 * An event stream that carries neither Content-Length nor
 * Transfer-Encoding is not a stream at all as far as an HTTP/1.1 peer is
 * concerned — the body is defined to be empty and everything after the
 * headers reads as the beginning of the next response. That is what these
 * tests exist to prevent, so they assert on the framing bytes themselves
 * rather than on the payload showing up somewhere in the output.
 *
 * Uses real TCP socketpairs following test_http_sse_filter_server_mode.cc.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/filter/sse_session_registry.h"
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

class FramingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& req) override {
    requests_.push_back(req);
  }
  void onRequestWithContext(const jsonrpc::Request& req,
                            MessageDispatchContext&) override {
    requests_.push_back(req);
  }
  void onNotification(const jsonrpc::Notification&) override {}
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}
  void onMessageEndpoint(const std::string&) override {}
  bool sendHttpPost(const std::string&) override { return true; }

  std::vector<jsonrpc::Request> requests_;
};

class ResponseStreamFramingTest : public test::RealIoTestBase {
 protected:
  struct Harness {
    std::shared_ptr<HttpSseFilterChainFactory> factory;
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  Harness makeServerHarness(FramingCallbacks& callbacks) {
    auto factory = std::make_shared<HttpSseFilterChainFactory>(
        *dispatcher_, callbacks,
        /*is_server=*/true, /*http_path=*/"/mcp",
        /*http_host=*/"localhost",
        /*use_sse=*/true, /*sse_path=*/"/sse", /*rpc_path=*/"/mcp");
    factory->setStreamGatePolicy(gate_policy_);
    factory->setGatedInputLimit(gated_input_limit_);

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

  // Stand the connection up and send the first request. Both have to happen
  // on the dispatcher thread: the socket pair arms file events there.
  void startServer(FramingCallbacks& callbacks,
                   const std::string& first_request) {
    executeInDispatcher([&]() {
      auto h = makeServerHarness(callbacks);
      conn_ = std::move(h.conn);
      peer_ = std::move(h.peer);
      factory_ = std::move(h.factory);
      writeClientBytes(*peer_, first_request);
    });
  }

  void TearDown() override {
    if (conn_ || factory_) {
      closeOnDispatcher(std::move(conn_), std::move(factory_));
    }
    peer_.reset();
    test::RealIoTestBase::TearDown();
  }

  // Header names are case-insensitive on the wire and the server's handlers
  // are not consistent about it, so match on a lowercased copy.
  static bool hasHeader(const std::string& wire, const std::string& name) {
    std::string lowered = wire;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char c) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return lowered.find("\r\n" + name + ":") != std::string::npos;
  }

  static std::string sseGetRequest() {
    return "GET /sse HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Accept: text/event-stream\r\n"
           "\r\n";
  }

  static std::string postRequest(const std::string& path,
                                 const std::string& body) {
    return "POST " + path +
           " HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
  }

  static std::string initializeBody() {
    return R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{)"
           R"("protocolVersion":"2025-06-18","capabilities":{},)"
           R"("clientInfo":{"name":"test","version":"1.0"}}})";
  }

  bool connectionClosed() {
    return executeInDispatcher(
        [&]() { return conn_->state() != network::ConnectionState::Open; });
  }

  StreamGatePolicy gate_policy_{StreamGatePolicy::Off};
  size_t gated_input_limit_{64 * 1024};
  std::unique_ptr<network::ServerConnection> conn_;
  network::IoHandlePtr peer_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
};

// The defect this whole change exists for: the stream prelude has to state
// how the body is delimited, or a peer reads it as an empty body.
TEST_F(ResponseStreamFramingTest, SsePreludeDeclaresChunkedFraming) {
  FramingCallbacks callbacks;
  startServer(callbacks, sseGetRequest());
  const std::string wire = drainPeer(*peer_);

  EXPECT_NE(wire.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << wire;
  EXPECT_NE(wire.find("\r\nTransfer-Encoding: chunked\r\n"), std::string::npos)
      << wire;
  EXPECT_NE(wire.find("\r\nContent-Type: text/event-stream\r\n"),
            std::string::npos)
      << wire;
  // A body cannot be both length-delimited and chunked.
  EXPECT_FALSE(hasHeader(wire, "content-length")) << wire;
}

// The endpoint event must arrive inside a chunk, not loose after the
// headers, or the framing the prelude promised is a lie.
TEST_F(ResponseStreamFramingTest, EndpointEventArrivesInsideAChunk) {
  FramingCallbacks callbacks;
  startServer(callbacks, sseGetRequest());
  const std::string wire = drainPeer(*peer_);

  const size_t body_start = wire.find("\r\n\r\n");
  ASSERT_NE(body_start, std::string::npos) << wire;
  const std::string body = wire.substr(body_start + 4);

  // The chunk is a hex size line, the SSE event, then a CRLF terminator.
  const size_t size_line_end = body.find("\r\n");
  ASSERT_NE(size_line_end, std::string::npos) << body;
  const std::string size_line = body.substr(0, size_line_end);
  ASSERT_FALSE(size_line.empty());

  const size_t declared = std::stoul(size_line, nullptr, 16);
  const std::string chunk_body = body.substr(size_line_end + 2, declared);

  EXPECT_EQ(chunk_body.substr(0, 16), "event: endpoint\n") << chunk_body;
  EXPECT_NE(chunk_body.find("data: callback/"), std::string::npos)
      << chunk_body;
  // The declared size must actually be followed by the chunk terminator,
  // which is what proves the size is right rather than merely plausible.
  EXPECT_EQ(body.substr(size_line_end + 2 + declared, 2), "\r\n") << body;
}

// A plain response must stay length-delimited; only streams are chunked.
TEST_F(ResponseStreamFramingTest, UnaryResponseStaysLengthDelimited) {
  FramingCallbacks callbacks;
  startServer(callbacks,
              "GET /health HTTP/1.1\r\n"
              "Host: localhost\r\n"
              "\r\n");
  const std::string wire = drainPeer(*peer_);

  EXPECT_NE(wire.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << wire;
  EXPECT_TRUE(hasHeader(wire, "content-length")) << wire;
  EXPECT_FALSE(hasHeader(wire, "transfer-encoding")) << wire;
}

// ── Stream connection policy ───────────────────────────────────────────────

// Overflowing the held-input budget leaves nowhere to put an error: a
// response body is already on the wire, so a status line spliced into it
// would corrupt the stream. The connection goes instead.
TEST_F(ResponseStreamFramingTest, OverflowingHeldInputTearsDownTheStream) {
  gate_policy_ = StreamGatePolicy::DecoderGate;
  gated_input_limit_ = 512;
  FramingCallbacks callbacks;
  startServer(callbacks, sseGetRequest());
  const std::string prelude = drainPeer(*peer_);
  ASSERT_FALSE(prelude.empty());
  ASSERT_FALSE(connectionClosed());

  writeClientBytes(*peer_, std::string(4096, 'x'));

  EXPECT_TRUE(waitFor([&]() { return connectionClosed(); }, 2000ms))
      << "connection survived an overflow of the held-input budget";

  // Nothing that looks like a second response may have been spliced into
  // the stream body on the way out.
  OwnedBuffer buf;
  auto r = peer_->read(buf, 4096);
  if (r.ok() && *r > 0) {
    const std::string tail = buf.toString();
    EXPECT_EQ(tail.find("HTTP/1.1"), std::string::npos) << tail;
  }
  EXPECT_TRUE(callbacks.requests_.empty());
}

// The other wire-legal answer to a request behind an open stream: say up
// front that the connection ends with this response.
TEST_F(ResponseStreamFramingTest, SingleUseStreamAnnouncesConnectionClose) {
  gate_policy_ = StreamGatePolicy::SingleUseClose;
  FramingCallbacks callbacks;
  startServer(callbacks, sseGetRequest());
  const std::string wire = drainPeer(*peer_);

  EXPECT_NE(wire.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << wire;
  EXPECT_NE(wire.find("\r\nConnection: close\r\n"), std::string::npos) << wire;
  // Still chunked: how the body is delimited and whether the connection is
  // reusable are separate questions.
  EXPECT_NE(wire.find("\r\nTransfer-Encoding: chunked\r\n"), std::string::npos)
      << wire;
}

// Arming the gate must not disturb a connection that is behaving normally.
TEST_F(ResponseStreamFramingTest, GateDoesNotDisturbAnOrdinaryExchange) {
  gate_policy_ = StreamGatePolicy::DecoderGate;
  FramingCallbacks callbacks;
  startServer(callbacks, postRequest("/mcp", initializeBody()));

  EXPECT_TRUE(waitFor([&]() { return !callbacks.requests_.empty(); }, 2000ms));
  EXPECT_EQ(callbacks.requests_.size(), 1u);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
