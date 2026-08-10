/**
 * What a Streamable HTTP client says about its session, and what it does
 * when the server says it has forgotten one.
 *
 * The peer here is scripted rather than a real McpServer, for two
 * reasons. The assertions are about what the server *received* — which
 * headers were on which request, how many initializes there were, in
 * what order — and a scripted peer records exactly that. And the
 * interesting cases are refusals: a session that has expired, one that
 * expires twice, a request refused for some other reason, a
 * notification answered with 202. Arranging those against a real server
 * means short timeouts and sleeps; arranging them here means writing the
 * status.
 *
 * Every count assertion is a count and not a search. A request sent
 * twice under two sessions and a request sent once look identical to an
 * assertion that merely finds one.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

/** One request as the server saw it. */
struct Seen {
  std::string method;  // GET / POST / DELETE
  std::string path;
  std::map<std::string, std::string> headers;  // names lower-cased
  std::string body;
  std::string rpc_method;  // "initialize", "ping", ... where there is one
  std::string rpc_id;      // as written, so it can be echoed back

  std::string header(const std::string& name) const {
    auto it = headers.find(name);
    return it == headers.end() ? std::string() : it->second;
  }
  bool hasHeader(const std::string& name) const {
    return headers.find(name) != headers.end();
  }
};

std::string lower(std::string value) {
  for (auto& c : value) {
    c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

/** Pull "name":"value" or "name":123 out of a JSON body, unquoted. */
std::string jsonField(const std::string& body, const std::string& name) {
  const std::string key = "\"" + name + "\":";
  auto pos = body.find(key);
  if (pos == std::string::npos) {
    return std::string();
  }
  pos += key.size();
  while (pos < body.size() && body[pos] == ' ') {
    ++pos;
  }
  if (pos >= body.size()) {
    return std::string();
  }
  if (body[pos] == '"') {
    auto end = body.find('"', pos + 1);
    return end == std::string::npos ? std::string()
                                    : body.substr(pos + 1, end - pos - 1);
  }
  auto end = body.find_first_of(",}", pos);
  return end == std::string::npos ? std::string() : body.substr(pos, end - pos);
}

// ===== Responses the script can write =====

std::string withBody(int status,
                     const std::string& reason,
                     const std::string& content_type,
                     const std::string& body,
                     const std::string& session_id) {
  std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                    "\r\nContent-Type: " + content_type + "\r\n";
  if (!session_id.empty()) {
    out += "Mcp-Session-Id: " + session_id + "\r\n";
  }
  out += "Content-Length: " + std::to_string(body.size()) +
         "\r\nConnection: keep-alive\r\n\r\n" + body;
  return out;
}

std::string answer(const Seen& seen, const std::string& result_json) {
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":" +
      (seen.rpc_id.empty() ? std::string("null") : seen.rpc_id) +
      ",\"result\":" + result_json + "}";
  return withBody(200, "OK", "application/json", body, std::string());
}

std::string handshakeAnswer(const Seen& seen,
                            const std::string& session_id,
                            const std::string& protocol_version) {
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":" +
      (seen.rpc_id.empty() ? std::string("null") : seen.rpc_id) +
      ",\"result\":{\"protocolVersion\":\"" + protocol_version +
      "\",\"serverInfo\":{\"name\":\"scripted\",\"version\":\"1\"},"
      "\"capabilities\":{}}}";
  return withBody(200, "OK", "application/json", body, session_id);
}

std::string accepted() {
  return "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\nConnection: "
         "keep-alive\r\n\r\n";
}

std::string refuse(int status,
                   const std::string& reason,
                   const std::string& message) {
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,"
      "\"message\":\"" +
      message + "\"}}";
  return withBody(status, reason, "application/json", body, std::string());
}

/**
 * A loopback listener that records what it is sent and answers from a
 * script. Runs on a thread of its own, since the client under test owns
 * its own dispatcher and the two have to make progress at once.
 */
class ScriptedServer {
 public:
  using Reply = std::function<std::string(const Seen&)>;

  ~ScriptedServer() { stop(); }

  uint16_t start(Reply reply) {
    reply_ = std::move(reply);

    auto& iface = network::socketInterface();
    auto fd =
        iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                     network::Address::IpVersion::v4);
    EXPECT_TRUE(fd.ok());
    listener_ = iface.ioHandleForFd(*fd, false);
    listener_->setBlocking(false);
    EXPECT_TRUE(
        listener_->bind(network::Address::parseInternetAddress("127.0.0.1", 0))
            .ok());
    EXPECT_TRUE(listener_->listen(16).ok());

    auto local = listener_->localAddress();
    EXPECT_TRUE(local.ok());
    const auto* ip = dynamic_cast<const network::Address::Ip*>(local->get());
    EXPECT_NE(ip, nullptr);
    const uint16_t port = ip ? ip->port() : 0;

    thread_ = std::thread([this]() { serve(); });
    return port;
  }

  void stop() {
    if (!thread_.joinable()) {
      return;
    }
    running_ = false;
    thread_.join();
    for (auto& conn : conns_) {
      conn->close();
    }
    conns_.clear();
    if (listener_) {
      listener_->close();
      listener_.reset();
    }
  }

  std::vector<Seen> seen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seen_;
  }

  size_t countOf(const std::string& rpc_method) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& s : seen_) {
      if (s.rpc_method == rpc_method) {
        ++n;
      }
    }
    return n;
  }

  size_t countOfMethod(const std::string& http_method) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& s : seen_) {
      if (s.method == http_method) {
        ++n;
      }
    }
    return n;
  }

  /** Every request whose JSON-RPC method is this one, in arrival order. */
  std::vector<Seen> allOf(const std::string& rpc_method) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Seen> out;
    for (const auto& s : seen_) {
      if (s.rpc_method == rpc_method) {
        out.push_back(s);
      }
    }
    return out;
  }

  /** Wait until at least n requests have arrived, or give up. */
  bool waitForRequests(size_t n, std::chrono::milliseconds budget = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (seen_.size() >= n) {
          return true;
        }
      }
      std::this_thread::sleep_for(5ms);
    }
    return false;
  }

  /** Wait for n requests using a given HTTP method, or give up. */
  bool waitForMethod(const std::string& http_method,
                     size_t n,
                     std::chrono::milliseconds budget = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      if (countOfMethod(http_method) >= n) {
        return true;
      }
      std::this_thread::sleep_for(5ms);
    }
    return false;
  }

 private:
  void serve() {
    std::vector<std::string> buffers;
    while (running_) {
      auto accepted_conn = listener_->accept();
      if (accepted_conn.ok() && *accepted_conn) {
        (*accepted_conn)->setBlocking(false);
        conns_.push_back(std::move(*accepted_conn));
        buffers.emplace_back();
      }

      bool progressed = false;
      for (size_t i = 0; i < conns_.size(); ++i) {
        OwnedBuffer in;
        auto read = conns_[i]->read(in, 8192);
        if (read.ok() && *read > 0) {
          buffers[i].append(in.toString());
          progressed = true;
        }
        while (takeOne(buffers[i], *conns_[i])) {
          progressed = true;
        }
      }

      if (!progressed) {
        std::this_thread::sleep_for(2ms);
      }
    }
  }

  /** Peel one complete request off the buffer and answer it. */
  bool takeOne(std::string& buffer, network::IoHandle& conn) {
    const auto header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return false;
    }

    Seen seen;
    const std::string head = buffer.substr(0, header_end);
    size_t line_end = head.find("\r\n");
    const std::string request_line = head.substr(0, line_end);
    const auto first_space = request_line.find(' ');
    const auto second_space = request_line.find(' ', first_space + 1);
    seen.method = request_line.substr(0, first_space);
    seen.path =
        request_line.substr(first_space + 1, second_space - first_space - 1);

    size_t content_length = 0;
    size_t pos = line_end + 2;
    while (pos < head.size()) {
      const auto end = head.find("\r\n", pos);
      const std::string line = head.substr(
          pos, end == std::string::npos ? std::string::npos : end - pos);
      const auto colon = line.find(':');
      if (colon != std::string::npos) {
        std::string name = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
          value.erase(value.begin());
        }
        if (name == "content-length") {
          content_length = static_cast<size_t>(std::stoul(value));
        }
        seen.headers[name] = value;
      }
      if (end == std::string::npos) {
        break;
      }
      pos = end + 2;
    }

    const size_t body_start = header_end + 4;
    if (buffer.size() < body_start + content_length) {
      return false;  // body still arriving
    }
    seen.body = buffer.substr(body_start, content_length);
    buffer.erase(0, body_start + content_length);

    seen.rpc_method = jsonField(seen.body, "method");
    seen.rpc_id = jsonField(seen.body, "id");

    const std::string response = reply_(seen);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      seen_.push_back(seen);
    }

    if (!response.empty()) {
      OwnedBuffer out;
      out.add(response);
      const auto deadline = std::chrono::steady_clock::now() + 2000ms;
      while (out.length() > 0 && std::chrono::steady_clock::now() < deadline) {
        auto written = conn.write(out);
        if (!written.ok() || *written == 0) {
          std::this_thread::sleep_for(2ms);
        }
      }
    }
    return true;
  }

  Reply reply_;
  network::IoHandlePtr listener_;
  std::vector<network::IoHandlePtr> conns_;
  std::thread thread_;
  std::atomic<bool> running_{true};
  mutable std::mutex mutex_;
  std::vector<Seen> seen_;
};

class StreamableHttpClientSessionTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  /** Bring up a client pointed at the scripted peer and connect it. */
  void startClient(uint16_t port) {
    client::McpClientConfig config;
    config.client_name = "session-test-client";
    config.client_version = "0.0.1";
    config.num_workers = 1;
    config.request_timeout = 5000ms;
    config.protocol_initialization_timeout = 5000ms;
    config.protocol_connection_timeout = 5000ms;

    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);

    // A plain http:// URL whose path is neither /sse nor /events is what
    // McpClient negotiates Streamable HTTP for.
    const std::string uri = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    auto connected = client_->connect(uri);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
        << "could not connect to the scripted server";
  }

  void handshake() {
    auto init = client_->initializeProtocol();
    ASSERT_EQ(init.wait_for(5s), std::future_status::ready)
        << "initialize never came back";
    ASSERT_NO_THROW(init.get());
  }

  ScriptedServer server_;
  std::unique_ptr<client::McpClient> client_;
};

constexpr const char* kSessionOne = "session-one";
constexpr const char* kSessionTwo = "session-two";

// A client that has not been given a session cannot name one, and until
// the handshake has settled a revision it cannot declare one either.
TEST_F(StreamableHttpClientSessionTest, TheHandshakeSaysNothingItWasNotTold) {
  const uint16_t port = server_.start([](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      return handshakeAnswer(seen, kSessionOne, "2025-06-18");
    }
    return accepted();
  });

  startClient(port);
  handshake();

  auto handshakes = server_.allOf("initialize");
  ASSERT_EQ(handshakes.size(), 1u);
  EXPECT_FALSE(handshakes[0].hasHeader("mcp-session-id"))
      << "named a session before being given one";
  EXPECT_FALSE(handshakes[0].hasHeader("mcp-protocol-version"))
      << "declared a revision before one was negotiated";
}

// And once it has been told, it says so on everything that follows.
TEST_F(StreamableHttpClientSessionTest, EveryRequestAfterNamesTheSession) {
  const uint16_t port = server_.start([](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      return handshakeAnswer(seen, kSessionOne, "2025-03-26");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    return answer(seen, "{}");
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());

  auto pings = server_.allOf("ping");
  ASSERT_EQ(pings.size(), 1u);
  EXPECT_EQ(pings[0].header("mcp-session-id"), kSessionOne);
  // The revision the server settled on, not the one the client asked
  // for — those differ here on purpose.
  EXPECT_EQ(pings[0].header("mcp-protocol-version"), "2025-03-26");
}

// The notification that completes the handshake is sent, and it names
// the session like everything else after initialize.
TEST_F(StreamableHttpClientSessionTest, TheHandshakeIsFollowedByItsNotice) {
  const uint16_t port = server_.start([](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      return handshakeAnswer(seen, kSessionOne, "2025-06-18");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    return answer(seen, "{}");
  });

  startClient(port);
  handshake();

  ASSERT_TRUE(server_.waitForRequests(2));
  auto notices = server_.allOf("notifications/initialized");
  ASSERT_EQ(notices.size(), 1u) << "the server was never told we were ready";
  EXPECT_EQ(notices[0].header("mcp-session-id"), kSessionOne);

  // A 202 for that notification must not have consumed the place
  // belonging to the request behind it.
  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());
}

// A server that keeps no sessions is never told about one, and the
// conversation works anyway.
TEST_F(StreamableHttpClientSessionTest, AServerKeepingNoSessionIsToldOfNone) {
  const uint16_t port = server_.start([](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      return handshakeAnswer(seen, std::string(), "2025-06-18");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    return answer(seen, "{}");
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());

  for (const auto& seen : server_.seen()) {
    EXPECT_FALSE(seen.hasHeader("mcp-session-id"))
        << "claimed a session that was never minted, on " << seen.rpc_method;
  }
  // The revision is still declared: it was negotiated whether or not a
  // session came with it.
  auto pings = server_.allOf("ping");
  ASSERT_EQ(pings.size(), 1u);
  EXPECT_EQ(pings[0].header("mcp-protocol-version"), "2025-06-18");
}

// The one this is all for: the server forgets, says so, and the client
// starts again by itself — once, and sends the refused request once.
TEST_F(StreamableHttpClientSessionTest, AForgottenSessionIsStartedAgainOnce) {
  std::atomic<int> handshakes{0};
  const uint16_t port = server_.start([&handshakes](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      const int n = ++handshakes;
      return handshakeAnswer(seen, n == 1 ? kSessionOne : kSessionTwo,
                             "2025-06-18");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    // Anything sent under the session that has been forgotten.
    if (seen.header("mcp-session-id") == kSessionOne) {
      return refuse(404, "Not Found", "no such session; send initialize again");
    }
    return answer(seen, "{}");
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready)
      << "the refused request was never answered";
  EXPECT_FALSE(ping.get().error.has_value())
      << "the request did not survive the new session";

  // Exactly one further handshake, and it carried nothing from the
  // session that was gone.
  auto all_handshakes = server_.allOf("initialize");
  ASSERT_EQ(all_handshakes.size(), 2u);
  EXPECT_FALSE(all_handshakes[1].hasHeader("mcp-session-id"))
      << "started a new session while still naming the old one";

  // Sent twice in total — once refused, once served — and not a third
  // time. The second one names the new session.
  auto pings = server_.allOf("ping");
  ASSERT_EQ(pings.size(), 2u);
  EXPECT_EQ(pings[0].header("mcp-session-id"), kSessionOne);
  EXPECT_EQ(pings[1].header("mcp-session-id"), kSessionTwo);
}

// A server that forgets every session it mints must not be able to keep
// a client going round.
TEST_F(StreamableHttpClientSessionTest, ARequestRefusedTwiceIsAnswered) {
  std::atomic<int> handshakes{0};
  const uint16_t port = server_.start([&handshakes](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      const int n = ++handshakes;
      return handshakeAnswer(seen, "session-" + std::to_string(n),
                             "2025-06-18");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    return refuse(404, "Not Found", "no such session; send initialize again");
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready)
      << "the request was left to sit out its deadline";
  EXPECT_TRUE(ping.get().error.has_value())
      << "a request nothing could serve was reported as served";

  // Two handshakes and two attempts, and then it stops. What ends it is
  // the client refusing to try again, not the deadline.
  EXPECT_EQ(server_.countOf("initialize"), 2u);
  EXPECT_EQ(server_.countOf("ping"), 2u);
}

// A refusal that is not about the session is answered where it is,
// rather than treated as something to recover from.
TEST_F(StreamableHttpClientSessionTest, ARefusalThatIsNotAboutTheSession) {
  const uint16_t port = server_.start([](const Seen& seen) {
    if (seen.rpc_method == "initialize") {
      return handshakeAnswer(seen, kSessionOne, "2025-06-18");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    return refuse(400, "Bad Request", "the origin is not allowed here");
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready)
      << "a refused request was left to sit out its deadline";
  auto response = ping.get();
  ASSERT_TRUE(response.error.has_value());
  EXPECT_NE(response.error->message.find("the origin is not allowed here"),
            std::string::npos)
      << "the caller was not told what the server said: "
      << response.error->message;

  // Nothing about a bad request says the session is gone.
  EXPECT_EQ(server_.countOf("initialize"), 1u);
  EXPECT_EQ(server_.countOf("ping"), 1u);
}

// A client that is finished says so, rather than leaving the session to
// expire on its own.
TEST_F(StreamableHttpClientSessionTest, EndingTheClientEndsTheSession) {
  const uint16_t port = server_.start([](const Seen& seen) {
    if (seen.method == "DELETE") {
      return withBody(200, "OK", "application/json", "{}", std::string());
    }
    if (seen.rpc_method == "initialize") {
      return handshakeAnswer(seen, kSessionOne, "2025-06-18");
    }
    if (seen.rpc_id.empty()) {
      return accepted();
    }
    return answer(seen, "{}");
  });

  startClient(port);
  handshake();

  auto ping = client_->sendRequest("ping");
  ASSERT_EQ(ping.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(ping.get().error.has_value());

  client_->shutdown();
  client_.reset();

  ASSERT_TRUE(server_.waitForMethod("DELETE", 1))
      << "the session was abandoned rather than given back";
  ASSERT_EQ(server_.countOfMethod("DELETE"), 1u);
  for (const auto& seen : server_.seen()) {
    if (seen.method == "DELETE") {
      EXPECT_EQ(seen.header("mcp-session-id"), kSessionOne)
          << "asked to end a session without saying which";
      EXPECT_EQ(seen.path, "/mcp");
      EXPECT_TRUE(seen.body.empty())
          << "a request to end a session has no body";
    }
  }
}

}  // namespace
}  // namespace mcp
