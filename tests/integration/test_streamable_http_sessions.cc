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
#include <cstdlib>
#include <functional>
#include <future>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/http_security_filter.h"
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

/** How the server under test is configured. */
struct ServerOptions {
  // False builds a stateless server, which mints no session and believes
  // no session id it is sent.
  bool keep_sessions = true;
  bool allow_termination = true;
  bool enable_get_stream = true;
  std::chrono::milliseconds timeout{300000};
  // Empty means no opinion about protocol revisions, refusing none.
  std::vector<std::string> protocol_versions;
  // True resolves each request's caller from a header, so a test can
  // present one caller's session as another.
  bool callers_differ = false;
};

class StreamableHttpSessionsTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    stopOtherWorker();
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

  void startServer(ServerOptions options = ServerOptions()) {
    executeInDispatcher(
        [&]() { factory_ = makeFactory(*dispatcher_, options); });
    connect(conn_, peer_, factory_);
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
    config.allow_client_termination = options.allow_termination;
    config.enable_get_stream = options.enable_get_stream;
    config.session_timeout = options.timeout;
    config.protocol_versions = options.protocol_versions;
    factory->setSessionConfig(config);
    factory->setSecurityConfig(config);
    if (options.callers_differ) {
      factory->setAuthCallback([](const RequestHeadersView& headers) {
        const std::string caller = headers.get("x-test-caller");
        return AuthResult::allow(caller.empty() ? "anonymous" : caller);
      });
    }
    return factory;
  }

  /** Bring up one more client, so two can be told apart on the wire. */
  void connectSecondClient() { connect(second_conn_, second_peer_, factory_); }

  void connect(std::unique_ptr<network::ServerConnection>& conn,
               network::IoHandlePtr& peer,
               const std::shared_ptr<HttpSseFilterChainFactory>& factory) {
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
      ASSERT_TRUE(factory->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();

      peer = std::move(pair.second);
    });
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

  void sendRequest(network::IoHandlePtr& peer,
                   const std::string& method,
                   const std::string& body,
                   const std::string& extra_headers = std::string()) {
    const std::string request = requestBytes(method, body, extra_headers);
    executeInDispatcher([&]() {
      OwnedBuffer buffer;
      buffer.add(request);
      auto result = peer->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
  }

  void sendPost(network::IoHandlePtr& peer,
                const std::string& body,
                const std::string& extra_headers = std::string()) {
    sendRequest(peer, "POST", body, extra_headers);
  }

  void sendDelete(network::IoHandlePtr& peer,
                  const std::string& extra_headers = std::string()) {
    sendRequest(peer, "DELETE", std::string(), extra_headers);
  }

  /**
   * Bring up a second listener on a thread of its own, serving the same
   * sessions as the first. This is what a deployment with more than one
   * worker looks like: a session belongs to the thread that accepted its
   * initialize, and the other thread has to cross over to reach it.
   */
  void startOtherWorker() {
    // The base fixture's dispatcher factory, named explicitly because
    // this fixture's own factory_ is the filter chain's.
    other_dispatcher_ =
        test::RealIoTestBase::factory_->createDispatcher("other_worker");
    std::promise<void> ready;
    auto ready_future = ready.get_future();
    other_thread_ = std::thread([this, &ready]() {
      other_dispatcher_->post([&ready]() { ready.set_value(); });
      other_dispatcher_->run(event::RunType::RunUntilExit);
    });
    ready_future.wait();

    ServerOptions options;
    runOnOtherWorker([&]() {
      other_factory_ = makeFactory(*other_dispatcher_, options);
      other_factory_->setSessionManager(factory_->sessionManagerShared());

      auto pair = createSocketPair();
      auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<network::ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      auto transport = std::make_unique<network::RawBufferTransportSocket>();
      other_stream_info_ = std::make_shared<stream_info::StreamInfoImpl>();

      other_conn_ = network::ConnectionImpl::createServerConnection(
          *other_dispatcher_, std::move(socket), std::move(transport),
          *other_stream_info_);
      auto* impl = static_cast<network::ConnectionImpl*>(other_conn_.get());
      ASSERT_TRUE(other_factory_->createFilterChain(impl->filterManager()));
      impl->filterManager().initializeReadFilters();

      other_peer_ = std::move(pair.second);
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

  /** Send on the second worker's connection and read what comes back. */
  std::string roundTripOnOtherWorker(const std::string& method,
                                     const std::string& body,
                                     const std::string& extra_headers) {
    const std::string request = requestBytes(method, body, extra_headers);
    runOnOtherWorker([&]() {
      OwnedBuffer buffer;
      buffer.add(request);
      auto result = other_peer_->write(buffer);
      ASSERT_TRUE(result.ok()) << "peer write failed: errno=" << errno;
    });
    return readResponse(other_peer_);
  }

  void stopOtherWorker() {
    if (!other_dispatcher_) {
      return;
    }
    runOnOtherWorker([&]() {
      if (other_conn_) {
        other_conn_->close(network::ConnectionCloseType::NoFlush);
      }
      other_conn_.reset();
      other_factory_.reset();
    });
    other_peer_.reset();
    other_dispatcher_->exit();
    if (other_thread_.joinable()) {
      other_thread_.join();
    }
    other_dispatcher_.reset();
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

  /**
   * Keep reading until `wanted` status lines have arrived or the budget
   * runs out. Pipelined answers trickle, so one read gap is not the end.
   */
  std::string drainResponses(network::IoHandlePtr& peer,
                             size_t wanted,
                             std::chrono::milliseconds budget = 5000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buffer;
      auto result = peer->read(buffer, 4096);
      if (result.ok() && *result > 0) {
        out.append(buffer.toString());
        if (countStatusLines(out) >= wanted) {
          return out;
        }
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }

  static size_t countStatusLines(const std::string& wire) {
    size_t count = 0;
    size_t at = 0;
    while ((at = wire.find("HTTP/1.1 ", at)) != std::string::npos) {
      ++count;
      at += 1;
    }
    return count;
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

  /** The status line's code, or 0 when there is not one. */
  static int statusOf(const std::string& response) {
    if (response.compare(0, 9, "HTTP/1.1 ") != 0) {
      return 0;
    }
    return std::atoi(response.c_str() + 9);
  }

  /** The value of a header, or empty when it was not sent. */
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
  event::DispatcherPtr other_dispatcher_;
  std::thread other_thread_;
  std::shared_ptr<HttpSseFilterChainFactory> other_factory_;
  std::unique_ptr<network::ServerConnection> other_conn_;
  network::IoHandlePtr other_peer_;
  std::shared_ptr<stream_info::StreamInfoImpl> other_stream_info_;
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

TEST_F(StreamableHttpSessionsTest, ArrivingWithoutOneIsARefusal) {
  startServer();
  sendPost(peer_, kListTools);

  const std::string response = readResponse(peer_);

  // Every request after initialize has to say which conversation it
  // belongs to, and nothing here does.
  EXPECT_EQ(response.find("HTTP/1.1 400 Bad Request\r\n"), 0u) << response;
  EXPECT_TRUE(sessionIdOf(response).empty()) << response;
  EXPECT_TRUE(callbacks_.sessions.empty())
      << "the refused request must not have reached a handler";
}

TEST_F(StreamableHttpSessionsTest, AStatelessServerHandsNothingBack) {
  ServerOptions stateless;
  stateless.keep_sessions = false;
  startServer(stateless);
  sendPost(peer_, kInitialize);

  const std::string response = readResponse(peer_);

  EXPECT_TRUE(sessionIdOf(response).empty()) << response;
  ASSERT_EQ(callbacks_.sessions.size(), 1u);
  EXPECT_EQ(callbacks_.sessions[0], "");
}

TEST_F(StreamableHttpSessionsTest, AStatelessServerDisregardsAnInventedId) {
  ServerOptions stateless;
  stateless.keep_sessions = false;
  startServer(stateless);
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

// ── Refusals ───────────────────────────────────────────────────────────────

TEST_F(StreamableHttpSessionsTest, AnIdThisServerNeverIssuedIsNotFound) {
  startServer();
  sendPost(peer_, kListTools,
           "Mcp-Session-Id: 0123456789abcdef0123456789abcdef\r\n");

  const std::string response = readResponse(peer_);

  // 404 rather than 403: the status a client is told to start again on.
  EXPECT_EQ(statusOf(response), 404) << response;
  EXPECT_TRUE(callbacks_.sessions.empty());
}

TEST_F(StreamableHttpSessionsTest, ASessionIsNoUseToAnotherCaller) {
  ServerOptions options;
  options.callers_differ = true;
  startServer(options);

  sendPost(peer_, kInitialize, "X-Test-Caller: alice\r\n");
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));

  std::chrono::steady_clock::time_point stamped;
  executeInDispatcher([&]() {
    auto* session = factory_->sessionManager()->find(id);
    ASSERT_NE(session, nullptr);
    stamped = session->last_activity;
  });

  sendPost(peer_, kListTools,
           "X-Test-Caller: mallory\r\nMcp-Session-Id: " + id + "\r\n");
  const std::string response = readResponse(peer_);

  EXPECT_EQ(statusOf(response), 403) << response;
  ASSERT_EQ(callbacks_.sessions.size(), 1u)
      << "the refused request must not have reached a handler";

  executeInDispatcher([&]() {
    auto* session = factory_->sessionManager()->find(id);
    ASSERT_NE(session, nullptr);
    // A caller who may not use the session may not keep it alive either,
    // or an unauthorized prod would postpone expiry indefinitely.
    EXPECT_EQ(session->last_activity, stamped);
  });
}

TEST_F(StreamableHttpSessionsTest, AnIdleSessionStopsWorking) {
  ServerOptions options;
  options.timeout = 60ms;
  startServer(options);

  sendPost(peer_, kInitialize);
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));

  std::this_thread::sleep_for(300ms);

  sendPost(peer_, kListTools, "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(readResponse(peer_)), 404);
}

// ── Ending a session ───────────────────────────────────────────────────────

TEST_F(StreamableHttpSessionsTest, AClientCanEndItsOwnSession) {
  startServer();
  sendPost(peer_, kInitialize);
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));

  sendDelete(peer_, "Mcp-Session-Id: " + id + "\r\n");
  const std::string ended = readResponse(peer_);
  EXPECT_EQ(statusOf(ended), 204) << ended;

  // Everything about that id is over: using it and ending it again both
  // answer the same way, because there is nothing there either time.
  sendPost(peer_, kListTools, "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(readResponse(peer_)), 404);

  sendDelete(peer_, "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(readResponse(peer_)), 404);
}

TEST_F(StreamableHttpSessionsTest, EndingASessionHasToNameOne) {
  startServer();
  sendDelete(peer_);

  EXPECT_EQ(statusOf(readResponse(peer_)), 400);
}

TEST_F(StreamableHttpSessionsTest, AServerThatForbidsItSaysWhatItDoesServe) {
  ServerOptions options;
  options.allow_termination = false;
  startServer(options);

  sendDelete(peer_, "Mcp-Session-Id: whatever\r\n");
  const std::string response = readResponse(peer_);

  EXPECT_EQ(statusOf(response), 405) << response;
  // Rendered from the route table rather than written out, so it names
  // what is actually served and nothing else.
  EXPECT_EQ(headerOf(response, "Allow"), "GET, OPTIONS, POST") << response;
}

TEST_F(StreamableHttpSessionsTest, AServerThatAllowsItAdvertisesIt) {
  // The mirror of the test above: with the event stream off instead, GET
  // is the refusal and DELETE is what shows up in Allow. Each optional
  // method appears exactly when it is served, because the header is
  // rendered from the routes rather than written anywhere.
  ServerOptions options;
  options.enable_get_stream = false;
  startServer(options);

  sendRequest(peer_, "GET", "");
  const std::string response = readResponse(peer_);

  EXPECT_EQ(statusOf(response), 405) << response;
  EXPECT_EQ(headerOf(response, "Allow"), "DELETE, OPTIONS, POST") << response;
}

TEST_F(StreamableHttpSessionsTest, AStatelessServerHasNoSessionToEnd) {
  ServerOptions stateless;
  stateless.keep_sessions = false;
  startServer(stateless);

  sendDelete(peer_, "Mcp-Session-Id: whatever\r\n");
  const std::string response = readResponse(peer_);

  // Neither ending a session nor opening a stream is served here: both
  // are a session's, and this server keeps none.
  EXPECT_EQ(statusOf(response), 405) << response;
  EXPECT_EQ(headerOf(response, "Allow"), "OPTIONS, POST") << response;
}

// ── Protocol revision ──────────────────────────────────────────────────────

TEST_F(StreamableHttpSessionsTest, ARevisionThisServerCannotServeIsRefused) {
  ServerOptions options;
  options.keep_sessions = false;
  options.protocol_versions = {"2025-11-25", "2025-06-18"};
  startServer(options);

  sendPost(peer_, kListTools, "MCP-Protocol-Version: 1999-01-01\r\n");
  const std::string response = readResponse(peer_);

  EXPECT_EQ(statusOf(response), 400) << response;
  EXPECT_NE(response.find("1999-01-01"), std::string::npos) << response;
  EXPECT_TRUE(callbacks_.sessions.empty());
}

TEST_F(StreamableHttpSessionsTest, NoRevisionHeaderIsStillServed) {
  ServerOptions options;
  options.keep_sessions = false;
  options.protocol_versions = {"2025-11-25", "2025-06-18"};
  startServer(options);

  sendPost(peer_, kListTools);

  EXPECT_EQ(statusOf(readResponse(peer_)), 200);
  ASSERT_EQ(callbacks_.sessions.size(), 1u);
}

// ── Reaching a session from another worker ─────────────────────────────────

TEST_F(StreamableHttpSessionsTest, ASessionIsUsableFromAnotherWorker) {
  // Two listeners on two threads over one set of sessions, which is what a
  // deployment with more than one worker looks like. The session belongs
  // to the thread that accepted its initialize, so everything the second
  // listener does with it has to cross over and come back.
  startServer();

  startOtherWorker();

  sendPost(peer_, kInitialize);
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));

  const std::string used = roundTripOnOtherWorker(
      "POST", kListTools, "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(used), 200) << used;

  const std::string ended = roundTripOnOtherWorker(
      "DELETE", std::string(), "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(ended), 204) << ended;

  EXPECT_FALSE(factory_->sessionManager()->known(id))
      << "ending it on one worker must end it everywhere";

  sendPost(peer_, kListTools, "Mcp-Session-Id: " + id + "\r\n");
  EXPECT_EQ(statusOf(readResponse(peer_)), 404);
}

TEST_F(StreamableHttpSessionsTest, TwoWorkersMayUseOneSessionAtOnce) {
  startServer();
  startOtherWorker();

  sendPost(peer_, kInitialize);
  const std::string id = sessionIdOf(readResponse(peer_));
  ASSERT_TRUE(looksLikeASessionId(id));
  const std::string with_id = "Mcp-Session-Id: " + id + "\r\n";

  // Fired from both threads without waiting in between, so the second
  // worker's hops overlap the first worker's direct reads of the same
  // session. Every one of them is answered and none of them deadlocks.
  const size_t kRounds = 5;
  for (size_t i = 0; i < kRounds; ++i) {
    sendPost(peer_, kListTools, with_id);
    runOnOtherWorker([&]() {
      OwnedBuffer buffer;
      buffer.add(requestBytes("POST", kListTools, with_id));
      auto result = other_peer_->write(buffer);
      ASSERT_TRUE(result.ok());
    });
  }

  size_t answered = 0;
  for (const std::string& wire :
       {drainResponses(peer_, kRounds), drainResponses(other_peer_, kRounds)}) {
    size_t at = 0;
    while ((at = wire.find("HTTP/1.1 200 OK", at)) != std::string::npos) {
      ++answered;
      at += 1;
    }
    EXPECT_EQ(wire.find("HTTP/1.1 4"), std::string::npos)
        << "a request was refused: " << wire;
  }
  EXPECT_EQ(answered, static_cast<size_t>(2 * kRounds));
}

}  // namespace
}  // namespace filter
}  // namespace mcp
