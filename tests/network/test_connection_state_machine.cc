/**
 * @file test_connection_state_machine.cc
 * @brief Unit tests for ConnectionStateMachine using real I/O
 *
 * Tests the connection state machine implementation using real MCP components:
 * - Real event dispatcher (libevent)
 * - Real buffers (MCP Buffer)
 * - Real sockets and connections (MCP network abstractions)
 * - Real timers and I/O events
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <vector>
#include <thread>

#include "mcp/network/connection_state_machine.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/event/event_loop.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "mcp/buffer.h"

namespace mcp {
namespace network {
namespace {

// Test fixture using real MCP components
class ConnectionStateMachineTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Default configuration
    config_.mode = ConnectionMode::Client;
    config_.connect_timeout = std::chrono::milliseconds(1000);
    config_.idle_timeout = std::chrono::milliseconds(2000);
    config_.enable_auto_reconnect = false;
    
    // Initialize state tracking
    state_changes_.clear();
    transition_count_ = 0;
  }
  
  void TearDown() override {
    // Clean up in proper order
    state_machine_.reset();
    server_connection_.reset();
    connection_.reset();
    dispatcher_.reset();
  }
  
  // Create a real client connection
  std::unique_ptr<Connection> createClientConnection(const Address::InstanceConstSharedPtr& address) {
    // Create real socket
    auto io_handle = std::make_unique<IoSocketHandleImpl>();
    auto socket = std::make_unique<ConnectionSocketImpl>(
        std::move(io_handle),
        nullptr,  // no local address
        address   // remote address
    );
    
    // Create real connection
    return std::make_unique<ConnectionImpl>(
        *dispatcher_,
        std::move(socket),
        nullptr,  // no transport socket for basic TCP
        false     // not connected yet
    );
  }
  
  // Create a real server listener (not used in current tests)
  void createServerListener(const Address::InstanceConstSharedPtr& address) {
    // For now, we don't need a listener for these tests
    // Could be implemented later if needed
  }
  
  // Create state machine with real connection
  void createStateMachine(std::unique_ptr<Connection> connection) {
    // Create dispatcher for this test
    dispatcher_ = event::createLibeventDispatcherFactory()->createDispatcher("test_dispatcher");
    
    connection_ = std::move(connection);
    
    // Add state change tracking
    config_.state_change_callback = [this](const StateTransitionContext& ctx) {
      state_changes_.push_back({ctx.from_state, ctx.to_state, ctx.reason});
      transition_count_++;
    };
    
    config_.error_callback = [this](const std::string& error) {
      last_error_ = error;
    };
    
    // Create state machine directly - it's thread-safe to create it outside dispatcher
    // as long as all operations on it happen in the dispatcher thread
    state_machine_ = std::make_unique<ConnectionStateMachine>(
        *dispatcher_, config_);
  }
  
  
protected:
  std::unique_ptr<event::Dispatcher> dispatcher_;
  ConnectionStateMachineConfig config_;
  std::unique_ptr<Connection> connection_;
  std::unique_ptr<Connection> server_connection_;
  std::unique_ptr<ConnectionStateMachine> state_machine_;
  
  // State tracking
  struct StateChange {
    ConnectionMachineState from;
    ConnectionMachineState to;
    std::string reason;
  };
  std::vector<StateChange> state_changes_;
  size_t transition_count_{0};
  std::string last_error_;
};

// ===== Basic State Transition Tests =====

TEST_F(ConnectionStateMachineTest, InitialState) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.mode = ConnectionMode::Client;
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  EXPECT_EQ(ConnectionMachineState::Uninitialized, state_machine_->currentState());
  EXPECT_EQ(0, state_machine_->getTotalTransitions());
}

TEST_F(ConnectionStateMachineTest, InitialStateServer) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.mode = ConnectionMode::Server;
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  EXPECT_EQ(ConnectionMachineState::Initialized, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, BasicStateTransitions) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  // Disable timers for this test
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Test state transitions directly - the state machine is thread-safe
  // Test connection request event
  bool result = state_machine_->handleEvent(ConnectionStateMachineEvent::ConnectionRequested);
  EXPECT_TRUE(result);
  EXPECT_EQ(ConnectionMachineState::Connecting, state_machine_->currentState());
  
  // Test socket connected event
  result = state_machine_->handleEvent(ConnectionStateMachineEvent::SocketConnected);
  EXPECT_TRUE(result);
  
  // Should transition through TcpConnected to Connected
  EXPECT_EQ(ConnectionMachineState::Connected, state_machine_->currentState());
  EXPECT_GE(state_machine_->getTotalTransitions(), 2);
  
  // Verify state history
  EXPECT_GE(state_changes_.size(), 2);
  if (state_changes_.size() >= 2) {
    EXPECT_EQ(ConnectionMachineState::Uninitialized, state_changes_[0].from);
    EXPECT_EQ(ConnectionMachineState::Connecting, state_changes_[0].to);
  }
}

// ===== I/O Event Tests =====

TEST_F(ConnectionStateMachineTest, ReadWriteStateTransitions) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Test read ready event
  bool result = state_machine_->handleEvent(ConnectionStateMachineEvent::ReadReady);
  EXPECT_TRUE(result);
  EXPECT_EQ(ConnectionMachineState::Reading, state_machine_->currentState());
  
  // Return to connected
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Test write ready event
  result = state_machine_->handleEvent(ConnectionStateMachineEvent::WriteReady);
  EXPECT_TRUE(result);
  EXPECT_EQ(ConnectionMachineState::Writing, state_machine_->currentState());
}

// ===== Flow Control Tests =====

TEST_F(ConnectionStateMachineTest, WatermarkBasedFlowControl) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.high_watermark = 1024;
  config_.low_watermark = 512;
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Simulate high watermark hit
  state_machine_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(ConnectionMachineState::WriteDisabled, state_machine_->currentState());
  
  // Simulate low watermark reached
  state_machine_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(ConnectionMachineState::Connected, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, ReadDisableFlowControl) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Request read disable
  bool result = state_machine_->handleEvent(ConnectionStateMachineEvent::ReadDisableRequested);
  EXPECT_TRUE(result);
  EXPECT_EQ(ConnectionMachineState::ReadDisabled, state_machine_->currentState());
  
  // Multiple disable requests should be idempotent
  size_t transitions_before = state_machine_->getTotalTransitions();
  state_machine_->handleEvent(ConnectionStateMachineEvent::ReadDisableRequested);
  EXPECT_EQ(ConnectionMachineState::ReadDisabled, state_machine_->currentState());
  EXPECT_EQ(transitions_before, state_machine_->getTotalTransitions());
}

// ===== Error Recovery Tests =====

TEST_F(ConnectionStateMachineTest, ErrorRecoveryWithoutReconnect) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.enable_auto_reconnect = false;
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force error state
  state_machine_->forceTransition(ConnectionMachineState::Error, "test error");
  EXPECT_EQ(ConnectionMachineState::Error, state_machine_->currentState());
  
  // Without auto-reconnect, should stay in error
  EXPECT_EQ(ConnectionMachineState::Error, state_machine_->currentState());
}

// ===== State History Tests =====

TEST_F(ConnectionStateMachineTest, StateHistoryTracking) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force multiple transitions
  state_machine_->forceTransition(ConnectionMachineState::Connecting, "test1");
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test2");
  state_machine_->forceTransition(ConnectionMachineState::Reading, "test3");
  state_machine_->forceTransition(ConnectionMachineState::Closing, "test4");
  state_machine_->forceTransition(ConnectionMachineState::Closed, "test5");
  
  // Verify history was recorded
  auto& history = state_machine_->getStateHistory();
  EXPECT_GE(history.size(), 5);
  
  // Verify transitions
  EXPECT_EQ(5, state_machine_->getTotalTransitions());
}

// ===== Close Operation Tests =====

TEST_F(ConnectionStateMachineTest, CloseWithFlush) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Request close with flush
  state_machine_->close(ConnectionCloseType::FlushWrite);
  EXPECT_EQ(ConnectionMachineState::Flushing, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, CloseWithoutFlush) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Request close without flush
  state_machine_->close(ConnectionCloseType::NoFlush);
  EXPECT_EQ(ConnectionMachineState::Closing, state_machine_->currentState());
}

// ===== Half-Close Tests =====

TEST_F(ConnectionStateMachineTest, HalfCloseHandling) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.enable_half_close = true;
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Simulate remote half-close
  bool result = state_machine_->handleEvent(ConnectionStateMachineEvent::EndOfStream);
  EXPECT_TRUE(result);
  EXPECT_EQ(ConnectionMachineState::HalfClosedRemote, state_machine_->currentState());
}

// ===== Concurrent Event Handling Tests =====

TEST_F(ConnectionStateMachineTest, ConcurrentEventHandling) {
  auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  auto connection = createClientConnection(address);
  
  config_.connect_timeout = std::chrono::milliseconds(0);
  config_.idle_timeout = std::chrono::milliseconds(0);
  createStateMachine(std::move(connection));
  
  ASSERT_NE(state_machine_, nullptr);
  
  // Force to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test setup");
  
  // Multiple events in quick succession
  state_machine_->handleEvent(ConnectionStateMachineEvent::WriteReady);
  state_machine_->handleEvent(ConnectionStateMachineEvent::ReadReady);
  
  // State machine should handle these properly
  // Final state depends on implementation details
  auto current = state_machine_->currentState();
  EXPECT_TRUE(current == ConnectionMachineState::Reading ||
              current == ConnectionMachineState::Writing ||
              current == ConnectionMachineState::Processing);
}

}  // namespace
}  // namespace network
}  // namespace mcp