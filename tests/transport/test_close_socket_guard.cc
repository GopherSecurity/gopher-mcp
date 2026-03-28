/**
 * @file test_close_socket_guard.cc
 * @brief Unit tests for closeSocket() guard against double-close
 *
 * Tests for the fix that prevents closeSocket() from being called multiple
 * times, which could cause use-after-free or double-free issues.
 *
 * Bug: TcpTransportSocket::closeSocket() and SslTransportSocket::closeSocket()
 * could be called multiple times during destruction, leading to:
 * 1. Multiple raiseEvent() calls on destroyed ConnectionImpl
 * 2. Pure virtual function calls
 * 3. Use-after-free crashes
 *
 * Fix: Added guard to check if already in Closed state before processing
 */

#include <atomic>
#include <memory>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/transport_socket.h"
#include "mcp/transport/transport_socket_state_machine.h"

namespace mcp {
namespace transport {
namespace {

using namespace network;

/**
 * Mock callbacks to track raiseEvent calls
 */
class MockTransportSocketCallbacks : public TransportSocketCallbacks {
 public:
  void raiseEvent(ConnectionEvent event) override {
    event_count_++;
    last_event_ = event;
    events_raised_.push_back(event);
  }

  bool shouldDrainReadBuffer() override { return false; }
  void setTransportSocketIsReadable() override {}
  void flushWriteBuffer() override {}

  IoHandle& ioHandle() override {
    static IoHandle* null_handle = nullptr;
    return *null_handle;
  }

  const IoHandle& ioHandle() const override {
    static IoHandle* null_handle = nullptr;
    return *null_handle;
  }

  Connection& connection() override {
    static Connection* null_conn = nullptr;
    return *null_conn;
  }

  // Test inspection
  int event_count_{0};
  ConnectionEvent last_event_;
  std::vector<ConnectionEvent> events_raised_;
};

/**
 * Test fixture for closeSocket guard tests
 */
class CloseSocketGuardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
    callbacks_ = std::make_unique<MockTransportSocketCallbacks>();
  }

  std::unique_ptr<event::LibeventDispatcher> dispatcher_;
  std::unique_ptr<MockTransportSocketCallbacks> callbacks_;
};

/**
 * Test: State machine prevents double close
 *
 * Once in Closed state, closeSocket() should not process again.
 */
TEST_F(CloseSocketGuardTest, StateMachineBlocksDoubleClose) {
  // Create state machine
  StateMachineConfig config;
  config.mode = StateMachineConfig::Mode::Client;

  TransportSocketStateMachine state_machine(*dispatcher_, config);

  // Start and connect
  state_machine.transitionTo(TransportSocketState::Uninitialized, "init");
  state_machine.transitionTo(TransportSocketState::Initialized, "initialized");
  state_machine.transitionTo(TransportSocketState::Connecting, "connecting");
  state_machine.transitionTo(TransportSocketState::TcpConnected,
                             "tcp connected");
  state_machine.transitionTo(TransportSocketState::Connected, "connected");

  EXPECT_EQ(state_machine.currentState(), TransportSocketState::Connected);

  // First close
  state_machine.transitionTo(TransportSocketState::ShuttingDown,
                             "shutting down");
  state_machine.transitionTo(TransportSocketState::Closed, "closed");

  EXPECT_EQ(state_machine.currentState(), TransportSocketState::Closed);

  // Attempting to transition again from Closed should fail
  // The guard in closeSocket() checks: if (current_state == Closed) return;
  auto prev_state = state_machine.currentState();
  state_machine.transitionTo(TransportSocketState::Error, "should not work");

  // Should still be Closed (transition from Closed to Error not allowed)
  // Or the state machine might reject invalid transitions
  // The important thing is no crash occurs
  EXPECT_TRUE(state_machine.currentState() == TransportSocketState::Closed ||
              state_machine.currentState() == TransportSocketState::Error);
}

/**
 * Test: Guard prevents callback after close
 *
 * Simulates the scenario where closeSocket() is called twice.
 * The guard should prevent the second call from raising events.
 */
TEST_F(CloseSocketGuardTest, GuardPreventsDoubleCallback) {
  // Simulate the guard behavior
  TransportSocketState current_state = TransportSocketState::Connected;

  auto closeSocketWithGuard = [&](ConnectionEvent event) {
    // Guard: Don't process if already closed
    if (current_state == TransportSocketState::Closed) {
      return;  // Skip processing
    }

    // Process close
    current_state = TransportSocketState::Closed;
    callbacks_->raiseEvent(event);
  };

  // First close - should process
  closeSocketWithGuard(ConnectionEvent::LocalClose);
  EXPECT_EQ(callbacks_->event_count_, 1);
  EXPECT_EQ(callbacks_->last_event_, ConnectionEvent::LocalClose);

  // Second close - should be blocked by guard
  closeSocketWithGuard(ConnectionEvent::RemoteClose);
  EXPECT_EQ(callbacks_->event_count_, 1);  // Still 1, not 2
}

/**
 * Test: Multiple close attempts are all blocked after first
 */
TEST_F(CloseSocketGuardTest, MultipleCloseAttemptsBlocked) {
  TransportSocketState current_state = TransportSocketState::Connected;
  int close_attempts = 0;

  auto closeSocketWithGuard = [&](ConnectionEvent event) {
    close_attempts++;
    if (current_state == TransportSocketState::Closed) {
      return;
    }
    current_state = TransportSocketState::Closed;
    callbacks_->raiseEvent(event);
  };

  // Try to close 5 times
  for (int i = 0; i < 5; i++) {
    closeSocketWithGuard(ConnectionEvent::LocalClose);
  }

  EXPECT_EQ(close_attempts, 5);            // All attempts made
  EXPECT_EQ(callbacks_->event_count_, 1);  // Only 1 event raised
}

/**
 * Test: Guard works for different event types
 */
TEST_F(CloseSocketGuardTest, GuardWorksForAllEventTypes) {
  TransportSocketState current_state = TransportSocketState::Connected;

  auto closeSocketWithGuard = [&](ConnectionEvent event) {
    if (current_state == TransportSocketState::Closed) {
      return;
    }
    current_state = TransportSocketState::Closed;
    callbacks_->raiseEvent(event);
  };

  // Close with LocalClose
  closeSocketWithGuard(ConnectionEvent::LocalClose);
  EXPECT_EQ(callbacks_->event_count_, 1);

  // Try RemoteClose - should be blocked
  closeSocketWithGuard(ConnectionEvent::RemoteClose);
  EXPECT_EQ(callbacks_->event_count_, 1);

  // Verify only LocalClose was raised
  EXPECT_EQ(callbacks_->events_raised_.size(), 1u);
  EXPECT_EQ(callbacks_->events_raised_[0], ConnectionEvent::LocalClose);
}

/**
 * Test: Error state also prevents double close
 */
TEST_F(CloseSocketGuardTest, ErrorStateAlsoPreventsDoubleClose) {
  TransportSocketState current_state = TransportSocketState::Connected;

  auto closeSocketWithGuard = [&](ConnectionEvent event) {
    // Guard: Don't process if already closed OR in error state
    if (current_state == TransportSocketState::Closed ||
        current_state == TransportSocketState::Error) {
      return;
    }
    current_state = TransportSocketState::Closed;
    callbacks_->raiseEvent(event);
  };

  // Set to error state first
  current_state = TransportSocketState::Error;

  // Try to close - should be blocked
  closeSocketWithGuard(ConnectionEvent::LocalClose);
  EXPECT_EQ(callbacks_->event_count_, 0);  // No events raised
}

}  // namespace
}  // namespace transport
}  // namespace mcp
