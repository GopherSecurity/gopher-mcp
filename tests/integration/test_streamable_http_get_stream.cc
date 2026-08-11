/**
 * @file test_streamable_http_get_stream.cc
 * @brief Wire-level tests for the standalone event stream
 *
 * The stream a client opens and leaves open is the only channel this
 * transport has for anything the server says on its own initiative. What
 * matters about it is not that a message arrives — it is that it arrives on
 * exactly one stream, so every routing test here also asserts on the stream
 * that must have received nothing.
 *
 * Real TCP socketpairs, following test_streamable_http_sessions.cc.
 */

#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
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

/** Answers requests and records anything the client sends back. */
class StreamTestCallbacks : public McpProtocolCallbacks {
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

  void onResponse(const jsonrpc::Response& response) override {
    responses.push_back(response);
  }

  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}

  std::vector<jsonrpc::Request> requests;
  std::vector<jsonrpc::Notification> notifications;
  std::vector<jsonrpc::Response> responses;
  std::vector<std::string> sessions;
};

/** How the server under test is configured. */
struct ServerOptions {
  bool keep_sessions = true;
  bool enable_get_stream = true;
  size_t max_streams = 4;
  size_t queue_limit = 256;
  std::chrono::milliseconds keepalive{30000};
};

/** One client: a connection and the socket the test reads from. */
struct Client {
  std::unique_ptr<network::ServerConnection> conn;
  network::IoHandlePtr peer;
  std::shared_ptr<stream_info::StreamInfo> info;
};

class StreamableHttpGetStreamTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    stopOtherWorker();
    executeInDispatcher([&]() {
      for (auto& client : clients_) {
        if (client->conn) {
          client->conn->close(network::ConnectionCloseType::NoFlush);
        }
        client->conn.reset();
      }
      clients_.clear();
      factory_.reset();
    });
    test::RealIoTestBase::TearDown();
  }

  void startServer(ServerOptions options = ServerOptions()) {
    options_ = options;
    executeInDispatcher(
        [&]() { factory_ = makeFactory(*dispatcher_, options_); });
  }

  std::shared_ptr<HttpSseFilterChainFactory> makeFactory(
      event::Dispatcher& dispatcher, const ServerOptions& options) {
    auto factory =
        std::make_shared<HttpSseFilterChainFactory>(dispatcher, callbacks_,
                                                    /*is_server=*/true,
                                                    /*http_path=*/"/mcp",
                                                    /*http_host=*/"localhost",
                                                    /*use_sse=*/true,
                                                    /*sse_path=*/"/sse",
                                                    /*rpc_path=*/"/mcp");
    transport::StreamableHttpConfig config;
    config.enable_sessions = options.keep_sessions;
    config.enable_get_stream = options.enable_get_stream;
    config.max_get_streams_per_session = options.max_streams;
    config.replay_buffer_events = options.queue_limit;
    config.keepalive_interval = options.keepalive;
    factory->setSessionConfig(config);
    factory->setSecurityConfig(config);
    return factory;
  }

  /** Bring up another client on the main worker. */
  Client* connect() { return connectTo(*dispatcher_, factory_, clients_); }

  Client* connectTo(event::Dispatcher& dispatcher,
                    const std::shared_ptr<HttpSseFilterChainFactory>& factory,
                    std::vector<std::unique_ptr<Client>>& into) {
    std::unique_ptr<Client> client(new Client());
    Client* raw = client.get();
    auto build = [&]() {
      auto pair = createSocketPair();
      auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      auto transport = std::make_unique<network::RawBufferTransportSocket>();
      raw->info = std::make_shared<stream_info::StreamInfoImpl>();

      raw->conn = network::ConnectionImpl::createServerConnection(
          dispatcher, std::move(socket), std::move(transport), *raw->info);
      auto* impl = static_cast<network::ConnectionImpl*>(raw->conn.get());
      ASSERT_TRUE(factory->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();
      raw->peer = std::move(pair.second);
    };
    if (&dispatcher == dispatcher_.get()) {
      executeInDispatcher(build);
    } else {
      runOnOtherWorker(build);
    }
    into.push_back(std::move(client));
    return raw;
  }

  static std::string requestBytes(const std::string& method,
                                  const std::string& body,
                                  const std::string& extra_headers) {
    return method +
           " /mcp HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n" +
           extra_headers + "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
  }

  void send(Client& client,
            const std::string& method,
            const std::string& body,
            const std::string& extra_headers = std::string()) {
    const std::string request = requestBytes(method, body, extra_headers);
    auto write = [&]() {
      OwnedBuffer buffer;
      buffer.add(request);
      auto result = client.peer->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    };
    if (client.conn->dispatcher().isThreadSafe()) {
      write();
    } else if (onOtherWorker(client)) {
      runOnOtherWorker(write);
    } else {
      executeInDispatcher(write);
    }
  }

  bool onOtherWorker(const Client& client) const {
    return other_dispatcher_ &&
           &client.conn->dispatcher() == other_dispatcher_.get();
  }

  /** Read whatever has arrived, giving it a moment to. */
  std::string read(Client& client, std::chrono::milliseconds budget = 500ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = client.peer->read(buffer, 4096);
      if (result.ok() && *result > 0) {
        out.append(buffer.toString());
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }

  /** Read until something arrives, or the budget runs out. */
  std::string readSomething(Client& client,
                            std::chrono::milliseconds budget = 2000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = client.peer->read(buffer, 4096);
      if (result.ok() && *result > 0) {
        out.append(buffer.toString());
        return out;
      }
      std::this_thread::sleep_for(5ms);
    }
    return out;
  }

  /** Introduce a client and hand back the session it was given. */
  std::string initialize(Client& client) {
    send(client, "POST", kInitialize);
    const std::string response = readSomething(client);
    const std::string name = "\r\nMcp-Session-Id: ";
    const size_t at = response.find(name);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t start = at + name.size();
    return response.substr(start, response.find("\r\n", start) - start);
  }

  /** Say something to a session that answers no request of its own. */
  void push(const std::string& id, const std::string& payload) {
    executeInDispatcher([&]() {
      auto* sessions = factory_->sessionManager();
      ASSERT_NE(sessions, nullptr);
      auto* session = sessions->find(id);
      ASSERT_NE(session, nullptr);
      sessions->routeUnsolicited(*session, payload);
    });
  }

  static int statusOf(const std::string& response) {
    if (response.compare(0, 9, "HTTP/1.1 ") != 0) {
      return 0;
    }
    return std::atoi(response.c_str() + 9);
  }

  static std::string headerOf(const std::string& response,
                              const std::string& name) {
    const std::string needle = "\r\n" + name + ": ";
    const size_t at = response.find(needle);
    if (at == std::string::npos) {
      return std::string();
    }
    const size_t start = at + needle.size();
    return response.substr(start, response.find("\r\n", start) - start);
  }

  // ── A second listener, on a thread of its own, over the same sessions ──

  void startOtherWorker() {
    other_dispatcher_ =
        test::RealIoTestBase::factory_->createDispatcher("other_worker");
    std::promise<void> ready;
    auto ready_future = ready.get_future();
    other_thread_ = std::thread([this, &ready]() {
      other_dispatcher_->post([&ready]() { ready.set_value(); });
      other_dispatcher_->run(event::RunType::RunUntilExit);
    });
    ready_future.wait();

    runOnOtherWorker([&]() {
      other_factory_ = makeFactory(*other_dispatcher_, options_);
      other_factory_->setSessionManager(factory_->sessionManagerShared());
    });
  }

  void runOnOtherWorker(const std::function<void()>& fn) {
    std::promise<void> done;
    auto done_future = done.get_future();
    other_dispatcher_->post([&fn, &done]() {
      fn();
      done.set_value();
    });
    ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready);
  }

  void stopOtherWorker() {
    if (!other_dispatcher_) {
      return;
    }
    runOnOtherWorker([&]() {
      for (auto& client : other_clients_) {
        if (client->conn) {
          client->conn->close(network::ConnectionCloseType::NoFlush);
        }
        client->conn.reset();
      }
      other_clients_.clear();
      other_factory_.reset();
    });
    other_dispatcher_->exit();
    if (other_thread_.joinable()) {
      other_thread_.join();
    }
    other_dispatcher_.reset();
  }

  Client* connectOnOtherWorker() {
    return connectTo(*other_dispatcher_, other_factory_, other_clients_);
  }

  static constexpr const char* kInitialize =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  static constexpr const char* kListTools =
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}";

  StreamTestCallbacks callbacks_;
  ServerOptions options_;
  std::shared_ptr<HttpSseFilterChainFactory> factory_;
  std::vector<std::unique_ptr<Client>> clients_;

  event::DispatcherPtr other_dispatcher_;
  std::thread other_thread_;
  std::shared_ptr<HttpSseFilterChainFactory> other_factory_;
  std::vector<std::unique_ptr<Client>> other_clients_;
};

constexpr const char* StreamableHttpGetStreamTest::kInitialize;
constexpr const char* StreamableHttpGetStreamTest::kListTools;

const char kSseAccept[] = "Accept: text/event-stream\r\n";

// ── Opening one ────────────────────────────────────────────────────────────

TEST_F(StreamableHttpGetStreamTest, AGetWithASessionOpensAStream) {
  startServer();
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  const std::string opened = readSomething(*client);

  EXPECT_EQ(statusOf(opened), 200) << opened;
  EXPECT_EQ(headerOf(opened, "Content-Type"), "text/event-stream") << opened;
  EXPECT_EQ(headerOf(opened, "Transfer-Encoding"), "chunked") << opened;
  // The deprecated transport announces a callback URL as its first event.
  // This one has no separate endpoint, and a client that believed one
  // would post its requests somewhere that does not exist.
  EXPECT_EQ(opened.find("endpoint"), std::string::npos) << opened;
}

TEST_F(StreamableHttpGetStreamTest, ADisabledStreamIsRefusedWithWhatIsServed) {
  ServerOptions options;
  options.enable_get_stream = false;
  startServer(options);
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  const std::string response = readSomething(*client);

  EXPECT_EQ(statusOf(response), 405) << response;
  EXPECT_EQ(headerOf(response, "Allow"), "DELETE, OPTIONS, POST") << response;
}

TEST_F(StreamableHttpGetStreamTest, AClientThatCannotReadOneIsToldSo) {
  startServer();
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "",
       "Accept: application/json\r\nMcp-Session-Id: " + id + "\r\n");

  EXPECT_EQ(statusOf(readSomething(*client)), 406);
}

TEST_F(StreamableHttpGetStreamTest, AStreamHasToNameASessionThatExists) {
  startServer();
  Client* nameless = connect();
  send(*nameless, "GET", "", kSseAccept);
  EXPECT_EQ(statusOf(readSomething(*nameless)), 400);

  Client* invented = connect();
  send(*invented, "GET", "",
       kSseAccept +
           std::string("Mcp-Session-Id: 0123456789abcdef0123456789abcdef\r\n"));
  EXPECT_EQ(statusOf(readSomething(*invented)), 404);
}

// ── Where a message goes ───────────────────────────────────────────────────

TEST_F(StreamableHttpGetStreamTest, TwoStreamsOpenAndOnlyTheNewerIsUsed) {
  startServer();
  Client* first = connect();
  const std::string id = initialize(*first);
  ASSERT_FALSE(id.empty());

  const std::string with_id = kSseAccept + ("Mcp-Session-Id: " + id + "\r\n");
  send(*first, "GET", "", with_id);
  ASSERT_EQ(statusOf(readSomething(*first)), 200);

  Client* second = connect();
  send(*second, "GET", "", with_id);
  ASSERT_EQ(statusOf(readSomething(*second)), 200);

  push(id, "{\"method\":\"notifications/one\"}");

  EXPECT_NE(readSomething(*second).find("notifications/one"),
            std::string::npos);
  // The one that must receive nothing is the point of the test: putting a
  // message on both streams would be a broadcast, which is forbidden here.
  EXPECT_TRUE(read(*first).empty()) << "the older stream received something";
}

TEST_F(StreamableHttpGetStreamTest, TheOlderStreamBecomesTheTargetAgain) {
  startServer();
  Client* first = connect();
  const std::string id = initialize(*first);
  ASSERT_FALSE(id.empty());
  const std::string with_id = kSseAccept + ("Mcp-Session-Id: " + id + "\r\n");

  send(*first, "GET", "", with_id);
  ASSERT_EQ(statusOf(readSomething(*first)), 200);
  Client* second = connect();
  send(*second, "GET", "", with_id);
  ASSERT_EQ(statusOf(readSomething(*second)), 200);

  executeInDispatcher([&]() {
    second->conn->close(network::ConnectionCloseType::NoFlush);
    second->conn.reset();
  });
  std::this_thread::sleep_for(100ms);

  push(id, "{\"method\":\"notifications/two\"}");

  EXPECT_NE(readSomething(*first).find("notifications/two"), std::string::npos);
}

TEST_F(StreamableHttpGetStreamTest, AStreamBeyondTheCapIsRefused) {
  ServerOptions options;
  options.max_streams = 1;
  startServer(options);
  Client* first = connect();
  const std::string id = initialize(*first);
  ASSERT_FALSE(id.empty());
  const std::string with_id = kSseAccept + ("Mcp-Session-Id: " + id + "\r\n");

  send(*first, "GET", "", with_id);
  ASSERT_EQ(statusOf(readSomething(*first)), 200);

  Client* second = connect();
  send(*second, "GET", "", with_id);
  EXPECT_EQ(statusOf(readSomething(*second)), 429);
}

TEST_F(StreamableHttpGetStreamTest, APushDoesNotLandOnAnAnsweringStream) {
  startServer();
  Client* streaming = connect();
  const std::string id = initialize(*streaming);
  ASSERT_FALSE(id.empty());

  send(*streaming, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  ASSERT_EQ(statusOf(readSomething(*streaming)), 200);

  // A second connection with a request in flight, whose answer is its own
  // business and nobody else's.
  Client* asking = connect();
  send(*asking, "POST", kListTools, "Mcp-Session-Id: " + id + "\r\n");
  const std::string answer = readSomething(*asking);
  ASSERT_EQ(statusOf(answer), 200) << answer;

  push(id, "{\"method\":\"notifications/three\"}");

  EXPECT_NE(readSomething(*streaming).find("notifications/three"),
            std::string::npos);
  EXPECT_TRUE(read(*asking).empty())
      << "an unsolicited message reached a request's own connection";
}

TEST_F(StreamableHttpGetStreamTest, WhatWasSaidWhileAwayArrivesOnConnecting) {
  startServer();
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  push(id, "{\"method\":\"notifications/early\"}");

  send(*client, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  const std::string opened = readSomething(*client);
  ASSERT_EQ(statusOf(opened), 200) << opened;

  const std::string body =
      opened.find("notifications/early") != std::string::npos
          ? opened
          : readSomething(*client);
  EXPECT_NE(body.find("notifications/early"), std::string::npos) << body;

  // Handed over rather than kept: a stream opened afterwards is not owed
  // it a second time.
  Client* later = connect();
  send(*later, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  ASSERT_EQ(statusOf(readSomething(*later)), 200);
  EXPECT_EQ(read(*later).find("notifications/early"), std::string::npos);
}

TEST_F(StreamableHttpGetStreamTest, TheQueueIsBoundedRatherThanUnlimited) {
  ServerOptions options;
  options.queue_limit = 2;
  startServer(options);
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  push(id, "{\"method\":\"notifications/a\"}");
  push(id, "{\"method\":\"notifications/b\"}");
  push(id, "{\"method\":\"notifications/c\"}");

  send(*client, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  const std::string body = readSomething(*client) + read(*client);

  // The oldest went, which is what a bound on memory means.
  EXPECT_EQ(body.find("notifications/a"), std::string::npos) << body;
  EXPECT_NE(body.find("notifications/b"), std::string::npos) << body;
  EXPECT_NE(body.find("notifications/c"), std::string::npos) << body;
}

// ── Lifetime ───────────────────────────────────────────────────────────────

TEST_F(StreamableHttpGetStreamTest, LosingTheStreamDoesNotLoseTheSession) {
  startServer();
  Client* streaming = connect();
  const std::string id = initialize(*streaming);
  ASSERT_FALSE(id.empty());

  send(*streaming, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  ASSERT_EQ(statusOf(readSomething(*streaming)), 200);

  executeInDispatcher([&]() {
    streaming->conn->close(network::ConnectionCloseType::NoFlush);
    streaming->conn.reset();
  });
  std::this_thread::sleep_for(100ms);

  // Losing a stream is neither ending the session nor cancelling anything.
  Client* asking = connect();
  send(*asking, "POST", kListTools, "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(readSomething(*asking)), 200);

  executeInDispatcher([&]() {
    auto* session = factory_->sessionManager()->find(id);
    ASSERT_NE(session, nullptr);
    // The stream itself is still the session's; only its connection went.
    ASSERT_EQ(session->streams.size(), 1u);
    EXPECT_EQ(session->streams[0]->conn, nullptr);
  });
}

TEST_F(StreamableHttpGetStreamTest, AnIdleStreamSaysSomethingMeaningless) {
  ServerOptions options;
  options.keepalive = 30ms;
  startServer(options);
  Client* client = connect();
  const std::string id = initialize(*client);
  ASSERT_FALSE(id.empty());

  send(*client, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  ASSERT_EQ(statusOf(readSomething(*client)), 200);

  const std::string idle = read(*client, 300ms);
  EXPECT_NE(idle.find(": keep-alive"), std::string::npos) << idle;
  EXPECT_EQ(idle.find("data:"), std::string::npos) << idle;
}

// ── A request the server asks ──────────────────────────────────────────────

TEST_F(StreamableHttpGetStreamTest, TheAnswerToAServerRequestComesBackByPost) {
  startServer();
  Client* streaming = connect();
  const std::string id = initialize(*streaming);
  ASSERT_FALSE(id.empty());

  send(*streaming, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  ASSERT_EQ(statusOf(readSomething(*streaming)), 200);

  push(id,
       "{\"jsonrpc\":\"2.0\",\"id\":\"srv-1\",\"method\":\"sampling/"
       "createMessage\",\"params\":{}}");
  EXPECT_NE(readSomething(*streaming).find("srv-1"), std::string::npos);

  // The answer arrives as a POST of its own, on whichever connection the
  // client cares to use — not back up the stream it was asked on.
  Client* answering = connect();
  send(*answering, "POST",
       "{\"jsonrpc\":\"2.0\",\"id\":\"srv-1\",\"result\":{}}",
       "Mcp-Session-Id: " + id + "\r\n");
  const std::string accepted = readSomething(*answering);
  EXPECT_EQ(statusOf(accepted), 202) << accepted;

  ASSERT_EQ(callbacks_.responses.size(), 1u);
  EXPECT_TRUE(holds_alternative<std::string>(callbacks_.responses[0].id));
  EXPECT_EQ(get<std::string>(callbacks_.responses[0].id), "srv-1");
}

// ── Across workers ─────────────────────────────────────────────────────────

TEST_F(StreamableHttpGetStreamTest,
       AStreamOpensOnOneWorkerForASessionOnAnother) {
  startServer();
  startOtherWorker();

  Client* owner = connect();
  const std::string id = initialize(*owner);
  ASSERT_FALSE(id.empty());

  // The stream is opened by a connection on the second worker, while the
  // session belongs to the first.
  Client* elsewhere = connectOnOtherWorker();
  send(*elsewhere, "GET", "", kSseAccept + ("Mcp-Session-Id: " + id + "\r\n"));
  ASSERT_EQ(statusOf(readSomething(*elsewhere)), 200);

  // Decided on the session's thread, written on the connection's.
  push(id, "{\"method\":\"notifications/across\"}");

  EXPECT_NE(readSomething(*elsewhere).find("notifications/across"),
            std::string::npos);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
