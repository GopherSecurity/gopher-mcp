/**
 * Regression test for ConnectionPoolImpl::createNewConnection lifetime bug.
 *
 * Prior code constructed a PendingConnection on the caller's stack, set up a
 * timeout Timer whose callback captured `&pending` by reference, and only
 * afterwards std::move-pushed the PendingConnection onto pending_connections_.
 * The captured pointer therefore pointed at a destroyed stack slot by the time
 * the timer fired, giving a classic use-after-free on the timeout path.
 *
 * The companion cleanup in onConnectionTimeout used the non-member
 * std::remove_if + list::erase idiom, which moves list elements to compact
 * them — moving out from under a PendingConnection whose timer lambda is
 * currently executing. std::list::remove_if (member) unlinks nodes in place
 * and is the correct tool.
 *
 * This test drives the timeout path with real IO: a real dispatcher thread,
 * a real socket, and a connect() to TEST-NET-1 (192.0.2.0/24, RFC 5737) so
 * the SYN has nowhere to go. The short pool-level connection_timeout fires
 * long before the kernel's SYN retransmit budget, exercising exactly the
 * path that used to touch the destroyed stack slot. Running under ASAN
 * (-DENABLE_ASAN=ON) is the second half of the regression-net — the plain
 * build can miss a UAF whose reused stack memory happens to still hold
 * plausible-looking bytes.
 */

#include <chrono>
#include <memory>

#include <gtest/gtest.h>

#include "mcp/network/address.h"
#include "mcp/network/connection_manager.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/transport_socket.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace network {
namespace {

// Minimal client transport factory. The pool test only exercises the
// SYN-is-pending window — no bytes are ever read or written — so the
// transport can be a pure stub.
class StubClientTransportSocketFactory : public UniversalTransportSocketFactory {
 public:
  bool implementsSecureTransport() const override { return false; }
  std::string name() const override { return "stub-client"; }

  TransportSocketPtr createTransportSocket(
      TransportSocketOptionsSharedPtr /*options*/) const override {
    return std::make_unique<StubTransportSocket>();
  }

  void hashKey(std::vector<uint8_t>& key,
               TransportSocketOptionsSharedPtr /*options*/) const override {
    const std::string tag = "stub-client";
    key.insert(key.end(), tag.begin(), tag.end());
  }

  TransportSocketPtr createTransportSocket() const override {
    return createTransportSocket(nullptr);
  }

 private:
  class StubTransportSocket : public TransportSocket {
   public:
    void setTransportSocketCallbacks(
        TransportSocketCallbacks& callbacks) override {
      callbacks_ = &callbacks;
    }
    std::string protocol() const override { return "stub"; }
    std::string failureReason() const override { return ""; }
    bool canFlushClose() override { return true; }
    VoidResult connect(Socket&) override { return makeVoidSuccess(); }
    void closeSocket(ConnectionEvent) override {}
    TransportIoResult doRead(Buffer&) override {
      return TransportIoResult::success(0);
    }
    TransportIoResult doWrite(Buffer&, bool) override {
      return TransportIoResult::success(0);
    }
    void onConnected() override {}

    TransportSocketCallbacks* callbacks_{nullptr};
  };
};

// Records pool-level outcomes. Counters are touched only from the dispatcher
// thread; the test polls them from the main thread via waitFor() on atomics.
class RecordingPoolCallbacks : public ConnectionPool::Callbacks {
 public:
  void onPoolReady(ClientConnection&, const std::string&) override {
    ready_called_.fetch_add(1, std::memory_order_relaxed);
  }

  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     const std::string&) override {
    last_reason_.store(static_cast<int>(reason), std::memory_order_relaxed);
    failure_called_.fetch_add(1, std::memory_order_release);
  }

  std::atomic<int> ready_called_{0};
  std::atomic<int> failure_called_{0};
  std::atomic<int> last_reason_{-1};
};

class ConnectionPoolPendingTimeoutTest : public mcp::test::RealIoTestBase {};

// Exercises the timeout path that previously used-after-free'd a stack-local
// PendingConnection. With the fix, the timer callback dereferences a stable
// list-element address and onPoolFailure(Timeout) is delivered cleanly.
TEST_F(ConnectionPoolPendingTimeoutTest, TimeoutFiresCleanly) {
  RecordingPoolCallbacks callbacks;
  std::unique_ptr<ConnectionManagerImpl> manager;
  std::unique_ptr<ConnectionPoolImpl> pool;

  executeInDispatcher([&]() {
    ConnectionManagerConfig config;
    config.max_connections = 1;
    // Tight enough that a broken timeout path surfaces as a test hang-then-
    // failure rather than a multi-second wait, loose enough to not race with
    // libevent's timer quantization on a loaded CI host.
    config.connection_timeout = std::chrono::milliseconds(150);
    config.per_connection_buffer_limit = 64 * 1024;
    config.client_transport_socket_factory =
        std::make_shared<StubClientTransportSocketFactory>();

    manager = std::make_unique<ConnectionManagerImpl>(
        *dispatcher_, socketInterface(), config);

    // TEST-NET-1 (RFC 5737) — reserved for documentation, guaranteed
    // non-routable in any sane network. The kernel will retransmit SYNs
    // for tens of seconds before giving up, which is orders of magnitude
    // longer than our 150ms pool timeout. The timeout timer wins the race
    // deterministically.
    auto unreachable =
        Address::parseInternetAddress("192.0.2.1", /*port=*/1);
    pool = std::make_unique<ConnectionPoolImpl>(*dispatcher_, unreachable,
                                                config, *manager);

    pool->newConnection(callbacks);
    // One pending slot should be present — the new connection is in SYN_SENT
    // against an unroutable peer and hasn't failed yet.
    EXPECT_EQ(1u, pool->numPendingConnections());
  });

  // The timer must fire on the dispatcher thread; we only observe the effect.
  // 3s covers the 150ms timer plus generous scheduling slack under ASAN.
  ASSERT_TRUE(waitFor(
      [&]() {
        return callbacks.failure_called_.load(std::memory_order_acquire) > 0;
      },
      std::chrono::seconds(3)))
      << "connection_timeout never delivered onPoolFailure — the timer's "
         "capture was likely cancelled or pointed at freed memory";

  EXPECT_EQ(1, callbacks.failure_called_.load());
  EXPECT_EQ(0, callbacks.ready_called_.load());
  EXPECT_EQ(static_cast<int>(ConnectionPool::PoolFailureReason::Timeout),
            callbacks.last_reason_.load());

  // Tear pool/manager down inside the dispatcher — their destructors touch
  // libevent state that must be mutated on the dispatcher thread.
  executeInDispatcher([&]() {
    pool.reset();
    manager.reset();
  });
}

}  // namespace
}  // namespace network
}  // namespace mcp
