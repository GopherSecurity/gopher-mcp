/**
 * @file test_connection_state_machine.cc
 * @brief Unit tests for ConnectionStateMachine
 *
 * Tests the connection state machine implementation including:
 * - State transitions and validation
 * - Event handling
 * - Timer management
 * - Error recovery and reconnection
 * - Flow control states
 * - Callbacks and observers
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <memory>
#include <vector>

#include "mcp/network/connection_state_machine.h"
#include "mcp/event/libevent_dispatcher.h"

namespace mcp {
namespace network {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::InSequence;
using ::testing::NiceMock;

// Mock connection for testing
class MockConnection : public Connection {
public:
  MOCK_METHOD(void, addConnectionCallbacks, (ConnectionCallbacks& cb), (override));
  MOCK_METHOD(void, removeConnectionCallbacks, (ConnectionCallbacks& cb), (override));
  MOCK_METHOD(void, addBytesSentCallback, (BytesSentCb cb), (override));
  MOCK_METHOD(void, enableHalfClose, (bool enabled), (override));
  MOCK_METHOD(bool, isHalfCloseEnabled, (), (const, override));
  MOCK_METHOD(void, close, (ConnectionCloseType type), (override));
  MOCK_METHOD(void, close, (ConnectionCloseType type, const std::string& details), (override));
  MOCK_METHOD(DetectedCloseType, detectedCloseType, (), (const, override));
  MOCK_METHOD(event::Dispatcher&, dispatcher, (), (const, override));
  MOCK_METHOD(uint64_t, id, (), (const, override));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>& hash), (const, override));
  MOCK_METHOD(std::string, nextProtocol, (), (const, override));
  MOCK_METHOD(void, noDelay, (bool enable), (override));
  MOCK_METHOD(ReadDisableStatus, readDisableWithStatus, (bool disable), (override));
  MOCK_METHOD(void, detectEarlyCloseWhenReadDisabled, (bool should_detect), (override));
  MOCK_METHOD(bool, readEnabled, (), (const, override));
  MOCK_METHOD(ConnectionInfoSetter&, connectionInfoSetter, (), (override));
  MOCK_METHOD(const ConnectionInfoProvider&, connectionInfoProvider, (), (const, override));
  MOCK_METHOD(ConnectionInfoProviderSharedPtr, connectionInfoProviderSharedPtr, (), (const, override));
  MOCK_METHOD(absl::optional<UnixDomainSocketPeerCredentials>, unixSocketPeerCredentials, (), (const, override));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const, override));
  MOCK_METHOD(ConnectionState, state, (), (const, override));
  MOCK_METHOD(bool, connecting, (), (const, override));
  MOCK_METHOD(void, write, (Buffer::Instance& data, bool end_stream), (override));
  MOCK_METHOD(void, setBufferLimits, (uint32_t limit), (override));
  MOCK_METHOD(uint32_t, bufferLimit, (), (const, override));
  MOCK_METHOD(bool, aboveHighWatermark, (), (const, override));
  MOCK_METHOD(const ConnectionSocket::OptionsSharedPtr&, socketOptions, (), (const, override));
  MOCK_METHOD(bool, setSocketOption, (int level, int name, const void* value, socklen_t len), (override));
  MOCK_METHOD(absl::string_view, requestedServerName, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const, override));
  MOCK_METHOD(absl::string_view, transportFailureReason, (), (const, override));
  MOCK_METHOD(bool, startSecureTransport, (), (override));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const, override));
  MOCK_METHOD(void, configureInitialCongestionWindow, (uint64_t, std::chrono::microseconds), (override));
  MOCK_METHOD(absl::optional<uint64_t>, congestionWindowInBytes, (), (const, override));
  
  // FilterManagerConnection methods
  MOCK_METHOD(void, initializeReadFilters, (), (override));
  MOCK_METHOD(bool, startWriteAndFlush, (), (override));
  MOCK_METHOD(void, rawWrite, (Buffer::Instance& data, bool end_stream), (override));
  MOCK_METHOD(void, closeConnection, (ConnectionCloseType type), (override));
  MOCK_METHOD(StreamBuffer, getReadBuffer, (), (override));
  MOCK_METHOD(StreamBuffer, getWriteBuffer, (), (override));
  MOCK_METHOD(void, onWrite, (Buffer::Instance& buffer, bool end_stream), (override));
  MOCK_METHOD(void, onRead, (Buffer::Instance& buffer, bool end_stream), (override));
  MOCK_METHOD(void, addReadFilter, (ReadFilterSharedPtr filter), (override));
  MOCK_METHOD(void, addWriteFilter, (WriteFilterSharedPtr filter), (override));
  MOCK_METHOD(void, addFilter, (FilterSharedPtr filter), (override));
  MOCK_METHOD(void, removeReadFilter, (ReadFilterSharedPtr filter), (override));
  MOCK_METHOD(bool, initializeReadFilters, (), (override));
  MOCK_METHOD(void, setBufferStats, (const BufferStats& stats), (override));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, remoteAddress, (), (const, override));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, directRemoteAddress, (), (const, override));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const, override));
  MOCK_METHOD(void, readDisable, (bool disable), (override));
  MOCK_METHOD(void, setDelayedCloseTimeout, (std::chrono::milliseconds), (override));
  MOCK_METHOD(std::chrono::milliseconds, delayedCloseTimeout, (), (const, override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(bool, isHalfClosed, (), (const, override));
  
  // DeferredDeletable
  MOCK_METHOD(void, deferredDelete, (), (override));
};

// Test fixture
class ConnectionStateMachineTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create real dispatcher for testing
    dispatcher_ = std::make_unique<event::LibeventDispatcher>();
    
    // Create mock connection
    connection_ = std::make_unique<NiceMock<MockConnection>>();
    ON_CALL(*connection_, dispatcher()).WillByDefault(ReturnRef(*dispatcher_));
    
    // Default configuration
    config_.mode = ConnectionMode::Client;
    config_.connect_timeout = std::chrono::milliseconds(100);
    config_.idle_timeout = std::chrono::milliseconds(200);
    config_.enable_auto_reconnect = false;
  }
  
  void TearDown() override {
    state_machine_.reset();
    connection_.reset();
    dispatcher_.reset();
  }
  
  void createStateMachine() {
    state_machine_ = std::make_unique<ConnectionStateMachine>(
        *dispatcher_, *connection_, config_);
  }
  
  // Helper to run event loop for a duration
  void runFor(std::chrono::milliseconds duration) {
    auto timer = dispatcher_->createTimer([this]() {
      dispatcher_->exit();
    });
    timer->enableTimer(duration);
    dispatcher_->run(event::Dispatcher::RunType::Block);
  }
  
  // Helper to capture state changes
  void captureStateChanges() {
    state_machine_->addStateChangeListener(
        [this](const StateTransitionContext& ctx) {
          state_changes_.push_back({ctx.from_state, ctx.to_state});
        });
  }
  
protected:
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<NiceMock<MockConnection>> connection_;
  ConnectionStateMachineConfig config_;
  std::unique_ptr<ConnectionStateMachine> state_machine_;
  
  // Captured state changes
  std::vector<std::pair<ConnectionMachineState, ConnectionMachineState>> state_changes_;
};

// ===== Basic State Transition Tests =====

TEST_F(ConnectionStateMachineTest, InitialState) {
  config_.mode = ConnectionMode::Client;
  createStateMachine();
  
  EXPECT_EQ(ConnectionMachineState::Uninitialized, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, InitialStateServer) {
  config_.mode = ConnectionMode::Server;
  createStateMachine();
  
  EXPECT_EQ(ConnectionMachineState::Initialized, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, BasicConnectionFlow) {
  createStateMachine();
  captureStateChanges();
  
  // Simulate connection flow
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::ConnectionRequested));
  EXPECT_EQ(ConnectionMachineState::Connecting, state_machine_->currentState());
  
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::SocketConnected));
  EXPECT_EQ(ConnectionMachineState::Connected, state_machine_->currentState());
  
  // Verify state changes
  ASSERT_EQ(3, state_changes_.size());
  EXPECT_EQ(ConnectionMachineState::Uninitialized, state_changes_[0].first);
  EXPECT_EQ(ConnectionMachineState::Connecting, state_changes_[0].second);
  EXPECT_EQ(ConnectionMachineState::Connecting, state_changes_[1].first);
  EXPECT_EQ(ConnectionMachineState::TcpConnected, state_changes_[1].second);
  EXPECT_EQ(ConnectionMachineState::TcpConnected, state_changes_[2].first);
  EXPECT_EQ(ConnectionMachineState::Connected, state_changes_[2].second);
}

TEST_F(ConnectionStateMachineTest, InvalidTransition) {
  createStateMachine();
  
  // Try invalid transition from Uninitialized to Connected
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  EXPECT_EQ(ConnectionMachineState::Connected, state_machine_->currentState());
  
  // But normal transition should be validated
  createStateMachine();  // Reset
  EXPECT_FALSE(state_machine_->handleEvent(ConnectionStateMachineEvent::HandshakeComplete));
  EXPECT_EQ(ConnectionMachineState::Uninitialized, state_machine_->currentState());
}

// ===== Timer Tests =====

TEST_F(ConnectionStateMachineTest, ConnectTimeout) {
  config_.connect_timeout = std::chrono::milliseconds(50);
  createStateMachine();
  captureStateChanges();
  
  // Start connection
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::ConnectionRequested));
  EXPECT_EQ(ConnectionMachineState::Connecting, state_machine_->currentState());
  
  // Wait for timeout
  runFor(std::chrono::milliseconds(100));
  
  // Should transition to error
  EXPECT_EQ(ConnectionMachineState::Error, state_machine_->currentState());
  
  // Verify timeout transition
  bool found_timeout = false;
  for (const auto& change : state_changes_) {
    if (change.first == ConnectionMachineState::Connecting &&
        change.second == ConnectionMachineState::Error) {
      found_timeout = true;
      break;
    }
  }
  EXPECT_TRUE(found_timeout);
}

TEST_F(ConnectionStateMachineTest, IdleTimeout) {
  config_.idle_timeout = std::chrono::milliseconds(50);
  createStateMachine();
  captureStateChanges();
  
  // Get to connected state
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Wait for idle timeout
  runFor(std::chrono::milliseconds(100));
  
  // Should transition to closing
  EXPECT_EQ(ConnectionMachineState::Closing, state_machine_->currentState());
}

// ===== I/O Event Tests =====

TEST_F(ConnectionStateMachineTest, ReadWriteTransitions) {
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Test read transition
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::ReadReady));
  EXPECT_EQ(ConnectionMachineState::Reading, state_machine_->currentState());
  
  // Back to connected
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Test write transition
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::WriteReady));
  EXPECT_EQ(ConnectionMachineState::Writing, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, EndOfStream) {
  config_.enable_half_close = true;
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // End of stream should trigger half close
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::EndOfStream));
  EXPECT_EQ(ConnectionMachineState::HalfClosedRemote, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, EndOfStreamNoHalfClose) {
  config_.enable_half_close = false;
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // End of stream should trigger full close
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::EndOfStream));
  EXPECT_EQ(ConnectionMachineState::Closing, state_machine_->currentState());
}

// ===== Flow Control Tests =====

TEST_F(ConnectionStateMachineTest, ReadDisable) {
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Disable reading
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::ReadDisableRequested));
  EXPECT_EQ(ConnectionMachineState::ReadDisabled, state_machine_->currentState());
  
  // Should not transition again if already disabled
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::ReadDisableRequested));
  EXPECT_EQ(ConnectionMachineState::ReadDisabled, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, WriteDisable) {
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Disable writing
  EXPECT_TRUE(state_machine_->handleEvent(ConnectionStateMachineEvent::WriteDisableRequested));
  EXPECT_EQ(ConnectionMachineState::WriteDisabled, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, WatermarkCallbacks) {
  createStateMachine();
  captureStateChanges();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Simulate high watermark
  state_machine_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(ConnectionMachineState::WriteDisabled, state_machine_->currentState());
  
  // Simulate low watermark
  state_machine_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(ConnectionMachineState::Connected, state_machine_->currentState());
}

// ===== Error Recovery Tests =====

TEST_F(ConnectionStateMachineTest, AutoReconnectDisabled) {
  config_.enable_auto_reconnect = false;
  createStateMachine();
  
  // Force error state
  state_machine_->forceTransition(ConnectionMachineState::Error, "test error");
  
  // Should not attempt reconnection
  runFor(std::chrono::milliseconds(100));
  EXPECT_EQ(ConnectionMachineState::Error, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, AutoReconnectEnabled) {
  config_.enable_auto_reconnect = true;
  config_.max_reconnect_attempts = 2;
  config_.initial_reconnect_delay = std::chrono::milliseconds(50);
  createStateMachine();
  captureStateChanges();
  
  // Force error state
  state_machine_->forceTransition(ConnectionMachineState::Error, "test error");
  
  // Should transition to waiting
  EXPECT_EQ(ConnectionMachineState::WaitingToReconnect, state_machine_->currentState());
  
  // Wait for reconnect timer
  runFor(std::chrono::milliseconds(100));
  
  // Should attempt reconnection
  bool found_reconnecting = false;
  for (const auto& change : state_changes_) {
    if (change.second == ConnectionMachineState::Reconnecting) {
      found_reconnecting = true;
      break;
    }
  }
  EXPECT_TRUE(found_reconnecting);
}

TEST_F(ConnectionStateMachineTest, MaxReconnectAttempts) {
  config_.enable_auto_reconnect = true;
  config_.max_reconnect_attempts = 1;
  config_.initial_reconnect_delay = std::chrono::milliseconds(10);
  createStateMachine();
  
  // First error - should attempt reconnect
  state_machine_->forceTransition(ConnectionMachineState::Error, "test error 1");
  EXPECT_EQ(ConnectionMachineState::WaitingToReconnect, state_machine_->currentState());
  
  runFor(std::chrono::milliseconds(50));
  
  // Second error - should not attempt reconnect (max attempts reached)
  state_machine_->forceTransition(ConnectionMachineState::Error, "test error 2");
  EXPECT_EQ(ConnectionMachineState::Closed, state_machine_->currentState());
}

// ===== Closing States Tests =====

TEST_F(ConnectionStateMachineTest, GracefulClose) {
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Request close with flush
  state_machine_->close(ConnectionCloseType::FlushWrite);
  EXPECT_EQ(ConnectionMachineState::Flushing, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, ImmediateClose) {
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Request immediate close
  state_machine_->close(ConnectionCloseType::NoFlush);
  EXPECT_EQ(ConnectionMachineState::Closing, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, DelayedClose) {
  config_.drain_timeout = std::chrono::milliseconds(50);
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  // Request delayed close
  state_machine_->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(ConnectionMachineState::Draining, state_machine_->currentState());
  
  // Wait for drain timeout
  runFor(std::chrono::milliseconds(100));
  EXPECT_EQ(ConnectionMachineState::Closed, state_machine_->currentState());
}

TEST_F(ConnectionStateMachineTest, Reset) {
  createStateMachine();
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test");
  
  EXPECT_CALL(*connection_, close(ConnectionCloseType::NoFlush));
  
  // Reset should force abort
  state_machine_->reset();
  EXPECT_EQ(ConnectionMachineState::Aborted, state_machine_->currentState());
}

// ===== State History Tests =====

TEST_F(ConnectionStateMachineTest, StateHistory) {
  createStateMachine();
  
  // Make some transitions
  state_machine_->forceTransition(ConnectionMachineState::Connecting, "test1");
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test2");
  state_machine_->forceTransition(ConnectionMachineState::Closing, "test3");
  state_machine_->forceTransition(ConnectionMachineState::Closed, "test4");
  
  // Check history
  const auto& history = state_machine_->getStateHistory();
  ASSERT_GE(history.size(), 4);
  
  // Verify transitions are recorded
  bool found_connecting = false;
  bool found_connected = false;
  bool found_closing = false;
  bool found_closed = false;
  
  for (const auto& entry : history) {
    if (entry.to_state == ConnectionMachineState::Connecting) found_connecting = true;
    if (entry.to_state == ConnectionMachineState::Connected) found_connected = true;
    if (entry.to_state == ConnectionMachineState::Closing) found_closing = true;
    if (entry.to_state == ConnectionMachineState::Closed) found_closed = true;
  }
  
  EXPECT_TRUE(found_connecting);
  EXPECT_TRUE(found_connected);
  EXPECT_TRUE(found_closing);
  EXPECT_TRUE(found_closed);
}

// ===== Metrics Tests =====

TEST_F(ConnectionStateMachineTest, TransitionMetrics) {
  createStateMachine();
  
  EXPECT_EQ(0, state_machine_->getTotalTransitions());
  
  state_machine_->forceTransition(ConnectionMachineState::Connecting, "test1");
  EXPECT_EQ(1, state_machine_->getTotalTransitions());
  
  state_machine_->forceTransition(ConnectionMachineState::Connected, "test2");
  EXPECT_EQ(2, state_machine_->getTotalTransitions());
}

TEST_F(ConnectionStateMachineTest, TimeInState) {
  createStateMachine();
  
  auto initial_time = state_machine_->getTimeInCurrentState();
  EXPECT_GE(initial_time.count(), 0);
  
  // Wait a bit
  runFor(std::chrono::milliseconds(50));
  
  auto later_time = state_machine_->getTimeInCurrentState();
  EXPECT_GT(later_time.count(), initial_time.count());
}

// ===== State Pattern Helper Tests =====

TEST_F(ConnectionStateMachineTest, StatePatternHelpers) {
  // Test canRead
  EXPECT_TRUE(ConnectionStatePatterns::canRead(ConnectionMachineState::Connected));
  EXPECT_TRUE(ConnectionStatePatterns::canRead(ConnectionMachineState::Reading));
  EXPECT_FALSE(ConnectionStatePatterns::canRead(ConnectionMachineState::Closed));
  
  // Test canWrite
  EXPECT_TRUE(ConnectionStatePatterns::canWrite(ConnectionMachineState::Connected));
  EXPECT_TRUE(ConnectionStatePatterns::canWrite(ConnectionMachineState::Writing));
  EXPECT_FALSE(ConnectionStatePatterns::canWrite(ConnectionMachineState::Closed));
  
  // Test isTerminal
  EXPECT_TRUE(ConnectionStatePatterns::isTerminal(ConnectionMachineState::Closed));
  EXPECT_TRUE(ConnectionStatePatterns::isTerminal(ConnectionMachineState::Error));
  EXPECT_TRUE(ConnectionStatePatterns::isTerminal(ConnectionMachineState::Aborted));
  EXPECT_FALSE(ConnectionStatePatterns::isTerminal(ConnectionMachineState::Connected));
  
  // Test isConnecting
  EXPECT_TRUE(ConnectionStatePatterns::isConnecting(ConnectionMachineState::Connecting));
  EXPECT_TRUE(ConnectionStatePatterns::isConnecting(ConnectionMachineState::TcpConnected));
  EXPECT_FALSE(ConnectionStatePatterns::isConnecting(ConnectionMachineState::Connected));
  
  // Test isConnected
  EXPECT_TRUE(ConnectionStatePatterns::isConnected(ConnectionMachineState::Connected));
  EXPECT_TRUE(ConnectionStatePatterns::isConnected(ConnectionMachineState::Reading));
  EXPECT_FALSE(ConnectionStatePatterns::isConnected(ConnectionMachineState::Connecting));
  
  // Test isClosing
  EXPECT_TRUE(ConnectionStatePatterns::isClosing(ConnectionMachineState::Closing));
  EXPECT_TRUE(ConnectionStatePatterns::isClosing(ConnectionMachineState::Draining));
  EXPECT_FALSE(ConnectionStatePatterns::isClosing(ConnectionMachineState::Connected));
  
  // Test canReconnect
  EXPECT_TRUE(ConnectionStatePatterns::canReconnect(ConnectionMachineState::Error));
  EXPECT_TRUE(ConnectionStatePatterns::canReconnect(ConnectionMachineState::Closed));
  EXPECT_FALSE(ConnectionStatePatterns::canReconnect(ConnectionMachineState::Connected));
}

// ===== Builder Pattern Tests =====

TEST_F(ConnectionStateMachineTest, StateMachineBuilder) {
  bool callback_called = false;
  
  auto machine = ConnectionStateMachineBuilder()
      .withMode(ConnectionMode::Client)
      .withConnectTimeout(std::chrono::milliseconds(5000))
      .withAutoReconnect(true, 5)
      .withBufferLimits(1024, 2048)
      .withWatermarks(512, 256)
      .withStateChangeCallback([&callback_called](const StateTransitionContext& ctx) {
        callback_called = true;
      })
      .build(*dispatcher_, *connection_);
  
  EXPECT_EQ(ConnectionMachineState::Uninitialized, machine->currentState());
  
  // Force a transition to trigger callback
  machine->forceTransition(ConnectionMachineState::Connecting, "test");
  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace network
}  // namespace mcp