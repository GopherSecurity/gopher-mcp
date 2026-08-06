/**
 * @file test_streamable_http_post.cc
 * @brief Wire-level tests for POST to the MCP endpoint
 *
 * What a client actually reads off the socket, byte for byte. The filter's
 * own tests cover which answer a body earns; these cover that the answer is
 * framed so a client can find where it ends — a response with neither a
 * Content-Length nor a transfer encoding is defined to have an empty body
 * on a persistent connection, whatever bytes follow it.
 *
 * Real TCP socketpairs, following test_http_sse_filter_server_mode.cc.
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

/** Answers every request, so a POST has something to carry back. */
class EchoingCallbacks : public McpProtocolCallbacks {
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
  std::vector<std::string> sessions;
};

class StreamableHttpPostTest : public test::RealIoTestBase {
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

  void startServer() {
    executeInDispatcher([&]() {
      factory_ =
          std::make_shared<HttpSseFilterChainFactory>(*dispatcher_, callbacks_,
                                                      /*is_server=*/true,
                                                      /*http_path=*/"/mcp",
                                                      /*http_host=*/"localhost",
                                                      /*use_sse=*/true,
                                                      /*sse_path=*/"/sse",
                                                      /*rpc_path=*/"/mcp");

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

  void sendPost(const std::string& body,
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
      auto result = peer_->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
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

  /** The declared body length, or -1 when no Content-Length was sent. */
  static long declaredLength(const std::string& response) {
    const size_t at = response.find("\r\nContent-Length: ");
    if (at == std::string::npos) {
      return -1;
    }
    return std::strtol(response.c_str() + at + 18, nullptr, 10);
  }

  static std::string bodyOf(const std::string& response) {
    const size_t at = response.find("\r\n\r\n");
    return at == std::string::npos ? std::string() : response.substr(at + 4);
  }

  EchoingCallbacks callbacks_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::unique_ptr<network::ServerConnection> conn_;
  network::IoHandlePtr peer_;
  std::shared_ptr<stream_info::StreamInfoImpl> stream_info_;
};

const char kInitialize[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
const char kInitialized[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";

TEST_F(StreamableHttpPostTest, ARequestComesBackAsLengthDelimitedJson) {
  startServer();
  sendPost(kInitialize);

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 200 OK\r\n"), 0u) << response;
  EXPECT_NE(response.find("Content-Type: application/json\r\n"),
            std::string::npos)
      << response;

  const std::string body = bodyOf(response);
  EXPECT_EQ(declaredLength(response), static_cast<long>(body.size()))
      << "a body a client cannot find the end of is no body at all: "
      << response;
  EXPECT_NE(body.find("\"id\":1"), std::string::npos) << body;

  ASSERT_EQ(callbacks_.requests.size(), 1u);
  EXPECT_EQ(callbacks_.requests[0].method, "initialize");
}

TEST_F(StreamableHttpPostTest, ANotificationComesBackAcceptedAndEmpty) {
  startServer();
  sendPost(kInitialized);

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 202 Accepted\r\n"), 0u) << response;
  EXPECT_EQ(declaredLength(response), 0L) << response;
  EXPECT_TRUE(bodyOf(response).empty())
      << "202 must carry nothing: " << response;

  ASSERT_EQ(callbacks_.notifications.size(), 1u);
  EXPECT_EQ(callbacks_.notifications[0].method, "notifications/initialized");
}

TEST_F(StreamableHttpPostTest, AGarbageBodyComesBackAsAnIdLessError) {
  startServer();
  sendPost("this is not json");

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << response;

  const std::string body = bodyOf(response);
  EXPECT_EQ(declaredLength(response), static_cast<long>(body.size()))
      << response;
  // Nothing was understood well enough to know whose request it was, so
  // there is no id to quote back.
  EXPECT_NE(body.find("\"id\":null"), std::string::npos) << body;
  EXPECT_NE(body.find("\"jsonrpc\":\"2.0\""), std::string::npos) << body;
  EXPECT_TRUE(callbacks_.requests.empty());
}

TEST_F(StreamableHttpPostTest, TwoMessagesInOneBodyRunNeitherOfThem) {
  startServer();
  sendPost(std::string(kInitialize) + "\n" + kInitialize);

  const std::string response = readResponse();

  EXPECT_EQ(response.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << response;
  EXPECT_TRUE(callbacks_.requests.empty())
      << "one HTTP response cannot answer two messages, so neither may run";
}

TEST_F(StreamableHttpPostTest, TwoPostsOnOneConnectionAreBothAnswered) {
  // The point of getting the framing right: the client can tell where the
  // first response ended, so the connection stays usable for a second.
  startServer();

  sendPost(kInitialize);
  const std::string first = readResponse();
  ASSERT_EQ(first.find("HTTP/1.1 200 OK\r\n"), 0u) << first;
  EXPECT_EQ(declaredLength(first), static_cast<long>(bodyOf(first).size()))
      << first;

  sendPost(
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");
  const std::string second = readResponse();

  ASSERT_EQ(second.find("HTTP/1.1 200 OK\r\n"), 0u) << second;
  EXPECT_NE(bodyOf(second).find("\"id\":2"), std::string::npos) << second;

  ASSERT_EQ(callbacks_.requests.size(), 2u);
  EXPECT_EQ(callbacks_.requests[1].method, "tools/list");
}

TEST_F(StreamableHttpPostTest, TheSessionHeaderTravelsWithTheMessage) {
  startServer();
  sendPost(kInitialize, "Mcp-Session-Id: session-7\r\n");

  ASSERT_FALSE(readResponse().empty());
  ASSERT_EQ(callbacks_.sessions.size(), 1u);
  EXPECT_EQ(callbacks_.sessions[0], "session-7");
}

}  // namespace
}  // namespace filter
}  // namespace mcp
