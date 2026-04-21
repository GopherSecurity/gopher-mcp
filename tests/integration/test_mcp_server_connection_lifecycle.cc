/**
 * Integration test: McpServer connection-lifecycle cleanup + shutdown drain.
 *
 * The observable contract under test (the drain fix in McpServer::shutdown):
 *
 *   1. Connect/close cycles against the server don't wedge the dispatcher.
 *      If the per-connection ConnectionLifecycleCallbacks adapter failed to
 *      erase its entry from active_connections_/lifecycle_callbacks_ on
 *      RemoteClose, subsequent connections would still work (they get their
 *      own slots) but the server would leak entries that outlive the
 *      dispatcher into ~McpServer. We stress this with three sequential
 *      connect/close cycles and require the listener to keep accepting.
 *
 *   2. McpServer::shutdown() tears down with a still-open connection --
 *      this is the drain path the fix added. The shutdown post clears
 *      active_connections_ on the dispatcher thread so each ConnectionImpl
 *      destructor fires closeSocket(LocalClose) there, rather than on
 *      whatever thread ends up running ~McpServer. Two observable effects:
 *
 *        a. The client socket, still open on the test thread, reads 0
 *           (EOF) after the server shuts down -- proof the server-side
 *           fd was actually closed by the drain.
 *
 *        b. ~McpServer returns normally. Pre-fix, ~McpServer destroyed
 *           active_connections_ on the caller thread, which triggered
 *           closeSocket(LocalClose) -> the lifecycle adapter ->
 *           McpServer::onConnectionLifecycleEvent, whose first line
 *           asserts dispatcher-thread. The test process crashed at
 *           teardown. With the fix, teardown is clean.
 *
 * We drive the lifecycle with a raw TCP client rather than a full
 * McpClient because the lifecycle adapter only observes transport-level
 * ConnectionEvent values; no HTTP or JSON-RPC parsing is required to
 * exercise the drain contract, and keeping the client dumb isolates the
 * test to the server-side callback routing.
 */

#include <sys/socket.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/network/address.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/socket_interface.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

// Bind ephemeral 0 to get a port the kernel thinks is free, then let go
// of it and hand the number to the server. Same mild TOCTOU as the other
// integration tests -- accepted on a loopback test bed.
uint16_t pickEphemeralPort() {
  auto& iface = network::socketInterface();

  auto fd_result = iface.socket(network::SocketType::Stream,
                                network::Address::Type::Ip,
                                network::Address::IpVersion::v4);
  if (!fd_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: socket() failed");
  }

  auto handle = iface.ioHandleForFd(*fd_result, /*socket_v6only=*/false);
  handle->setBlocking(false);

  auto bind_addr = network::Address::parseInternetAddress("127.0.0.1", 0);
  auto bind_result = handle->bind(bind_addr);
  if (!bind_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: bind() failed");
  }

  auto local_addr_result = handle->localAddress();
  if (!local_addr_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: localAddress() failed");
  }

  const auto* ip =
      dynamic_cast<const network::Address::Ip*>(local_addr_result->get());
  if (ip == nullptr) {
    throw std::runtime_error("pickEphemeralPort: not an IP address");
  }
  uint16_t port = ip->port();
  handle->close();
  return port;
}

class McpServerConnectionLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = pickEphemeralPort();

    server::McpServerConfig config;
    config.server_name = "lifecycle-test-server";
    config.server_version = "0.0.1";
    config.supported_transports = {TransportType::HttpSse};
    config.num_workers = 1;

    server_ = server::createMcpServer(config);
    ASSERT_NE(server_, nullptr);

    const std::string listen_address =
        "http://127.0.0.1:" + std::to_string(port_);
    auto listen_result = server_->listen(listen_address);
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(listen_result))
        << "McpServer::listen failed";

    // run() blocks; every dispatcher-thread callback in this test runs
    // inside that thread.
    server_thread_ = std::thread([this]() { server_->run(); });

    ASSERT_TRUE(waitForListenerReady(port_, 5s))
        << "Server did not begin accepting on port " << port_;

    // The readiness probe in waitForListenerReady opens a TCP
    // connection and closes it. The kernel accepts the SYN and queues
    // the connection; the server-side onNewConnection may run slightly
    // after this point and bump connections_total / connections_active.
    // Wait for the counters to stop moving so tests that take a
    // baseline snapshot see a stable starting value. Polling for
    // "unchanged for one tick" is cheaper than guessing a sleep long
    // enough to cover the slowest machine.
    waitForConnectionStatsIdle(200ms, 2s);
  }

  void TearDown() override {
    // With the shutdown-drain fix in place, shutting down before
    // destroying the server is safe: the shutdown post clears
    // active_connections_ and lifecycle_callbacks_ on the dispatcher
    // thread, so ~McpServer finds empty containers. If a test already
    // shut the server down explicitly, shutdown() is a no-op.
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  // Open a blocking connection probe until the listener accepts or the
  // budget elapses. listen()/performListen() is two-step; the listener
  // only starts accepting once the dispatcher picks it up.
  static bool waitForListenerReady(uint16_t port,
                                   std::chrono::milliseconds budget) {
    auto& iface = network::socketInterface();
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      auto fd_result = iface.socket(network::SocketType::Stream,
                                    network::Address::Type::Ip,
                                    network::Address::IpVersion::v4);
      if (fd_result.ok()) {
        auto handle = iface.ioHandleForFd(*fd_result, false);
        handle->setBlocking(true);
        auto connect_result = handle->connect(addr);
        handle->close();
        if (connect_result.ok()) {
          return true;
        }
      }
      std::this_thread::sleep_for(25ms);
    }
    return false;
  }

  // Open a blocking TCP connection to the listener. Returned handle
  // owns the fd; dropping it (or calling close()) sends FIN and lets
  // the server see RemoteClose.
  network::IoHandlePtr openClient() {
    auto& iface = network::socketInterface();
    auto fd_result = iface.socket(network::SocketType::Stream,
                                  network::Address::Type::Ip,
                                  network::Address::IpVersion::v4);
    if (!fd_result.ok()) {
      return nullptr;
    }
    auto handle = iface.ioHandleForFd(*fd_result, /*socket_v6only=*/false);
    handle->setBlocking(true);
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port_);
    auto connect_result = handle->connect(addr);
    if (!connect_result.ok()) {
      handle->close();
      return nullptr;
    }
    return handle;
  }

  // Open a client whose close() will send RST rather than FIN. This is
  // achieved with SO_LINGER (l_onoff=1, l_linger=0): close() on a socket
  // with a zero-second linger pushes the kernel onto the abortive-close
  // path, flushing any queued bytes and emitting RST. This exercises the
  // server's EOF-detection on the Abort branch of the transport socket,
  // distinct from the graceful-FIN path covered by openClient().
  network::IoHandlePtr openClientAbort() {
    auto handle = openClient();
    if (!handle) {
      return nullptr;
    }
    struct linger l{};
    l.l_onoff = 1;
    l.l_linger = 0;
    auto opt_result = handle->setSocketOption(
        SOL_SOCKET, SO_LINGER, &l, static_cast<socklen_t>(sizeof(l)));
    if (!opt_result.ok()) {
      handle->close();
      return nullptr;
    }
    return handle;
  }

  // Poll a blocking read for EOF (return value of 0) within a budget.
  // When the server closes its side, the kernel sends FIN and the next
  // blocking read on the client side returns 0.
  static bool waitForEof(network::IoHandle& handle,
                         std::chrono::milliseconds budget) {
    handle.setBlocking(false);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = handle.read(buf, /*max_length=*/4096);
      if (r.ok()) {
        // read() returning 0 bytes on a non-error result is EOF on this
        // IoHandle abstraction.
        if (*r == 0) {
          return true;
        }
        // Got some bytes -- not what we expected, but keep polling.
      } else if (!r.wouldBlock()) {
        // Any non-wouldblock error on read means the peer is gone.
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }

  // Poll until connections_total has not changed for `quiet_for` or
  // the overall budget elapses. Used after SetUp to let any in-flight
  // probe accepts land before a test snapshots the baseline.
  void waitForConnectionStatsIdle(std::chrono::milliseconds quiet_for,
                                  std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    uint64_t last = server_->getServerStats().connections_total.load();
    auto stable_since = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(10ms);
      uint64_t now_total = server_->getServerStats().connections_total.load();
      if (now_total != last) {
        last = now_total;
        stable_since = std::chrono::steady_clock::now();
        continue;
      }
      if (std::chrono::steady_clock::now() - stable_since >= quiet_for) {
        return;
      }
    }
  }

  // Wait for connections_active to match `expected` within the budget.
  // Poll because the counter is updated on the dispatcher thread in
  // response to async events.
  bool waitForActiveConnections(uint64_t expected,
                                std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      if (server_->getServerStats().connections_active.load() == expected) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return server_->getServerStats().connections_active.load() == expected;
  }

  uint16_t port_{0};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
};

// connections_active / connections_total are maintained correctly for
// server-accepted TCP sockets. ConnectionImpl does not raise Connected
// for server sockets -- it is only raised on the client-side
// connect-completion path -- so the increment can't piggyback on the
// lifecycle adapter's Connected branch. Instead it lives in
// McpServer::onNewConnection. This test pins that: the counter has to
// go up on connect and back down on close, symmetrically, across
// multiple concurrent connections.
TEST_F(McpServerConnectionLifecycleTest, AcceptedConnectionsAreCounted) {
  // Snapshot the baseline rather than assume zero. waitForListenerReady
  // in SetUp opens and closes a probe socket, and the kernel may queue
  // that as a server-accepted connection whose close does not reach
  // onConnectionLifecycleEvent within the test budget (ConnectionImpl
  // on a server socket registers the read-EOF listener only once the
  // filter chain is wired up). Measuring deltas against whatever the
  // server has already observed keeps the test honest without depending
  // on the probe's teardown.
  const auto& stats = server_->getServerStats();
  const uint64_t base_active = stats.connections_active.load();
  const uint64_t base_total = stats.connections_total.load();

  auto a = openClient();
  auto b = openClient();
  auto c = openClient();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);

  // onNewConnection runs on the server's dispatcher thread in response
  // to the listener's accept; poll until each of the three accepts has
  // been counted (relative to the baseline).
  ASSERT_TRUE(waitForActiveConnections(base_active + 3u, 2s))
      << "connections_active never reached baseline+3 after three "
         "client connects";
  EXPECT_GE(stats.connections_total.load(), base_total + 3u);

  // Shut the server down. The drain path closes each live connection
  // on the dispatcher thread (LocalClose), which runs through
  // onConnectionLifecycleEvent and decrements connections_active. If
  // the pre-fix code path were still in place the counter would not
  // have been incremented on accept, so the drain's N decrements would
  // wrap it into UINT64_MAX; observing it return cleanly to zero is a
  // direct proof that increment and decrement agree.
  //
  // We use the drain path rather than client-initiated close because a
  // raw TCP client that never sent any application bytes may not drive
  // the server-side ConnectionImpl to observe the FIN promptly -- that
  // is an HTTP filter behavior we don't want to couple the stats test
  // to. The drain is deterministic and dispatcher-driven.
  server_->shutdown();
  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  EXPECT_EQ(stats.connections_active.load(), 0u)
      << "connections_active did not return to zero after server drain";

  // total is monotonic -- never decremented -- and must reflect every
  // accept we observed, regardless of what closed.
  EXPECT_GE(stats.connections_total.load(), base_total + 3u);

  // Drop the clients so the test process releases their fds cleanly.
  // The server is already gone, so their reads would return EOF.
  a->close();
  a.reset();
  b->close();
  b.reset();
  c->close();
  c.reset();

  // TearDown's shutdown() will no-op on the already-stopped server.
}

// Peer FIN on a raw TCP client must propagate up to the server's
// connection-lifecycle callback in bounded time, so the server can
// reclaim connection state (active_connections_, stats) without
// depending on an idle-timeout.
//
// The observable contract is: once the client sends FIN,
// connections_active drops back to its baseline well within the TCP
// keepalive / idle-timeout window. The fix this test guards lives in
// ConnectionImpl::closeThroughFilterManager(): the read path detects
// EOF from the transport socket, calls closeThroughFilterManager, and
// that helper must queue the deferred close on the dispatcher such
// that the callback survives stack unwinding of the current doRead()
// frame. A prior implementation used a stack-local Timer, which was
// destroyed (and the libevent timer cancelled) as soon as the helper
// returned — so the RemoteClose event never propagated and the
// connection leaked until the TearDown-driven shutdown.
//
// Two variants are covered to exercise both branches of the doRead
// loop: the "silent close" path goes through
// RawBufferTransportSocket::doRead -> endStream(0), while the
// "after-write" path exercises the same EOF detection after the
// filter chain has already been engaged with real data.
TEST_F(McpServerConnectionLifecycleTest, RawClientSilentCloseDropsConnection) {
  const auto& stats = server_->getServerStats();
  const uint64_t base = stats.connections_active.load();

  auto client = openClient();
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(waitForActiveConnections(base + 1u, 2s));

  client->close();
  client.reset();

  // Generous bound: the fix drops the connection in single-digit
  // milliseconds on loopback. 2s is plenty for loaded CI while still
  // catching the "deferred close never runs" regression, which
  // previously manifested as a 10s+ hang.
  ASSERT_TRUE(waitForActiveConnections(base, 2s))
      << "Server did not observe peer FIN; closeThroughFilterManager "
         "likely dropped its deferred-close callback.";
}

TEST_F(McpServerConnectionLifecycleTest, RawClientCloseAfterWriteDropsConnection) {
  const auto& stats = server_->getServerStats();
  const uint64_t base = stats.connections_active.load();

  auto client = openClient();
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(waitForActiveConnections(base + 1u, 2s));

  // Send a plausible-looking HTTP prefix so the server's filter chain
  // engages before FIN arrives. This exercises the EOF path after at
  // least one successful read, which is a different doRead() trip
  // than the silent-close case.
  OwnedBuffer out;
  out.add("GET / HTTP/1.1\r\n\r\n", 18);
  auto w = client->write(out);
  ASSERT_TRUE(w.ok()) << "client write failed";

  client->close();
  client.reset();

  ASSERT_TRUE(waitForActiveConnections(base, 2s))
      << "Server did not observe peer FIN after write; deferred-close "
         "callback likely cancelled.";
}

// Abortive client close (RST) must drop the server-side connection in
// the same bounded time as graceful FIN. A raw TCP client with SO_LINGER
// set to (on, 0) turns close() into an abortive close: the kernel sends
// RST and flushes any queued bytes, rather than the four-way FIN dance.
//
// On the server side this surfaces as ECONNRESET on the next read, which
// RawBufferTransportSocket::doRead maps to a transport error. The
// connection's error-detection path must still run the same deferred-
// close-through-dispatcher post that FIN does — otherwise an aborting
// client (misbehaving proxy, killed peer, network reset) would leave the
// server holding the connection until an idle-timeout we don't currently
// enforce on accepted sockets.
//
// This variant pairs with RawClientSilentCloseDropsConnection to pin
// both EOF branches of the server-side teardown: the graceful (FIN →
// endStream) path and the abortive (RST → transport error) path end in
// the same closeThroughFilterManager post and drop connections_active
// symmetrically.
TEST_F(McpServerConnectionLifecycleTest, RawClientAbortDropsConnection) {
  const auto& stats = server_->getServerStats();
  const uint64_t base = stats.connections_active.load();

  auto client = openClientAbort();
  ASSERT_NE(client, nullptr) << "failed to open client with SO_LINGER=0";
  ASSERT_TRUE(waitForActiveConnections(base + 1u, 2s));

  // Closing the handle with SO_LINGER (1, 0) set sends RST on this
  // socket. The kernel does not perform the FIN handshake, so the
  // server sees a connection reset on its next read attempt.
  client->close();
  client.reset();

  ASSERT_TRUE(waitForActiveConnections(base, 2s))
      << "Server did not observe RST; abortive-close teardown likely "
         "skipped the deferred-close post that the FIN path uses.";
}

// Repeated connect/close cycles don't wedge the listener. Each cycle
// relies on the lifecycle adapter firing RemoteClose on the dispatcher
// and erasing the connection from active_connections_ /
// lifecycle_callbacks_ on the deferred-delete queue. If that path ever
// failed to run, entries would accumulate and the third connect might
// still succeed at the TCP layer but leave dead state behind -- which
// would crash ~McpServer during TearDown.
TEST_F(McpServerConnectionLifecycleTest, RepeatedConnectCloseCyclesStayStable) {
  for (int i = 0; i < 3; ++i) {
    auto client = openClient();
    ASSERT_NE(client, nullptr) << "connect #" << i << " failed";
    // Drop the handle -> FIN -> server sees RemoteClose on its
    // dispatcher, runs the unwind branch of onConnectionLifecycleEvent,
    // and deferred-deletes the adapter + connection. Give the
    // dispatcher a beat to actually pick up the close before the next
    // iteration opens a fresh connection.
    client->close();
    client.reset();
    std::this_thread::sleep_for(50ms);
  }

  // The server should still be accepting. If the lifecycle path had
  // wedged, the next connect would eventually hang or fail.
  auto sanity = openClient();
  EXPECT_NE(sanity, nullptr) << "listener stopped accepting after 3 cycles";
  if (sanity) {
    sanity->close();
  }
}

// The shutdown-drain contract. Hold a connection open, shut the server
// down, and verify two things that are only true if the drain ran:
//
//   1. The client-side read returns EOF. The drain called
//      closeSocket(LocalClose) on the server side, which closed the
//      fd, which shows up on the client as FIN.
//
//   2. TearDown runs to completion without the process aborting.
//      Pre-fix, ~McpServer destroyed active_connections_ on the test
//      thread and the connection destructor fired LocalClose there,
//      which reaches onConnectionLifecycleEvent's dispatcher-thread
//      assert (mcp_server.cc:804) and kills the process.
TEST_F(McpServerConnectionLifecycleTest, ShutdownClosesLiveConnections) {
  auto client = openClient();
  ASSERT_NE(client, nullptr) << "initial TCP connect failed";

  // Small settle so the server has definitely added the connection
  // to active_connections_ before we initiate shutdown. 50ms is
  // plenty on loopback; not a real correctness dependency -- missing
  // the window just means the drain has nothing to do, which is fine.
  std::this_thread::sleep_for(50ms);

  // Kick off shutdown. Under the fix, the shutdown post clears
  // active_connections_ and lifecycle_callbacks_ on the dispatcher
  // thread; each ConnectionImpl destructor fires LocalClose there and
  // then closes the underlying fd.
  server_->shutdown();
  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  // Client must see EOF because the server actually closed its side.
  // A pure dispatcher-exit without draining would leave the server fd
  // open until ~McpServer -- and the test would only see EOF after
  // server_.reset(), which is also where the pre-fix assert fires.
  ASSERT_TRUE(waitForEof(*client, 2s))
      << "client never saw EOF -- server didn't close its side during drain";

  client->close();
  client.reset();

  // Explicit reset here, on purpose: the drain path is supposed to
  // leave active_connections_/lifecycle_callbacks_ empty by the time
  // the dispatcher has exited, so ~McpServer is a plain destruction
  // with no cross-thread callback hazards. If the assert fires, gtest
  // will report an abort; if the destructor returns normally, the
  // drain did what it was meant to.
  server_.reset();
  // Prevent TearDown's shutdown() from running against a moved-from
  // unique_ptr (server_.reset() already nulled it).
  SUCCEED();
}

}  // namespace
}  // namespace mcp
