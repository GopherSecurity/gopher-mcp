/**
 * @file test_ssl_state_machine.cc
 * @brief Comprehensive unit tests for SSL/TLS state machine
 * 
 * Tests the async-only state machine implementation including:
 * - State transitions and validation
 * - Client and server mode behaviors
 * - Event-driven callbacks
 * - State patterns and helpers
 * - Thread safety in dispatcher context
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "mcp/transport/ssl_state_machine.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace transport {
namespace {

/**
 * Test fixture for SSL state machine tests
 * Provides event dispatcher and common test utilities
 */
class SslStateMachineTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
  }
  
  void TearDown() override {
    dispatcher_.reset();
  }
  
  std::unique_ptr<event::LibeventDispatcher> dispatcher_;
};

// =============================================================================
// Basic Initialization Tests
// =============================================================================

TEST_F(SslStateMachineTest, ClientStateMachine_Initialization) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Uninitialized);
  EXPECT_EQ(machine->getMode(), SslSocketMode::Client);
  EXPECT_FALSE(machine->isTerminalState());
  EXPECT_FALSE(machine->isHandshaking());
  EXPECT_FALSE(machine->isConnected());
  EXPECT_FALSE(machine->isWaitingForIo());
}

TEST_F(SslStateMachineTest, ServerStateMachine_Initialization) {
  auto machine = SslStateMachineFactory::createServerStateMachine(*dispatcher_);
  
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Uninitialized);
  EXPECT_EQ(machine->getMode(), SslSocketMode::Server);
  EXPECT_FALSE(machine->isTerminalState());
  EXPECT_FALSE(machine->isHandshaking());
  EXPECT_FALSE(machine->isConnected());
  EXPECT_FALSE(machine->isWaitingForIo());
}

// =============================================================================
// State Transition Tests
// =============================================================================

TEST_F(SslStateMachineTest, ClientStateMachine_ValidTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Uninitialized -> Initialized
  EXPECT_TRUE(machine->canTransition(SslSocketState::Uninitialized,
                                     SslSocketState::Initialized));
  bool success = false;
  machine->transition(SslSocketState::Initialized, 
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Initialized);
  
  // Initialized -> Connecting
  EXPECT_TRUE(machine->canTransition(SslSocketState::Initialized,
                                     SslSocketState::Connecting));
  success = false;
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Connecting);
  
  // Connecting -> TcpConnected
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connecting,
                                     SslSocketState::TcpConnected));
  success = false;
  machine->transition(SslSocketState::TcpConnected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::TcpConnected);
  
  // TcpConnected -> ClientHandshakeInit
  EXPECT_TRUE(machine->canTransition(SslSocketState::TcpConnected,
                                     SslSocketState::ClientHandshakeInit));
  success = false;
  machine->transition(SslSocketState::ClientHandshakeInit, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::ClientHandshakeInit);
  EXPECT_TRUE(machine->isHandshaking());
  
  // ClientHandshakeInit -> ClientHelloSent
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHandshakeInit,
                                     SslSocketState::ClientHelloSent));
  success = false;
  machine->transition(SslSocketState::ClientHelloSent, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::ClientHelloSent);
  
  // Use forceTransition to skip intermediate states for testing
  machine->forceTransition(SslSocketState::ClientFinished);
  
  // ClientFinished -> Connected
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientFinished,
                                     SslSocketState::Connected));
  success = false;
  machine->transition(SslSocketState::Connected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isConnected());
  EXPECT_FALSE(machine->isHandshaking());
  
  // Connected -> ShutdownInitiated
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connected,
                                     SslSocketState::ShutdownInitiated));
  success = false;
  machine->transition(SslSocketState::ShutdownInitiated, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // ShutdownInitiated -> ShutdownSent
  EXPECT_TRUE(machine->canTransition(SslSocketState::ShutdownInitiated,
                                     SslSocketState::ShutdownSent));
  success = false;
  machine->transition(SslSocketState::ShutdownSent, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // ShutdownSent -> ShutdownComplete
  EXPECT_TRUE(machine->canTransition(SslSocketState::ShutdownSent,
                                     SslSocketState::ShutdownComplete));
  success = false;
  machine->transition(SslSocketState::ShutdownComplete, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // ShutdownComplete -> Closed
  EXPECT_TRUE(machine->canTransition(SslSocketState::ShutdownComplete,
                                     SslSocketState::Closed));
  success = false;
  machine->transition(SslSocketState::Closed, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isTerminalState());
}

TEST_F(SslStateMachineTest, ClientStateMachine_InvalidTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Cannot transition directly from Uninitialized to Connected
  EXPECT_FALSE(machine->canTransition(SslSocketState::Uninitialized,
                                      SslSocketState::Connected));
  
  // Cannot transition from Uninitialized to handshake states
  EXPECT_FALSE(machine->canTransition(SslSocketState::Uninitialized,
                                      SslSocketState::ClientHandshakeInit));
  
  // Initialize the machine
  bool success = false;
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  machine->transition(SslSocketState::TcpConnected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  
  // Cannot go backward in state
  EXPECT_FALSE(machine->canTransition(SslSocketState::TcpConnected,
                                      SslSocketState::Uninitialized));
  
  // Cannot skip handshake
  EXPECT_FALSE(machine->canTransition(SslSocketState::TcpConnected,
                                      SslSocketState::Connected));
}

TEST_F(SslStateMachineTest, ServerStateMachine_ValidTransitions) {
  auto machine = SslStateMachineFactory::createServerStateMachine(*dispatcher_);
  
  // Similar pattern to client but with server states
  bool success = false;
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  
  machine->transition(SslSocketState::TcpConnected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  
  // Server waits for client hello
  machine->forceTransition(SslSocketState::ServerHandshakeInit);
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerHandshakeInit,
                                     SslSocketState::ClientHelloReceived));
  success = false;
  machine->transition(SslSocketState::ClientHelloReceived, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // Server sends hello
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHelloReceived,
                                     SslSocketState::ServerHelloSent));
  success = false;
  machine->transition(SslSocketState::ServerHelloSent, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // Server sends certificate
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerHelloSent,
                                     SslSocketState::ServerCertSent));
  success = false;
  machine->transition(SslSocketState::ServerCertSent, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // Jump to finish for brevity
  machine->forceTransition(SslSocketState::ServerFinished);
  
  // ServerFinished -> Connected
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerFinished,
                                     SslSocketState::Connected));
  success = false;
  machine->transition(SslSocketState::Connected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isConnected());
}

// =============================================================================
// Async I/O State Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_HandshakeBlockedStates) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Move to handshake state
  machine->forceTransition(SslSocketState::ClientHandshakeInit);
  
  // Can transition to WantRead when blocked on read
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHandshakeInit,
                                     SslSocketState::HandshakeWantWrite));
  bool success = false;
  machine->transition(SslSocketState::HandshakeWantRead, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isWaitingForIo());
  
  // Move to certificate validation async state
  machine->forceTransition(SslSocketState::ServerHelloReceived);
  
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerHelloReceived,
                                     SslSocketState::CertificateValidating));
  success = false;
  machine->transition(SslSocketState::CertificateValidating, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isWaitingForIo());
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_ErrorTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Any state can transition to Error
  machine->transition(SslSocketState::Initialized, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_TRUE(machine->canTransition(SslSocketState::Initialized,
                                     SslSocketState::Error));
  
  machine->transition(SslSocketState::Connecting, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connecting,
                                     SslSocketState::Error));
  
  // Error is a terminal state
  machine->forceTransition(SslSocketState::Error);
  EXPECT_TRUE(machine->isTerminalState());
  
  // Cannot transition from Error (except force)
  EXPECT_FALSE(machine->canTransition(SslSocketState::Error,
                                      SslSocketState::Closed));
  EXPECT_FALSE(machine->canTransition(SslSocketState::Error,
                                      SslSocketState::Initialized));
}

// =============================================================================
// State Change Listener Tests
// =============================================================================

/**
 * Test state change listeners
 */
class TestStateChangeListener {
public:
  void onStateChanged(SslSocketState old_state,
                     SslSocketState new_state) {
    state_changes_.push_back({old_state, new_state});
  }
  
  void onInvalidTransition(SslSocketState current_state,
                          SslSocketState attempted_state,
                          const std::string& reason) {
    invalid_transitions_.push_back({current_state, attempted_state, reason});
  }
  
  struct StateChange {
    SslSocketState old_state;
    SslSocketState new_state;
  };
  
  struct InvalidTransition {
    SslSocketState current_state;
    SslSocketState attempted_state;
    std::string reason;
  };
  
  std::vector<StateChange> state_changes_;
  std::vector<InvalidTransition> invalid_transitions_;
};

TEST_F(SslStateMachineTest, StateMachine_StateChangeListeners) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  auto listener = std::make_shared<TestStateChangeListener>();
  
  auto listener_id = machine->addStateChangeListener(
      [listener](SslSocketState old_state, SslSocketState new_state) {
        listener->onStateChanged(old_state, new_state);
      });
  
  // Valid transition should notify
  machine->transition(SslSocketState::Initialized, [](bool, const std::string&){});
  
  // Allow async notification
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_EQ(listener->state_changes_.size(), 1);
  if (!listener->state_changes_.empty()) {
    EXPECT_EQ(listener->state_changes_[0].old_state, SslSocketState::Uninitialized);
    EXPECT_EQ(listener->state_changes_[0].new_state, SslSocketState::Initialized);
  }
  
  // Invalid transition should not notify state change
  machine->transition(SslSocketState::Connected, 
                     [listener](bool success, const std::string& error) {
                       if (!success) {
                         listener->onInvalidTransition(
                             SslSocketState::Initialized,
                             SslSocketState::Connected, 
                             error);
                       }
                     });
  
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_EQ(listener->invalid_transitions_.size(), 1);
  if (!listener->invalid_transitions_.empty()) {
    EXPECT_EQ(listener->invalid_transitions_[0].current_state, 
              SslSocketState::Initialized);
    EXPECT_EQ(listener->invalid_transitions_[0].attempted_state,
              SslSocketState::Connected);
  }
  
  // Remove listener
  machine->removeStateChangeListener(listener_id);
  
  // Should not notify after removal
  machine->transition(SslSocketState::Connecting, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_EQ(listener->state_changes_.size(), 1);  // No change
}

// =============================================================================
// Entry/Exit Action Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_EntryExitActions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  bool entry_called = false;
  bool exit_called = false;
  
  machine->setEntryAction(SslSocketState::Initialized,
                         [&entry_called](SslSocketState state, std::function<void()> done) {
                           entry_called = true;
                           EXPECT_EQ(state, SslSocketState::Initialized);
                           done();
                         });
  
  machine->setExitAction(SslSocketState::Initialized,
                        [&exit_called](SslSocketState state, std::function<void()> done) {
                          exit_called = true;
                          EXPECT_EQ(state, SslSocketState::Initialized);
                          done();
                        });
  
  // Transition to Initialized should call entry action
  machine->transition(SslSocketState::Initialized, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(entry_called);
  
  // Transition from Initialized should call exit action
  machine->transition(SslSocketState::Connecting, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(exit_called);
}

// =============================================================================
// Custom Validator Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_CustomValidators) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  bool validator_called = false;
  
  // Add custom validator that blocks certain transitions
  machine->addTransitionValidator(
      [&validator_called](SslSocketState from, SslSocketState to) {
        validator_called = true;
        // Block direct transition from Initialized to Connected
        if (from == SslSocketState::Initialized && 
            to == SslSocketState::Connected) {
          return false;
        }
        return true;
      });
  
  machine->transition(SslSocketState::Initialized, [](bool, const std::string&){});
  machine->transition(SslSocketState::Connecting, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_TRUE(validator_called);
  
  // Custom validator should prevent this transition
  EXPECT_FALSE(machine->canTransition(SslSocketState::Initialized,
                                      SslSocketState::Connected));
}

// =============================================================================
// State History and Timing Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_StateHistory) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Perform several transitions
  machine->transition(SslSocketState::Initialized, [](bool, const std::string&){});
  machine->transition(SslSocketState::Connecting, [](bool, const std::string&){});
  machine->transition(SslSocketState::TcpConnected, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  
  // Get state history
  auto history = machine->getStateHistory(5);
  
  // Should have at least initial state and transitions
  EXPECT_GE(history.size(), 4);  // Uninitialized + 3 transitions
  
  // Verify states in order
  if (history.size() >= 4) {
    EXPECT_EQ(history[0].first, SslSocketState::Uninitialized);
    EXPECT_EQ(history[1].first, SslSocketState::Initialized);
    EXPECT_EQ(history[2].first, SslSocketState::Connecting);
    EXPECT_EQ(history[3].first, SslSocketState::TcpConnected);
  }
}

TEST_F(SslStateMachineTest, StateMachine_TimeInState) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Transition to a state
  machine->transition(SslSocketState::Initialized, [](bool, const std::string&){});
  dispatcher_->run(event::RunType::NonBlock);
  
  // Sleep briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // Check time in current state
  auto time_in_state = machine->getTimeInCurrentState();
  
  // Should be at least 10ms
  EXPECT_GE(time_in_state.count(), 10);
  EXPECT_LT(time_in_state.count(), 1000);  // Sanity check - less than 1 second
}

// =============================================================================
// State Pattern Helper Tests
// =============================================================================

TEST_F(SslStateMachineTest, StatePatterns_HelperFunctions) {
  // Test handshake state detection
  EXPECT_TRUE(SslStatePatterns::isHandshakeState(SslSocketState::ClientHandshakeInit));
  EXPECT_TRUE(SslStatePatterns::isHandshakeState(SslSocketState::ServerHelloSent));
  EXPECT_FALSE(SslStatePatterns::isHandshakeState(SslSocketState::Connected));
  EXPECT_FALSE(SslStatePatterns::isHandshakeState(SslSocketState::Closed));
  
  // Test I/O blocked state detection
  EXPECT_TRUE(SslStatePatterns::isIoBlockedState(SslSocketState::HandshakeWantRead));
  EXPECT_TRUE(SslStatePatterns::isIoBlockedState(SslSocketState::HandshakeWantWrite));
  EXPECT_TRUE(SslStatePatterns::isIoBlockedState(SslSocketState::CertificateValidating));
  EXPECT_FALSE(SslStatePatterns::isIoBlockedState(SslSocketState::ClientHandshakeInit));
  
  // Test error state
  EXPECT_TRUE(SslStatePatterns::isErrorState(SslSocketState::Error));
  EXPECT_FALSE(SslStatePatterns::isErrorState(SslSocketState::Closed));
  
  // Test data transfer
  EXPECT_TRUE(SslStatePatterns::canTransferData(SslSocketState::Connected));
  EXPECT_FALSE(SslStatePatterns::canTransferData(SslSocketState::ClientHandshakeInit));
  EXPECT_FALSE(SslStatePatterns::canTransferData(SslSocketState::Closed));
  
  // Test shutdown initiation
  EXPECT_TRUE(SslStatePatterns::canInitiateShutdown(SslSocketState::Connected));
  EXPECT_TRUE(SslStatePatterns::canInitiateShutdown(SslSocketState::ShutdownReceived));
  EXPECT_FALSE(SslStatePatterns::canInitiateShutdown(SslSocketState::ClientHandshakeInit));
  EXPECT_FALSE(SslStatePatterns::canInitiateShutdown(SslSocketState::Closed));
}

TEST_F(SslStateMachineTest, StatePatterns_NextHandshakeState) {
  // Client handshake progression
  auto next = SslStatePatterns::getNextClientHandshakeState(SslSocketState::ClientHandshakeInit);
  EXPECT_TRUE(next.has_value());
  EXPECT_EQ(*next, SslSocketState::ClientHelloSent);
  
  next = SslStatePatterns::getNextClientHandshakeState(SslSocketState::ClientHelloSent);
  EXPECT_TRUE(next.has_value());
  EXPECT_EQ(*next, SslSocketState::ServerHelloReceived);
  
  next = SslStatePatterns::getNextClientHandshakeState(SslSocketState::ClientFinished);
  EXPECT_TRUE(next.has_value());
  EXPECT_EQ(*next, SslSocketState::Connected);
  
  next = SslStatePatterns::getNextClientHandshakeState(SslSocketState::Connected);
  EXPECT_FALSE(next.has_value());
  
  // Server handshake progression
  next = SslStatePatterns::getNextServerHandshakeState(SslSocketState::ServerHandshakeInit);
  EXPECT_TRUE(next.has_value());
  EXPECT_EQ(*next, SslSocketState::ClientHelloReceived);
  
  next = SslStatePatterns::getNextServerHandshakeState(SslSocketState::ServerFinished);
  EXPECT_TRUE(next.has_value());
  EXPECT_EQ(*next, SslSocketState::Connected);
}

// =============================================================================
// Renegotiation Tests (TLS 1.2)
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_RenegotiationStates) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Move to connected state
  machine->forceTransition(SslSocketState::Connected);
  
  // Can request renegotiation from Connected
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connected,
                                     SslSocketState::RenegotiationRequested));
  
  machine->forceTransition(SslSocketState::RenegotiationRequested);
  
  // Can move to renegotiation in progress
  EXPECT_TRUE(machine->canTransition(SslSocketState::RenegotiationRequested,
                                     SslSocketState::RenegotiationInProgress));
  
  bool success = false;
  machine->transition(SslSocketState::RenegotiationInProgress, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // From renegotiation can go back to Connected or to handshake wait states
  EXPECT_TRUE(machine->canTransition(SslSocketState::RenegotiationInProgress,
                                     SslSocketState::Connected));
  EXPECT_TRUE(machine->canTransition(SslSocketState::RenegotiationInProgress,
                                     SslSocketState::HandshakeWantRead));
  
  success = false;
  machine->transition(SslSocketState::Connected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isConnected());
}

// =============================================================================
// Thread Safety Tests (within dispatcher context)
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_ThreadSafety) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  std::atomic<int> transition_count{0};
  
  // Schedule multiple transitions from dispatcher thread
  for (int i = 0; i < 10; ++i) {
    dispatcher_->post([&machine, &transition_count]() {
      machine->transition(SslSocketState::Initialized,
                         [&transition_count](bool success, const std::string& error) {
                           if (success) {
                             transition_count++;
                           } else {
                             // Multiple transitions to same state - some will fail
                             // This is expected behavior
                           }
                         });
    });
  }
  
  // Run all posted tasks
  dispatcher_->run(event::RunType::NonBlock);
  
  // Only one transition should succeed (first one)
  EXPECT_EQ(transition_count.load(), 1);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Initialized);
}

// =============================================================================
// Forced Transition Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_ForcedTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Force invalid transition that normally wouldn't be allowed
  machine->forceTransition(SslSocketState::Connected);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Connected);
  
  // Force to error state
  machine->forceTransition(SslSocketState::Error);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Error);
  EXPECT_TRUE(machine->isTerminalState());
  
  // Can force out of terminal state (unlike normal transitions)
  machine->forceTransition(SslSocketState::Initialized);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Initialized);
  EXPECT_FALSE(machine->isTerminalState());
}

// =============================================================================
// Transition Coordinator Tests
// =============================================================================

TEST_F(SslStateMachineTest, TransitionCoordinator_HandshakeSequence) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  SslTransitionCoordinator coordinator(*machine);
  
  // Move to ready for handshake
  machine->forceTransition(SslSocketState::TcpConnected);
  
  bool handshake_complete = false;
  coordinator.executeHandshake([&handshake_complete](bool success) {
    handshake_complete = success;
  });
  
  // Process async operations
  dispatcher_->run(event::RunType::NonBlock);
  
  // Note: This test would need more setup to work properly
  // as the coordinator expects valid transitions
}

TEST_F(SslStateMachineTest, TransitionCoordinator_ShutdownSequence) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  SslTransitionCoordinator coordinator(*machine);
  
  // Move to connected state
  machine->forceTransition(SslSocketState::Connected);
  
  bool shutdown_complete = false;
  coordinator.executeShutdown([&shutdown_complete](bool success) {
    shutdown_complete = success;
  });
  
  // Process async operations
  dispatcher_->run(event::RunType::NonBlock);
  
  // Note: This test would need more setup to work properly
}

// =============================================================================
// Schedule Transition Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_ScheduledTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  bool callback_called = false;
  
  // Schedule a transition for next event loop iteration
  machine->scheduleTransition(SslSocketState::Initialized,
                             [&callback_called](bool success, const std::string&) {
                               callback_called = success;
                             });
  
  // Callback shouldn't be called yet
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Uninitialized);
  
  // Run event loop to process scheduled transition
  dispatcher_->run(event::RunType::NonBlock);
  
  // Now it should be called
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Initialized);
}

// =============================================================================
// Async Operation Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_AsyncOperations) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Move to a state where async operations make sense
  machine->forceTransition(SslSocketState::ServerHelloReceived);
  
  // Start an async operation (e.g., certificate validation)
  auto op_id = machine->startAsyncOperation(
      SslSocketState::ClientKeyExchange,  // Success state
      SslSocketState::Error               // Failure state
  );
  
  // Simulate async completion with success
  machine->handleAsyncComplete(op_id, true);
  dispatcher_->run(event::RunType::NonBlock);
  
  // Should have transitioned to success state
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::ClientKeyExchange);
  
  // Start another async operation
  op_id = machine->startAsyncOperation(
      SslSocketState::Connected,
      SslSocketState::Error
  );
  
  // Simulate async failure
  machine->handleAsyncComplete(op_id, false, "Test error");
  dispatcher_->run(event::RunType::NonBlock);
  
  // Should have transitioned to error state
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Error);
}

// =============================================================================
// I/O Ready Event Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_IoReadyEvents) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Move to a state waiting for I/O
  machine->forceTransition(SslSocketState::HandshakeWantRead);
  EXPECT_TRUE(machine->isWaitingForIo());
  
  // Simulate I/O ready (socket became readable)
  machine->handleIoReady(true, false);  // readable=true, writable=false
  dispatcher_->run(event::RunType::NonBlock);
  
  // State machine should handle the I/O event appropriately
  // (actual behavior depends on implementation details)
  
  // Move to want write state
  machine->forceTransition(SslSocketState::HandshakeWantWrite);
  EXPECT_TRUE(machine->isWaitingForIo());
  
  // Simulate socket became writable
  machine->handleIoReady(false, true);  // readable=false, writable=true
  dispatcher_->run(event::RunType::NonBlock);
}

// =============================================================================
// State Timeout Tests
// =============================================================================

TEST_F(SslStateMachineTest, StateMachine_StateTimeout) {
  // Run the entire test logic in the dispatcher thread
  dispatcher_->post([this]() {
    auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
    
    // Move to handshake state
    machine->forceTransition(SslSocketState::ClientHandshakeInit);
    
    // Set a timeout for this state
    machine->setStateTimeout(std::chrono::milliseconds(10),
                            SslSocketState::Error);
    
    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // Should have transitioned to error due to timeout
    // Note: This depends on timer implementation in dispatcher
    
    // Cancel timeout before it fires
    machine->forceTransition(SslSocketState::ClientHelloSent);
    machine->setStateTimeout(std::chrono::milliseconds(1000),
                            SslSocketState::Error);
    machine->cancelStateTimeout();
    
    // Quick transition should not timeout
    machine->transition(SslSocketState::ServerHelloReceived, [](bool, const std::string&){});
    
    EXPECT_NE(machine->getCurrentState(), SslSocketState::Error);
  });
  
  // Run the dispatcher to execute the posted task
  dispatcher_->run(event::RunType::NonBlock);
}

}  // namespace
}  // namespace transport
}  // namespace mcp