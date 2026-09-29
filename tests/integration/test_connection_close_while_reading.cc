// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * A connection closed from inside its own read filters.
 *
 * A filter answering what it reads can find, while it writes, that the
 * peer has gone — and the connection closes there and then, in the middle
 * of the read. The filter is still reading the connection's buffer at
 * that point and drains what it consumed once it is done. Emptying the
 * buffer under it frees what it is reading and turns that drain into an
 * exception, which used to take the whole process down.
 *
 * Uses real socket pairs, following test_http_sse_filter_server_mode.cc.
 */

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/filter.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"

#include "real_io_test_base.h"

namespace mcp {
namespace network {
namespace {

using namespace std::chrono_literals;

/** What the filter below found while it read. */
struct Observed {
  std::atomic<bool> read{false};
  std::atomic<size_t> before{0};
  std::atomic<size_t> after_close{0};
  std::atomic<bool> drain_threw{false};
};

/**
 * Reads everything, closes the connection as a failed write would, and
 * then drains what it consumed — which is what a parser does.
 */
class ClosingReader : public ReadFilter {
 public:
  explicit ClosingReader(Observed& observed) : observed_(observed) {}

  FilterStatus onData(Buffer& data, bool) override {
    const size_t length = data.length();
    observed_.before = length;
    callbacks_->connection().close(ConnectionCloseType::NoFlush);
    observed_.after_close = data.length();
    try {
      data.drain(length);
    } catch (const std::exception&) {
      observed_.drain_threw = true;
    }
    observed_.read = true;
    return FilterStatus::StopIteration;
  }
  FilterStatus onNewConnection() override { return FilterStatus::Continue; }
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

 private:
  Observed& observed_;
  ReadFilterCallbacks* callbacks_{nullptr};
};

/** Fails on whatever it is given. */
class ThrowingReader : public ReadFilter {
 public:
  FilterStatus onData(Buffer&, bool) override {
    throw std::runtime_error("a filter could not make sense of this");
  }
  FilterStatus onNewConnection() override { return FilterStatus::Continue; }
  void initializeReadFilterCallbacks(ReadFilterCallbacks&) override {}
};

class ClosedEvents : public ConnectionCallbacks {
 public:
  void onEvent(ConnectionEvent event) override {
    if (event == ConnectionEvent::LocalClose ||
        event == ConnectionEvent::RemoteClose) {
      closed = true;
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::atomic<bool> closed{false};
};

class ConnectionCloseWhileReadingTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    executeInDispatcher([this]() { conn_.reset(); });
    test::RealIoTestBase::TearDown();
  }

  /** A server connection reading through this filter, and its peer. */
  void connect(ReadFilterSharedPtr filter) {
    executeInDispatcher([&]() {
      auto pair = createSocketPair();
      auto local = Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      conn_ = ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket),
          std::make_unique<RawBufferTransportSocket>(), stream_info_);
      auto* impl = static_cast<ConnectionImpl*>(conn_.get());
      impl->addConnectionCallbacks(events_);
      impl->filterManager().addReadFilter(filter);
      impl->filterManager().initializeReadFilters();
      peer_ = std::move(pair.second);
    });
  }

  void send(const std::string& bytes) {
    OwnedBuffer out;
    out.add(bytes);
    ASSERT_TRUE(peer_->write(out).ok());
  }

  bool waitFor(const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(5ms);
    }
    return done();
  }

  stream_info::StreamInfoImpl stream_info_;
  ClosedEvents events_;
  std::unique_ptr<ServerConnection> conn_;
  IoHandlePtr peer_;
};

// The buffer a filter is reading stays whole until the filter is done,
// however the connection closes underneath it.
TEST_F(ConnectionCloseWhileReadingTest, AReaderKeepsItsBufferThroughAClose) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));

  const std::string request =
      "DELETE /mcp HTTP/1.1\r\nHost: localhost\r\n"
      "Mcp-Session-Id: 0123456789abcdef\r\n\r\n";
  send(request);

  ASSERT_TRUE(waitFor([&]() { return observed.read.load(); }))
      << "the filter never saw the request";
  EXPECT_EQ(observed.before.load(), request.size());
  EXPECT_EQ(observed.after_close.load(), request.size())
      << "the connection emptied the buffer while it was being read";
  EXPECT_FALSE(observed.drain_threw.load())
      << "draining what was read failed after the connection closed";
  EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }));
}

// A filter failing on what it reads ends its connection, not the process.
TEST_F(ConnectionCloseWhileReadingTest, AFailingReaderClosesOnlyItsConnection) {
  connect(std::make_shared<ThrowingReader>());
  send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

  EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }))
      << "the connection stayed open after its filter failed";

  // Still running: the dispatcher goes on serving work.
  std::atomic<bool> ran{false};
  executeInDispatcher([&]() { ran = true; });
  EXPECT_TRUE(ran.load());
}

}  // namespace
}  // namespace network
}  // namespace mcp
