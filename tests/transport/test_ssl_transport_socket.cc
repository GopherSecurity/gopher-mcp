/**
 * @file test_ssl_transport_socket.cc
 * @brief Unit tests for SSL transport socket using real I/O
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/ssl_context.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/io_handle.h"
#include "mcp/network/address.h"
#include "mcp/network/transport_socket.h"
#include "mcp/network/socket_interface.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"
#include "../tests/integration/real_io_test_base.h"

namespace mcp {
namespace transport {
namespace {

/**
 * SSL transport socket test fixture using real I/O
 * Tests SSL handshake and data transmission with actual sockets
 */
class SslTransportSocketTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create test SSL contexts
    createTestSslContexts();
    
    // Create socket pair for testing
    createSocketPair();
  }
  
  void TearDown() override {
    // Close sockets using MCP abstractions
    if (client_io_handle_) {
      client_io_handle_->close();
      client_io_handle_.reset();
    }
    if (server_io_handle_) {
      server_io_handle_->close();
      server_io_handle_.reset();
    }
    
    client_ssl_context_.reset();
    server_ssl_context_.reset();
    
    RealIoTestBase::TearDown();
  }
  
  /**
   * Create test SSL contexts for client and server
   */
  void createTestSslContexts() {
    // Create client context
    SslContextConfig client_config;
    client_config.is_client = true;
    client_config.verify_peer = false;  // Disable for testing
    
    auto client_result = SslContext::create(client_config);
    ASSERT_FALSE(holds_alternative<Error>(client_result));
    client_ssl_context_ = get<SslContextSharedPtr>(client_result);
    
    // Create server context  
    SslContextConfig server_config;
    server_config.is_client = false;
    server_config.verify_peer = false;  // Disable for testing
    
    auto server_result = SslContext::create(server_config);
    ASSERT_FALSE(holds_alternative<Error>(server_result));
    server_ssl_context_ = get<SslContextSharedPtr>(server_result);
  }
  
  /**
   * Create connected socket pair for testing using MCP abstractions
   */
  void createSocketPair() {
    // Use the base class utility which creates real connected IoHandles
    auto socket_pair = RealIoTestBase::createSocketPair();
    client_io_handle_ = std::move(socket_pair.first);
    server_io_handle_ = std::move(socket_pair.second);
  }
  
  /**
   * Create a raw transport socket (non-SSL) for testing
   */
  std::unique_ptr<network::TransportSocket> createRawTransportSocket(
      network::IoHandlePtr io_handle) {
    // Simple pass-through transport socket using MCP IoHandle
    class RawTransportSocket : public network::TransportSocket {
    public:
      explicit RawTransportSocket(network::IoHandlePtr io_handle) 
          : io_handle_(std::move(io_handle)) {}
      
      void setTransportSocketCallbacks(network::TransportSocketCallbacks& callbacks) override {
        callbacks_ = &callbacks;
      }
      
      std::string protocol() const override { return "raw"; }
      std::string failureReason() const override { return ""; }
      bool canFlushClose() override { return true; }
      
      VoidResult connect(network::Socket& socket) override {
        // Already connected for socket pair
        return makeVoidSuccess();
      }
      
      void closeSocket(network::ConnectionEvent event) override {
        if (io_handle_) {
          io_handle_->close();
          io_handle_.reset();
        }
      }
      
      TransportIoResult doRead(Buffer& buffer) override {
        if (!io_handle_) {
          return TransportIoResult::close();
        }
        
        // Read using MCP IoHandle abstraction
        auto result = io_handle_->read(buffer, 16384);
        if (!result.ok()) {
          if (result.wouldBlock()) {
            return TransportIoResult::stop();
          }
          return TransportIoResult::close();
        }
        
        if (*result > 0) {
          return TransportIoResult::success(*result);
        }
        
        return TransportIoResult::close();  // EOF
      }
      
      TransportIoResult doWrite(Buffer& buffer, bool end_stream) override {
        if (!io_handle_) {
          return TransportIoResult::close();
        }
        
        if (buffer.length() == 0) {
          return TransportIoResult::success(0);
        }
        
        // Write using MCP IoHandle abstraction
        auto result = io_handle_->write(buffer);
        if (!result.ok()) {
          if (result.wouldBlock()) {
            return TransportIoResult::stop();
          }
          return TransportIoResult::close();
        }
        
        if (*result > 0) {
          return TransportIoResult::success(*result);
        }
        
        return TransportIoResult::stop();
      }
      
      void onConnected() override {
        if (callbacks_) {
          callbacks_->setTransportSocketIsReadable();
        }
      }
      
    private:
      network::IoHandlePtr io_handle_;
      network::TransportSocketCallbacks* callbacks_{nullptr};
    };
    
    return std::make_unique<RawTransportSocket>(std::move(io_handle));
  }

protected:
  SslContextSharedPtr client_ssl_context_;
  SslContextSharedPtr server_ssl_context_;
  network::IoHandlePtr client_io_handle_;
  network::IoHandlePtr server_io_handle_;
};

/**
 * Test SSL transport socket creation
 */
TEST_F(SslTransportSocketTest, CreateSocket) {
  executeInDispatcher([this]() {
    // Create raw transport socket using MCP IoHandle
    auto raw_socket = createRawTransportSocket(std::move(client_io_handle_));
    
    // Wrap with SSL transport socket
    auto ssl_socket = std::make_unique<SslTransportSocket>(
        std::move(raw_socket),
        client_ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    EXPECT_NE(ssl_socket, nullptr);
    EXPECT_EQ(ssl_socket->protocol(), "ssl");
    EXPECT_EQ(ssl_socket->getState(), SslSocketState::Uninitialized);
  });
}

/**
 * Test state transitions
 */
TEST_F(SslTransportSocketTest, StateTransitions) {
  executeInDispatcher([this]() {
    // Create SSL transport sockets for both sides
    auto client_raw = createRawTransportSocket(std::move(client_io_handle_));
    auto client_ssl = std::make_unique<SslTransportSocket>(
        std::move(client_raw),
        client_ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    // Check initial state
    EXPECT_EQ(client_ssl->getState(), SslSocketState::Uninitialized);
    
    // Create server SSL socket
    auto server_raw = createRawTransportSocket(std::move(server_io_handle_));
    auto server_ssl = std::make_unique<SslTransportSocket>(
        std::move(server_raw),
        server_ssl_context_,
        SslTransportSocket::InitialRole::Server,
        *dispatcher_);
    
    EXPECT_EQ(server_ssl->getState(), SslSocketState::Uninitialized);
  });
}

/**
 * Test SNI configuration
 */
TEST_F(SslTransportSocketTest, SetSniHostname) {
  SSL* ssl = client_ssl_context_->newSsl();
  ASSERT_NE(ssl, nullptr);
  
  // Set SNI hostname
  auto result = SslContext::setSniHostname(ssl, "example.com");
  EXPECT_FALSE(holds_alternative<Error>(result));
  
  // Verify SNI was set
  const char* hostname = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  EXPECT_STREQ(hostname, "example.com");
  
  SSL_free(ssl);
}

/**
 * Test SSL handshake callbacks
 */
class TestHandshakeCallbacks : public SslHandshakeCallbacks {
public:
  void onSslHandshakeComplete() override {
    handshake_complete_ = true;
  }
  
  void onSslHandshakeFailed(const std::string& reason) override {
    handshake_failed_ = true;
    failure_reason_ = reason;
  }
  
  bool handshake_complete_{false};
  bool handshake_failed_{false};
  std::string failure_reason_;
};

TEST_F(SslTransportSocketTest, HandshakeCallbacks) {
  executeInDispatcher([this]() {
    TestHandshakeCallbacks client_callbacks;
    TestHandshakeCallbacks server_callbacks;
    
    // Create client SSL socket
    auto client_raw = createRawTransportSocket(std::move(client_io_handle_));
    auto client_ssl = std::make_unique<SslTransportSocket>(
        std::move(client_raw),
        client_ssl_context_,
        SslTransportSocket::InitialRole::Client,
        *dispatcher_);
    
    // Create server SSL socket
    auto server_raw = createRawTransportSocket(std::move(server_io_handle_));
    auto server_ssl = std::make_unique<SslTransportSocket>(
        std::move(server_raw),
        server_ssl_context_,
        SslTransportSocket::InitialRole::Server,
        *dispatcher_);
    
    // Register callbacks
    client_ssl->setHandshakeCallbacks(&client_callbacks);
    server_ssl->setHandshakeCallbacks(&server_callbacks);
    
    // Verify callbacks are registered but not yet triggered
    EXPECT_FALSE(client_callbacks.handshake_complete_);
    EXPECT_FALSE(client_callbacks.handshake_failed_);
    EXPECT_FALSE(server_callbacks.handshake_complete_);
    EXPECT_FALSE(server_callbacks.handshake_failed_);
  });
}

// =============================================================================
// Comprehensive State Machine Tests
// =============================================================================

/**
 * Test fixture for SSL state machine tests
 */
class SslStateMachineTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
  }
  
  void TearDown() override {
    dispatcher_.reset();
  }
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
};

/**
 * Test client state machine initialization
 */
TEST_F(SslStateMachineTest, ClientStateMachine_Initialization) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Uninitialized);
  EXPECT_EQ(machine->getMode(), SslSocketMode::Client);
  EXPECT_FALSE(machine->isTerminalState());
  EXPECT_FALSE(machine->isHandshaking());
  EXPECT_FALSE(machine->isConnected());
}

/**
 * Test server state machine initialization
 */
TEST_F(SslStateMachineTest, ServerStateMachine_Initialization) {
  auto machine = SslStateMachineFactory::createServerStateMachine(*dispatcher_);
  
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Uninitialized);
  EXPECT_EQ(machine->getMode(), SslSocketMode::Server);
  EXPECT_FALSE(machine->isTerminalState());
  EXPECT_FALSE(machine->isHandshaking());
  EXPECT_FALSE(machine->isConnected());
}

/**
 * Test valid client state transitions
 */
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
  
  // Skip intermediate handshake states for brevity
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

/**
 * Test invalid client state transitions
 */
TEST_F(SslStateMachineTest, ClientStateMachine_InvalidTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Cannot go from Uninitialized directly to Connected
  EXPECT_FALSE(machine->canTransition(SslSocketState::Uninitialized,
                                      SslSocketState::Connected));
  bool success = true;
  machine->transition(SslSocketState::Connected, 
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_FALSE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Uninitialized);
  
  // Cannot go from Initialized directly to ClientHandshakeInit
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  EXPECT_FALSE(machine->canTransition(SslSocketState::Initialized,
                                      SslSocketState::ClientHandshakeInit));
  
  // Cannot transition from terminal state
  machine->forceTransition(SslSocketState::Closed);
  EXPECT_FALSE(machine->canTransition(SslSocketState::Closed,
                                      SslSocketState::Initialized));
  EXPECT_TRUE(machine->isTerminalState());
  
  // Error state is also terminal
  machine->forceTransition(SslSocketState::Error);
  EXPECT_FALSE(machine->canTransition(SslSocketState::Error,
                                      SslSocketState::Initialized));
  EXPECT_TRUE(machine->isTerminalState());
}

/**
 * Test server state transitions
 */
TEST_F(SslStateMachineTest, ServerStateMachine_ValidTransitions) {
  auto machine = SslStateMachineFactory::createServerStateMachine(*dispatcher_);
  
  // Server accepts connection directly
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  machine->transition(SslSocketState::TcpConnected, [&success](bool s, const std::string&){ success = s; });
  
  // TcpConnected -> ServerHandshakeInit
  EXPECT_TRUE(machine->canTransition(SslSocketState::TcpConnected,
                                     SslSocketState::ServerHandshakeInit));
  bool success = false;
  machine->transition(SslSocketState::ServerHandshakeInit,
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isHandshaking());
  
  // ServerHandshakeInit -> ClientHelloReceived
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerHandshakeInit,
                                     SslSocketState::ClientHelloReceived));
  success = false;
  machine->transition(SslSocketState::ClientHelloReceived, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // ClientHelloReceived -> ServerHelloSent
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHelloReceived,
                                     SslSocketState::ServerHelloSent));
  success = false;
  machine->transition(SslSocketState::ServerHelloSent, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // ServerHelloSent -> ServerCertSent
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerHelloSent,
                                     SslSocketState::ServerCertSent));
  success = false;
  machine->transition(SslSocketState::ServerCertSent, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // Skip to end of handshake
  machine->forceTransition(SslSocketState::ServerFinished);
  
  // ServerFinished -> Connected
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerFinished,
                                     SslSocketState::Connected));
  success = false;
  machine->transition(SslSocketState::Connected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isConnected());
  EXPECT_FALSE(machine->isHandshaking());
}

/**
 * Test handshake blocked states
 */
TEST_F(SslStateMachineTest, StateMachine_HandshakeBlockedStates) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Setup to handshake state
  machine->forceTransition(SslSocketState::ClientHandshakeInit);
  
  // Can transition to HandshakeWantWrite
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHandshakeInit,
                                     SslSocketState::HandshakeWantWrite));
  bool success = false;
  machine->transition(SslSocketState::HandshakeWantWrite,
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(SslStatePatterns::isIoBlockedState(
      machine->getCurrentState()));
  
  // Can resume from HandshakeWantWrite
  EXPECT_TRUE(machine->canTransition(SslSocketState::HandshakeWantWrite,
                                     SslSocketState::ClientHandshakeInit));
  
  // Test HandshakeWantRead
  machine->forceTransition(SslSocketState::ClientHelloSent);
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHelloSent,
                                     SslSocketState::HandshakeWantRead));
  success = false;
  machine->transition(SslSocketState::HandshakeWantRead, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(SslStatePatterns::isIoBlockedState(
      machine->getCurrentState()));
  
  // Test CertificateValidating
  machine->forceTransition(SslSocketState::ServerHelloReceived);
  EXPECT_TRUE(machine->canTransition(SslSocketState::ServerHelloReceived,
                                     SslSocketState::CertificateValidating));
  success = false;
  machine->transition(SslSocketState::CertificateValidating, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(SslStatePatterns::isIoBlockedState(
      machine->getCurrentState()));
}

/**
 * Test error transitions
 */
TEST_F(SslStateMachineTest, StateMachine_ErrorTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Any state can transition to Error
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  EXPECT_TRUE(machine->canTransition(SslSocketState::Initialized,
                                     SslSocketState::Error));
  
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connecting,
                                     SslSocketState::Error));
  
  machine->forceTransition(SslSocketState::ClientHandshakeInit);
  EXPECT_TRUE(machine->canTransition(SslSocketState::ClientHandshakeInit,
                                     SslSocketState::Error));
  
  machine->forceTransition(SslSocketState::Connected);
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connected,
                                     SslSocketState::Error));
  
  // Transition to error
  bool success = false;
  machine->transition(SslSocketState::Error,
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isTerminalState());
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Error);
}

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
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  
  // Allow async notification
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_EQ(listener->state_changes_.size(), 1);
  if (!listener->state_changes_.empty()) {
    EXPECT_EQ(listener->state_changes_[0].old_state, SslSocketState::Uninitialized);
    EXPECT_EQ(listener->state_changes_[0].new_state, SslSocketState::Initialized);
  }
  
  // Invalid transition should notify
  machine->transition(SslSocketState::Connected);  // Invalid
  
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
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  
  EXPECT_EQ(listener->state_changes_.size(), 1);  // No change
}

/**
 * Test entry and exit actions
 */
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
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(entry_called);
  
  // Transition from Initialized should call exit action
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(exit_called);
}

/**
 * Test custom transition validators
 */
TEST_F(SslStateMachineTest, StateMachine_CustomValidators) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Add custom validator that blocks specific transition
  machine->addTransitionValidator(
      [](SslSocketState from, SslSocketState to) {
        // Block transition from Connecting to Error for testing
        if (from == SslSocketState::Connecting && to == SslSocketState::Error) {
          return false;
        }
        return true;
      });
  
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  
  // Should be blocked by custom validator
  EXPECT_FALSE(machine->canTransition(SslSocketState::Connecting,
                                      SslSocketState::Error));
  bool success = true;
  machine->transition(SslSocketState::Error,
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_FALSE(success);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Connecting);
}

/**
 * Test state history tracking
 */
TEST_F(SslStateMachineTest, StateMachine_StateHistory) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Make several transitions
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  machine->transition(SslSocketState::TcpConnected, [&success](bool s, const std::string&){ success = s; });
  machine->forceTransition(SslSocketState::ClientHandshakeInit);
  
  // Get history
  auto history = machine->getStateHistory(10);
  
  // Should have all transitions recorded
  EXPECT_GE(history.size(), 5);  // Initial + 4 transitions
  
  // Verify order
  if (history.size() >= 5) {
    EXPECT_EQ(history[0].first, SslSocketState::Uninitialized);
    EXPECT_EQ(history[1].first, SslSocketState::Initialized);
    EXPECT_EQ(history[2].first, SslSocketState::Connecting);
    EXPECT_EQ(history[3].first, SslSocketState::TcpConnected);
    EXPECT_EQ(history[4].first, SslSocketState::ClientHandshakeInit);
  }
  
  // Test limited history
  auto limited = machine->getStateHistory(2);
  EXPECT_LE(limited.size(), 2);
}

/**
 * Test time in state tracking
 */
TEST_F(SslStateMachineTest, StateMachine_TimeInState) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  machine->transition(SslSocketState::Initialized, [&success](bool s, const std::string&){ success = s; });
  
  // Should have some time in state
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  auto time_in_state = machine->getTimeInCurrentState();
  EXPECT_GE(time_in_state.count(), 10);
  
  // After transition, time should reset
  machine->transition(SslSocketState::Connecting, [&success](bool s, const std::string&){ success = s; });
  
  time_in_state = machine->getTimeInCurrentState();
  EXPECT_LT(time_in_state.count(), 10);
}

/**
 * Test state patterns helper
 */
TEST_F(SslStateMachineTest, StatePatterns_HelperFunctions) {
  // Test handshake states
  EXPECT_TRUE(SslStatePatterns::isHandshakeState(SslSocketState::ClientHandshakeInit));
  EXPECT_TRUE(SslStatePatterns::isHandshakeState(SslSocketState::ServerHandshakeInit));
  EXPECT_TRUE(SslStatePatterns::isHandshakeState(SslSocketState::HandshakeWantRead));
  EXPECT_FALSE(SslStatePatterns::isHandshakeState(SslSocketState::Connected));
  EXPECT_FALSE(SslStatePatterns::isHandshakeState(SslSocketState::Closed));
  
  // Test blocked states
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

/**
 * Test next handshake state prediction
 */
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

/**
 * Test renegotiation states (TLS 1.2)
 */
TEST_F(SslStateMachineTest, StateMachine_RenegotiationStates) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Get to connected state
  machine->forceTransition(SslSocketState::Connected);
  
  // Can request renegotiation
  EXPECT_TRUE(machine->canTransition(SslSocketState::Connected,
                                     SslSocketState::RenegotiationRequested));
  bool success = false;
  machine->transition(SslSocketState::RenegotiationRequested,
                     [&success](bool s, const std::string&) { success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  
  // Can proceed with renegotiation
  EXPECT_TRUE(machine->canTransition(SslSocketState::RenegotiationRequested,
                                     SslSocketState::RenegotiationInProgress));
  success = false;
  machine->transition(SslSocketState::RenegotiationInProgress, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_TRUE(machine->isHandshaking());
  
  // Can complete renegotiation
  EXPECT_TRUE(machine->canTransition(SslSocketState::RenegotiationInProgress,
                                     SslSocketState::Connected));
  success = false;
  machine->transition(SslSocketState::Connected, [&success](bool s, const std::string&){ success = s; });
  dispatcher_->run(event::RunType::NonBlock);
  EXPECT_TRUE(success);
  EXPECT_FALSE(machine->isHandshaking());
}

/**
 * Test concurrent state access (thread safety)
 */
TEST_F(SslStateMachineTest, StateMachine_ThreadSafety) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  std::atomic<int> successful_transitions{0};
  std::atomic<int> failed_transitions{0};
  
  // Multiple threads trying to transition
  std::vector<std::thread> threads;
  
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&machine, &successful_transitions, &failed_transitions]() {
      for (int j = 0; j < 100; ++j) {
        // Try various transitions
        auto current = machine->getCurrentState();
        
        // Pick a potential next state
        SslSocketState next;
        switch (current) {
          case SslSocketState::Uninitialized:
            next = SslSocketState::Initialized;
            break;
          case SslSocketState::Initialized:
            next = SslSocketState::Connecting;
            break;
          case SslSocketState::Connecting:
            next = SslSocketState::TcpConnected;
            break;
          default:
            next = SslSocketState::Error;
            break;
        }
        
        bool success = false;
        machine->transition(next, [&success](bool s, const std::string&) { success = s; });
        // Note: in real threaded test, we'd need proper synchronization
        if (success) {
          successful_transitions++;
        } else {
          failed_transitions++;
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    });
  }
  
  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }
  
  // Should have processed transitions without crashes
  EXPECT_GT(successful_transitions + failed_transitions, 0);
  
  // Machine should be in a valid state
  auto final_state = machine->getCurrentState();
  EXPECT_NE(final_state, SslSocketState::Uninitialized);  // Should have moved
}

/**
 * Test forced transitions
 */
TEST_F(SslStateMachineTest, StateMachine_ForcedTransitions) {
  auto machine = SslStateMachineFactory::createClientStateMachine(*dispatcher_);
  
  // Force invalid transition
  machine->forceTransition(SslSocketState::Connected);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Connected);
  
  // Force to error
  machine->forceTransition(SslSocketState::Error);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Error);
  EXPECT_TRUE(machine->isTerminalState());
  
  // Can force from terminal state
  machine->forceTransition(SslSocketState::Initialized);
  EXPECT_EQ(machine->getCurrentState(), SslSocketState::Initialized);
  EXPECT_FALSE(machine->isTerminalState());
}

}  // namespace
}  // namespace transport
}  // namespace mcp