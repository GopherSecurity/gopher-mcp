#include <gtest/gtest.h>
#include "mcp/network/connection_manager.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/network/listener.h"
#include "mcp/event/event_loop.h"
#include "mcp/result.h"
#include "../mocks/network_mocks.h"
#include <memory>

namespace mcp {
namespace network {
namespace {

// Mock transport socket factory
class MockTransportSocketFactory : public UniversalTransportSocketFactory {
public:
  // TransportSocketFactoryBase interface
  bool implementsSecureTransport() const override { return false; }
  std::string name() const override { return "mock"; }
  
  // ClientTransportSocketFactory interface
  TransportSocketPtr createTransportSocket(
      TransportSocketOptionsSharedPtr options) const override {
    (void)options;
    create_count_++;
    auto socket = std::make_unique<MockTransportSocket>();
    last_created_ = socket.get();
    return socket;
  }
  
  void hashKey(std::vector<uint8_t>& key,
               TransportSocketOptionsSharedPtr options) const override {
    (void)options;
    const std::string name = "mock";
    key.insert(key.end(), name.begin(), name.end());
  }
  
  // ServerTransportSocketFactory interface
  TransportSocketPtr createTransportSocket() const override {
    return createTransportSocket(nullptr);
  }
  
  // Test helpers
  class MockTransportSocket : public TransportSocket {
  public:
    void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override {
      callbacks_ = &callbacks;
    }
    
    std::string protocol() const override { return "mock"; }
    std::string failureReason() const override { return ""; }
    bool canFlushClose() override { return true; }
    VoidResult connect(Socket& socket) override { 
      (void)socket;
      return makeVoidSuccess(); 
    }
    void closeSocket(ConnectionEvent event) override { (void)event; }
    TransportIoResult doRead(Buffer& buffer) override { 
      (void)buffer;
      return TransportIoResult::success(0); 
    }
    TransportIoResult doWrite(Buffer& buffer, bool end_stream) override { 
      (void)buffer;
      (void)end_stream;
      return TransportIoResult::success(0); 
    }
    void onConnected() override {}
    
    TransportSocketCallbacks* callbacks_{nullptr};
  };
  
  mutable int create_count_{0};
  mutable MockTransportSocket* last_created_{nullptr};
};

// Mock filter chain factory
class MockFilterChainFactory : public FilterChainFactory {
public:
  bool createFilterChain(FilterManager& filter_manager) const override {
    (void)filter_manager;
    create_chain_called_++;
    return true;
  }
  
  bool createNetworkFilterChain(FilterManager& filter_manager,
                                const std::vector<FilterFactoryCb>& factories) const override {
    (void)filter_manager;
    (void)factories;
    return true;
  }
  
  bool createListenerFilterChain(FilterManager& filter_manager) const override {
    (void)filter_manager;
    return true;
  }
  
  mutable int create_chain_called_{0};
};

// Mock connection pool callbacks
class MockConnectionPoolCallbacks : public ConnectionPoolCallbacks {
public:
  void onConnectionCreate(Connection& connection) override {
    (void)connection;
    create_called_++;
  }
  
  void onConnectionEvent(Connection& connection, ConnectionEvent event) override {
    (void)connection;
    events_.push_back(event);
  }
  
  int create_called_{0};
  std::vector<ConnectionEvent> events_;
};

class ConnectionHandlerImplTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    mock_socket_interface_ = std::make_unique<test::MockSocketInterface>();
    socket_interface_ = mock_socket_interface_.get();
    handler_ = std::make_unique<ConnectionHandlerImpl>(*dispatcher_, *socket_interface_);
  }
  
  void TearDown() override {
    handler_.reset();
    dispatcher_->exit();
  }
  
  event::DispatcherPtr dispatcher_;
  std::unique_ptr<test::MockSocketInterface> mock_socket_interface_;
  network::SocketInterface* socket_interface_;
  std::unique_ptr<ConnectionHandlerImpl> handler_;
};

TEST_F(ConnectionHandlerImplTest, InitialState) {
  EXPECT_EQ(0, handler_->numConnections());
}

TEST_F(ConnectionHandlerImplTest, ListenerManagement) {
  // Create mock listener
  ListenerConfig config;
  config.name = "test_listener";
  config.address = Address::parseInternetAddress("127.0.0.1", 0);
  
  auto listener = std::make_unique<ActiveListener>(
      *dispatcher_, *socket_interface_, *handler_, std::move(config));
  
  // Add listener
  handler_->addListener(std::move(listener));
  
  // Remove listener
  handler_->removeListener("test_listener");
  
  // Stop all listeners
  handler_->stopListeners();
}

TEST_F(ConnectionHandlerImplTest, DisableEnableListeners) {
  // Initially not disabled
  
  // Disable listeners
  handler_->disableListeners();
  
  // Re-enable listeners
  handler_->enableListeners();
}

class ConnectionManagerImplTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    mock_socket_interface_ = std::make_unique<test::MockSocketInterface>();
    socket_interface_ = mock_socket_interface_.get();
    
    // Create config
    config_.max_connections = 10;
    config_.connection_timeout = std::chrono::milliseconds(5000);
    config_.per_connection_buffer_limit = 1024 * 1024;
    
    // Create mock factories
    auto client_factory = std::make_shared<MockTransportSocketFactory>();
    auto server_factory = std::make_shared<MockTransportSocketFactory>();
    
    mock_client_factory_ = client_factory.get();
    mock_server_factory_ = server_factory.get();
    
    config_.client_transport_socket_factory = client_factory;
    config_.server_transport_socket_factory = server_factory;
    
    auto filter_factory = std::make_shared<MockFilterChainFactory>();
    mock_filter_factory_ = filter_factory.get();
    config_.filter_chain_factory = filter_factory;
    
    manager_ = std::make_unique<ConnectionManagerImpl>(
        *dispatcher_, *socket_interface_, config_);
  }
  
  void TearDown() override {
    manager_.reset();
    dispatcher_->exit();
  }
  
  event::DispatcherPtr dispatcher_;
  std::unique_ptr<test::MockSocketInterface> mock_socket_interface_;
  network::SocketInterface* socket_interface_;
  ConnectionManagerConfig config_;
  std::unique_ptr<ConnectionManagerImpl> manager_;
  MockTransportSocketFactory* mock_client_factory_;
  MockTransportSocketFactory* mock_server_factory_;
  MockFilterChainFactory* mock_filter_factory_;
  MockConnectionPoolCallbacks callbacks_;
};

TEST_F(ConnectionManagerImplTest, CreateClientConnection) {
  auto addr = Address::parseInternetAddress("127.0.0.1", 8080);
  
  manager_->setConnectionCallbacks(callbacks_);
  
  auto connection = manager_->createClientConnection(addr);
  ASSERT_NE(nullptr, connection);
  
  // Verify transport socket was created
  EXPECT_EQ(1, mock_client_factory_->create_count_);
  
  // Verify filter chain was applied
  EXPECT_EQ(1, mock_filter_factory_->create_chain_called_);
  
  // Verify connection count
  EXPECT_EQ(1, manager_->numConnections());
}

TEST_F(ConnectionManagerImplTest, ConnectionLimit) {
  auto addr = Address::parseInternetAddress("127.0.0.1", 8080);
  
  // Create connections up to limit
  std::vector<ClientConnectionPtr> connections;
  for (size_t i = 0; i < config_.max_connections.value(); ++i) {
    auto conn = manager_->createClientConnection(addr);
    ASSERT_NE(nullptr, conn);
    connections.push_back(std::move(conn));
  }
  
  // Try to create one more - should fail
  auto conn = manager_->createClientConnection(addr);
  EXPECT_EQ(nullptr, conn);
  
  // Verify connection count
  EXPECT_EQ(config_.max_connections.value(), manager_->numConnections());
}

TEST_F(ConnectionManagerImplTest, CloseAllConnections) {
  auto addr = Address::parseInternetAddress("127.0.0.1", 8080);
  
  // Create some connections and keep them alive
  std::vector<ClientConnectionPtr> connections;
  for (int i = 0; i < 5; ++i) {
    auto conn = manager_->createClientConnection(addr);
    if (conn) {
      connections.push_back(std::move(conn));
    }
  }
  
  EXPECT_EQ(5, manager_->numConnections());
  
  // Close all
  manager_->closeAllConnections();
  
  // After closeAllConnections, the map should be cleared
  EXPECT_EQ(0, manager_->numConnections());
  
  // The connections themselves are still alive (we hold the unique_ptrs)
  // but they've been closed and removed from tracking
  connections.clear();
}

TEST_F(ConnectionManagerImplTest, TransportSocketOptions) {
  auto addr = Address::parseInternetAddress("127.0.0.1", 8080);
  
  // Create connection with transport options
  auto options = std::make_shared<TransportSocketOptionsImpl>();
  options->setServerNameOverride("example.com");
  
  auto connection = manager_->createClientConnection(addr, options);
  ASSERT_NE(nullptr, connection);
  
  // Verify transport socket was created with options
  EXPECT_EQ(1, mock_client_factory_->create_count_);
}

// ConnectionPoolImpl tests

class ConnectionPoolImplTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    mock_socket_interface_ = std::make_unique<test::MockSocketInterface>();
    socket_interface_ = mock_socket_interface_.get();
    
    // Create config
    config_.max_connections = 5;
    config_.connection_timeout = std::chrono::milliseconds(1000);
    
    // Add transport socket factories so createClientConnection doesn't fail
    auto client_factory = std::make_shared<MockTransportSocketFactory>();
    config_.client_transport_socket_factory = client_factory;
    
    // Create connection manager
    manager_ = std::make_unique<ConnectionManagerImpl>(
        *dispatcher_, *socket_interface_, config_);
    
    // Create pool
    addr_ = Address::parseInternetAddress("127.0.0.1", 8080);
    pool_ = std::make_unique<ConnectionPoolImpl>(
        *dispatcher_, addr_, config_, *manager_);
  }
  
  void TearDown() override {
    pool_.reset();
    manager_.reset();
    dispatcher_->exit();
  }
  
  event::DispatcherPtr dispatcher_;
  std::unique_ptr<test::MockSocketInterface> mock_socket_interface_;
  network::SocketInterface* socket_interface_;
  ConnectionManagerConfig config_;
  std::unique_ptr<ConnectionManagerImpl> manager_;
  Address::InstanceConstSharedPtr addr_;
  std::unique_ptr<ConnectionPoolImpl> pool_;
};

// Mock pool callbacks
class MockPoolCallbacks : public ConnectionPool::Callbacks {
public:
  void onPoolReady(ClientConnection& connection, const std::string& protocol) override {
    ready_called_++;
    last_connection_ = &connection;
    last_protocol_ = protocol;
  }
  
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     const std::string& failure_reason) override {
    failure_called_++;
    last_failure_reason_ = reason;
    last_failure_msg_ = std::string(failure_reason);
  }
  
  int ready_called_{0};
  int failure_called_{0};
  ClientConnection* last_connection_{nullptr};
  std::string last_protocol_;
  ConnectionPool::PoolFailureReason last_failure_reason_;
  std::string last_failure_msg_;
};

TEST_F(ConnectionPoolImplTest, InitialState) {
  EXPECT_EQ(0, pool_->numActiveConnections());
  EXPECT_EQ(0, pool_->numPendingConnections());
  EXPECT_EQ(0, pool_->numIdleConnections());
  EXPECT_TRUE(pool_->isIdle());
}

TEST_F(ConnectionPoolImplTest, NewConnection) {
  MockPoolCallbacks callbacks;
  
  // Request new connection
  pool_->newConnection(callbacks);
  
  // Should have one pending
  EXPECT_EQ(1, pool_->numPendingConnections());
  EXPECT_FALSE(pool_->isIdle());
  
  // Note: Full test would require simulating connection completion
}

TEST_F(ConnectionPoolImplTest, ConnectionLimit) {
  // Request connections up to limit
  std::vector<MockPoolCallbacks> callbacks(config_.max_connections.value() + 1);
  
  for (size_t i = 0; i < config_.max_connections.value(); ++i) {
    pool_->newConnection(callbacks[i]);
  }
  
  // Should have max pending
  EXPECT_EQ(config_.max_connections.value(), pool_->numPendingConnections());
  
  // Try one more - should fail
  pool_->newConnection(callbacks[config_.max_connections.value()]);
  EXPECT_EQ(1, callbacks[config_.max_connections.value()].failure_called_);
  EXPECT_EQ(ConnectionPool::PoolFailureReason::Overflow, 
            callbacks[config_.max_connections.value()].last_failure_reason_);
}

TEST_F(ConnectionPoolImplTest, CloseConnections) {
  MockPoolCallbacks callbacks;
  
  // Request some connections
  pool_->newConnection(callbacks);
  pool_->newConnection(callbacks);
  
  EXPECT_EQ(2, pool_->numPendingConnections());
  
  // Close all
  pool_->closeConnections();
  
  // Should be empty
  EXPECT_EQ(0, pool_->numPendingConnections());
  EXPECT_TRUE(pool_->isIdle());
}

TEST_F(ConnectionPoolImplTest, DrainConnections) {
  MockPoolCallbacks callbacks;
  
  // Request connection
  pool_->newConnection(callbacks);
  
  // Drain pool
  pool_->drainConnections();
  
  // New requests should fail
  MockPoolCallbacks new_callbacks;
  pool_->newConnection(new_callbacks);
  EXPECT_EQ(1, new_callbacks.failure_called_);
}

TEST_F(ConnectionPoolImplTest, IdleCallbacks) {
  bool idle_called = false;
  pool_->addIdleCallback([&idle_called]() { idle_called = true; });
  
  // Initially idle, so callback should be called
  EXPECT_TRUE(pool_->isIdle());
  
  // Note: Full implementation would trigger callbacks when becoming idle
}

} // namespace
} // namespace network
} // namespace mcp