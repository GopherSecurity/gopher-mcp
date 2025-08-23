/**
 * Unit tests for MCP Protocol State Machine
 */

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>

#include "mcp/protocol/mcp_protocol_state_machine.h"
#include "../integration/real_io_test_base.h"

using namespace mcp::protocol;

class McpProtocolStateMachineTest : public mcp::test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Create config with test callbacks
    config_.initialization_timeout = std::chrono::milliseconds(100);
    config_.connection_timeout = std::chrono::milliseconds(100);
    config_.drain_timeout = std::chrono::milliseconds(100);
    
    config_.state_change_callback = [this](const ProtocolStateTransitionContext& ctx) {
      std::lock_guard<std::mutex> lock(test_mutex_);
      last_transition_ = ctx;
      transition_count_++;
    };
    
    config_.error_callback = [this](const mcp::Error& error) {
      std::lock_guard<std::mutex> lock(test_mutex_);
      last_error_ = error;
      error_count_++;
    };
    
    // Create state machine in dispatcher thread
    executeInDispatcher([this]() {
      state_machine_ = std::make_unique<McpProtocolStateMachine>(*dispatcher_, config_);
    });
  }
  
  void TearDown() override {
    // Clean up state machine in dispatcher thread
    if (state_machine_) {
      executeInDispatcher([this]() {
        state_machine_.reset();
      });
    }
    RealIoTestBase::TearDown();
  }
  
  // Helper to run dispatcher for a short time
  void runDispatcher(std::chrono::milliseconds duration) {
    // Let the dispatcher process events for the specified duration
    std::this_thread::sleep_for(duration);
  }

protected:
  McpProtocolStateMachineConfig config_;
  std::unique_ptr<McpProtocolStateMachine> state_machine_;
  
  // Test tracking (protected by mutex for thread safety)
  std::mutex test_mutex_;
  ProtocolStateTransitionContext last_transition_;
  mcp::Error last_error_;
  std::atomic<int> transition_count_{0};
  std::atomic<int> error_count_{0};
};

// Test initial state
TEST_F(McpProtocolStateMachineTest, InitialState) {
  EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
  EXPECT_FALSE(state_machine_->isReady());
  EXPECT_FALSE(state_machine_->isError());
  EXPECT_FALSE(state_machine_->getLastError().has_value());
}

// Test basic connection flow
TEST_F(McpProtocolStateMachineTest, BasicConnectionFlow) {
  // All state transitions in dispatcher thread
  executeInDispatcher([this]() {
    // Request connection
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CONNECTING);
    
    // Network connected
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CONNECTED);
    
    // Initialize protocol
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::INITIALIZING);
    
    // Protocol initialized
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::READY);
    EXPECT_TRUE(state_machine_->isReady());
  });
  
  // Verify final state
  EXPECT_EQ(transition_count_, 4);
}

// Test connection timeout
TEST_F(McpProtocolStateMachineTest, DISABLED_ConnectionTimeout) {
  // Request connection in dispatcher thread to ensure timer is created properly
  executeInDispatcher([this]() {
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
  });
  
  EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CONNECTING);
  
  // Simply wait for the timeout to fire (100ms timeout + buffer)
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  // Should have transitioned to disconnected on timeout
  EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
  EXPECT_GE(error_count_, 1);
}

// Test initialization timeout
TEST_F(McpProtocolStateMachineTest, DISABLED_InitializationTimeout) {
  // Move to initializing state in dispatcher thread
  executeInDispatcher([this]() {
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
  });
  
  EXPECT_EQ(state_machine_->currentState(), McpProtocolState::INITIALIZING);
  
  // Simply wait for the timeout to fire
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  // Should have transitioned to error on timeout
  EXPECT_EQ(state_machine_->currentState(), McpProtocolState::ERROR);
  EXPECT_TRUE(state_machine_->isError());
  EXPECT_GE(error_count_, 1);
}

// Test graceful shutdown
TEST_F(McpProtocolStateMachineTest, GracefulShutdown) {
  executeInDispatcher([this]() {
    // Get to ready state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::READY);
    
    // Request shutdown
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::SHUTDOWN_REQUESTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DRAINING);
    
    // Complete drain
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::DRAIN_COMPLETE));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CLOSED);
  });
}

// Test network disconnection handling
TEST_F(McpProtocolStateMachineTest, NetworkDisconnection) {
  executeInDispatcher([this]() {
    // Get to ready state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::READY);
    
    // Network disconnected
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_DISCONNECTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
  });
}

// Test auto-reconnection
TEST_F(McpProtocolStateMachineTest, AutoReconnection) {
  // Enable auto-reconnect
  config_.auto_reconnect = true;
  config_.max_reconnect_attempts = 2;
  
  executeInDispatcher([this]() {
    state_machine_ = std::make_unique<McpProtocolStateMachine>(*dispatcher_, config_);
    
    // Get to ready state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::READY);
    
    // First disconnection - should auto-reconnect
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_DISCONNECTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CONNECTING);
    
    // Fail connection
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_DISCONNECTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
  });
}

// Test protocol error handling
TEST_F(McpProtocolStateMachineTest, ProtocolError) {
  executeInDispatcher([this]() {
    // Get to ready state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::READY);
    
    // Protocol error - handleError internally triggers PROTOCOL_ERROR event
    mcp::Error error;
    error.code = -1;
    error.message = "Test protocol error";
    state_machine_->handleError(error);
    
    // Should have transitioned to ERROR state
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::ERROR);
    EXPECT_TRUE(state_machine_->isError());
    
    auto last_error = state_machine_->getLastError();
    EXPECT_TRUE(last_error.has_value());
    EXPECT_EQ(last_error->message, "Test protocol error");
  });
}

// Test recovery from error state
TEST_F(McpProtocolStateMachineTest, ErrorRecovery) {
  executeInDispatcher([this]() {
    // Get to error state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::PROTOCOL_ERROR));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::ERROR);
    
    // Attempt reconnection
    config_.max_reconnect_attempts = 1;
    state_machine_ = std::make_unique<McpProtocolStateMachine>(*dispatcher_, config_);
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::PROTOCOL_ERROR));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::RECONNECT_REQUESTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CONNECTING);
  });
}

// Test invalid transitions
TEST_F(McpProtocolStateMachineTest, InvalidTransitions) {
  executeInDispatcher([this]() {
    // Cannot initialize from disconnected state
    EXPECT_FALSE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
    
    // Cannot shutdown from disconnected state
    EXPECT_FALSE(state_machine_->handleEvent(McpProtocolEvent::SHUTDOWN_REQUESTED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
  });
}

// Test state machine reset
TEST_F(McpProtocolStateMachineTest, Reset) {
  executeInDispatcher([this]() {
    // Get to ready state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::NETWORK_CONNECTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZE_REQUESTED));
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::INITIALIZED));
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::READY);
    
    // Reset
    state_machine_->reset();
    EXPECT_EQ(state_machine_->currentState(), McpProtocolState::DISCONNECTED);
    EXPECT_FALSE(state_machine_->getLastError().has_value());
  });
}

// Test time tracking
TEST_F(McpProtocolStateMachineTest, TimeInState) {
  auto initial_time = state_machine_->getTimeInCurrentState();
  EXPECT_GE(initial_time.count(), 0);
  
  // Wait a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  auto later_time = state_machine_->getTimeInCurrentState();
  EXPECT_GT(later_time.count(), initial_time.count());
  
  executeInDispatcher([this]() {
    // Change state
    EXPECT_TRUE(state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED));
  });
  
  auto new_state_time = state_machine_->getTimeInCurrentState();
  EXPECT_LT(new_state_time.count(), later_time.count());
}

// Test state names
TEST_F(McpProtocolStateMachineTest, StateNames) {
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::DISCONNECTED), "DISCONNECTED");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::CONNECTING), "CONNECTING");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::CONNECTED), "CONNECTED");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::INITIALIZING), "INITIALIZING");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::READY), "READY");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::DRAINING), "DRAINING");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::ERROR), "ERROR");
  EXPECT_EQ(McpProtocolStateMachine::stateToString(McpProtocolState::CLOSED), "CLOSED");
}

// Test event names
TEST_F(McpProtocolStateMachineTest, EventNames) {
  EXPECT_EQ(McpProtocolStateMachine::eventToString(McpProtocolEvent::CONNECT_REQUESTED), "CONNECT_REQUESTED");
  EXPECT_EQ(McpProtocolStateMachine::eventToString(McpProtocolEvent::NETWORK_CONNECTED), "NETWORK_CONNECTED");
  EXPECT_EQ(McpProtocolStateMachine::eventToString(McpProtocolEvent::INITIALIZED), "INITIALIZED");
  EXPECT_EQ(McpProtocolStateMachine::eventToString(McpProtocolEvent::PROTOCOL_ERROR), "PROTOCOL_ERROR");
}

// Test concurrent state changes (thread safety)
TEST_F(McpProtocolStateMachineTest, ThreadSafety) {
  std::atomic<int> success_count{0};
  std::vector<std::thread> threads;
  
  // Multiple threads trying to change state through dispatcher
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this, &success_count]() {
      executeInDispatcher([this, &success_count]() {
        if (state_machine_->handleEvent(McpProtocolEvent::CONNECT_REQUESTED)) {
          success_count++;
        }
      });
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  // Only one should succeed (first one to get CONNECT_REQUESTED through)
  EXPECT_EQ(success_count, 1);
  EXPECT_EQ(state_machine_->currentState(), McpProtocolState::CONNECTING);
}