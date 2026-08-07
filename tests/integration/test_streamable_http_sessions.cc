/**
 * @file test_streamable_http_sessions.cc
 * @brief Wire-level tests for session identity on the MCP endpoint
 *
 * A session id is what lets a client's second request be recognised as the
 * same conversation as its first. These tests are about what a client can
 * actually read off the socket and send back — whether the id is there at
 * all, whether a browser is allowed to read it, and what happens to one a
 * client makes up.
 *
 * Real TCP socketpairs, following test_streamable_http_post.cc.
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

/** Answers every request and records the session it was served under. */
class SessionRecordingCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request& request) override {
    requests.push_back(request);
  }

  void onRequestWithContext(const jsonrpc::Request& request,
                            MessageDispatchContext& context) override {
    requests.push_back(request);
    sessions.push_back(context.transportSessionId());

    jsonrpc::Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    json::JsonValue result = json::JsonValue::object();
    result["protocolVersion"] = std::string("2025-06-18");
    response.result = mcp::make_optional(jsonrpc::ResponseResult(result));
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
  std::vector<std::string> sessions;
};

class StreamableHttpSessionsTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    executeInDispatcher([&]() {
      closeConnection(conn_);
      closeConnection(second_conn_);
      conn_.reset();
      second_conn_.reset();
      factory_.reset();
    });
    peer_.reset();
    second_peer_.reset();
    test::RealIoTestBase::TearDown();
  }

  static void closeConnection(
      const std::unique_ptr<network::ServerConnection>& conn) {
    if (conn) {
      conn->close(network::ConnectionCloseType::NoFlush);
    }
  }

  /**
   * @param keep_sessions False builds a stateless server, which mints no
   *                      session and believes no session id it is sent.
   */
  void startServer(bool keep_sessions = true) {
    executeInDispatcher([&]() {
      factory_ =
          std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks_,
                                                      /*is_server=*/true,
                                                      /*http_path=*/"/mcp",
                                                      /*http_host=*/"localhost",
                                                      /*use_sse=*/true,
                                                      /*sse_path=*/"/sse",
                                                      /*rpc_path=*/"/mcp");
      transport::StreamableHttpConfig config;
      config.enable_sessions = keep_sessions;
      factory_->setSessionConfig(config);
      factory_->setSecurityConfig(config);
    });
    connect(conn_, peer_);
  }

  /** Bring up one more client, so two can be told apart on the wire. */
  void connectSecondClient() { connect(second_conn_, second_peer_); }

  void connect(std::unique_ptr<network::ServerConnection>& conn,
               network::IoHandlePtr& peer) {
    executeInDispatcher([&]() {
      auto pair = createSocketPair();
      auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      auto transport = std::make_unique<network::RawBufferTransportSocket>();
      stream_info_ = std::make_shared<stream_info::StreamInfoImpl>();

      conn = network::ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket), std::move(transport), *stream_info_);
      auto* impl = static_cast<network::ConnectionImpl*>(conn.get());
      ASSERT_TRUE(factory_->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();

      peer = std::move(pair.second);
    });
  }

  void sendPost(network::IoHandlePtr& peer,
                const std::string& body,
                const std::string& extra_headers = std::string()) {
    const std::string request =
        "POST /mcp HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n" +
        extra_headers + "Content-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body;
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(request);
      auto result = peer->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
  }

  std::string readResponse(network::IoHandlePtr& peer,
                           std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = peer->read(buffer, 4096);
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

  /** The session id a response handed back, or empty when it handed none. */
  static std::string sessionIdOf(const std::string& response) {
    const std::string name = "\r\nMcp-Session-Id: ";
    const size_t at = response.find(name);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t start = at + name.size();
    return response.substr(start, response.find("\r\n", start) - start);
  }

  static bool looksLikeASessionId(const std::string& id) {
    if (id.size() != 32) {
      return false;
    }
    for (char c : id) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
        return false;
      }
    }
    return true;
  }

  SessionRecordingCallbacks callbacks_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::unique_ptr<network::ServerConnection> conn_;
  std::unique_ptr<network::ServerConnection> second_conn_;
  network::IoHandlePtr peer_;
  network::IoHandlePtr second_peer_;
  std::shared_ptr<stream_info::StreamInfoImpl> stream_info_;
};

const char kInitialize[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
const char kListTools[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}";

TEST_F(StreamableHttpSessionsTest, IntroducingYourselfEarnsASessionId) {
  startServer();
  sendPost(peer_, kInitialize);

  const std::string response = readResponse(peer_);

  const std::string id = sessionIdOf(response);
  EXPECT_TRUE(looksLikeASessionId(id))
      << "not something a client could echo back: '" << id << "' in "
      << response;

  // Attaching a header takes the response off the codec's framing path, so
  // this is where a client would stop being told what it is reading.
  EXPECT_EQ(response.find("HTTP/1.1 200 OK\r\n"), 0u) << response;
  EXPECT_NE(response.find("Content-Type: application/json\r\n"),
            std::string::npos)
      << response;
  EXPECT_NE(response.find("Content-Length: "), std::string::npos) << response;
}

TEST_F(StreamableHttpSessionsTest, TwoClientsAreNotGivenTheSameId) {
  startServer();
  connectSecondClient();

  sendPost(peer_, kInitialize);
  const std::string first = sessionIdOf(readResponse(peer_));
  sendPost(second_peer_, kInitialize);
  const std::string second = sessionIdOf(readResponse(second_peer_));

  ASSERT_TRUE(looksLikeASessionId(first));
  ASSERT_TRUE(looksLikeASessionId(second));
  EXPECT_NE(first, second);
}

TEST_F(StreamableHttpSessionsTest, ComingBackWithTheIdContinuesTheSession) {
  startServer();
  sendPost(peer_, kInitialize);
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));

  sendPost(peer_, kListTools, "Mcp-Session-Id: " + id + "\r\n");
  const std::string second = readResponse(peer_);
  EXPECT_EQ(second.find("HTTP/1.1 200 OK\r\n"), 0u) << second;

  ASSERT_EQ(callbacks_.sessions.size(), 2u);
  // The request that created the session is served under it too, so what
  // was agreed at initialize is recorded against the identity the client
  // will actually come back with.
  EXPECT_EQ(callbacks_.sessions[0], id);
  EXPECT_EQ(callbacks_.sessions[1], id);
}

TEST_F(StreamableHttpSessionsTest, ASecondConnectionCarriesTheSameSession) {
  startServer();
  sendPost(peer_, kInitialize);
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));

  // The point of a session id rather than a connection: the conversation
  // survives the connection it started on.
  connectSecondClient();
  sendPost(second_peer_, kListTools, "Mcp-Session-Id: " + id + "\r\n");
  readResponse(second_peer_);

  ASSERT_EQ(callbacks_.sessions.size(), 2u);
  EXPECT_EQ(callbacks_.sessions[1], id);
}

TEST_F(StreamableHttpSessionsTest, ABrowserIsAllowedToReadTheId) {
  startServer();
  sendPost(peer_, kInitialize, "Origin: http://localhost:3000\r\n");

  const std::string response = readResponse(peer_);

  ASSERT_TRUE(looksLikeASessionId(sessionIdOf(response))) << response;
  EXPECT_NE(response.find("Access-Control-Allow-Origin: http://localhost:3000"),
            std::string::npos)
      << response;
  // Without this the header is there and a browser still cannot see it,
  // which leaves the session unusable from a page.
  EXPECT_NE(response.find("Access-Control-Expose-Headers: Mcp-Session-Id"),
            std::string::npos)
      << response;
}

TEST_F(StreamableHttpSessionsTest, NothingButInitializeEarnsAnId) {
  startServer();
  sendPost(peer_, kListTools);

  const std::string response = readResponse(peer_);

  EXPECT_TRUE(sessionIdOf(response).empty()) << response;
  ASSERT_EQ(callbacks_.sessions.size(), 1u);
  EXPECT_EQ(callbacks_.sessions[0], "");
}

TEST_F(StreamableHttpSessionsTest, AStatelessServerHandsNothingBack) {
  startServer(/*keep_sessions=*/false);
  sendPost(peer_, kInitialize);

  const std::string response = readResponse(peer_);

  EXPECT_TRUE(sessionIdOf(response).empty()) << response;
  ASSERT_EQ(callbacks_.sessions.size(), 1u);
  EXPECT_EQ(callbacks_.sessions[0], "");
}

TEST_F(StreamableHttpSessionsTest, AStatelessServerDisregardsAnInventedId) {
  startServer(/*keep_sessions=*/false);
  connectSecondClient();

  // Two callers agreeing on an id they made up. On a server that keeps no
  // sessions this must not put them in one together — believing an id it
  // never issued is how one caller reaches another's state.
  const std::string invented = "Mcp-Session-Id: shared-secret-guess\r\n";
  sendPost(peer_, kListTools, invented);
  readResponse(peer_);
  sendPost(second_peer_, kListTools, invented);
  readResponse(second_peer_);

  ASSERT_EQ(callbacks_.sessions.size(), 2u);
  EXPECT_EQ(callbacks_.sessions[0], "");
  EXPECT_EQ(callbacks_.sessions[1], "");
}

}  // namespace
}  // namespace filter
}  // namespace mcp
