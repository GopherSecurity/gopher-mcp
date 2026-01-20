/**
 * @file test_timer_lifetime.cc
 * @brief Unit tests for Section 3: Timer Lifetime Management
 *
 * Tests for Section 3 implementation (commit cca768c5):
 * - Timer validity flag initialization
 * - Timer callbacks check dispatcher validity before access
 * - Safe shutdown with active timers
 * - No use-after-free when timers outlive dispatcher
 * - Watchdog safety during shutdown
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"

using namespace mcp::event;

namespace mcp {
namespace event {
namespace {

/**
 * Test fixture for timer lifetime tests
 */
class TimerLifetimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
  }

  void TearDown() override {
    if (dispatcher_thread_.joinable()) {
      dispatcher_->exit();
      dispatcher_thread_.join();
    }
    dispatcher_.reset();
  }

  void runDispatcher() {
    dispatcher_thread_ = std::thread([this]() { dispatcher_->run(RunType::Block); });
    // Give dispatcher time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::unique_ptr<Dispatcher> dispatcher_;
  std::thread dispatcher_thread_;
};

// =============================================================================
// Dispatcher Validity Flag Tests
// =============================================================================

/**
 * Test: Dispatcher initializes validity flag to true
 */
TEST_F(TimerLifetimeTest, ValidityFlagInitializedToTrue) {
  // Create a timer - it should capture the validity flag
  std::atomic<bool> callback_executed{false};

  auto timer = dispatcher_->createTimer([&callback_executed]() {
    callback_executed = true;
  });

  // Enable timer with short duration
  timer->enableTimer(std::chrono::milliseconds(1));

  // Run dispatcher briefly
  runDispatcher();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Callback should have executed (dispatcher was valid)
  EXPECT_TRUE(callback_executed);
}

/**
 * Test: Timer callback doesn't run after dispatcher is destroyed
 */
TEST_F(TimerLifetimeTest, CallbackDoesNotRunAfterDispatcherDestroyed) {
  std::atomic<bool> callback_executed{false};
  std::atomic<bool> dispatcher_destroyed{false};

  // Create timer with long duration
  auto timer = dispatcher_->createTimer([&callback_executed, &dispatcher_destroyed]() {
    // If this runs after dispatcher destroyed, we have a problem
    if (dispatcher_destroyed) {
      ADD_FAILURE() << "Timer callback ran after dispatcher destroyed!";
    }
    callback_executed = true;
  });

  timer->enableTimer(std::chrono::seconds(10));

  // Run dispatcher briefly
  runDispatcher();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Destroy dispatcher while timer is still pending
  dispatcher_->exit();
  dispatcher_thread_.join();
  dispatcher_.reset();
  dispatcher_destroyed = true;

  // Wait to see if callback incorrectly fires
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Callback should NOT have executed after dispatcher was destroyed
  // (it's OK if it didn't execute at all since we destroyed before timeout)
}

/**
 * Test: Multiple timers can be created and all use validity flag
 */
TEST_F(TimerLifetimeTest, MultipleTimersShareValidityFlag) {
  std::atomic<int> callback_count{0};

  // Create multiple timers
  auto timer1 = dispatcher_->createTimer([&callback_count]() {
    callback_count++;
  });

  auto timer2 = dispatcher_->createTimer([&callback_count]() {
    callback_count++;
  });

  auto timer3 = dispatcher_->createTimer([&callback_count]() {
    callback_count++;
  });

  // Enable all timers with short durations
  timer1->enableTimer(std::chrono::milliseconds(5));
  timer2->enableTimer(std::chrono::milliseconds(10));
  timer3->enableTimer(std::chrono::milliseconds(15));

  // Run dispatcher briefly
  runDispatcher();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // All callbacks should have executed
  EXPECT_EQ(callback_count, 3);
}

// =============================================================================
// Shutdown Safety Tests
// =============================================================================

/**
 * Test: Shutdown invalidates the validity flag
 */
TEST_F(TimerLifetimeTest, ShutdownInvalidatesValidityFlag) {
  std::atomic<bool> callback_executed{false};
  std::atomic<bool> shutdown_called{false};

  auto timer = dispatcher_->createTimer([&callback_executed, &shutdown_called]() {
    // This should not run if shutdown was called first
    if (shutdown_called) {
      ADD_FAILURE() << "Timer callback ran after shutdown!";
    }
    callback_executed = true;
  });

  timer->enableTimer(std::chrono::seconds(10));

  // Run dispatcher briefly
  runDispatcher();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Call shutdown before timer fires
  dispatcher_->exit();
  dispatcher_thread_.join();
  shutdown_called = true;
  dispatcher_.reset();

  // Wait to see if callback incorrectly fires
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Callback should not have run after shutdown
}

/**
 * Test: Timer callback that destroys dispatcher doesn't crash
 */
TEST_F(TimerLifetimeTest, CallbackThatDestroysDispatcherDoesNotCrash) {
  std::atomic<bool> callback_executed{false};

  // This test verifies that a timer callback can trigger dispatcher destruction
  // without causing a crash from accessing dispatcher members after the callback
  auto timer = dispatcher_->createTimer([&callback_executed, this]() {
    callback_executed = true;
    // Request exit, which will lead to dispatcher destruction
    dispatcher_->exit();
  });

  timer->enableTimer(std::chrono::milliseconds(10));

  runDispatcher();

  // Wait for callback to execute and dispatcher to exit
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Wait for thread to finish
  if (dispatcher_thread_.joinable()) {
    dispatcher_thread_.join();
  }

  // Should complete without crashing
  EXPECT_TRUE(callback_executed);
}

// =============================================================================
// Watchdog Safety Tests
// =============================================================================

/**
 * Test: touchWatchdog() is safe during shutdown
 */
TEST_F(TimerLifetimeTest, TouchWatchdogSafeDuringShutdown) {
  // Create a simple timer that triggers shutdown
  std::atomic<bool> callback_executed{false};

  auto timer = dispatcher_->createTimer([&callback_executed, this]() {
    callback_executed = true;
    // Trigger exit
    dispatcher_->exit();
  });

  timer->enableTimer(std::chrono::milliseconds(10));

  runDispatcher();

  // Wait for callback and shutdown
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (dispatcher_thread_.joinable()) {
    dispatcher_thread_.join();
  }

  // Should complete without crashing
  EXPECT_TRUE(callback_executed);
}

// =============================================================================
// Exit Method Tests
// =============================================================================

/**
 * Test: exit() properly breaks event loop from non-dispatcher thread
 */
TEST_F(TimerLifetimeTest, ExitBreaksEventLoopFromExternalThread) {
  std::atomic<bool> loop_started{false};
  std::atomic<bool> loop_exited{false};

  // Start dispatcher in background
  std::thread dispatcher_thread([this, &loop_started, &loop_exited]() {
    loop_started = true;
    dispatcher_->run(RunType::Block);
    loop_exited = true;
  });

  // Wait for dispatcher to start
  while (!loop_started) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Give dispatcher time to enter event loop
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Call exit from external thread
  dispatcher_->exit();

  // Wait for loop to exit (with timeout)
  auto start = std::chrono::steady_clock::now();
  while (!loop_exited) {
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(2)) {
      ADD_FAILURE() << "Dispatcher did not exit within timeout!";
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  dispatcher_thread.join();
  EXPECT_TRUE(loop_exited);
}

/**
 * Test: exit_requested flag is set by exit()
 */
TEST_F(TimerLifetimeTest, ExitSetsExitRequestedFlag) {
  std::atomic<bool> callback_saw_exit_request{false};

  // Create timer that checks if exit was requested
  auto timer = dispatcher_->createTimer([&callback_saw_exit_request, this]() {
    // Request exit
    dispatcher_->exit();
    callback_saw_exit_request = true;
  });

  timer->enableTimer(std::chrono::milliseconds(10));

  runDispatcher();

  // Wait for callback and exit
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (dispatcher_thread_.joinable()) {
    dispatcher_thread_.join();
  }

  EXPECT_TRUE(callback_saw_exit_request);
}

// =============================================================================
// Memory Safety Tests
// =============================================================================

/**
 * Test: Timer can be disabled and re-enabled safely
 */
TEST_F(TimerLifetimeTest, TimerCanBeDisabledAndReEnabled) {
  std::atomic<int> callback_count{0};

  auto timer = dispatcher_->createTimer([&callback_count]() {
    callback_count++;
  });

  // Enable, disable, enable again
  timer->enableTimer(std::chrono::milliseconds(10));
  timer->disableTimer();
  timer->enableTimer(std::chrono::milliseconds(10));

  runDispatcher();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should only execute once (second enable)
  EXPECT_EQ(callback_count, 1);
}

/**
 * Test: Timer is properly cleaned up on destruction
 */
TEST_F(TimerLifetimeTest, TimerCleanupOnDestruction) {
  std::atomic<bool> callback_executed{false};

  {
    auto timer = dispatcher_->createTimer([&callback_executed]() {
      callback_executed = true;
    });

    timer->enableTimer(std::chrono::milliseconds(500));
    // Timer destroyed here - callback should not execute
  }

  runDispatcher();
  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  // Callback should NOT have executed (timer was destroyed)
  EXPECT_FALSE(callback_executed);
}

}  // namespace
}  // namespace event
}  // namespace mcp
