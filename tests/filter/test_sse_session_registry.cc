/**
 * Unit tests for mcp::filter::SseSessionRegistry.
 *
 * The registry is the piece that maps an SSE session ID to the long-
 * lived SSE-stream connection, so a short-lived POST /callback/{id}
 * handler can hand its JSON-RPC response back through the matching
 * stream. These tests verify, in order of complexity:
 *
 *   1. Pure state-machine behavior (unique IDs, removal, lookups) — no
 *      real Connection needed, so we store a null pointer and never
 *      call sendResponse on those slots.
 *   2. sendResponse against a real socketpair-backed ConnectionImpl —
 *      we read the bytes off the peer IoHandle and confirm they arrived
 *      on the wire, which exercises the filter/write path rather than
 *      mocking it out.
 *
 * Dispatcher invariants: every registry method asserts that it runs on
 * the dispatcher thread, so every test body is wrapped in
 * executeInDispatcher() to keep that contract honest.
 */

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/sse_session_registry.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

class SseSessionRegistryTest : public test::RealIoTestBase {
 protected:
  // Build a server-side ConnectionImpl wrapped around one end of a real
  // TCP socketpair, plus the raw peer IoHandle so the test can observe
  // bytes that the connection writes. Must be called from the
  // dispatcher thread because socket creation and ConnectionImpl
  // construction both assert it.
  struct Endpoint {
    std::unique_ptr<network::ServerConnection> conn;
    network::IoHandlePtr peer;
    std::shared_ptr<stream_info::StreamInfo> stream_info;
  };

  Endpoint makeLiveConnection() {
    auto pair = createSocketPair();
    auto local = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto remote = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(pair.first), local, remote);
    auto transport = std::make_unique<network::RawBufferTransportSocket>();
    auto stream_info = std::make_shared<stream_info::StreamInfoImpl>();
    auto conn = network::ConnectionImpl::createServerConnection(
        *dispatcher_, std::move(socket), std::move(transport), *stream_info);
    return Endpoint{std::move(conn), std::move(pair.second),
                    std::move(stream_info)};
  }

  // Read up to `budget` milliseconds of whatever the peer has buffered.
  // IoHandle::read fills an OwnedBuffer; we drain it to a std::string so
  // tests can string-match the payload the registry routed.
  std::string drainPeer(network::IoHandle& peer,
                        std::chrono::milliseconds budget = 1000ms) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      OwnedBuffer buf;
      auto r = peer.read(buf, /*max_length=*/4096);
      if (r.ok() && *r > 0) {
        out.append(buf.toString());
      } else if (!out.empty()) {
        // Got something and nothing more is coming right now — good
        // enough. Bail before the full budget elapses.
        return out;
      } else {
        std::this_thread::sleep_for(5ms);
      }
    }
    return out;
  }
};

// ----------------------------------------------------------------------
// State-machine tests (no live Connection needed).
// ----------------------------------------------------------------------

TEST_F(SseSessionRegistryTest, RegisterReturnsUniqueIds) {
  executeInDispatcher([this]() {
    SseSessionRegistry registry(*dispatcher_);

    const std::string a = registry.registerSession(nullptr);
    const std::string b = registry.registerSession(nullptr);
    const std::string c = registry.registerSession(nullptr);

    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
    EXPECT_EQ(registry.sessionCount(), 3u);
    EXPECT_TRUE(registry.hasSession(a));
    EXPECT_TRUE(registry.hasSession(b));
    EXPECT_TRUE(registry.hasSession(c));
  });
}

TEST_F(SseSessionRegistryTest, RemoveSessionClearsLookup) {
  executeInDispatcher([this]() {
    SseSessionRegistry registry(*dispatcher_);
    const std::string id = registry.registerSession(nullptr);
    ASSERT_TRUE(registry.hasSession(id));

    registry.removeSession(id);
    EXPECT_FALSE(registry.hasSession(id));
    EXPECT_EQ(registry.sessionCount(), 0u);
  });
}

TEST_F(SseSessionRegistryTest, RemoveUnknownSessionIsSafeNoop) {
  executeInDispatcher([this]() {
    SseSessionRegistry registry(*dispatcher_);
    // Filter destructor can call removeSession after an already-cleaned
    // entry (e.g. same session removed twice on teardown). Must not
    // crash or change state.
    registry.removeSession("never_registered");
    EXPECT_EQ(registry.sessionCount(), 0u);
  });
}

TEST_F(SseSessionRegistryTest, SendResponseUnknownSessionReturnsFalse) {
  executeInDispatcher([this]() {
    SseSessionRegistry registry(*dispatcher_);
    // Contract: the POST /callback handler expects sendResponse to
    // return false if the SSE stream is already gone, so it can drop
    // the response rather than pretending it was delivered.
    EXPECT_FALSE(registry.sendResponse("vanished", "{}"));
  });
}

TEST_F(SseSessionRegistryTest, RemovedSessionNoLongerRoutes) {
  executeInDispatcher([this]() {
    SseSessionRegistry registry(*dispatcher_);
    const std::string id = registry.registerSession(nullptr);
    registry.removeSession(id);
    // After removal, any late POST /callback must not dereference the
    // stale pointer. Verified by: sendResponse returns false and
    // doesn't touch the null connection.
    EXPECT_FALSE(registry.sendResponse(id, "{}"));
  });
}

TEST_F(SseSessionRegistryTest, MultipleSessionsAreIndependent) {
  executeInDispatcher([this]() {
    SseSessionRegistry registry(*dispatcher_);
    const std::string a = registry.registerSession(nullptr);
    const std::string b = registry.registerSession(nullptr);
    ASSERT_EQ(registry.sessionCount(), 2u);

    // Dropping one leaves the other intact — a server with multiple
    // live clients must not lose the rest when one disconnects.
    registry.removeSession(a);
    EXPECT_FALSE(registry.hasSession(a));
    EXPECT_TRUE(registry.hasSession(b));
    EXPECT_EQ(registry.sessionCount(), 1u);
  });
}

// ----------------------------------------------------------------------
// Live-write test (real ConnectionImpl over a TCP socketpair).
// ----------------------------------------------------------------------

TEST_F(SseSessionRegistryTest, SendResponseWritesBytesToRegisteredConnection) {
  network::IoHandlePtr peer;
  std::unique_ptr<network::ServerConnection> conn;
  std::shared_ptr<stream_info::StreamInfo> stream_info;
  std::string session_id;

  executeInDispatcher([&]() {
    auto ep = makeLiveConnection();
    peer = std::move(ep.peer);
    conn = std::move(ep.conn);
    stream_info = std::move(ep.stream_info);

    SseSessionRegistry registry(*dispatcher_);
    session_id = registry.registerSession(conn.get());

    const std::string payload =
        R"({"jsonrpc":"2.0","id":1,"result":{"ok":true}})";
    EXPECT_TRUE(registry.sendResponse(session_id, payload));

    // Registry lives only for the duration of this lambda; the
    // connection outlives it. That mirrors production where the
    // factory (owning the registry) and the connection have
    // independent lifetimes — removeSession must be called if the
    // registry outlives the connection, but here we tear the
    // registry down first, which is always safe.
  });

  // On the dispatcher thread the write queued bytes but the actual
  // send happens asynchronously when the socket reports writable.
  // Drain with a short budget — the loopback pair is effectively
  // instant once the write event fires.
  const std::string wire = drainPeer(*peer, 1000ms);
  EXPECT_NE(wire.find(R"("result":{"ok":true})"), std::string::npos)
      << "expected JSON payload on peer socket, got: " << wire;

  // Close the connection on the dispatcher thread so teardown is
  // ordered correctly — freeing ConnectionImpl from the test thread
  // would trip the isThreadSafe() assert in its destructor.
  executeInDispatcher([&]() {
    conn->close(network::ConnectionCloseType::NoFlush);
    conn.reset();
  });
}

}  // namespace
}  // namespace filter
}  // namespace mcp
