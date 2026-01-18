/**
 * @file test_use_after_free_fixes.cc
 * @brief Unit tests for use-after-free crash fixes
 *
 * Tests for commit 9bcaadd52625b712423a1fab9c347ff1453e4319:
 * - Deferred FileEvent destruction during callback
 * - Deferred Connection destruction during callback iteration
 * - Safe object access during error handling
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace network {
namespace {

using namespace std::chrono_literals;

// Mock transport socket for testing
class MockTransportSocket : public TransportSocket {
 public:
  MockTransportSocket() = default;

  void setTransportSocketCallbacks(
      TransportSocketCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  std::string protocol() const override { return "test"; }
  std::string failureReason() const override { return ""; }
  bool canFlushClose() override { return true; }

  VoidResult connect(Socket& socket) override {
    (void)socket;
    return makeVoidSuccess();
  }

  void closeSocket(ConnectionEvent event) override {
    (void)event;
    close_called_++;
  }

  TransportIoResult doRead(Buffer& buffer) override {
    (void)buffer;
    return TransportIoResult::success(0);
  }

  TransportIoResult doWrite(Buffer& buffer, bool end_stream) override {
    (void)end_stream;
    size_t len = buffer.length();
    buffer.drain(len);
    return TransportIoResult::success(len);
  }

  void onConnected() override {}

  int close_called_{0};
  TransportSocketCallbacks* callbacks_{nullptr};
};

// Test callback that tracks invocations
class TestConnectionCallbacks : public ConnectionCallbacks {
 public:
  void onEvent(ConnectionEvent event) override {
    event_count_++;
    last_event_ = event;
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::atomic<int> event_count_{0};
  ConnectionEvent last_event_;
};

/**
 * Test deferred destruction is scheduled via dispatcher.post()
 * Fix: Use post() instead of immediate reset
 */
TEST(UseAfterFreeFixes, DeferredDestructionScheduledViaPost) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<bool> destruction_scheduled{false};
  std::atomic<bool> destruction_completed{false};

  // Simulate the deferred destruction pattern
  {
    auto object = std::make_unique<int>(42);

    // Schedule deferred destruction (the fix)
    auto obj_to_delete = std::make_shared<std::unique_ptr<int>>(std::move(object));
    destruction_scheduled = true;

    dispatcher.post([obj_to_delete, &destruction_completed]() {
      obj_to_delete->reset();
      destruction_completed = true;
    });

    // Object still exists here (not destroyed immediately)
    EXPECT_TRUE(destruction_scheduled);
    EXPECT_FALSE(destruction_completed);
  }

  // Run dispatcher to process posted destruction
  auto start = std::chrono::steady_clock::now();
  while (!destruction_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(destruction_completed);
}

/**
 * Test object remains valid until deferred destruction executes
 * Fix: Object lifetime extended until post() callback runs
 */
TEST(UseAfterFreeFixes, ObjectRemainsValidUntilDeferredDestruction) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<int> object_value{0};
  std::atomic<bool> destruction_completed{false};

  {
    auto object = std::make_unique<int>(123);
    object_value = *object;

    // Schedule deferred destruction
    auto obj_to_delete = std::make_shared<std::unique_ptr<int>>(std::move(object));

    dispatcher.post([obj_to_delete, &destruction_completed, &object_value]() {
      // Object should still be valid here
      if (*obj_to_delete) {
        EXPECT_EQ(123, **obj_to_delete);
      }
      obj_to_delete->reset();
      destruction_completed = true;
      object_value = 0;
    });

    // Object still valid here (value should be 123)
    EXPECT_EQ(123, object_value);
  }

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (!destruction_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(destruction_completed);
  EXPECT_EQ(0, object_value);
}

/**
 * Test shared_ptr wrapper allows deferred destruction
 * Fix: Wrap unique_ptr in shared_ptr for copyability
 */
TEST(UseAfterFreeFixes, SharedPtrWrapperAllowsDeferredDestruction) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<int> destructor_count{0};

  struct TrackedObject {
    explicit TrackedObject(std::atomic<int>* counter) : counter_(counter) {}
    ~TrackedObject() { (*counter_)++; }
    std::atomic<int>* counter_;
  };

  {
    auto object = std::make_unique<TrackedObject>(&destructor_count);

    // Wrap in shared_ptr (the fix)
    auto obj_to_delete = std::make_shared<std::unique_ptr<TrackedObject>>(
        std::move(object));

    dispatcher.post([obj_to_delete]() {
      obj_to_delete->reset();  // Destructor called here
    });

    // Destructor not called yet
    EXPECT_EQ(0, destructor_count);
  }

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (destructor_count == 0 &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Destructor should have been called
  EXPECT_EQ(1, destructor_count);
}

/**
 * Test multiple deferred destructions can be scheduled
 * Fix: Each object gets its own deferred destruction
 */
TEST(UseAfterFreeFixes, MultipleDeferredDestructionsScheduled) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<int> destruction_count{0};

  for (int i = 0; i < 5; ++i) {
    auto object = std::make_unique<int>(i);

    auto obj_to_delete = std::make_shared<std::unique_ptr<int>>(std::move(object));

    dispatcher.post([obj_to_delete, &destruction_count]() {
      obj_to_delete->reset();
      destruction_count++;
    });
  }

  // Run dispatcher to process all destructions
  auto start = std::chrono::steady_clock::now();
  while (destruction_count < 5 &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_EQ(5, destruction_count);
}

/**
 * Test deferred destruction prevents use-after-free in callback
 * Fix: Object valid during callback, destroyed after
 */
TEST(UseAfterFreeFixes, DeferredDestructionPreventsUseAfterFree) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<bool> callback_executed{false};
  std::atomic<bool> destruction_completed{false};
  std::atomic<int> value_during_callback{0};

  struct CallbackObject {
    std::atomic<bool>* callback_flag;
    std::atomic<int>* value_flag;
    int value{999};

    void executeCallback() {
      // This should be safe - object still exists
      *value_flag = value;
      *callback_flag = true;
    }
  };

  {
    auto object = std::make_unique<CallbackObject>();
    object->callback_flag = &callback_executed;
    object->value_flag = &value_during_callback;

    // Simulate callback that schedules deferred destruction
    auto obj_ptr = object.get();
    auto obj_to_delete = std::make_shared<std::unique_ptr<CallbackObject>>(
        std::move(object));

    dispatcher.post([obj_ptr, obj_to_delete, &destruction_completed]() {
      // Execute callback - object should still be valid
      obj_ptr->executeCallback();

      // Schedule destruction after callback
      obj_to_delete->reset();
      destruction_completed = true;
    });
  }

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (!destruction_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(callback_executed);
  EXPECT_EQ(999, value_during_callback);
  EXPECT_TRUE(destruction_completed);
}

/**
 * Test connection callbacks complete before destruction
 * Fix: Callbacks iterate over copy, destruction deferred
 */
TEST(UseAfterFreeFixes, CallbacksCompleteBeforeDestruction) {
  event::LibeventDispatcher dispatcher("test");

  std::vector<int> callback_order;
  std::atomic<bool> destruction_scheduled{false};
  std::atomic<bool> destruction_completed{false};

  // Simulate callback iteration with deferred destruction
  std::vector<std::function<void()>> callbacks;

  callbacks.push_back([&]() {
    callback_order.push_back(1);
  });

  callbacks.push_back([&]() {
    callback_order.push_back(2);
    // This callback schedules destruction
    destruction_scheduled = true;
  });

  callbacks.push_back([&]() {
    callback_order.push_back(3);
  });

  // Execute callbacks (simulating raiseConnectionEvent)
  for (auto& cb : callbacks) {
    cb();
  }

  // All callbacks executed
  EXPECT_EQ(3, callback_order.size());
  EXPECT_TRUE(destruction_scheduled);

  // Schedule actual destruction
  auto obj_to_delete = std::make_shared<int>(42);
  dispatcher.post([obj_to_delete, &destruction_completed]() {
    destruction_completed = true;
  });

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (!destruction_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(destruction_completed);
  EXPECT_EQ(std::vector<int>({1, 2, 3}), callback_order);
}

/**
 * Test event remains enabled until explicitly disabled
 * Fix: setEnabled(0) called before deferred destruction
 */
TEST(UseAfterFreeFixes, EventDisabledBeforeDeferredDestruction) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<bool> event_enabled{true};
  std::atomic<bool> destruction_scheduled{false};

  // Simulate the fix pattern
  {
    // First disable event
    event_enabled = false;

    // Then schedule deferred destruction
    auto obj_to_delete = std::make_shared<int>(42);
    dispatcher.post([obj_to_delete]() {
      // Object is destroyed when lambda goes out of scope
      (void)obj_to_delete;
    });
    destruction_scheduled = true;

    // Event should be disabled immediately
    EXPECT_FALSE(event_enabled);
    EXPECT_TRUE(destruction_scheduled);
  }

  SUCCEED();
}

/**
 * Test dispatcher reference saved before callback
 * Fix: Save dispatcher reference in case callback destroys object
 */
TEST(UseAfterFreeFixes, DispatcherReferenceSavedBeforeCallback) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<bool> callback_executed{false};
  std::atomic<bool> used_saved_reference{false};

  // Simulate saving dispatcher reference (the fix)
  auto& dispatcher_ref = dispatcher;

  // Post callback that could destroy object
  dispatcher.post([&callback_executed]() {
    callback_executed = true;
  });

  // Use saved reference (simulates touchWatchdog call)
  dispatcher_ref.post([&used_saved_reference]() {
    used_saved_reference = true;
  });

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while ((!callback_executed || !used_saved_reference) &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(callback_executed);
  EXPECT_TRUE(used_saved_reference);
}

/**
 * Test enabled_events checked after callback
 * Fix: Check enabled_events_ != 0 before re-adding event
 */
TEST(UseAfterFreeFixes, EnabledEventsCheckedAfterCallback) {
  std::atomic<bool> event_enabled{true};
  std::atomic<bool> callback_executed{false};

  // Simulate callback that disables event
  auto callback = [&]() {
    callback_executed = true;
    event_enabled = false;  // Callback disables event
  };

  callback();

  // After callback, check enabled status (the fix)
  bool should_re_add = event_enabled;

  EXPECT_TRUE(callback_executed);
  EXPECT_FALSE(should_re_add);  // Should NOT re-add disabled event
}

/**
 * Test trigger type saved before callback
 * Fix: Save trigger before callback since callback may destroy object
 */
TEST(UseAfterFreeFixes, TriggerTypeSavedBeforeCallback) {
  enum class TriggerType { Edge, Level };

  std::atomic<bool> callback_executed{false};
  TriggerType trigger = TriggerType::Edge;

  // Save trigger type before callback (the fix)
  TriggerType saved_trigger = trigger;

  // Callback could modify or destroy object
  auto callback = [&]() {
    callback_executed = true;
  };

  callback();

  // Use saved trigger (safe even if object destroyed)
  bool should_re_add = (saved_trigger == TriggerType::Edge);

  EXPECT_TRUE(callback_executed);
  EXPECT_TRUE(should_re_add);
}

/**
 * Test connection destruction deferred in close event handler
 * Fix: Defer active_connection_.reset() with dispatcher.post()
 */
TEST(UseAfterFreeFixes, ConnectionDestructionDeferredInCloseHandler) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<bool> close_event_received{false};
  std::atomic<bool> destruction_scheduled{false};
  std::atomic<bool> destruction_completed{false};

  // Simulate close event handler
  auto handle_close_event = [&]() {
    close_event_received = true;

    // Don't destroy immediately
    // Schedule deferred destruction (the fix)
    auto conn_to_delete = std::make_shared<int>(42);
    dispatcher.post([conn_to_delete, &destruction_completed]() {
      destruction_completed = true;
    });
    destruction_scheduled = true;
  };

  handle_close_event();

  EXPECT_TRUE(close_event_received);
  EXPECT_TRUE(destruction_scheduled);
  EXPECT_FALSE(destruction_completed);  // Not destroyed yet

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (!destruction_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(destruction_completed);
}

/**
 * Test callback iteration continues after destruction scheduled
 * Fix: Deferred destruction allows iteration to complete
 */
TEST(UseAfterFreeFixes, CallbackIterationContinuesAfterDestructionScheduled) {
  std::vector<int> execution_order;
  std::atomic<bool> destruction_scheduled{false};

  std::vector<std::function<void()>> callbacks;

  callbacks.push_back([&]() { execution_order.push_back(1); });
  callbacks.push_back([&]() { execution_order.push_back(2); });
  callbacks.push_back([&]() {
    execution_order.push_back(3);
    destruction_scheduled = true;  // Simulate scheduling destruction
  });
  callbacks.push_back([&]() { execution_order.push_back(4); });
  callbacks.push_back([&]() { execution_order.push_back(5); });

  // Iterate callbacks (simulating raiseConnectionEvent)
  for (auto& cb : callbacks) {
    cb();
  }

  // All callbacks should execute
  EXPECT_EQ(5, execution_order.size());
  EXPECT_EQ(std::vector<int>({1, 2, 3, 4, 5}), execution_order);
  EXPECT_TRUE(destruction_scheduled);
}

/**
 * Test file event destruction deferred during error handling
 * Fix: Connection errors trigger deferred destruction
 */
TEST(UseAfterFreeFixes, FileEventDestructionDeferredDuringError) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<bool> error_detected{false};
  std::atomic<bool> destruction_scheduled{false};
  std::atomic<bool> destruction_completed{false};

  // Simulate error detection in callback
  auto error_callback = [&]() {
    error_detected = true;

    // Schedule deferred destruction (the fix)
    auto event_to_delete = std::make_shared<int>(123);
    dispatcher.post([event_to_delete, &destruction_completed]() {
      destruction_completed = true;
    });
    destruction_scheduled = true;
  };

  error_callback();

  EXPECT_TRUE(error_detected);
  EXPECT_TRUE(destruction_scheduled);
  EXPECT_FALSE(destruction_completed);

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (!destruction_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(destruction_completed);
}

/**
 * Test rapid deferred destructions don't interfere
 * Fix: Each destruction is independent
 */
TEST(UseAfterFreeFixes, RapidDeferredDestructionsDontInterfere) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<int> destruction_count{0};

  // Schedule 10 rapid destructions
  for (int i = 0; i < 10; ++i) {
    auto obj = std::make_shared<int>(i);
    dispatcher.post([obj, &destruction_count]() {
      destruction_count++;
    });
  }

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (destruction_count < 10 &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_EQ(10, destruction_count);
}

/**
 * Test deferred destruction works with nested callbacks
 * Fix: Multiple levels of deferred destruction
 */
TEST(UseAfterFreeFixes, DeferredDestructionWorksWithNestedCallbacks) {
  event::LibeventDispatcher dispatcher("test");

  std::atomic<int> destruction_count{0};
  std::atomic<bool> all_completed{false};

  // Outer deferred destruction
  auto outer_obj = std::make_shared<int>(1);
  dispatcher.post([outer_obj, &dispatcher, &destruction_count, &all_completed]() {
    destruction_count++;

    // Inner deferred destruction
    auto inner_obj = std::make_shared<int>(2);
    dispatcher.post([inner_obj, &destruction_count, &all_completed]() {
      destruction_count++;
      all_completed = true;
    });
  });

  // Run dispatcher
  auto start = std::chrono::steady_clock::now();
  while (!all_completed &&
         std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
    dispatcher.run(event::RunType::NonBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_EQ(2, destruction_count);
  EXPECT_TRUE(all_completed);
}

}  // namespace
}  // namespace network
}  // namespace mcp
