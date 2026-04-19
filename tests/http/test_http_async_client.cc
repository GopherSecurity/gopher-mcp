/**
 * Round-trip integration tests for mcp::http::HttpAsyncClient.
 *
 * We stand up a real TCP listener on 127.0.0.1 (ephemeral port) via
 * RealListenerTestBase, drive HttpAsyncClient::send at it, then hand-
 * handle the server-side accept/read/write with the MCP socket
 * interface. That gives us real I/O across the whole stack — the
 * client codec, the deferred-delete teardown, and the connection
 * lifecycle all run against a live kernel socket rather than a mock.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/http/http_async_client.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/transport_socket.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace http {
namespace {

using namespace std::chrono_literals;

// Collects the outcome of an HttpAsyncClient::send so the test thread
// can block on it without racing the dispatcher thread.
class ResponseSink {
 public:
  void setResponse(HttpResponse r) {
    std::lock_guard<std::mutex> g(mu_);
    response_ = std::move(r);
    got_response_ = true;
    cv_.notify_all();
  }
  void setError(const std::string& msg) {
    std::lock_guard<std::mutex> g(mu_);
    error_ = msg;
    got_error_ = true;
    cv_.notify_all();
  }
  bool wait(std::chrono::milliseconds d = 2000ms) {
    std::unique_lock<std::mutex> g(mu_);
    return cv_.wait_for(g, d,
                        [&] { return got_response_ || got_error_; });
  }
  bool hasResponse() const {
    std::lock_guard<std::mutex> g(mu_);
    return got_response_;
  }
  bool hasError() const {
    std::lock_guard<std::mutex> g(mu_);
    return got_error_;
  }
  HttpResponse response() const {
    std::lock_guard<std::mutex> g(mu_);
    return response_;
  }
  std::string error() const {
    std::lock_guard<std::mutex> g(mu_);
    return error_;
  }

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool got_response_{false};
  bool got_error_{false};
  HttpResponse response_;
  std::string error_;
};

class HttpAsyncClientTest : public test::RealListenerTestBase {
 protected:
  void SetUp() override { RealListenerTestBase::SetUp(); }

  void TearDown() override {
    executeInDispatcher([this]() { client_.reset(); });
    RealListenerTestBase::TearDown();
  }

  void createClient() {
    executeInDispatcher([this]() {
      client_ = std::make_unique<HttpAsyncClient>(
          *dispatcher_, network::socketInterface(),
          std::make_unique<network::RawBufferTransportSocketFactory>());
    });
  }

  // Busy-accept a single pending connection — the listen socket is
  // non-blocking so we retry briefly.
  network::IoHandlePtr acceptOne(std::chrono::milliseconds budget = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      network::IoHandlePtr accepted;
      executeInDispatcher([this, &accepted]() {
        auto r = listen_handle_->accept();
        if (r.ok()) {
          accepted = std::move(*r);
        }
      });
      if (accepted) {
        return accepted;
      }
      std::this_thread::sleep_for(5ms);
    }
    return nullptr;
  }

  // Read until we see the request line + headers terminator, then
  // slurp Content-Length bytes of body. IoHandle::read fills a Buffer,
  // which we drain to a std::string so the test can pattern-match.
  std::string readRequest(network::IoHandle& handle,
                          std::chrono::milliseconds budget = 2000ms) {
    std::string data;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    bool headers_done = false;
    size_t content_length = 0;
    size_t header_end = 0;

    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = handle.read(buf, /*max_length=*/4096);
      if (r.ok() && *r > 0) {
        data.append(buf.toString());
      } else {
        std::this_thread::sleep_for(5ms);
      }
      if (!headers_done) {
        auto pos = data.find("\r\n\r\n");
        if (pos != std::string::npos) {
          headers_done = true;
          header_end = pos + 4;
          auto cl_pos = data.find("Content-Length:");
          if (cl_pos != std::string::npos && cl_pos < header_end) {
            content_length =
                std::stoul(data.substr(cl_pos + 15, header_end - cl_pos - 15));
          }
        }
      }
      if (headers_done && data.size() >= header_end + content_length) {
        return data;
      }
    }
    return data;
  }

  void writeResponse(network::IoHandle& handle, const std::string& bytes) {
    // IoHandle::write drains the buffer as bytes go on the wire. We
    // keep looping until the buffer is empty or the deadline fires so
    // partial sends don't silently truncate the fake response.
    OwnedBuffer buf;
    buf.add(bytes);
    const auto deadline = std::chrono::steady_clock::now() + 2000ms;
    while (buf.length() > 0 &&
           std::chrono::steady_clock::now() < deadline) {
      auto r = handle.write(buf);
      if (!r.ok() || *r == 0) {
        std::this_thread::sleep_for(5ms);
      }
    }
  }

  std::unique_ptr<HttpAsyncClient> client_;
};

TEST_F(HttpAsyncClientTest, PostRoundTripDeliversResponseBody) {
  uint16_t port = createRealListener();
  createClient();

  ResponseSink sink;
  HttpRequest req;
  req.method = "POST";
  req.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
  req.headers["Content-Type"] = "application/json";
  req.body = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}";

  executeInDispatcher([this, &req, &sink]() {
    const bool ok = client_->send(
        req,
        [&sink](HttpResponse r) { sink.setResponse(std::move(r)); },
        [&sink](const std::string& e) { sink.setError(e); });
    ASSERT_TRUE(ok);
  });

  auto accepted = acceptOne();
  ASSERT_TRUE(accepted) << "server never saw the inbound POST";

  const std::string request_wire = readRequest(*accepted);
  EXPECT_NE(request_wire.find("POST /mcp HTTP/1.1"), std::string::npos);
  EXPECT_NE(request_wire.find("Host: 127.0.0.1:"), std::string::npos);
  EXPECT_NE(request_wire.find("Content-Type: application/json"),
            std::string::npos);
  EXPECT_NE(request_wire.find(req.body), std::string::npos);

  const std::string reply_body = "{\"result\":\"pong\"}";
  std::string reply = "HTTP/1.1 200 OK\r\n";
  reply += "Content-Type: application/json\r\n";
  reply += "Content-Length: " + std::to_string(reply_body.size()) + "\r\n";
  reply += "Connection: close\r\n\r\n";
  reply += reply_body;
  writeResponse(*accepted, reply);

  ASSERT_TRUE(sink.wait());
  ASSERT_TRUE(sink.hasResponse()) << "error instead: " << sink.error();
  auto r = sink.response();
  EXPECT_EQ(r.status_code, 200);
  EXPECT_EQ(r.body, reply_body);
  EXPECT_EQ(r.headers["content-type"], "application/json");
}

TEST_F(HttpAsyncClientTest, RejectsMalformedUrl) {
  createClient();
  bool cb_fired = false;
  executeInDispatcher([this, &cb_fired]() {
    HttpRequest req;
    req.method = "POST";
    req.url = "not-a-url";
    const bool ok = client_->send(
        req, [&](HttpResponse) { cb_fired = true; },
        [&](const std::string&) { cb_fired = true; });
    EXPECT_FALSE(ok);
  });
  // Contract: send returns false for a malformed URL and does NOT fire
  // either callback. Give the dispatcher a tick to prove nothing posts.
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(cb_fired);
}

TEST_F(HttpAsyncClientTest, MalformedResponseFiresErrorCallback) {
  uint16_t port = createRealListener();
  createClient();

  // Shared with the callbacks so a late-firing callback (e.g. from
  // teardown draining the dispatcher) doesn't hit a destroyed stack
  // object.
  auto sink = std::make_shared<ResponseSink>();

  executeInDispatcher([this, port, sink]() {
    HttpRequest req;
    req.method = "POST";
    req.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    req.body = "{}";
    ASSERT_TRUE(client_->send(
        req,
        [sink](HttpResponse r) { sink->setResponse(std::move(r)); },
        [sink](const std::string& e) { sink->setError(e); }));
  });

  auto accepted = acceptOne();
  ASSERT_TRUE(accepted);
  // Consume the request so the client moves past writeRequestBytes.
  (void)readRequest(*accepted, 2000ms);
  // Reply with bytes that cannot be parsed as an HTTP/1.1 response
  // status line. The client codec surfaces this as onError, which
  // RequestContext forwards as an error callback.
  writeResponse(*accepted, "NOT AN HTTP RESPONSE\r\n\r\n");

  ASSERT_TRUE(sink->wait());
  EXPECT_TRUE(sink->hasError());
  EXPECT_FALSE(sink->hasResponse());
}

}  // namespace
}  // namespace http
}  // namespace mcp
