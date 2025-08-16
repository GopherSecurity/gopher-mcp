/**
 * @file test_transport_socket_state_machine.cc
 * @brief Unit tests for transport socket state machine
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

#include "mcp/transport/transport_socket_state_machine.h"
#include "mcp/transport/tcp_transport_socket_state_machine.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/socket_impl.h"
#include "../integration/real_io_test_base.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

// =================================================================
// Test Implementation of State Machine
// =================================================================

class TestTransportSocketStateMachine : public TransportSocketStateMachine {
public:
  TestTransportSocketStateMachine(event::Dispatcher& dispatcher,
                                  StateMachineConfig config)
      : TransportSocketStateMachine(dispatcher, config) {}
  
  // Expose protected methods for testing
  using TransportSocketStateMachine::onStateEnter;
  using TransportSocketStateMachine::onStateExit;
  using TransportSocketStateMachine::getValidTransitions;
};

// =================================================================
// Basic State Machine Tests
// =================================================================

class TransportSocketStateMachineTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Create config with test callbacks
    config_.mode = StateMachineConfig::Mode::Client;
    config_.allow_force_transitions = true;
    config_.max_state_history = 10;
    
    config_.state_change_callback = [this](const StateChangeEvent& event) {
      state_changes_.push_back(event);
    };
    
    config_.error_callback = [this](const std::string& error) {
      errors_.push_back(error);
    };
  }
  
  void createStateMachine() {
    executeInDispatcher([this]() {
      state_machine_ = std::make_unique<TestTransportSocketStateMachine>(
          *dispatcher_, config_);
    });
  }
  
  StateMachineConfig config_;
  std::unique_ptr<TestTransportSocketStateMachine> state_machine_;
  std::vector<StateChangeEvent> state_changes_;
  std::vector<std::string> errors_;
};

TEST_F(TransportSocketStateMachineTest, InitialState) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Uninitialized);
    EXPECT_EQ(state_machine_->getTotalTransitions(), 0);
    EXPECT_EQ(state_machine_->getErrorRecoveries(), 0);
  });
}

TEST_F(TransportSocketStateMachineTest, BasicTransition) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    auto result = state_machine_->transitionTo(
        TransportSocketState::Initialized,
        "Test initialization",
        nullptr);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Initialized);
    EXPECT_EQ(state_machine_->getTotalTransitions(), 1);
  });
  
  // Check state change was recorded
  EXPECT_EQ(state_changes_.size(), 1);
  EXPECT_EQ(state_changes_[0].from_state, TransportSocketState::Uninitialized);
  EXPECT_EQ(state_changes_[0].to_state, TransportSocketState::Initialized);
  EXPECT_EQ(state_changes_[0].reason, "Test initialization");
}

TEST_F(TransportSocketStateMachineTest, InvalidTransition) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Try invalid transition from Uninitialized to Connected
    auto result = state_machine_->transitionTo(
        TransportSocketState::Connected,
        "Invalid transition",
        nullptr);
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Uninitialized);
    EXPECT_EQ(state_machine_->getTotalTransitions(), 0);
  });
  
  // Check error was recorded
  EXPECT_EQ(errors_.size(), 1);
  EXPECT_TRUE(errors_[0].find("Invalid transition") != std::string::npos);
}

TEST_F(TransportSocketStateMachineTest, ForceTransition) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Force transition to a normally invalid state
    auto result = state_machine_->forceTransitionTo(
        TransportSocketState::Connected,
        "Forced for testing");
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Connected);
    EXPECT_EQ(state_machine_->getTotalTransitions(), 1);
  });
  
  // Check forced transition was recorded
  EXPECT_EQ(state_changes_.size(), 1);
  EXPECT_TRUE(state_changes_[0].reason.find("[FORCED]") != std::string::npos);
}

TEST_F(TransportSocketStateMachineTest, ScheduledTransition) {
  createStateMachine();
  
  std::atomic<bool> callback_called{false};
  
  executeInDispatcher([this, &callback_called]() {
    // First transition to Initialized
    state_machine_->transitionTo(
        TransportSocketState::Initialized,
        "Init",
        nullptr);
    
    // Schedule another transition
    state_machine_->scheduleTransition(
        TransportSocketState::Connecting,
        "Scheduled connect",
        [&callback_called](bool success) {
          callback_called = success;
        });
    
    // State should still be Initialized
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Initialized);
  });
  
  // Let the scheduled transition execute
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  executeInDispatcher([this, &callback_called]() {
    // Now should be in Connecting state
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Connecting);
    EXPECT_TRUE(callback_called.load());
  });
}

TEST_F(TransportSocketStateMachineTest, StateHistory) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Perform several transitions
    state_machine_->transitionTo(TransportSocketState::Initialized, "1", nullptr);
    state_machine_->transitionTo(TransportSocketState::Connecting, "2", nullptr);
    state_machine_->handleConnectionResult(true);
    state_machine_->transitionTo(TransportSocketState::HandshakeInProgress, "4", nullptr);
    state_machine_->handleHandshakeResult(true);
    
    // Check history
    auto& history = state_machine_->getStateHistory();
    EXPECT_GE(history.size(), 5);
    
    // Verify transitions are in order
    EXPECT_EQ(history[0].to_state, TransportSocketState::Initialized);
    EXPECT_EQ(history[1].to_state, TransportSocketState::Connecting);
    EXPECT_EQ(history[2].to_state, TransportSocketState::TcpConnected);
  });
}

TEST_F(TransportSocketStateMachineTest, CustomValidator) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Add custom validator that blocks all transitions to Error
    state_machine_->addTransitionValidator(
        [](TransportSocketState from, TransportSocketState to) {
          return to != TransportSocketState::Error;
        });
    
    // Try to transition to Error (should be blocked)
    state_machine_->transitionTo(TransportSocketState::Initialized, "Init", nullptr);
    auto result = state_machine_->transitionTo(
        TransportSocketState::Error,
        "Test error",
        nullptr);
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Initialized);
  });
}

TEST_F(TransportSocketStateMachineTest, StateTimeout) {
  config_.connect_timeout = std::chrono::milliseconds(100);
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Transition to Connecting state
    state_machine_->transitionTo(TransportSocketState::Initialized, "Init", nullptr);
    state_machine_->transitionTo(TransportSocketState::Connecting, "Connect", nullptr);
    
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Connecting);
  });
  
  // Wait for timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  executeInDispatcher([this]() {
    // Should have transitioned to Error due to timeout
    EXPECT_EQ(state_machine_->currentState(), 
              TransportSocketState::Error);
  });
  
  // Check state change reason
  auto it = std::find_if(state_changes_.begin(), state_changes_.end(),
      [](const StateChangeEvent& e) {
        return e.reason.find("timeout") != std::string::npos;
      });
  EXPECT_NE(it, state_changes_.end());
}

TEST_F(TransportSocketStateMachineTest, IoEventHandling) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Setup: get to Connected state
    state_machine_->transitionTo(TransportSocketState::Initialized, "Init", nullptr);
    state_machine_->transitionTo(TransportSocketState::Connecting, "Connect", nullptr);
    state_machine_->handleConnectionResult(true);
    state_machine_->transitionTo(TransportSocketState::HandshakeInProgress, "Handshake", nullptr);
    state_machine_->handleHandshakeResult(true);
    
    EXPECT_EQ(state_machine_->currentState(), TransportSocketState::Connected);
    
    // Handle readable event
    state_machine_->handleIoReady(true, false);
    EXPECT_EQ(state_machine_->currentState(), TransportSocketState::Reading);
    
    // Back to connected
    state_machine_->transitionTo(TransportSocketState::Connected, "Read complete", nullptr);
    
    // Handle writable event when write-blocked
    state_machine_->transitionTo(TransportSocketState::WriteBlocked, "Buffer full", nullptr);
    state_machine_->handleIoReady(false, true);
    EXPECT_EQ(state_machine_->currentState(), TransportSocketState::Writing);
  });
}

TEST_F(TransportSocketStateMachineTest, ErrorRecovery) {
  config_.max_error_recoveries = 2;
  createStateMachine();
  
  executeInDispatcher([this]() {
    // First error and recovery
    state_machine_->transitionTo(TransportSocketState::Initialized, "Init", nullptr);
    state_machine_->transitionTo(TransportSocketState::Error, "Error 1", nullptr);
    EXPECT_EQ(state_machine_->getErrorRecoveries(), 0);
    
    state_machine_->transitionTo(TransportSocketState::Initialized, "Retry 1", nullptr);
    EXPECT_EQ(state_machine_->getErrorRecoveries(), 1);
    
    // Second error and recovery
    state_machine_->transitionTo(TransportSocketState::Error, "Error 2", nullptr);
    state_machine_->transitionTo(TransportSocketState::Initialized, "Retry 2", nullptr);
    EXPECT_EQ(state_machine_->getErrorRecoveries(), 2);
    
    // Third error - should not be able to recover
    state_machine_->transitionTo(TransportSocketState::Error, "Error 3", nullptr);
    auto result = state_machine_->transitionTo(
        TransportSocketState::Initialized, 
        "Retry 3", 
        nullptr);
    
    EXPECT_FALSE(result.success);  // Recovery blocked
    EXPECT_EQ(state_machine_->currentState(), TransportSocketState::Error);
  });
}

TEST_F(TransportSocketStateMachineTest, ReentrantTransitionPrevention) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    bool nested_called = false;
    
    // Add state change listener that tries to transition again
    state_machine_->addStateChangeListener(
        [this, &nested_called](const StateChangeEvent& event) {
          if (!nested_called && event.to_state == TransportSocketState::Initialized) {
            nested_called = true;
            // Try to transition during another transition
            auto result = state_machine_->transitionTo(
                TransportSocketState::Connecting,
                "Nested transition",
                nullptr);
            // Should be scheduled, not executed immediately
            EXPECT_FALSE(result.success);
          }
        });
    
    // Initial transition
    state_machine_->transitionTo(TransportSocketState::Initialized, "Init", nullptr);
    EXPECT_TRUE(nested_called);
    
    // Should still be in Initialized (nested transition was scheduled)
    EXPECT_EQ(state_machine_->currentState(), TransportSocketState::Initialized);
  });
  
  // Let scheduled transition execute
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  executeInDispatcher([this]() {
    // Now should be in Connecting state from scheduled transition
    EXPECT_EQ(state_machine_->currentState(), TransportSocketState::Connecting);
  });
}

// =================================================================
// State Machine Test Helper Tests
// =================================================================

TEST_F(TransportSocketStateMachineTest, ValidateTransitionMatrix) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    // Should pass - all states have valid exits
    EXPECT_TRUE(StateMachineTestHelper::validateTransitionMatrix(*state_machine_));
  });
}

TEST_F(TransportSocketStateMachineTest, GenerateStateDiagram) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    auto diagram = StateMachineTestHelper::generateStateDiagram(*state_machine_);
    
    // Check diagram contains expected elements
    EXPECT_TRUE(diagram.find("digraph TransportSocketStateMachine") != std::string::npos);
    EXPECT_TRUE(diagram.find("Uninitialized") != std::string::npos);
    EXPECT_TRUE(diagram.find("Connected") != std::string::npos);
    EXPECT_TRUE(diagram.find("->") != std::string::npos);  // Has transitions
  });
}

TEST_F(TransportSocketStateMachineTest, RunCommonScenarios) {
  createStateMachine();
  
  executeInDispatcher([this]() {
    EXPECT_TRUE(StateMachineTestHelper::runCommonScenarios(*state_machine_));
  });
}

// =================================================================
// TCP Transport Socket State Machine Tests
// =================================================================

class TcpTransportSocketStateMachineTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Setup TCP config
    tcp_config_.mode = StateMachineConfig::Mode::Client;
    tcp_config_.tcp_nodelay = true;
    tcp_config_.keep_alive = true;
    tcp_config_.enable_auto_reconnect = false;
    tcp_config_.connect_timeout = std::chrono::milliseconds(5000);
  }
  
  TcpStateMachineConfig tcp_config_;
};

TEST_F(TcpTransportSocketStateMachineTest, CreateTcpStateMachine) {
  executeInDispatcher([this]() {
    // Create mock connection
    auto socket_pair = createSocketPair();
    auto client_handle = std::move(socket_pair.first);
    
    // Create connection socket
    auto local_addr = std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
    auto remote_addr = std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
    auto conn_socket = std::make_unique<network::AcceptedSocketImpl>(
        std::move(client_handle), local_addr, remote_addr);
    
    // Create TCP transport socket with state machine
    auto tcp_transport = std::make_unique<TcpTransportSocketStateMachine>(
        *conn_socket, *dispatcher_, tcp_config_);
    
    // Verify initial state
    EXPECT_EQ(tcp_transport->currentState(), TransportSocketState::Uninitialized);
    EXPECT_EQ(tcp_transport->protocol(), "tcp");
    EXPECT_TRUE(tcp_transport->canFlushClose());
    EXPECT_EQ(tcp_transport->ssl(), nullptr);
  });
}

TEST_F(TcpTransportSocketStateMachineTest, TcpConnectionFlow) {
  executeInDispatcher([this]() {
    // Create socket pair
    auto socket_pair = createSocketPair();
    auto client_handle = std::move(socket_pair.first);
    auto server_handle = std::move(socket_pair.second);
    
    // Create connection socket
    auto local_addr = std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
    auto remote_addr = std::make_shared<network::Address::Ipv4Instance>("127.0.0.1", 0);
    auto conn_socket = std::make_unique<network::AcceptedSocketImpl>(
        std::move(client_handle), local_addr, remote_addr);
    
    // Create TCP transport
    auto tcp_transport = std::make_unique<TcpTransportSocketStateMachine>(
        *conn_socket, *dispatcher_, tcp_config_);
    
    // Perform connection
    auto result = tcp_transport->connect(*conn_socket);
    EXPECT_TRUE(result.ok());
    
    // Notify connected
    tcp_transport->onConnected();
    
    // Should be in connected state
    EXPECT_EQ(tcp_transport->currentState(), TransportSocketState::Connected);
    
    // Test read/write
    auto buffer = createBuffer();
    buffer->add("test data");
    
    auto write_result = tcp_transport->doWrite(*buffer, false);
    EXPECT_EQ(write_result.action_, TransportIoResult::CONTINUE);
    
    // Close
    tcp_transport->closeSocket(network::ConnectionEvent::RemoteClose);
    EXPECT_EQ(tcp_transport->currentState(), TransportSocketState::Closed);
  });
}

}  // namespace
}  // namespace transport
}  // namespace mcp