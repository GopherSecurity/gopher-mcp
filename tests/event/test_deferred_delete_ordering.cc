// Verifies the ordering contract of Dispatcher::deferredDelete(): an object
// handed to the dispatcher from inside an event callback must NOT be
// destroyed synchronously — destruction has to wait for the current callback
// stack to unwind, otherwise close-event handlers would tear down objects
// while their own callback loops are still iterating.
//
// This test exists because the McpConnectionManager close path and the
// McpServer per-connection teardown both rely on this guarantee. Regressions
// here would silently reintroduce a use-after-free in the transport layer.

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace event {
namespace {

using namespace std::chrono_literals;

class DeferredDeleteOrderingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("dd-test");
    loop_thread_ = std::thread([this]() { dispatcher_->run(RunType::Block); });
    // Give libevent a moment to actually enter the loop before we start
    // posting callbacks into it.
    std::this_thread::sleep_for(20ms);
  }

  void TearDown() override {
    if (dispatcher_) {
      dispatcher_->exit();
    }
    if (loop_thread_.joinable()) {
      loop_thread_.join();
    }
    dispatcher_.reset();
  }

  std::unique_ptr<Dispatcher> dispatcher_;
  std::thread loop_thread_;
};

// A tracer object that records whether its destructor has run.
struct Tracer : public DeferredDeletable {
  explicit Tracer(std::atomic<bool>& destroyed) : destroyed_(destroyed) {}
  ~Tracer() override { destroyed_ = true; }
  std::atomic<bool>& destroyed_;
};

// When deferredDelete is called from inside a dispatcher callback, the
// destructor must NOT run before that callback returns. This is the contract
// that makes it safe to deferredDelete(std::move(active_connection_)) while
// the connection is still iterating its own callback list.
TEST_F(DeferredDeleteOrderingTest, DestructionDeferredUntilAfterCallback) {
  std::atomic<bool> destroyed{false};
  std::atomic<bool> callback_still_alive_at_delete_point{false};
  auto tracer = std::make_unique<Tracer>(destroyed);

  std::atomic<bool> callback_done{false};

  dispatcher_->post([&, this]() {
    dispatcher_->deferredDelete(std::move(tracer));
    // Immediately after the deferredDelete call, the tracer must still be
    // alive. If deferredDelete destroyed synchronously, `destroyed` would
    // already be true at this observation point.
    callback_still_alive_at_delete_point = !destroyed.load();
    callback_done = true;
  });

  // Wait for the post to run.
  for (int i = 0; i < 200 && !callback_done.load(); ++i) {
    std::this_thread::sleep_for(5ms);
  }
  ASSERT_TRUE(callback_done.load()) << "dispatcher never ran the callback";

  EXPECT_TRUE(callback_still_alive_at_delete_point)
      << "deferredDelete destroyed the object synchronously inside the "
         "callback — this would cause a use-after-free for connection close "
         "handlers";

  // Eventually the dispatcher must drain the deferred-delete queue.
  for (int i = 0; i < 200 && !destroyed.load(); ++i) {
    std::this_thread::sleep_for(5ms);
  }
  EXPECT_TRUE(destroyed.load())
      << "deferred delete never drained — object leaked";
}

// Multiple objects queued in one callback should all be destroyed on the
// same drain pass, not interleaved with unrelated work.
TEST_F(DeferredDeleteOrderingTest, BatchDrainsAfterCallbackReturns) {
  constexpr int kCount = 5;
  std::atomic<int> destroyed_count{0};
  std::vector<std::unique_ptr<DeferredDeletable>> tracers;
  std::vector<std::atomic<bool>*> flags;
  for (int i = 0; i < kCount; ++i) {
    auto* flag = new std::atomic<bool>(false);
    flags.push_back(flag);
    tracers.emplace_back(std::make_unique<Tracer>(*flag));
  }

  std::atomic<bool> callback_done{false};
  int destroyed_inside_callback = -1;

  dispatcher_->post([&, this]() {
    for (auto& t : tracers) {
      dispatcher_->deferredDelete(std::move(t));
    }
    destroyed_inside_callback = 0;
    for (auto* f : flags) {
      if (f->load()) ++destroyed_inside_callback;
    }
    callback_done = true;
  });

  for (int i = 0; i < 200 && !callback_done.load(); ++i) {
    std::this_thread::sleep_for(5ms);
  }
  ASSERT_TRUE(callback_done.load());

  EXPECT_EQ(destroyed_inside_callback, 0)
      << "no tracer should have been destroyed synchronously";

  for (int i = 0; i < 400 && destroyed_count.load() < kCount; ++i) {
    destroyed_count = 0;
    for (auto* f : flags) {
      if (f->load()) ++destroyed_count;
    }
    if (destroyed_count.load() >= kCount) break;
    std::this_thread::sleep_for(5ms);
  }
  EXPECT_EQ(destroyed_count.load(), kCount)
      << "all queued tracers must eventually drain";

  for (auto* f : flags) delete f;
}

// A Tracer variant that itself queues another Tracer for deferred deletion
// when it runs. Models the real-world case where a connection's destructor
// releases sub-objects that also need to defer (filters, session state).
struct CascadingTracer : public DeferredDeletable {
  CascadingTracer(std::atomic<bool>& destroyed,
                  Dispatcher& dispatcher,
                  std::unique_ptr<DeferredDeletable> child)
      : destroyed_(destroyed),
        dispatcher_(dispatcher),
        child_(std::move(child)) {}
  ~CascadingTracer() override {
    if (child_) {
      dispatcher_.deferredDelete(std::move(child_));
    }
    destroyed_ = true;
  }
  std::atomic<bool>& destroyed_;
  Dispatcher& dispatcher_;
  std::unique_ptr<DeferredDeletable> child_;
};

// If a destructor re-enters deferredDelete, the dispatcher must drain the
// newly-queued item on a subsequent pass instead of losing it.
TEST_F(DeferredDeleteOrderingTest, NestedDeferredDeleteDuringDrainDrainsLater) {
  std::atomic<bool> outer_destroyed{false};
  std::atomic<bool> inner_destroyed{false};

  auto inner = std::make_unique<Tracer>(inner_destroyed);
  auto outer = std::make_unique<CascadingTracer>(outer_destroyed, *dispatcher_,
                                                 std::move(inner));

  dispatcher_->post(
      [this, outer_raw = outer.release()]() mutable {
        dispatcher_->deferredDelete(
            std::unique_ptr<DeferredDeletable>(outer_raw));
      });

  for (int i = 0; i < 400 && !inner_destroyed.load(); ++i) {
    std::this_thread::sleep_for(5ms);
  }
  EXPECT_TRUE(outer_destroyed.load()) << "outer must drain first";
  EXPECT_TRUE(inner_destroyed.load())
      << "inner queued from outer's destructor must also drain";
}

// Models McpServer::ConnectionLifecycleCallbacks: a callback adapter that, on
// receiving a close-like event, hands *itself* (and a "connection" peer it
// owns) to the dispatcher's deferred-delete queue.  Destroying either
// synchronously would be a use-after-free because the adapter's own onEvent()
// is still on the stack, and the connection is still iterating its callback
// list.
struct PeerConnection : public DeferredDeletable {
  explicit PeerConnection(std::atomic<bool>& destroyed) : destroyed_(destroyed) {}
  ~PeerConnection() override { destroyed_ = true; }
  std::atomic<bool>& destroyed_;
};

struct LifecycleAdapter : public DeferredDeletable {
  LifecycleAdapter(Dispatcher& dispatcher,
                   std::atomic<bool>& adapter_destroyed,
                   std::unique_ptr<PeerConnection> peer)
      : dispatcher_(dispatcher),
        adapter_destroyed_(adapter_destroyed),
        peer_(std::move(peer)) {}
  ~LifecycleAdapter() override { adapter_destroyed_ = true; }

  // Mirrors ConnectionLifecycleCallbacks::onEvent: on close, hand both self
  // and the peer connection to deferred-delete.  Self-move via
  // unique_ptr-holder avoids a double-delete.
  void onClose(std::unique_ptr<LifecycleAdapter> self_ptr) {
    assert(self_ptr.get() == this && "self_ptr must own *this*");
    dispatcher_.deferredDelete(std::move(peer_));
    dispatcher_.deferredDelete(std::move(self_ptr));
  }

  Dispatcher& dispatcher_;
  std::atomic<bool>& adapter_destroyed_;
  std::unique_ptr<PeerConnection> peer_;
};

// Regression test for the crash at McpServer::onConnectionLifecycleEvent:
// when the adapter's own onEvent() hands both itself and the owning
// connection to deferredDelete, neither may run its destructor before the
// onEvent frame returns.  If destruction ran synchronously, the caller
// iterating connection callbacks would free and then read this adapter's
// vptr from its own onEvent — immediate UAF.
TEST_F(DeferredDeleteOrderingTest,
       LifecycleAdapterDefersSelfAndPeerFromOwnCallback) {
  std::atomic<bool> adapter_destroyed{false};
  std::atomic<bool> peer_destroyed{false};
  std::atomic<bool> observed_alive_after_dispatch{false};
  std::atomic<bool> callback_done{false};

  auto peer = std::make_unique<PeerConnection>(peer_destroyed);
  auto adapter = std::make_unique<LifecycleAdapter>(
      *dispatcher_, adapter_destroyed, std::move(peer));

  dispatcher_->post([&, adapter_raw = adapter.release()]() mutable {
    std::unique_ptr<LifecycleAdapter> self(adapter_raw);
    auto* adapter_ptr = self.get();
    adapter_ptr->onClose(std::move(self));
    // After onClose returns we must still observe both objects alive —
    // the dispatcher has queued them but hasn't drained yet.
    observed_alive_after_dispatch =
        !adapter_destroyed.load() && !peer_destroyed.load();
    callback_done = true;
  });

  for (int i = 0; i < 400 && !callback_done.load(); ++i) {
    std::this_thread::sleep_for(5ms);
  }
  ASSERT_TRUE(callback_done.load());

  EXPECT_TRUE(observed_alive_after_dispatch)
      << "adapter or peer was destroyed inside its own onEvent — would UAF "
         "the ConnectionCallbacks list iterator in production";

  for (int i = 0; i < 400 &&
       !(adapter_destroyed.load() && peer_destroyed.load());
       ++i) {
    std::this_thread::sleep_for(5ms);
  }
  EXPECT_TRUE(adapter_destroyed.load()) << "adapter must eventually drain";
  EXPECT_TRUE(peer_destroyed.load()) << "peer must eventually drain";
}

}  // namespace
}  // namespace event
}  // namespace mcp
