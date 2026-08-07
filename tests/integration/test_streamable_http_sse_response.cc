/**
 * @file test_streamable_http_sse_response.cc
 * @brief What a client reads when a POSTed request is answered with a stream
 *
 * The filter's own tests cover which answer a request earns; these cover
 * that the answer is framed so a client can read it — a stream with no
 * chunk framing is defined to have an empty body, and a chunk that has been
 * framed twice reads as the end of the response rather than part of it.
 *
 * Real TCP socketpairs, following test_streamable_http_post.cc.
 */

#include <chrono>
#include <string>
#include <thread>
#include <vector>

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

/**
 * A handler whose streaming behaviour each test dictates.
 *
 * It keeps the stream handle rather than dropping it at the end of the
 * dispatch, which is the whole point of the handle: a test can then drive
 * the handler forward after the client has gone.
 */
class StreamingCallbacks : public McpProtocolCallbacks {
 public:
  StreamingMode streamingFor(const jsonrpc::Request&) const override {
    return streaming;
  }

  void onRequest(const jsonrpc::Request& request) override {
    requests.push_back(request);
  }

  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    requests.push_back(request);

    if (streaming == StreamingMode::None) {
      context.sendResponse(resultFor(request));
      return;
    }

    stream = context.beginResponseStream();
    if (!stream) {
      return;
    }
    for (size_t i = 0; i < progress_count; ++i) {
      stream->sendNotification(progressNotification(i));
    }
    if (answer_now) {
      stream->sendResponse(resultFor(request));
    } else {
      pending = request.id;
    }
  }

  void onNotification(const jsonrpc::Notification&) override {}
  void onNotificationWithContext(const jsonrpc::Notification&,
                                 MessageDispatchContext&) override {}
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  static jsonrpc::Response resultFor(const jsonrpc::Request& request) {
    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
    return response;
  }

  static jsonrpc::Notification progressNotification(size_t step) {
    jsonrpc::Notification notification;
    notification.jsonrpc = "2.0";
    notification.method = "notifications/progress";
    Metadata params;
    params["step"] = MetadataValue(static_cast<int64_t>(step));
    notification.params = mcp::make_optional(params);
    return notification;
  }

  StreamingMode streaming{StreamingMode::None};
  size_t progress_count{0};
  bool answer_now{true};
  ResponseStreamPtr stream;
  optional<RequestId> pending;
  std::vector<jsonrpc::Request> requests;
};

class StreamableHttpSseResponseTest : public test::RealIoTestBase {
 protected:
  struct Peer {
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr handle;
    std::shared_ptr<stream_info::StreamInfoImpl> stream_info;
  };

  void TearDown() override {
    executeInDispatcher([&]() {
      for (auto& peer : peers_) {
        if (peer.conn) {
          peer.conn->close(network::ConnectionCloseType::NoFlush);
        }
        peer.conn.reset();
      }
      factory_.reset();
    });
    peers_.clear();
    test::RealIoTestBase::TearDown();
  }

  void startServer(StreamGatePolicy gate = StreamGatePolicy::DecoderGate) {
    executeInDispatcher([&]() {
      factory_ =
          std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks_,
                                                      /*is_server=*/true,
                                                      /*http_path=*/"/mcp",
                                                      /*http_host=*/"localhost",
                                                      /*use_sse=*/true,
                                                      /*sse_path=*/"/sse",
                                                      /*rpc_path=*/"/mcp");
      factory_->setStreamGatePolicy(gate);
      // Stateless, because what is under test here is framing rather than
      // identity: with sessions on, every request below would have to
      // introduce itself first, which the session tests already cover.
      transport::StreamableHttpConfig config;
      config.enable_sessions = false;
      factory_->setSessionConfig(config);
    });
  }

  /** Another client connection onto the same server. */
  size_t connect() {
    size_t index = 0;
    executeInDispatcher([&]() {
      auto pair = createSocketPair();
      auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      auto transport = std::make_unique<network::RawBufferTransportSocket>();

      Peer peer;
      peer.stream_info = std::make_shared<stream_info::StreamInfoImpl>();
      peer.conn = network::ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket), std::move(transport),
          *peer.stream_info);
      auto* impl = static_cast<network::ConnectionImpl*>(peer.conn.get());
      ASSERT_TRUE(factory_->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();
      peer.handle = std::move(pair.second);

      index = peers_.size();
      peers_.push_back(std::move(peer));
    });
    return index;
  }

  static std::string post(const std::string& body, const std::string& accept) {
    return "POST /mcp HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n"
           "Accept: " +
           accept + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
  }

  void send(size_t peer, const std::string& bytes) {
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(bytes);
      auto result = peers_[peer].handle->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
  }

  /** Read until nothing more arrives for a moment, or the budget runs out. */
  std::string read(size_t peer, std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = peers_[peer].handle->read(buffer, 4096);
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

  void disconnect(size_t peer) {
    peers_[peer].handle.reset();
    executeInDispatcher([&]() {
      if (peers_[peer].conn) {
        peers_[peer].conn->close(network::ConnectionCloseType::NoFlush);
      }
    });
    // Let the close travel through the filter chain.
    executeInDispatcher([]() {});
  }

  static size_t countOf(const std::string& haystack,
                        const std::string& needle) {
    size_t count = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + 1)) {
      ++count;
    }
    return count;
  }

  StreamingCallbacks callbacks_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::vector<Peer> peers_;
};

const char kRequest[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"work\",\"params\":{}}";
const char kSecondRequest[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"work\",\"params\":{}}";
const char kBothTypes[] = "application/json, text/event-stream";

// ── The streamed answer ────────────────────────────────────────────────────

TEST_F(StreamableHttpSseResponseTest, ProgressAndResultArriveAsOneChunkedBody) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 3;
  startServer();
  const size_t peer = connect();

  send(peer, post(kRequest, kBothTypes));
  const std::string wire = read(peer);

  EXPECT_EQ(wire.find("HTTP/1.1 200 OK\r\n"), 0u) << wire;
  EXPECT_NE(wire.find("Content-Type: text/event-stream"), std::string::npos)
      << wire;
  EXPECT_NE(wire.find("Transfer-Encoding: chunked"), std::string::npos)
      << "a stream a client cannot find the end of is no stream at all: "
      << wire;
  EXPECT_NE(wire.find("X-Accel-Buffering: no"), std::string::npos)
      << "a proxy that buffers the stream defeats the point of it: " << wire;

  EXPECT_EQ(countOf(wire, "notifications/progress"), 3u) << wire;

  // The response is the last thing said, and the terminating chunk is what
  // tells the client the body ended.
  const size_t response_at = wire.find("\"result\"");
  ASSERT_NE(response_at, std::string::npos) << wire;
  EXPECT_GT(response_at, wire.rfind("notifications/progress")) << wire;
  EXPECT_NE(wire.rfind("0\r\n\r\n"), std::string::npos) << wire;

  // Framed once. A chunk wrapped in a second complete response would put
  // another status line in the middle of the body.
  EXPECT_EQ(countOf(wire, "HTTP/1.1"), 1u) << wire;
}

TEST_F(StreamableHttpSseResponseTest, NoEventCarriesAnIdYet) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 2;
  startServer();
  const size_t peer = connect();

  send(peer, post(kRequest, kBothTypes));

  // An id invites a client to come back and resume from it, and nothing
  // can honour that yet.
  EXPECT_EQ(read(peer).find("\nid: "), std::string::npos);
}

TEST_F(StreamableHttpSseResponseTest, AnOrdinaryAnswerIsStillLengthDelimited) {
  callbacks_.streaming = StreamingMode::None;
  startServer();
  const size_t peer = connect();

  send(peer, post(kRequest, kBothTypes));
  const std::string wire = read(peer);

  EXPECT_EQ(wire.find("HTTP/1.1 200 OK\r\n"), 0u) << wire;
  EXPECT_NE(wire.find("Content-Type: application/json"), std::string::npos)
      << wire;
  EXPECT_NE(wire.find("Content-Length:"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("text/event-stream"), std::string::npos) << wire;
}

// ── What the client will accept ────────────────────────────────────────────

TEST_F(StreamableHttpSseResponseTest, ProgressIsDroppedRatherThanRefused) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 3;
  startServer();
  const size_t peer = connect();

  send(peer, post(kRequest, "application/json"));
  const std::string wire = read(peer);

  // Still answerable without the progress, so it is answered.
  EXPECT_EQ(wire.find("HTTP/1.1 200 OK\r\n"), 0u) << wire;
  EXPECT_EQ(wire.find("text/event-stream"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("notifications/progress"), std::string::npos) << wire;
  EXPECT_NE(wire.find("\"result\""), std::string::npos) << wire;
  EXPECT_EQ(callbacks_.requests.size(), 1u);
}

TEST_F(StreamableHttpSseResponseTest, ARequiredStreamIsRefusedBeforeAnyOutput) {
  callbacks_.streaming = StreamingMode::Required;
  startServer();
  const size_t peer = connect();

  send(peer, post(kRequest, "application/json"));
  const std::string wire = read(peer);

  EXPECT_EQ(wire.find("HTTP/1.1 406 Not Acceptable\r\n"), 0u) << wire;
  EXPECT_NE(wire.find("text/event-stream"), std::string::npos) << wire;
  EXPECT_EQ(wire.find("\"result\""), std::string::npos)
      << "the refusal must come before any of the answer: " << wire;
  EXPECT_TRUE(callbacks_.requests.empty())
      << "a handler that would wait on a question the client cannot see must "
         "not be started";
}

// ── Losing the client ──────────────────────────────────────────────────────

TEST_F(StreamableHttpSseResponseTest, AHandlerOutlivesTheClientThatLeft) {
  callbacks_.streaming = StreamingMode::Required;
  callbacks_.answer_now = false;
  startServer();
  const size_t peer = connect();

  send(peer, post(kRequest, kBothTypes));
  ASSERT_FALSE(read(peer).empty());
  ASSERT_EQ(callbacks_.requests.size(), 1u);
  ASSERT_TRUE(callbacks_.stream);

  disconnect(peer);

  // A disconnect is not a cancellation in this protocol revision: the work
  // carries on and its output is kept for a client that comes back.
  EXPECT_FALSE(callbacks_.stream->alive())
      << "nothing written now would reach anyone";

  bool wrote = false;
  executeInDispatcher([&]() {
    wrote = !holds_alternative<Error>(callbacks_.stream->sendNotification(
        StreamingCallbacks::progressNotification(9)));
    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = RequestId(static_cast<int64_t>(1));
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
    callbacks_.stream->sendResponse(response);
  });
  EXPECT_TRUE(wrote) << "the work behind the request was still wanted";
}

// ── More than one at a time ────────────────────────────────────────────────

TEST_F(StreamableHttpSseResponseTest, TwoConnectionsStreamIndependently) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 2;
  startServer();
  const size_t first = connect();
  const size_t second = connect();

  send(first, post(kRequest, kBothTypes));
  send(second, post(kSecondRequest, kBothTypes));

  const std::string a = read(first);
  const std::string b = read(second);

  EXPECT_EQ(countOf(a, "notifications/progress"), 2u) << a;
  EXPECT_EQ(countOf(b, "notifications/progress"), 2u) << b;
  EXPECT_NE(a.find("\"id\":1"), std::string::npos) << a;
  EXPECT_NE(b.find("\"id\":2"), std::string::npos) << b;
  EXPECT_EQ(a.find("\"id\":2"), std::string::npos)
      << "one connection's answer must not carry the other's: " << a;
}

// ── A request arriving behind an open stream ───────────────────────────────

TEST_F(StreamableHttpSseResponseTest, ASecondRequestWaitsForTheStreamToEnd) {
  callbacks_.streaming = StreamingMode::Required;
  callbacks_.answer_now = false;
  startServer(StreamGatePolicy::DecoderGate);
  const size_t peer = connect();

  send(peer, post(kRequest, kBothTypes));
  const std::string opened = read(peer);
  ASSERT_NE(opened.find("text/event-stream"), std::string::npos) << opened;

  // HTTP/1.1 delivers responses in request order, so this one cannot be
  // answered until the stream in front of it finishes. Answering it now —
  // even with an error — is not something a client could read.
  send(peer, post(kSecondRequest, kBothTypes));
  const std::string during = read(peer, 300ms);
  EXPECT_EQ(during.find("\"id\":2"), std::string::npos) << during;
  EXPECT_EQ(callbacks_.requests.size(), 1u)
      << "the request behind the stream must not even be dispatched yet";

  // Finishing the first frees the connection, and the second is answered
  // in its turn.
  executeInDispatcher([&]() {
    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = RequestId(static_cast<int64_t>(1));
    response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
    callbacks_.stream->sendResponse(response);
  });

  const std::string after = read(peer);
  EXPECT_NE(after.find("\"id\":1"), std::string::npos) << after;
  EXPECT_EQ(callbacks_.requests.size(), 2u)
      << "and is dispatched once the stream in front of it has ended";
}

TEST_F(StreamableHttpSseResponseTest, ASingleUseStreamSaysTheConnectionEnds) {
  callbacks_.streaming = StreamingMode::Optional;
  callbacks_.progress_count = 1;
  startServer(StreamGatePolicy::SingleUseClose);
  const size_t peer = connect();

  send(peer, post(kRequest, kBothTypes));
  const std::string wire = read(peer);

  // The other wire-legal answer to a request arriving behind a stream:
  // say up front that there will not be one.
  EXPECT_NE(wire.find("Connection: close"), std::string::npos) << wire;
}

}  // namespace
}  // namespace filter
}  // namespace mcp
