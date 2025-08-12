/**
 * @file test_ssl_transport_socket.cc
 * @brief Unit tests for SSL transport socket and state machine
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <thread>
#include <chrono>

#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"

namespace mcp {
namespace transport {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AtLeast;
using ::testing::NiceMock;

/**
 * Mock transport socket for testing SSL wrapper
 */
class MockTransportSocket : public network::TransportSocket {
public:
  MOCK_METHOD(void, setTransportSocketCallbacks,
              (network::TransportSocketCallbacks& callbacks), (override));
  MOCK_METHOD(std::string, protocol, (), (const, override));
  MOCK_METHOD(std::string, failureReason, (), (const, override));
  MOCK_METHOD(bool, canFlushClose, (), (override));
  MOCK_METHOD(VoidResult, connect, (network::Socket& socket), (override));
  MOCK_METHOD(void, closeSocket, (network::ConnectionEvent event), (override));
  MOCK_METHOD(TransportIoResult, doRead, (Buffer& buffer), (override));
  MOCK_METHOD(TransportIoResult, doWrite, (Buffer& buffer, bool end_stream), (override));
  MOCK_METHOD(void, onConnected, (), (override));
};

/**
 * Mock transport socket callbacks
 */
class MockTransportSocketCallbacks : public network::TransportSocketCallbacks {
public:
  MOCK_METHOD(void, onConnected, (), (override));
  MOCK_METHOD(void, onData, (Buffer& buffer), (override));
  MOCK_METHOD(void, onEvent, (network::ConnectionEvent event), (override));
  MOCK_METHOD(void, raiseEvent, (network::ConnectionEvent event), (override));
  MOCK_METHOD(void, flushWriteBuffer, (), (override));
  MOCK_METHOD(void, setTransportSocketIsReadable, (), (override));
};

/**
 * Mock SSL handshake callbacks
 */
class MockSslHandshakeCallbacks : public SslHandshakeCallbacks {
public:
  MOCK_METHOD(void, onSslHandshakeComplete, (), (override));
  MOCK_METHOD(void, onSslHandshakeFailed, (const std::string& reason), (override));
};

/**
 * Mock network socket
 */
class MockSocket : public network::Socket {
public:
  MOCK_METHOD(int, fd, (), (const, override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(VoidResult, bind, (const network::Address& address), (override));
  MOCK_METHOD(VoidResult, listen, (int backlog), (override));
  MOCK_METHOD(VoidResult, connect, (const network::Address& address), (override));
  MOCK_METHOD(network::IoResult, read, (Buffer& buffer, size_t max_length), (override));
  MOCK_METHOD(network::IoResult, write, (const Buffer& buffer), (override));
  MOCK_METHOD(VoidResult, setSocketOption, (int level, int optname, const void* optval, socklen_t optlen), (override));
  MOCK_METHOD(VoidResult, getSocketOption, (int level, int optname, void* optval, socklen_t* optlen), (const, override));
};

/**
 * Test fixture for SSL transport socket tests
 */
class SslTransportSocketTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher for async operations
    dispatcher_ = std::make_unique<event::LibeventDispatcher>();
    
    // Create SSL context for testing
    SslContextConfig ssl_config;
    ssl_config.is_client = true;
    ssl_config.verify_peer = false;  // Disable verification for unit tests
    
    auto context_result = SslContext::create(ssl_config);
    ASSERT_TRUE(context_result.ok());
    ssl_context_ = context_result.value();
    
    // Create mock inner socket
    auto mock_socket = std::make_unique<NiceMock<MockTransportSocket>>();
    mock_inner_socket_ = mock_socket.get();
    
    // Create SSL transport socket
    ssl_socket_ = std::make_unique<SslTransportSocket>(
        std::move(mock_socket),
        ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    // Set up callbacks
    ssl_socket_->setTransportSocketCallbacks(mock_callbacks_);
    ssl_socket_->setHandshakeCallbacks(&mock_handshake_callbacks_);
  }
  
  void TearDown() override {
    ssl_socket_.reset();
    dispatcher_.reset();
  }

protected:
  std::unique_ptr<event::Dispatcher> dispatcher_;
  SslContextSharedPtr ssl_context_;
  std::unique_ptr<SslTransportSocket> ssl_socket_;
  MockTransportSocket* mock_inner_socket_;  // Non-owning pointer
  NiceMock<MockTransportSocketCallbacks> mock_callbacks_;
  NiceMock<MockSslHandshakeCallbacks> mock_handshake_callbacks_;
};

/**
 * Test initial state
 */
TEST_F(SslTransportSocketTest, InitialState) {
  EXPECT_EQ(ssl_socket_->getState(), SslState::Initial);
  EXPECT_FALSE(ssl_socket_->isSecure());
  EXPECT_TRUE(ssl_socket_->failureReason().empty());
}

/**
 * Test state transition: Initial -> Connecting
 */
TEST_F(SslTransportSocketTest, StateTransitionToConnecting) {
  MockSocket mock_socket;
  
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  
  auto result = ssl_socket_->connect(mock_socket);
  ASSERT_TRUE(result.ok());
  
  EXPECT_EQ(ssl_socket_->getState(), SslState::Connecting);
}

/**
 * Test state transition: Connecting -> Handshaking
 */
TEST_F(SslTransportSocketTest, StateTransitionToHandshaking) {
  MockSocket mock_socket;
  
  // Connect first
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  ssl_socket_->connect(mock_socket);
  
  // Trigger onConnected to start handshake
  ssl_socket_->onConnected();
  
  // Should transition to handshaking
  // Note: Actual state may vary based on async operations
  auto state = ssl_socket_->getState();
  EXPECT_TRUE(state == SslState::Handshaking ||
              state == SslState::WantRead ||
              state == SslState::WantWrite);
}

/**
 * Test invalid state transitions
 */
TEST_F(SslTransportSocketTest, InvalidStateTransitions) {
  // Cannot connect twice
  MockSocket mock_socket;
  
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  
  auto result1 = ssl_socket_->connect(mock_socket);
  ASSERT_TRUE(result1.ok());
  
  // Second connect should fail
  auto result2 = ssl_socket_->connect(mock_socket);
  EXPECT_FALSE(result2.ok());
}

/**
 * Test protocol reporting
 */
TEST_F(SslTransportSocketTest, ProtocolReporting) {
  // Before handshake, should return "ssl"
  EXPECT_EQ(ssl_socket_->protocol(), "ssl");
  
  // After handshake would return TLS version or negotiated protocol
  // Cannot test without actual handshake
}

/**
 * Test canFlushClose behavior
 */
TEST_F(SslTransportSocketTest, CanFlushClose) {
  // Can flush close in initial state
  EXPECT_TRUE(ssl_socket_->canFlushClose());
  
  // Cannot flush close during handshake
  MockSocket mock_socket;
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  ssl_socket_->connect(mock_socket);
  ssl_socket_->onConnected();
  
  EXPECT_FALSE(ssl_socket_->canFlushClose());
}

/**
 * Test close socket behavior
 */
TEST_F(SslTransportSocketTest, CloseSocket) {
  EXPECT_CALL(*mock_inner_socket_, closeSocket(_))
      .Times(1);
  
  ssl_socket_->closeSocket(network::ConnectionEvent::LocalClose);
  
  EXPECT_EQ(ssl_socket_->getState(), SslState::Closed);
}

/**
 * Test handshake callback notifications
 */
TEST_F(SslTransportSocketTest, HandshakeCallbacks) {
  MockSocket mock_socket;
  
  // Set up for handshake failure
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  
  // Expect handshake failure callback
  // Note: In real test, would need to trigger actual SSL handshake failure
  EXPECT_CALL(mock_handshake_callbacks_, onSslHandshakeFailed(_))
      .Times(AtLeast(0));  // May or may not be called in unit test
  
  ssl_socket_->connect(mock_socket);
}

/**
 * Test read operations before handshake complete
 */
TEST_F(SslTransportSocketTest, ReadBeforeHandshake) {
  Buffer buffer;
  
  // Read before handshake should fail
  auto result = ssl_socket_->doRead(buffer);
  
  EXPECT_EQ(result.action_, network::PostIoAction::Close);
  EXPECT_EQ(result.bytes_processed_, 0);
}

/**
 * Test write operations before handshake complete
 */
TEST_F(SslTransportSocketTest, WriteBeforeHandshake) {
  Buffer buffer;
  buffer.add("test data");
  
  // Connect first
  MockSocket mock_socket;
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  ssl_socket_->connect(mock_socket);
  ssl_socket_->onConnected();
  
  // Write during handshake should buffer data
  auto result = ssl_socket_->doWrite(buffer, false);
  
  // Should keep connection open and buffer data
  EXPECT_EQ(result.action_, network::PostIoAction::KeepOpen);
}

/**
 * Test setting handshake callbacks
 */
TEST_F(SslTransportSocketTest, SetHandshakeCallbacks) {
  MockSslHandshakeCallbacks callbacks;
  ssl_socket_->setHandshakeCallbacks(&callbacks);
  
  // Callbacks are set, would be invoked during handshake
  // Cannot test actual invocation without real SSL handshake
}

/**
 * Test getting peer certificate info
 */
TEST_F(SslTransportSocketTest, GetPeerCertificateInfo) {
  // Before handshake, should return empty
  EXPECT_TRUE(ssl_socket_->getPeerCertificateInfo().empty());
  
  // After handshake would return peer cert info
  // Cannot test without actual handshake
}

/**
 * Test getting negotiated protocol
 */
TEST_F(SslTransportSocketTest, GetNegotiatedProtocol) {
  // Before handshake, should return empty
  EXPECT_TRUE(ssl_socket_->getNegotiatedProtocol().empty());
  
  // After handshake would return ALPN protocol
  // Cannot test without actual handshake
}

/**
 * Test server role initialization
 */
TEST_F(SslTransportSocketTest, ServerRoleInitialization) {
  // Create server context
  SslContextConfig ssl_config;
  ssl_config.is_client = false;
  ssl_config.verify_peer = false;
  
  auto context_result = SslContext::create(ssl_config);
  ASSERT_TRUE(context_result.ok());
  
  // Create server SSL socket
  auto mock_socket = std::make_unique<NiceMock<MockTransportSocket>>();
  
  SslTransportSocket server_socket(
      std::move(mock_socket),
      context_result.value(),
      SslTransportSocket::InitialRole::Server,
      *dispatcher_);
  
  EXPECT_EQ(server_socket.getState(), SslState::Initial);
}

/**
 * Test state machine with all transitions
 */
TEST_F(SslTransportSocketTest, CompleteStateMachine) {
  // Track all state transitions
  std::vector<SslState> states;
  
  // Initial state
  states.push_back(ssl_socket_->getState());
  EXPECT_EQ(states.back(), SslState::Initial);
  
  // Connect -> Connecting
  MockSocket mock_socket;
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  ssl_socket_->connect(mock_socket);
  states.push_back(ssl_socket_->getState());
  EXPECT_EQ(states.back(), SslState::Connecting);
  
  // Would continue with handshake states in integration test
}

/**
 * Test buffering during handshake
 */
TEST_F(SslTransportSocketTest, BufferingDuringHandshake) {
  // Connect and start handshake
  MockSocket mock_socket;
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(VoidResult::success()));
  ssl_socket_->connect(mock_socket);
  ssl_socket_->onConnected();
  
  // Write data during handshake
  Buffer write_buffer;
  write_buffer.add("buffered data");
  
  auto result = ssl_socket_->doWrite(write_buffer, false);
  
  // Should accept data for buffering
  EXPECT_EQ(result.action_, network::PostIoAction::KeepOpen);
  EXPECT_EQ(write_buffer.length(), 0);  // Data consumed for buffering
}

/**
 * Test error handling during connect
 */
TEST_F(SslTransportSocketTest, ConnectError) {
  MockSocket mock_socket;
  
  EXPECT_CALL(*mock_inner_socket_, connect(_))
      .WillOnce(Return(makeError("Connection failed")));
  
  auto result = ssl_socket_->connect(mock_socket);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ssl_socket_->getState(), SslState::Error);
  EXPECT_FALSE(ssl_socket_->failureReason().empty());
}

/**
 * Test multiple close calls
 */
TEST_F(SslTransportSocketTest, MultipleCloseCalls) {
  EXPECT_CALL(*mock_inner_socket_, closeSocket(_))
      .Times(1);  // Should only be called once
  
  ssl_socket_->closeSocket(network::ConnectionEvent::LocalClose);
  ssl_socket_->closeSocket(network::ConnectionEvent::LocalClose);
  ssl_socket_->closeSocket(network::ConnectionEvent::LocalClose);
  
  EXPECT_EQ(ssl_socket_->getState(), SslState::Closed);
}

}  // namespace
}  // namespace transport
}  // namespace mcp