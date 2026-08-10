/**
 * A scripted HTTP peer for driving a real McpClient.
 *
 * A loopback listener that records every request it is sent — request
 * line, headers, body — and answers each one from a script the test
 * supplies. What it buys over a real McpServer is control of the things
 * that are hard to arrange and easy to get wrong: a refusal with a
 * chosen status, a stream cut after a chosen event, an exact set of
 * replayed ids, and the ability to assert on the headers the server
 * actually received rather than on a proxy for them.
 *
 * Runs on a thread of its own, because the client under test owns its
 * own dispatcher and the two have to make progress at the same time.
 */

#ifndef MCP_TESTS_CLIENT_SCRIPTED_HTTP_SERVER_H
#define MCP_TESTS_CLIENT_SCRIPTED_HTTP_SERVER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"

namespace mcp {
namespace test {

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

/**
 * What to do about a request: bytes to write now, and what becomes of
 * the connection afterwards.
 */
struct Reply {
  // Written straight away. Empty writes nothing, which is how a request
  // is left unanswered.
  std::string bytes;

  // Remember this connection as the stream, so a test can push onto it
  // later or cut it. Only one at a time — the newest wins, which is
  // what a client that reconnects looks like from here.
  bool keep_open = false;

  // Cut the connection once the bytes are out. A stream that ends
  // mid-event is what a client has to recover from.
  bool close_after = false;

  static Reply write(std::string bytes) {
    Reply reply;
    reply.bytes = std::move(bytes);
    return reply;
  }
  static Reply stream(std::string bytes) {
    Reply reply;
    reply.bytes = std::move(bytes);
    reply.keep_open = true;
    return reply;
  }
  static Reply writeThenCut(std::string bytes) {
    Reply reply;
    reply.bytes = std::move(bytes);
    reply.close_after = true;
    return reply;
  }
  static Reply nothing() { return Reply(); }
};

inline std::string lowerAscii(std::string value) {
  for (auto& c : value) {
    c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

/** Pull "name":"value" or "name":123 out of a JSON body, unquoted. */
inline std::string jsonField(const std::string& body, const std::string& name) {
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

// ===== Responses a script can write =====

inline std::string withBody(int status,
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

inline std::string answer(const Seen& seen, const std::string& result_json) {
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":" +
      (seen.rpc_id.empty() ? std::string("null") : seen.rpc_id) +
      ",\"result\":" + result_json + "}";
  return withBody(200, "OK", "application/json", body, std::string());
}

inline std::string handshakeAnswer(const Seen& seen,
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

inline std::string accepted() {
  return "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\nConnection: "
         "keep-alive\r\n\r\n";
}

inline std::string refuse(int status,
                          const std::string& reason,
                          const std::string& message) {
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,"
      "\"message\":\"" +
      message + "\"}}";
  return withBody(status, reason, "application/json", body, std::string());
}

/**
 * The head of a streamed response. No Content-Length: chunked is how a
 * response of unknown length is framed, and a stream is the case that
 * has no length to give.
 */
inline std::string streamPrelude(
    const std::string& session_id = std::string()) {
  std::string out =
      "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: "
      "no-cache\r\n";
  if (!session_id.empty()) {
    out += "Mcp-Session-Id: " + session_id + "\r\n";
  }
  out += "Transfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n";
  return out;
}

/** One SSE event, chunk-framed so it can follow a prelude. */
inline std::string streamEvent(const std::string& id, const std::string& data) {
  std::string frame;
  if (!id.empty()) {
    frame += "id: " + id + "\r\n";
  }
  frame += "data: " + data + "\r\n\r\n";

  std::ostringstream chunk;
  chunk << std::hex << frame.size() << "\r\n" << frame << "\r\n";
  return chunk.str();
}

/** The end of a chunked body, which is what ends a stream cleanly. */
inline std::string streamEnd() { return "0\r\n\r\n"; }

class ScriptedServer {
 public:
  using Script = std::function<Reply(const Seen&)>;

  ~ScriptedServer() { stop(); }

  uint16_t start(Script script) {
    script_ = std::move(script);

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
      // Some are already gone: a reply that cut its connection, or a
      // stream the test cut, drops the handle where it stands.
      if (conn) {
        conn->close();
      }
    }
    conns_.clear();
    if (listener_) {
      listener_->close();
      listener_.reset();
    }
  }

  // ===== What arrived =====

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

  /** Every request using this HTTP method, in arrival order. */
  std::vector<Seen> allOfMethod(const std::string& http_method) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Seen> out;
    for (const auto& s : seen_) {
      if (s.method == http_method) {
        out.push_back(s);
      }
    }
    return out;
  }

  bool waitForRequests(size_t n, std::chrono::milliseconds budget = 5000ms) {
    return waitFor([&]() { return seenCount() >= n; }, budget);
  }

  bool waitForMethod(const std::string& http_method,
                     size_t n,
                     std::chrono::milliseconds budget = 5000ms) {
    return waitFor([&]() { return countOfMethod(http_method) >= n; }, budget);
  }

  bool waitForRpc(const std::string& rpc_method,
                  size_t n,
                  std::chrono::milliseconds budget = 5000ms) {
    return waitFor([&]() { return countOf(rpc_method) >= n; }, budget);
  }

  size_t seenCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seen_.size();
  }

  // ===== The stream =====

  /** True once some reply asked for its connection to be kept. */
  bool hasStream() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stream_index_ >= 0;
  }

  bool waitForStream(std::chrono::milliseconds budget = 5000ms) {
    return waitFor([&]() { return hasStream(); }, budget);
  }

  /** Write onto the connection currently held as the stream. */
  void pushToStream(const std::string& bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    stream_pending_ += bytes;
  }

  /** Cut the connection currently held as the stream. */
  void cutStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    stream_cut_ = true;
  }

  /** Spin until something the test is waiting for is true, or give up. */
  bool waitFor(const std::function<bool()>& done,
               std::chrono::milliseconds budget = 5000ms) const {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(5ms);
    }
    return done();
  }

 private:
  void serve() {
    while (running_) {
      auto accepted_conn = listener_->accept();
      if (accepted_conn.ok() && *accepted_conn) {
        (*accepted_conn)->setBlocking(false);
        conns_.push_back(std::move(*accepted_conn));
        buffers_.emplace_back();
      }

      bool progressed = false;
      for (size_t i = 0; i < conns_.size(); ++i) {
        if (!conns_[i]) {
          continue;
        }
        OwnedBuffer in;
        auto read = conns_[i]->read(in, 8192);
        if (read.ok() && *read > 0) {
          buffers_[i].append(in.toString());
          progressed = true;
        }
        while (conns_[i] && takeOne(i)) {
          progressed = true;
        }
      }

      if (drainStreamWork()) {
        progressed = true;
      }

      if (!progressed) {
        std::this_thread::sleep_for(2ms);
      }
    }
  }

  /** Write and cut what the test asked for on the held stream. */
  bool drainStreamWork() {
    std::string pending;
    bool cut = false;
    int index = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      index = stream_index_;
      pending.swap(stream_pending_);
      cut = stream_cut_;
      stream_cut_ = false;
    }
    if (index < 0 || static_cast<size_t>(index) >= conns_.size() ||
        !conns_[index]) {
      return false;
    }
    bool did = false;
    if (!pending.empty()) {
      writeAll(*conns_[index], pending);
      did = true;
    }
    if (cut) {
      conns_[index]->close();
      conns_[index].reset();
      std::lock_guard<std::mutex> lock(mutex_);
      stream_index_ = -1;
      did = true;
    }
    return did;
  }

  void writeAll(network::IoHandle& conn, const std::string& bytes) {
    OwnedBuffer out;
    out.add(bytes);
    const auto deadline = std::chrono::steady_clock::now() + 2000ms;
    while (out.length() > 0 && std::chrono::steady_clock::now() < deadline) {
      auto written = conn.write(out);
      if (!written.ok() || *written == 0) {
        std::this_thread::sleep_for(2ms);
      }
    }
  }

  /** Peel one complete request off a connection's buffer and answer it. */
  bool takeOne(size_t index) {
    std::string& buffer = buffers_[index];
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
        std::string name = lowerAscii(line.substr(0, colon));
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

    const Reply reply = script_(seen);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      seen_.push_back(seen);
      if (reply.keep_open) {
        stream_index_ = static_cast<int>(index);
      }
    }

    if (!reply.bytes.empty()) {
      writeAll(*conns_[index], reply.bytes);
    }
    if (reply.close_after) {
      conns_[index]->close();
      conns_[index].reset();
      std::lock_guard<std::mutex> lock(mutex_);
      if (stream_index_ == static_cast<int>(index)) {
        stream_index_ = -1;
      }
      return false;
    }
    return true;
  }

  Script script_;
  network::IoHandlePtr listener_;
  std::vector<network::IoHandlePtr> conns_;
  std::vector<std::string> buffers_;
  std::thread thread_;
  std::atomic<bool> running_{true};

  mutable std::mutex mutex_;
  std::vector<Seen> seen_;
  int stream_index_{-1};
  std::string stream_pending_;
  bool stream_cut_{false};
};

}  // namespace test
}  // namespace mcp

#endif  // MCP_TESTS_CLIENT_SCRIPTED_HTTP_SERVER_H
