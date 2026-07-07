/**
 * Integration test: stdio server-push reaches the client.
 *
 * Since the per-message dispatch context supplies the origin connection
 * for every transport, stdio sessions are keyed on the pipe connection.
 * sendNotificationToSession must therefore route a connection-keyed
 * session through the connection manager that OWNS that connection —
 * treating every connection-keyed session as "plain HTTP, no push
 * channel" silently regresses stdio server push (the issue #240
 * scenario: notifications/resources/updated never reaches a stdio
 * subscriber, and the only evidence is a per-call error return the
 * caller usually ignores).
 *
 * The test drives a real McpServer over real pipes: the server's stdio
 * transport is pointed at pipe fds (use_bridge=false, the test-pipe
 * mode), a subscribe request goes in on the "stdin" pipe, and the
 * resource-update notification must come back on the "stdout" pipe.
 */

#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

// The server-side stdio chain uses length-prefixed framing (4-byte
// big-endian), the same framing McpConnectionManager defaults to.
std::string frame(const std::string& json) {
  std::string framed;
  uint32_t len = static_cast<uint32_t>(json.size());
  framed.push_back(static_cast<char>((len >> 24) & 0xff));
  framed.push_back(static_cast<char>((len >> 16) & 0xff));
  framed.push_back(static_cast<char>((len >> 8) & 0xff));
  framed.push_back(static_cast<char>(len & 0xff));
  framed += json;
  return framed;
}

class StdioServerPushTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // client_to_server: test writes requests, server reads them as stdin.
    // server_to_client: server writes replies/pushes, test reads them.
    ASSERT_EQ(::pipe(client_to_server_), 0);
    ASSERT_EQ(::pipe(server_to_client_), 0);

    server::McpServerConfig config;
    config.server_name = "stdio-push-test-server";
    config.server_version = "0.0.1";
    config.supported_transports = {TransportType::Stdio};
    config.num_workers = 1;

    transport::StdioTransportSocketConfig stdio_config;
    stdio_config.stdin_fd = client_to_server_[0];
    stdio_config.stdout_fd = server_to_client_[1];
    stdio_config.non_blocking = true;
    stdio_config.use_bridge = false;  // test pipes, no bridge threads
    config.stdio_transport_config = mcp::make_optional(stdio_config);

    server_ = server::createMcpServer(config);
    ASSERT_NE(server_, nullptr);

    auto listen_result = server_->listen("stdio://");
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(listen_result))
        << "McpServer::listen failed for stdio";

    server_thread_ = std::thread([this]() { server_->run(); });

    // Keep the test-side read end non-blocking so read polls can time out.
    ::fcntl(server_to_client_[0], F_SETFL, O_NONBLOCK);
  }

  void TearDown() override {
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
    // The transport closed the fds it owned; close the test-side ends.
    ::close(client_to_server_[1]);
    ::close(server_to_client_[0]);
  }

  bool writeToServer(const std::string& bytes) {
    return ::write(client_to_server_[1], bytes.data(), bytes.size()) ==
           static_cast<ssize_t>(bytes.size());
  }

  // Accumulate framed bytes from the server until the payload of some
  // frame contains `needle` or the budget elapses. Returns everything
  // read (frames included) for diagnostics.
  std::string readUntilContains(const std::string& needle,
                                std::chrono::milliseconds budget) {
    std::string received;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    char buf[4096];
    while (std::chrono::steady_clock::now() < deadline) {
      ssize_t n = ::read(server_to_client_[0], buf, sizeof(buf));
      if (n > 0) {
        received.append(buf, static_cast<size_t>(n));
        if (received.find(needle) != std::string::npos) {
          return received;
        }
      } else if (n == 0) {
        return received;  // EOF
      }
      std::this_thread::sleep_for(10ms);
    }
    return received;
  }

  int client_to_server_[2]{-1, -1};
  int server_to_client_[2]{-1, -1};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
};

// Subscribe over stdio, then push a resource update: the notification
// must arrive on the stdio client's pipe. With connection-keyed stdio
// sessions misclassified as "no push channel", the subscribe response
// still arrives but the notification never does.
TEST_F(StdioServerPushTest, ResourceUpdateReachesStdioSubscriber) {
  const std::string kUri = "test://resource/stdio-watched";

  ASSERT_TRUE(writeToServer(frame(
      R"({"jsonrpc":"2.0","id":1,"method":"resources/subscribe","params":{"uri":")" +
      kUri + R"("}})")));

  std::string subscribe_reply = readUntilContains("\"id\":1", 5s);
  ASSERT_NE(subscribe_reply.find("\"id\":1"), std::string::npos)
      << "resources/subscribe was never answered on the stdio pipe; got: "
      << subscribe_reply;

  // The subscription is recorded under the session keyed on the stdio
  // connection. The push must route back through the stdio manager.
  server_->notifyResourceUpdate(kUri);

  std::string pushed = readUntilContains("notifications/resources/updated", 5s);
  EXPECT_NE(pushed.find("notifications/resources/updated"), std::string::npos)
      << "resource-update notification never reached the stdio client; "
         "connection-keyed stdio session was likely classified as having "
         "no push channel. Received instead: "
      << pushed;
  EXPECT_NE(pushed.find(kUri), std::string::npos);
}

}  // namespace
}  // namespace mcp
