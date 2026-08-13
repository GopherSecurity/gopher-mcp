/**
 * The revision with no handshake, over a real socket, on a server that
 * still serves everyone else.
 *
 * The per-rule matrix — which header disagreeing with which field earns
 * which refusal — lives in tests/filter/test_streamable_http_filter.cc,
 * where a decision can be asked for without a connection. What needs a
 * wire is everything the filter cannot answer alone: the `Allow` header a
 * refusal carries, which is built from the route table two layers up, and
 * era isolation, which is only a claim worth testing when both eras are
 * being served by one server at the same time.
 *
 * Real TCP socketpairs, following test_streamable_http_sessions.cc.
 */

#include <chrono>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/types.h"

#include "real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

/** Answers everything, and keeps what the request said about its caller. */
class EraRecordingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests.push_back(request);
  }

  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    requests.push_back(request);

    if (hold_the_stream) {
      // A handler with work still to do: it takes a stream, says
      // something on it — which is what opens one — and keeps it.
      auto stream = context.beginResponseStream();
      if (stream) {
        held.push_back(stream);
        jsonrpc::Notification progress;
        progress.jsonrpc = "2.0";
        progress.method = "notifications/progress";
        stream->sendNotification(progress);
        watching = stream->onCancelled([this]() { ++cancellations; });
        return;
      }
    }

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

  bool knowsMethod(const std::string& method) const override {
    return method != "tools/invent";
  }

  std::vector<jsonrpc::Request> requests;
  std::vector<jsonrpc::Notification> notifications;

  /** Answer by taking a stream and keeping it, rather than by answering. */
  bool hold_the_stream{false};
  std::vector<ResponseStreamPtr> held;
  /** Whether the transport had any way to report a cancellation at all. */
  bool watching{false};
  int cancellations{0};
};

class ModernEraHeadersTest : public test::RealIoTestBase {
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

  /** One server, serving the newest revision beside every older one. */
  void startServer(bool enable_modern_era = true,
                   bool enable_get_stream = true) {
    executeInDispatcher([&]() {
      factory_ = std::make_shared<HttpSseFilterChainFactory>(
          *dispatcher_, callbacks_, /*is_server=*/true, "/mcp", "localhost",
          /*use_sse=*/true, "/sse", "/mcp");
      transport::StreamableHttpConfig config;
      config.enable_modern_era = enable_modern_era;
      config.enable_get_stream = enable_get_stream;
      factory_->setSessionConfig(config);
      factory_->setSecurityConfig(config);
    });
    connect();
  }

  void connect() {
    executeInDispatcher([&]() {
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

  void send(const std::string& bytes) {
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(bytes);
      auto result = peer_->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
  }

  /** Everything the server has written back, once it has stopped. */
  std::string readResponse(std::chrono::milliseconds budget = 2000ms) {
    std::string collected;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      std::string chunk;
      executeInDispatcher([&]() {
        OwnedBuffer buffer;
        auto result = peer_->read(buffer, 65536);
        if (result.ok() && buffer.length() > 0) {
          chunk.assign(
              static_cast<const char*>(buffer.linearize(buffer.length())),
              buffer.length());
        }
      });
      collected += chunk;
      if (!collected.empty() && chunk.empty()) {
        break;
      }
      std::this_thread::sleep_for(20ms);
    }
    return collected;
  }

  /** The `_meta` a modern request carries. */
  static std::string modernMeta() {
    return std::string("\"_meta\":{\"") +
           protocol::modern::kMetaProtocolVersion + "\":\"" +
           protocol::kProtocolVersion20260728 + "\",\"" +
           protocol::modern::kMetaClientCapabilities + "\":{\"roots\":{}}}";
  }

  static std::string modernPost(const std::string& method,
                                const std::string& extra_params = "",
                                const std::string& extra_headers = "") {
    std::string params = "{";
    if (!extra_params.empty()) {
      params += extra_params + ",";
    }
    params += modernMeta() + "}";
    const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" +
                             method + "\",\"params\":" + params + "}";
    return "POST /mcp HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n"
           "Accept: application/json, text/event-stream\r\n" +
           std::string(protocol::modern::kProtocolVersionHeader) + ": " +
           protocol::kProtocolVersion20260728 + "\r\n" +
           protocol::modern::kMethodHeader + ": " + method + "\r\n" +
           extra_headers + "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
  }

  /** Shake hands as a client of the older era, and take the session id. */
  std::string classicSession() {
    const std::string body =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2025-06-18\"}}";
    send(
        "POST /mcp HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Accept: application/json, text/event-stream\r\n"
        "Content-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body);

    const std::string wire = readResponse(500ms);
    const size_t named = wire.find("Mcp-Session-Id: ");
    if (named == std::string::npos) {
      return std::string();
    }
    const size_t from = named + std::strlen("Mcp-Session-Id: ");
    return wire.substr(from, wire.find("\r\n", from) - from);
  }

  /** What a client aborting looks like from the other end. */
  void closePeer() {
    executeInDispatcher([&]() {
      if (peer_) {
        peer_->close();
      }
    });
  }

  EraRecordingCallbacks callbacks_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::unique_ptr<network::ServerConnection> conn_;
  network::IoHandlePtr peer_;
  std::shared_ptr<stream_info::StreamInfoImpl> stream_info_;
};

// The whole of a well-formed request, answered, with the field every
// result in this revision has to carry.
TEST_F(ModernEraHeadersTest, AWellFormedRequestIsServedAndSaysItIsComplete) {
  startServer();
  send(modernPost("tools/list"));

  const std::string wire = readResponse();
  EXPECT_EQ(wire.find("HTTP/1.1 200 "), 0u) << wire;
  EXPECT_NE(wire.find("\"resultType\":\"complete\""), std::string::npos)
      << "an answer arrived without saying what kind of result it is: " << wire;
  EXPECT_EQ(wire.find("Mcp-Session-Id"), std::string::npos)
      << "a revision with no sessions named one: " << wire;
  EXPECT_EQ(callbacks_.requests.size(), 1u);
}

// A method this server does not have is a 404 carrying a JSON-RPC error,
// which is the only thing telling a client the server is there and the
// method is not.
TEST_F(ModernEraHeadersTest, AnUnknownMethodIsNotFoundRatherThanNoEndpoint) {
  startServer();
  send(modernPost("tools/invent"));

  const std::string wire = readResponse();
  EXPECT_EQ(wire.find("HTTP/1.1 404 "), 0u) << wire;
  EXPECT_NE(wire.find(std::to_string(protocol::modern::kMethodNotFound)),
            std::string::npos)
      << wire;
  EXPECT_TRUE(callbacks_.requests.empty());
}

// The endpoint serves POST alone in this revision, and the Allow it sends
// is built two layers up from the route table — so this is where it can
// be seen. A GET is refused even though the server is serving streams to
// everyone else.
TEST_F(ModernEraHeadersTest, AModernCallerIsToldOnlyPostIsAllowed) {
  startServer(/*enable_modern_era=*/true, /*enable_get_stream=*/true);

  send(std::string("GET /mcp HTTP/1.1\r\nHost: localhost\r\n"
                   "Accept: text/event-stream\r\n") +
       protocol::modern::kProtocolVersionHeader + ": " +
       protocol::kProtocolVersion20260728 + "\r\nContent-Length: 0\r\n\r\n");

  const std::string wire = readResponse();
  EXPECT_EQ(wire.find("HTTP/1.1 405 "), 0u) << wire;
  EXPECT_NE(wire.find("Allow: POST\r\n"), std::string::npos)
      << "a caller was told about methods its revision does not have: " << wire;
  EXPECT_EQ(wire.find("DELETE"), std::string::npos)
      << "the Allow named a method this caller may not send: " << wire;
}

// The same server, the same moment, a caller from the older era: it opens
// the stream it asked for. Era isolation is only a claim worth testing
// while both are being served at once.
TEST_F(ModernEraHeadersTest, AClassicCallerOnTheSameServerIsUnaffected) {
  startServer(/*enable_modern_era=*/true, /*enable_get_stream=*/true);

  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
      "\"params\":{\"protocolVersion\":\"2025-06-18\"}}";
  send(
      "POST /mcp HTTP/1.1\r\nHost: localhost\r\n"
      "Content-Type: application/json\r\n"
      "Accept: application/json, text/event-stream\r\n"
      "Content-Length: " +
      std::to_string(body.size()) + "\r\n\r\n" + body);

  const std::string wire = readResponse();
  EXPECT_EQ(wire.find("HTTP/1.1 200 "), 0u) << wire;
  EXPECT_NE(wire.find("Mcp-Session-Id"), std::string::npos)
      << "a classic client was not given the session it is owed: " << wire;
  EXPECT_EQ(wire.find("resultType"), std::string::npos)
      << "a classic client was handed a field its revision has no place "
         "for: "
      << wire;
}

// And with the pipeline switched off, a caller declaring the revision is
// refused rather than served by rules this server does not follow.
TEST_F(ModernEraHeadersTest, TheRevisionIsRefusedWhileItIsSwitchedOff) {
  startServer(/*enable_modern_era=*/false);
  send(modernPost("tools/list"));

  const std::string wire = readResponse();
  EXPECT_EQ(wire.find("HTTP/1.1 400 "), 0u) << wire;
  EXPECT_NE(
      wire.find(std::to_string(protocol::modern::kUnsupportedProtocolVersion)),
      std::string::npos)
      << "a revision this server does not serve was refused in the wrong "
         "era's shape: "
      << wire;
  EXPECT_NE(wire.find("\"supported\""), std::string::npos)
      << "the refusal did not say what is served: " << wire;
}

// There is no message for cancelling a request in this revision, so
// closing the stream is the whole of it. A handler still working has to
// be told, because nothing else will ever tell it: this client cannot
// come back to the stream it dropped — there is no id to hold a place
// with and no session to look one up under — so its work has no reader
// and no way to acquire one.
TEST_F(ModernEraHeadersTest, AModernClientClosingItsStreamCancelsTheWork) {
  callbacks_.hold_the_stream = true;
  startServer();
  send(modernPost("tools/list"));

  const std::string wire = readResponse(500ms);
  ASSERT_NE(wire.find("text/event-stream"), std::string::npos)
      << "the handler never got a stream to be cancelled on: " << wire;
  ASSERT_TRUE(callbacks_.watching)
      << "the transport offered no way to hear about a cancellation";
  ASSERT_EQ(callbacks_.cancellations, 0) << "cancelled while still connected";

  closePeer();
  EXPECT_TRUE(waitFor([&]() { return callbacks_.cancellations > 0; }, 2000ms))
      << "a client went away and the work behind its request carried on "
         "with nobody able to read it";
}

// The same close, on the era where a client can come back: the stream is
// kept rather than cancelled, and the work goes on producing into it. The
// two policies are opposite on purpose, so this is the one that proves
// the modern rule did not become everyone's.
TEST_F(ModernEraHeadersTest, AClassicClientClosingItsStreamCancelsNothing) {
  startServer();
  // The handshake this era has and the other does not, and the session it
  // hands back. Held open from here on.
  const std::string session = classicSession();
  ASSERT_FALSE(session.empty());

  callbacks_.hold_the_stream = true;
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\","
      "\"params\":{}}";
  send(
      "POST /mcp HTTP/1.1\r\nHost: localhost\r\n"
      "Content-Type: application/json\r\n"
      "Accept: application/json, text/event-stream\r\n"
      "MCP-Protocol-Version: 2025-06-18\r\n"
      "Mcp-Session-Id: " +
      session + "\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\n\r\n" + body);

  const std::string wire = readResponse(500ms);
  ASSERT_NE(wire.find("text/event-stream"), std::string::npos)
      << "the handler never got a stream: " << wire;

  closePeer();
  // Given the same chance the modern case needed, and it must not have
  // taken it.
  EXPECT_FALSE(waitFor([&]() { return callbacks_.cancellations > 0; }, 500ms))
      << "work a client could have come back for was thrown away";
}

}  // namespace
}  // namespace filter
}  // namespace mcp
