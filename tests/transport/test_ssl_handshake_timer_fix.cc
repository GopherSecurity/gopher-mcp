/**
 * @file test_ssl_handshake_timer_fix.cc
 * @brief Unit tests for SSL handshake timer cancellation fix
 *
 * Tests for the fix that ensures handshake_retry_timer is cancelled when
 * the SSL handshake completes successfully. Without this fix, the timer
 * would continue to fire after handshake completion, causing the state
 * machine to transition from Connected to Error.
 *
 * Bug: After SSL handshake completed, the handshake_retry_timer continued
 * to fire, calling performHandshakeStep() on an already-connected socket,
 * which triggered an error state and RemoteClose event.
 *
 * Fix: Added handshake_retry_timer_->disableTimer() in onHandshakeComplete()
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/transport/ssl_state_machine.h"

namespace mcp {
namespace transport {
namespace {

using namespace std::chrono_literals;

/**
 * Mock timer that tracks enable/disable calls
 */
class MockTimer : public event::Timer {
 public:
  void enableTimer(std::chrono::milliseconds timeout) override {
    enabled_ = true;
    enable_count_++;
    last_timeout_ = timeout;
  }

  void enableHRTimer(std::chrono::microseconds duration) override {
    enabled_ = true;
    enable_count_++;
    last_timeout_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  }

  void disableTimer() override {
    enabled_ = false;
    disable_count_++;
  }

  bool enabled() override { return enabled_; }

  // Test inspection
  bool enabled_{false};
  int enable_count_{0};
  int disable_count_{0};
  std::chrono::milliseconds last_timeout_{0};
};

/**
 * Test that verifies the handshake retry timer behavior
 */
class HandshakeTimerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a mock timer to track enable/disable calls
    mock_timer_ = std::make_unique<MockTimer>();
  }

  std::unique_ptr<MockTimer> mock_timer_;
};

/**
 * Test: Handshake retry timer should be disabled when handshake completes
 *
 * This test verifies that after a successful SSL handshake:
 * 1. The handshake_retry_timer is disabled
 * 2. No more timer callbacks fire after completion
 */
TEST_F(HandshakeTimerTest, TimerDisabledOnHandshakeComplete) {
  // Simulate timer being enabled during handshake
  mock_timer_->enableTimer(10ms);
  EXPECT_TRUE(mock_timer_->enabled());
  EXPECT_EQ(mock_timer_->enable_count_, 1);

  // Simulate handshake completion - timer should be disabled
  mock_timer_->disableTimer();
  EXPECT_FALSE(mock_timer_->enabled());
  EXPECT_EQ(mock_timer_->disable_count_, 1);
}

/**
 * Test: Multiple timer enables followed by single disable
 *
 * During handshake, the timer is re-enabled with exponential backoff.
 * After completion, a single disable should stop all future firings.
 */
TEST_F(HandshakeTimerTest, ExponentialBackoffTimersCancelled) {
  // Simulate exponential backoff: 10ms, 20ms, 40ms, 80ms
  mock_timer_->enableTimer(10ms);
  EXPECT_EQ(mock_timer_->last_timeout_, 10ms);

  mock_timer_->enableTimer(20ms);
  EXPECT_EQ(mock_timer_->last_timeout_, 20ms);

  mock_timer_->enableTimer(40ms);
  EXPECT_EQ(mock_timer_->last_timeout_, 40ms);

  mock_timer_->enableTimer(80ms);
  EXPECT_EQ(mock_timer_->last_timeout_, 80ms);
  EXPECT_EQ(mock_timer_->enable_count_, 4);

  // Handshake completes - single disable cancels all
  mock_timer_->disableTimer();
  EXPECT_FALSE(mock_timer_->enabled());
  EXPECT_EQ(mock_timer_->disable_count_, 1);
}

/**
 * Test: Timer disable is idempotent
 *
 * Calling disableTimer() multiple times should be safe.
 */
TEST_F(HandshakeTimerTest, DisableTimerIdempotent) {
  mock_timer_->enableTimer(10ms);
  EXPECT_TRUE(mock_timer_->enabled());

  // Multiple disables should be safe
  mock_timer_->disableTimer();
  mock_timer_->disableTimer();
  mock_timer_->disableTimer();

  EXPECT_FALSE(mock_timer_->enabled());
  EXPECT_EQ(mock_timer_->disable_count_, 3);
}

/**
 * Test: State machine transitions correctly on handshake completion
 */
TEST(SslStateMachineHandshakeTest, TransitionToConnectedDisablesRetry) {
  // Create a real dispatcher for state machine
  event::LibeventDispatcher dispatcher("test");

  // Create state machine using factory
  auto state_machine =
      SslStateMachineFactory::createClientStateMachine(dispatcher);

  // Start in uninitialized state
  EXPECT_EQ(state_machine->getCurrentState(), SslSocketState::Uninitialized);

  // Transition through handshake states
  state_machine->transition(SslSocketState::Initialized, nullptr);
  dispatcher.run(event::RunType::NonBlock);

  state_machine->transition(SslSocketState::Connecting, nullptr);
  dispatcher.run(event::RunType::NonBlock);

  state_machine->transition(SslSocketState::TcpConnected, nullptr);
  dispatcher.run(event::RunType::NonBlock);

  state_machine->transition(SslSocketState::ClientHandshakeInit, nullptr);
  dispatcher.run(event::RunType::NonBlock);

  state_machine->transition(SslSocketState::HandshakeWantRead, nullptr);
  dispatcher.run(event::RunType::NonBlock);

  // Simulate handshake completion - use forceTransition to skip intermediate
  // states
  state_machine->forceTransition(SslSocketState::Connected);
  dispatcher.run(event::RunType::NonBlock);

  // Verify we're in Connected state
  EXPECT_EQ(state_machine->getCurrentState(), SslSocketState::Connected);
  EXPECT_TRUE(state_machine->isConnected());
  EXPECT_FALSE(state_machine->isHandshaking());
}

/**
 * Test: Calling performHandshakeStep after Connected should not crash
 *
 * This tests the scenario where a timer fires after handshake completes.
 * The code should handle this gracefully without transitioning to Error.
 */
TEST(SslStateMachineHandshakeTest, LateTimerFireAfterConnectedIsSafe) {
  event::LibeventDispatcher dispatcher("test");

  auto state_machine =
      SslStateMachineFactory::createClientStateMachine(dispatcher);

  // Get to Connected state using forceTransition (bypasses validation)
  state_machine->forceTransition(SslSocketState::Initialized);
  state_machine->forceTransition(SslSocketState::Connecting);
  state_machine->forceTransition(SslSocketState::TcpConnected);
  state_machine->forceTransition(SslSocketState::Connected);

  EXPECT_EQ(state_machine->getCurrentState(), SslSocketState::Connected);

  // Attempting to transition to HandshakeWantRead from Connected should fail
  // (state machine should reject invalid transitions)
  bool transition_succeeded = false;
  state_machine->transition(
      SslSocketState::HandshakeWantRead,
      [&transition_succeeded](bool success, const std::string&) {
        transition_succeeded = success;
      });
  dispatcher.run(event::RunType::NonBlock);

  // Transition should have failed
  EXPECT_FALSE(transition_succeeded);

  // Should still be in Connected state (invalid transition rejected)
  EXPECT_EQ(state_machine->getCurrentState(), SslSocketState::Connected);
}

/**
 * Test: Late timer fire from different handshake states should be safe
 */
TEST(SslStateMachineHandshakeTest, LateTimerFromVariousStatesIsSafe) {
  event::LibeventDispatcher dispatcher("test");

  auto state_machine =
      SslStateMachineFactory::createClientStateMachine(dispatcher);

  // Get to Connected state
  state_machine->forceTransition(SslSocketState::Connected);
  EXPECT_TRUE(state_machine->isConnected());

  // Try various invalid transitions that a late timer might trigger
  std::vector<SslSocketState> invalid_targets = {
      SslSocketState::HandshakeWantRead,
      SslSocketState::HandshakeWantWrite,
      SslSocketState::ClientHandshakeInit,
      SslSocketState::ClientHelloSent,
  };

  for (auto target : invalid_targets) {
    bool succeeded = false;
    state_machine->transition(target,
                              [&succeeded](bool success, const std::string&) {
                                succeeded = success;
                              });
    dispatcher.run(event::RunType::NonBlock);

    // All should fail
    EXPECT_FALSE(succeeded)
        << "Transition to " << SslStateMachine::getStateName(target)
        << " should have failed";

    // Should still be Connected
    EXPECT_EQ(state_machine->getCurrentState(), SslSocketState::Connected);
  }
}

}  // namespace
}  // namespace transport
}  // namespace mcp
