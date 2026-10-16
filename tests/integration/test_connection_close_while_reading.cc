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
#include <functional>
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

class ConnectionImplTestPeer {
 public:
  static void setReadBufferReady(ConnectionImpl& connection) {
    connection.setReadBufferReady();
  }
  static void disableFileEvents(ConnectionImpl& connection) {
    connection.enableFileEvents(0);
  }
  static bool delayedCloseTimerEnabled(ConnectionImpl& connection) {
    return connection.delayed_close_timer_ &&
           connection.delayed_close_timer_->enabled();
  }
  static bool readEventsEnabled(ConnectionImpl& connection) {
    return (connection.file_event_state_ &
            static_cast<uint32_t>(event::FileReadyType::Read)) != 0;
  }
};

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
  explicit ClosingReader(
      Observed& observed,
      ConnectionCloseType close_type = ConnectionCloseType::NoFlush,
      std::string response = "")
      : observed_(observed),
        close_type_(close_type),
        response_(std::move(response)) {}

  FilterStatus onData(Buffer& data, bool) override {
    const size_t length = data.length();
    observed_.before = length;
    if (!response_.empty()) {
      OwnedBuffer response;
      response.add(response_);
      callbacks_->connection().write(response, false);
    }
    callbacks_->connection().close(close_type_);
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
  ConnectionCloseType close_type_;
  std::string response_;
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

/** Fails on whatever it is asked to send. */
class ThrowingWriter : public WriteFilter {
 public:
  FilterStatus onWrite(Buffer&, bool) override {
    throw std::runtime_error("a filter could not frame this");
  }
  void initializeWriteFilterCallbacks(WriteFilterCallbacks&) override {}
};

class ClosedEvents : public ConnectionCallbacks {
 public:
  void onEvent(ConnectionEvent event) override {
    if (event == ConnectionEvent::LocalClose ||
        event == ConnectionEvent::RemoteClose) {
      last_close_event = event;
      closed = true;
      ++close_count;
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::atomic<bool> closed{false};
  std::atomic<int> close_count{0};
  std::atomic<ConnectionEvent> last_close_event{ConnectionEvent::Connected};
};

class ControlledTransport : public RawBufferTransportSocket {
 public:
  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override {
    ++write_count;
    if (blocked)
      return TransportIoResult::stop();
    if (fail_write)
      return TransportIoResult::close();
    return RawBufferTransportSocket::doWrite(buffer, end_stream);
  }
  void closeSocket(ConnectionEvent event) override {
    ++close_count;
    if (on_close)
      on_close();
    RawBufferTransportSocket::closeSocket(event);
  }

  bool blocked{false};
  bool fail_write{false};
  int write_count{0};
  int close_count{0};
  std::function<void()> on_close;
};

class ConnectionCloseWhileReadingTest : public test::RealIoTestBase {
 protected:
  void TearDown() override {
    executeInDispatcher([this]() { conn_.reset(); });
    test::RealIoTestBase::TearDown();
  }

  /** A server connection reading through this filter, and its peer. */
  void connect(ReadFilterSharedPtr filter,
               WriteFilterSharedPtr writer = nullptr,
               TransportSocketPtr transport = nullptr,
               bool use_raw_socket = false) {
    executeInDispatcher([&]() {
      auto pair = createSocketPair();
      auto local = Address::parseInternetAddress("127.0.0.1", 0);
      auto remote = Address::parseInternetAddress("127.0.0.1", 0);
      auto socket = std::make_unique<ConnectionSocketImpl>(
          std::move(pair.first), local, remote);
      if (!transport && !use_raw_socket) {
        transport = std::make_unique<RawBufferTransportSocket>();
      }
      auto server = ConnectionImpl::createServerConnection(
          *dispatcher_, std::move(socket), std::move(transport), stream_info_);
      conn_.reset(static_cast<ConnectionImpl*>(server.release()));
      auto* impl = static_cast<ConnectionImpl*>(conn_.get());
      impl->addConnectionCallbacks(events_);
      impl->filterManager().addReadFilter(filter);
      if (writer) {
        impl->filterManager().addWriteFilter(writer);
      }
      impl->filterManager().initializeReadFilters();
      peer_ = std::move(pair.second);
    });
  }

  void send(const std::string& bytes) {
    OwnedBuffer out;
    out.add(bytes);
    ASSERT_TRUE(peer_->write(out).ok());
  }

  void failConnect(std::function<void(ConnectionImpl&)> after_failure) {
    std::atomic<bool> failed{false};
    event::FileEventPtr ready;
    IoHandlePtr readiness_handle;
    executeInDispatcher([&]() {
      auto& sockets = socketInterface();
      auto reserved_fd = sockets.socket(SocketType::Stream, Address::Type::Ip,
                                        Address::IpVersion::v4);
      ASSERT_TRUE(reserved_fd.ok());
      peer_ = sockets.ioHandleForFd(*reserved_fd, false);
      auto local = Address::parseInternetAddress("127.0.0.1", 0);
      ASSERT_TRUE(peer_->bind(local).ok());
      auto remote = peer_->localAddress();
      ASSERT_TRUE(remote.ok());
      // Reserve an ephemeral local port, then close it before connecting so
      // the kernel refuses the TCP connection.
      ASSERT_TRUE(peer_->close().ok());
      auto client_fd = sockets.socket(SocketType::Stream, Address::Type::Ip,
                                      Address::IpVersion::v4);
      ASSERT_TRUE(client_fd.ok());
      auto handle = sockets.ioHandleForFd(*client_fd, false);
      ASSERT_TRUE(handle->setBlocking(false).ok());
      auto socket = std::make_unique<ConnectionSocketImpl>(std::move(handle),
                                                           local, *remote);
      auto transport = std::make_unique<ControlledTransport>();
      transport->blocked = true;
      auto client = ConnectionImpl::createClientConnection(
          *dispatcher_, std::move(socket), std::move(transport), stream_info_);
      auto* impl = static_cast<ConnectionImpl*>(client.get());
      impl->addConnectionCallbacks(events_);
      client->connect();
      EXPECT_TRUE(impl->connecting());
      conn_.reset(static_cast<ConnectionImpl*>(client.release()));
      // Drive the real failure event ourselves so the test can destroy the
      // connection or request a flush before its posted close runs.
      ConnectionImplTestPeer::disableFileEvents(*impl);
      readiness_handle = impl->ioHandle().duplicate();
      ASSERT_TRUE(readiness_handle);
      ready = dispatcher_->createFileEvent(
          readiness_handle->fd(),
          [&, impl](uint32_t) {
            ready->setEnabled(0);
            impl->onWriteReady();
            EXPECT_FALSE(impl->connecting());
            after_failure(*impl);
            if (conn_) {
              ConnectionImplTestPeer::disableFileEvents(*conn_);
            }
            failed = true;
          },
          event::PlatformDefaultTriggerType,
          static_cast<uint32_t>(event::FileReadyType::Write));
    });
    EXPECT_TRUE(waitFor([&]() { return failed.load(); }));
    executeInDispatcher([&]() {
      ready.reset();
      readiness_handle.reset();
    });
  }

  bool waitFor(const std::function<bool()>& done,
               std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(5ms);
    }
    return done();
  }

  void expectClosingAfter(std::chrono::milliseconds interval) {
    std::atomic<bool> checked{false};
    event::TimerPtr checkpoint;
    executeInDispatcher([&]() {
      checkpoint = dispatcher_->createTimer([&]() {
        EXPECT_EQ(conn_->state(), ConnectionState::Closing);
        EXPECT_TRUE(conn_->ioHandle().isOpen());
        EXPECT_EQ(events_.close_count.load(), 0);
        checked = true;
      });
      checkpoint->enableTimer(interval);
    });
    EXPECT_TRUE(waitFor([&]() { return checked.load(); }));
    // Destroy the timer on its dispatcher, after its callback has unwound.
    executeInDispatcher([&]() { checkpoint.reset(); });
  }

  void limitSocketBuffers() {
    executeInDispatcher([&]() {
      const int size = 4096;
      EXPECT_TRUE(
          conn_->ioHandle()
              .setSocketOption(SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))
              .ok());
      EXPECT_TRUE(
          peer_->setSocketOption(SOL_SOCKET, SO_RCVBUF, &size, sizeof(size))
              .ok());
    });
  }

  void expectClosed(std::chrono::milliseconds timeout = 2s) {
    EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }, timeout));
    executeInDispatcher([&]() {
      EXPECT_EQ(conn_->state(), ConnectionState::Closed);
      EXPECT_FALSE(conn_->ioHandle().isOpen());
      EXPECT_EQ(events_.close_count.load(), 1);
    });
  }

  void expectClosedWithResponse(const std::string& expected) {
    expectClosed();
    std::string response;
    bool eof = false;
    EXPECT_TRUE(waitFor([&]() {
      OwnedBuffer buffer;
      auto result = peer_->read(buffer, 4096);
      if (result.ok()) {
        response += buffer.toString();
        eof = *result == 0;
      }
      return eof;
    })) << "peer never received EOF";
    EXPECT_EQ(response, expected);
  }

  stream_info::StreamInfoImpl stream_info_;
  ClosedEvents events_;
  std::unique_ptr<ConnectionImpl> conn_;
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

// A write filter failing on what the application sends ends its
// connection too. Writes come from posted tasks, not socket events, so
// this is a separate way for an exception to reach the event loop.
TEST_F(ConnectionCloseWhileReadingTest, AFailingWriterClosesOnlyItsConnection) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed),
          std::make_shared<ThrowingWriter>());

  EXPECT_NO_THROW(executeInDispatcher([this]() {
    OwnedBuffer out;
    out.add("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    conn_->write(out, false);
  })) << "a failed write escaped into the event loop";

  EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }))
      << "the connection stayed open after its write filter failed";
  std::atomic<bool> ran{false};
  executeInDispatcher([&]() { ran = true; });
  EXPECT_TRUE(ran.load());
}

TEST_F(ConnectionCloseWhileReadingTest,
       FlushWriteWithEmptyBufferClosesSocketOnce) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    conn_->close(ConnectionCloseType::FlushWrite);
    conn_->close(ConnectionCloseType::FlushWrite);
    conn_->close(ConnectionCloseType::NoFlush);
  });
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest,
       FlushWriteFromReaderDeliversReplyAndCloses) {
  Observed observed;
  const std::string response =
      "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: "
      "close\r\n\r\n";
  connect(std::make_shared<ClosingReader>(
      observed, ConnectionCloseType::FlushWrite, response));
  send("GET /mcp HTTP/1.1\r\nHost: localhost\r\n\r\n");
  ASSERT_TRUE(waitFor([&]() { return observed.read.load(); }));
  EXPECT_EQ(observed.before.load(), observed.after_close.load());
  EXPECT_FALSE(observed.drain_threw.load());
  expectClosedWithResponse(response);
}

TEST_F(ConnectionCloseWhileReadingTest,
       FlushWriteResumesQueuedReplyAfterBackpressure) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  const std::string response =
      "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n";
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->close(ConnectionCloseType::FlushWrite);
    EXPECT_EQ(conn_->state(), ConnectionState::Closing);
    EXPECT_EQ(controlled->close_count, 0);
    EXPECT_TRUE(conn_->ioHandle().isOpen());
  });
  executeInDispatcher([&]() { controlled->blocked = false; });
  expectClosedWithResponse(response);
  executeInDispatcher([&]() { EXPECT_EQ(controlled->close_count, 1); });
}

class DelayedFlushHalfCloseTest : public ConnectionCloseWhileReadingTest,
                                  public ::testing::WithParamInterface<bool> {};

TEST_P(DelayedFlushHalfCloseTest, PeerEofWaitsForTheQueuedReplyToFlush) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  const std::string response = "complete response";
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    ASSERT_TRUE(peer_->shutdown(SHUT_WR).ok());
    // Cover the write-ready registration and a read callback that was already
    // queued when the connection started flushing.
    conn_->onWriteReady();
    EXPECT_FALSE(ConnectionImplTestPeer::readEventsEnabled(*conn_));
    if (GetParam()) {
      conn_->onReadReady();
    }
  });
  expectClosingAfter(40ms);
  executeInDispatcher([&]() {
    EXPECT_EQ(conn_->writeBuffer().toString(), response);
    controlled->blocked = false;
    conn_->onWriteReady();
  });
  expectClosedWithResponse(response);
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::RemoteClose);
}

INSTANTIATE_TEST_SUITE_P(QueuedAndReadReady,
                         DelayedFlushHalfCloseTest,
                         ::testing::Bool());

TEST_F(ConnectionCloseWhileReadingTest,
       DelayedCloseFromReadFilterFlushesBeforeReadingPeerEof) {
  Observed observed;
  const std::string response =
      "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nreply";
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(
              observed, ConnectionCloseType::FlushWriteAndDelay, response),
          nullptr, std::move(transport));
  executeInDispatcher([&]() { controlled->blocked = true; });
  send("GET / HTTP/1.0\r\n\r\n");
  executeInDispatcher([&]() { ASSERT_TRUE(peer_->shutdown(SHUT_WR).ok()); });
  ASSERT_TRUE(waitFor([&]() { return observed.read.load(); }));
  expectClosingAfter(40ms);
  executeInDispatcher([&]() {
    EXPECT_EQ(conn_->writeBuffer().toString(), response);
    controlled->blocked = false;
    conn_->onWriteReady();
  });
  expectClosedWithResponse(response);
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::RemoteClose);
}

TEST_F(ConnectionCloseWhileReadingTest,
       DelayedCloseTimeoutStillBoundsAStalledReplyAfterPeerEof) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(200ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    ASSERT_TRUE(peer_->shutdown(SHUT_WR).ok());
  });
  expectClosingAfter(40ms);
  expectClosedWithResponse("");
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::LocalClose);
}

TEST_F(ConnectionCloseWhileReadingTest,
       DelayedCloseTimeoutReleasesBlockedSocket) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(10ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    EXPECT_EQ(conn_->state(), ConnectionState::Closing);
  });
  expectClosedWithResponse("");
}

class DelayedConnectionCloseTest : public ConnectionCloseWhileReadingTest,
                                   public ::testing::WithParamInterface<bool> {
};

TEST_P(DelayedConnectionCloseTest,
       DefaultFlushDeadlineBoundsAReplyFromReadFilters) {
  Observed observed;
  const std::string response =
      "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: "
      "close\r\n\r\n";
  connect(std::make_shared<ClosingReader>(
              observed, ConnectionCloseType::FlushWrite, response),
          nullptr, nullptr, GetParam());
  limitSocketBuffers();
  executeInDispatcher([&]() {
    // The peer stops reading with an earlier response still queued. The
    // final 401 reply cannot drain, even though it is small on its own.
    OwnedBuffer buffer;
    buffer.add(std::string(1024 * 1024, 'x'));
    conn_->write(buffer, false);
  });
  send("GET /mcp HTTP/1.1\r\nHost: localhost\r\n\r\n");
  ASSERT_TRUE(waitFor([&]() { return observed.read.load(); }));
  executeInDispatcher([&]() {
    EXPECT_EQ(conn_->state(), ConnectionState::Closing);
    EXPECT_GT(conn_->writeBuffer().length(), 0u);
    EXPECT_TRUE(ConnectionImplTestPeer::delayedCloseTimerEnabled(*conn_));
    // Freeze readiness after backpressure: kernel receive-buffer tuning can
    // otherwise allow more writes and legitimately restart the deadline.
    ConnectionImplTestPeer::disableFileEvents(*conn_);
  });

  // Both listener paths use this accepted-connection factory. Leave the
  // timeout unconfigured to verify that its default bounds the stalled fd.
  expectClosingAfter(100ms);
  expectClosed(7s);
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::LocalClose);
}

TEST_P(DelayedConnectionCloseTest, DefaultDelayedCloseWaitsForPeerEof) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  executeInDispatcher([&]() {
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    EXPECT_TRUE(ConnectionImplTestPeer::delayedCloseTimerEnabled(*conn_));
  });
  expectClosingAfter(40ms);
  executeInDispatcher([&]() { peer_->close(); });
  expectClosed();
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::RemoteClose);
}

TEST_P(DelayedConnectionCloseTest, FlushWriteTimesOutWhenThePeerStopsReading) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  limitSocketBuffers();
  executeInDispatcher([&]() {
    conn_->setDelayedCloseTimeout(100ms);
    OwnedBuffer buffer;
    buffer.add(std::string(1024 * 1024, 'x'));
    conn_->write(buffer, false);
    conn_->close(ConnectionCloseType::FlushWrite);
    // The peer never reads: both the kernel buffers and the connection's
    // pending reply remain full until the flush timeout closes the socket.
    EXPECT_GT(conn_->writeBuffer().length(), 0u);
    EXPECT_EQ(conn_->state(), ConnectionState::Closing);
  });

  expectClosed();
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::LocalClose);
}

TEST_P(DelayedConnectionCloseTest, EmptyBufferWaitsForTheCloseTimeout) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  executeInDispatcher([&]() {
    conn_->setDelayedCloseTimeout(200ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });

  expectClosingAfter(40ms);
  expectClosedWithResponse("");
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::LocalClose);
}

TEST_P(DelayedConnectionCloseTest, DrainedReplyKeepsTheCloseGracePeriod) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  const std::string response = "complete response";
  executeInDispatcher([&]() {
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(200ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    EXPECT_EQ(conn_->writeBuffer().length(), 0u);
  });

  expectClosingAfter(40ms);
  expectClosedWithResponse(response);
}

TEST_P(DelayedConnectionCloseTest, PeerEofEndsTheWaitBeforeItsTimeout) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  executeInDispatcher([&]() {
    conn_->setDelayedCloseTimeout(5s);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });
  expectClosingAfter(20ms);
  executeInDispatcher([&]() { peer_->close(); });

  EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }))
      << "peer EOF did not end the grace period";
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::RemoteClose);
  EXPECT_EQ(events_.close_count.load(), 1);
}

TEST_P(DelayedConnectionCloseTest, InputDuringTheWaitDoesNotReachReadFilters) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  executeInDispatcher([&]() {
    conn_->setDelayedCloseTimeout(5s);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });
  send("another request while the connection is closing");
  expectClosingAfter(40ms);

  executeInDispatcher([&]() {
    EXPECT_FALSE(observed.read.load());
    EXPECT_EQ(conn_->readBuffer().length(), 0u);
    // The input was consumed, rather than left triggering read readiness.
    OwnedBuffer pending;
    EXPECT_TRUE(conn_->ioHandle().read(pending).wouldBlock());
    conn_->close(ConnectionCloseType::NoFlush);
  });
  expectClosedWithResponse("");
}

TEST_P(DelayedConnectionCloseTest, FlushWriteCanOverrideTheWait) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed), nullptr, nullptr,
          GetParam());
  executeInDispatcher([&]() {
    conn_->setDelayedCloseTimeout(5s);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });
  expectClosingAfter(20ms);
  executeInDispatcher([&]() { conn_->close(ConnectionCloseType::FlushWrite); });
  expectClosedWithResponse("");
}

INSTANTIATE_TEST_SUITE_P(TransportAndDirectIo,
                         DelayedConnectionCloseTest,
                         ::testing::Bool());

TEST_F(ConnectionCloseWhileReadingTest,
       FlushTimeoutIsNotExtendedByRepeatedCloseOrWriteReadiness) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  event::TimerPtr retries;
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(100ms);
    conn_->close(ConnectionCloseType::FlushWrite);
    retries = dispatcher_->createTimer([&]() {
      if (!events_.closed.load()) {
        conn_->close(ConnectionCloseType::FlushWrite);
        static_cast<ConnectionImpl*>(conn_.get())->onWriteReady();
        retries->enableTimer(10ms);
      }
    });
    retries->enableTimer(10ms);
  });

  const bool closed = waitFor([&]() { return events_.closed.load(); });
  executeInDispatcher([&]() {
    retries.reset();
    if (!closed) {
      conn_->close(ConnectionCloseType::NoFlush);
    }
  });
  EXPECT_TRUE(closed) << "a flush without write progress never timed out";
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest,
       SwitchingToFlushWriteKeepsThePendingTimeout) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(500ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });
  expectClosingAfter(300ms);
  executeInDispatcher([&]() { conn_->close(ConnectionCloseType::FlushWrite); });

  // Switching the close mode must neither cancel nor restart the original
  // deadline, which is now less than 300 ms away.
  EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }, 300ms));
  executeInDispatcher([&]() { conn_->close(ConnectionCloseType::NoFlush); });
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest,
       PartialFlushWriteProgressRestartsTheTimeout) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  limitSocketBuffers();
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(std::string(1024 * 1024, 'x'));
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(500ms);
    conn_->close(ConnectionCloseType::FlushWrite);
  });
  expectClosingAfter(250ms);
  executeInDispatcher([&]() {
    const auto queued = conn_->writeBuffer().length();
    controlled->blocked = false;
    static_cast<ConnectionImpl*>(conn_.get())->onWriteReady();
    controlled->blocked = true;
    EXPECT_LT(conn_->writeBuffer().length(), queued);
    EXPECT_GT(conn_->writeBuffer().length(), 0u);
  });
  // Partial progress earns a fresh inactivity window even though the
  // response is still blocked and cannot finish flushing.
  expectClosingAfter(300ms);
  expectClosed();
}

TEST_F(ConnectionCloseWhileReadingTest, ZeroTimeoutDisablesTheFlushDeadline) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  const std::string response = "complete response";
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(0ms);
    conn_->close(ConnectionCloseType::FlushWrite);
    EXPECT_FALSE(ConnectionImplTestPeer::delayedCloseTimerEnabled(*conn_));
  });
  expectClosingAfter(40ms);
  executeInDispatcher([&]() { controlled->blocked = false; });
  expectClosedWithResponse(response);
}

TEST_F(ConnectionCloseWhileReadingTest,
       CompletedFlushCancelsTheTimeoutBeforeConnectionDestruction) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  const std::string response = "complete response";
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(200ms);
    conn_->close(ConnectionCloseType::FlushWrite);
  });
  expectClosingAfter(20ms);
  executeInDispatcher([&]() {
    controlled->blocked = false;
    static_cast<ConnectionImpl*>(conn_.get())->onWriteReady();
  });
  // A successful flush closes immediately, without waiting for its timeout.
  EXPECT_TRUE(waitFor([&]() { return events_.closed.load(); }, 100ms));
  expectClosedWithResponse(response);
  executeInDispatcher([&]() { conn_.reset(); });
  std::atomic<bool> checked{false};
  event::TimerPtr checkpoint;
  executeInDispatcher([&]() {
    checkpoint = dispatcher_->createTimer([&]() { checked = true; });
    checkpoint->enableTimer(250ms);
  });
  EXPECT_TRUE(waitFor([&]() { return checked.load(); }));
  executeInDispatcher([&]() { checkpoint.reset(); });
  EXPECT_EQ(events_.close_count.load(), 1);
}

TEST_F(ConnectionCloseWhileReadingTest,
       BackpressuredFlushGetsAFullGracePeriodAfterItsLastWrite) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  const std::string response = "complete response";
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(500ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });
  expectClosingAfter(250ms);
  executeInDispatcher([&]() {
    controlled->blocked = false;
    static_cast<ConnectionImpl*>(conn_.get())->onWriteReady();
    EXPECT_EQ(conn_->writeBuffer().length(), 0u);
  });
  // Beyond the original deadline, but still inside the grace period that
  // starts when the response is actually flushed.
  expectClosingAfter(300ms);
  expectClosedWithResponse(response);
}

TEST_F(ConnectionCloseWhileReadingTest,
       ZeroDelayStillFlushesAReplyAfterBackpressure) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  const std::string response = "complete response";
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add(response);
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(0ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
  });
  expectClosingAfter(20ms);
  executeInDispatcher([&]() { controlled->blocked = false; });
  expectClosedWithResponse(response);
}

TEST_F(ConnectionCloseWhileReadingTest, WriteFailureEndsADelayedClose) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    conn_->setDelayedCloseTimeout(5s);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    controlled->blocked = false;
    controlled->fail_write = true;
  });
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest,
       NoFlushCancelsTheDelayBeforeConnectionDestruction) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    conn_->setDelayedCloseTimeout(10ms);
    conn_->close(ConnectionCloseType::FlushWriteAndDelay);
    conn_->close(ConnectionCloseType::NoFlush);
    conn_.reset();
  });
  std::atomic<bool> checked{false};
  event::TimerPtr checkpoint;
  executeInDispatcher([&]() {
    checkpoint = dispatcher_->createTimer([&]() { checked = true; });
    checkpoint->enableTimer(30ms);
  });
  EXPECT_TRUE(waitFor([&]() { return checked.load(); }));
  executeInDispatcher([&]() { checkpoint.reset(); });
  EXPECT_EQ(events_.close_count.load(), 1);
}

TEST_F(ConnectionCloseWhileReadingTest, NoFlushCanFinishAPendingFlushClose) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    conn_->close(ConnectionCloseType::FlushWrite);
    conn_->close(ConnectionCloseType::NoFlush);
  });
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest, WriteFailureDuringFlushStillCloses) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->blocked = true;
    OwnedBuffer buffer;
    buffer.add("pending response");
    conn_->write(buffer, false);
    controlled->blocked = false;
    controlled->fail_write = true;
    conn_->close(ConnectionCloseType::FlushWrite);
  });
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest, ReentrantTransportCloseRunsOnlyOnce) {
  Observed observed;
  auto transport = std::make_unique<ControlledTransport>();
  auto* controlled = transport.get();
  connect(std::make_shared<ClosingReader>(observed), nullptr,
          std::move(transport));
  executeInDispatcher([&]() {
    controlled->on_close = [&]() {
      conn_->close(ConnectionCloseType::NoFlush);
    };
    conn_->close(ConnectionCloseType::NoFlush);
    EXPECT_EQ(controlled->close_count, 1);
  });
  expectClosedWithResponse("");
}

class ReentrantConnectionCloseTest
    : public ConnectionCloseWhileReadingTest,
      public ::testing::WithParamInterface<ConnectionCloseType> {
 protected:
  void expectReentrantCloseIgnored(const std::string& response) {
    Observed observed;
    auto transport = std::make_unique<ControlledTransport>();
    auto* controlled = transport.get();
    connect(std::make_shared<ClosingReader>(observed), nullptr,
            std::move(transport));
    executeInDispatcher([&]() {
      controlled->blocked = true;
      if (!response.empty()) {
        OwnedBuffer buffer;
        buffer.add(response);
        conn_->write(buffer, false);
      }
      conn_->setDelayedCloseTimeout(500ms);
      controlled->on_close = [&]() {
        // The transport is still writable here, but teardown has already
        // started and cancelled timers. A nested close must not restart IO.
        controlled->blocked = false;
        const auto writes = controlled->write_count;
        EXPECT_FALSE(ConnectionImplTestPeer::delayedCloseTimerEnabled(*conn_));
        conn_->close(GetParam());
        EXPECT_EQ(controlled->write_count, writes);
        EXPECT_EQ(conn_->writeBuffer().length(), response.size());
        EXPECT_FALSE(ConnectionImplTestPeer::delayedCloseTimerEnabled(*conn_));
      };
      conn_->close(ConnectionCloseType::NoFlush);
      EXPECT_EQ(controlled->close_count, 1);
      EXPECT_FALSE(ConnectionImplTestPeer::delayedCloseTimerEnabled(*conn_));
    });
    // NoFlush must discard the pending reply even if the transport asks to
    // flush reentrantly while it is being closed.
    expectClosedWithResponse("");
  }
};

TEST_P(ReentrantConnectionCloseTest, QueuedReplyIsNotWrittenDuringTeardown) {
  expectReentrantCloseIgnored("pending response");
}

TEST_P(ReentrantConnectionCloseTest, EmptyCloseDoesNotRestartIOOrTheTimer) {
  expectReentrantCloseIgnored("");
}

INSTANTIATE_TEST_SUITE_P(
    FlushModes,
    ReentrantConnectionCloseTest,
    ::testing::Values(ConnectionCloseType::FlushWrite,
                      ConnectionCloseType::FlushWriteAndDelay));

TEST_F(ConnectionCloseWhileReadingTest,
       DeferredFlushCloseDoesNotOutliveConnection) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    OwnedBuffer buffer;
    buffer.add("complete response");
    conn_->write(buffer, false);
    conn_->close(ConnectionCloseType::FlushWrite);
    conn_->close(ConnectionCloseType::NoFlush);
    conn_.reset();
  });
  executeInDispatcher([]() {});
  EXPECT_EQ(events_.close_count.load(), 1);
}

TEST_F(ConnectionCloseWhileReadingTest, ConnectFailurePostsRemoteClose) {
  failConnect([](ConnectionImpl& connection) {
    EXPECT_EQ(connection.state(), ConnectionState::Open);
  });
  expectClosed();
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::RemoteClose);
}

TEST_F(ConnectionCloseWhileReadingTest, ConnectFailurePostClosesAPendingFlush) {
  failConnect([](ConnectionImpl& connection) {
    OwnedBuffer response;
    response.add("pending response");
    connection.write(response, false);
    connection.close(ConnectionCloseType::FlushWrite);
    EXPECT_EQ(connection.state(), ConnectionState::Closing);
    EXPECT_GT(connection.writeBuffer().length(), 0u);
  });
  expectClosed();
  EXPECT_EQ(events_.last_close_event.load(), ConnectionEvent::RemoteClose);
}

TEST_F(ConnectionCloseWhileReadingTest,
       ConnectFailurePostDoesNotOutliveConnection) {
  failConnect([&](ConnectionImpl&) { conn_.reset(); });
  executeInDispatcher([]() {});
  EXPECT_EQ(events_.close_count.load(), 1);
}

TEST_F(ConnectionCloseWhileReadingTest,
       ReadBufferReadyProcessesBufferedDataWhileAlive) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    conn_->readBuffer().add("buffered request");
    ConnectionImplTestPeer::setReadBufferReady(
        *static_cast<ConnectionImpl*>(conn_.get()));
  });
  executeInDispatcher([]() {});
  EXPECT_TRUE(observed.read.load());
  EXPECT_EQ(observed.before.load(), observed.after_close.load());
  EXPECT_FALSE(observed.drain_threw.load());
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest,
       ReadBufferReadySkipsReadDisabledConnection) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    conn_->readDisable(true);
    conn_->readBuffer().add("buffered request");
    ConnectionImplTestPeer::setReadBufferReady(
        *static_cast<ConnectionImpl*>(conn_.get()));
  });
  executeInDispatcher([&]() {
    EXPECT_FALSE(observed.read.load());
    EXPECT_EQ(conn_->readBuffer().toString(), "buffered request");
    conn_->close(ConnectionCloseType::NoFlush);
  });
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest, ReadBufferReadySkipsClosedConnection) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    conn_->readBuffer().add("buffered request");
    ConnectionImplTestPeer::setReadBufferReady(
        *static_cast<ConnectionImpl*>(conn_.get()));
    conn_->close(ConnectionCloseType::NoFlush);
  });
  executeInDispatcher([]() {});
  EXPECT_FALSE(observed.read.load());
  expectClosedWithResponse("");
}

TEST_F(ConnectionCloseWhileReadingTest,
       ReadBufferReadyDoesNotOutliveConnection) {
  Observed observed;
  connect(std::make_shared<ClosingReader>(observed));
  executeInDispatcher([&]() {
    conn_->readBuffer().add("buffered request");
    ConnectionImplTestPeer::setReadBufferReady(
        *static_cast<ConnectionImpl*>(conn_.get()));
    conn_.reset();
  });
  executeInDispatcher([]() {});
  EXPECT_FALSE(observed.read.load());
  EXPECT_EQ(events_.close_count.load(), 1);
}

}  // namespace
}  // namespace network
}  // namespace mcp
