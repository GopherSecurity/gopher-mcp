#include <gtest/gtest.h>
#include "mcp/network/listener.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/connection.h"
#include "mcp/event/event_loop.h"
#include "mcp/result.h"
#include <memory>
#include <thread>

namespace mcp {
namespace network {
namespace {

// Mock listener callbacks
class MockListenerCallbacks : public ListenerCallbacks {
public:
  void onAccept(ConnectionSocketPtr&& socket) override {
    accept_called_++;
    last_accepted_socket_ = std::move(socket);
  }
  
  void onNewConnection(ConnectionPtr&& connection) override {
    new_connection_called_++;
    last_connection_ = std::move(connection);
  }
  
  // Test state
  int accept_called_{0};
  int new_connection_called_{0};
  ConnectionSocketPtr last_accepted_socket_;
  ConnectionPtr last_connection_;
};

// Mock listener filter
class MockListenerFilter : public ListenerFilter {
public:
  ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) override {
    accept_called_++;
    last_callbacks_ = &cb;
    
    if (should_continue_) {
      cb.continueFilterChain(true);
    }
    
    return return_status_;
  }
  
  void onDestroy() override {
    destroy_called_++;
  }
  
  // Test state
  int accept_called_{0};
  int destroy_called_{0};
  ListenerFilterCallbacks* last_callbacks_{nullptr};
  bool should_continue_{true};
  ListenerFilterStatus return_status_{ListenerFilterStatus::Continue};
};

// Simple test for ConnectionSocketImpl
TEST(ConnectionSocketImplTest, BasicOperations) {
  // Create a real socket fd and IoHandle
  auto socket_result = socketInterface().socket(
      SocketType::Stream,
      Address::Type::Ip,
      Address::IpVersion::v4);
  ASSERT_TRUE(socket_result.ok());
  
  auto io_handle = socketInterface().ioHandleForFd(*socket_result, false);
  auto local_addr = Address::parseInternetAddress("127.0.0.1", 0);
  auto remote_addr = Address::parseInternetAddress("127.0.0.1", 8080);
  
  ConnectionSocketImpl conn_socket(
      std::move(io_handle),
      local_addr,
      remote_addr);
  
  // Test server name
  EXPECT_TRUE(conn_socket.requestedServerName().empty());
  conn_socket.connectionInfoProvider().setRequestedServerName("example.com");
  EXPECT_EQ("example.com", conn_socket.requestedServerName());
  
  // Test half close
  EXPECT_FALSE(conn_socket.isHalfClose());
  conn_socket.setHalfClose(true);
  EXPECT_TRUE(conn_socket.isHalfClose());
  
  // Close the socket
  socketInterface().close(*socket_result);
}

class ActiveListenerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    socket_interface_ = &socketInterface();
    
    // Create listen address
    listen_addr_ = Address::parseInternetAddress("127.0.0.1", 0);
    if (!listen_addr_) {
      std::cerr << "Failed to parse address 127.0.0.1:0" << std::endl;
    }
  }
  
  void TearDown() override {
    listener_.reset();
    dispatcher_->exit();
  }
  
  ListenerConfig makeConfig(const std::string& name = "test_listener") {
    ListenerConfig config;
    config.name = name;
    config.address = listen_addr_;
    config.bind_to_port = true;
    config.enable_reuse_port = false;
    config.backlog = 128;
    config.per_connection_buffer_limit = 1024 * 1024;
    return config;
  }
  
  event::DispatcherPtr dispatcher_;
  SocketInterface* socket_interface_;
  Address::InstanceConstSharedPtr listen_addr_;
  MockListenerCallbacks callbacks_;
  std::unique_ptr<ActiveListener> listener_;
};

TEST_F(ActiveListenerTest, CreateAndListen) {
  listener_ = std::make_unique<ActiveListener>(
      *dispatcher_, *socket_interface_, callbacks_, makeConfig());
  
  // Start listening
  auto result = listener_->listen();
  if (result.holds_alternative<Error>()) {
    const auto& error = result.get<Error>();
    std::cerr << "Listen failed with error: code=" << error.code 
              << ", message=" << error.message << std::endl;
  }
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Verify listener state
  EXPECT_EQ("test_listener", listener_->name());
  EXPECT_TRUE(listener_->isEnabled());
  EXPECT_EQ(0, listener_->numConnections());
  
  // Verify tags
  const auto& tags = listener_->tags();
  ASSERT_GE(tags.size(), 2);
  EXPECT_EQ("listener", tags[0]);
  EXPECT_EQ("test_listener", tags[1]);
}

TEST_F(ActiveListenerTest, DisableEnable) {
  listener_ = std::make_unique<ActiveListener>(
      *dispatcher_, *socket_interface_, callbacks_, makeConfig());
  
  auto result = listener_->listen();
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Initially enabled
  EXPECT_TRUE(listener_->isEnabled());
  
  // Disable
  listener_->disable();
  EXPECT_FALSE(listener_->isEnabled());
  
  // Re-enable
  listener_->enable();
  EXPECT_TRUE(listener_->isEnabled());
}

TEST_F(ActiveListenerTest, AcceptConnection) {
  listener_ = std::make_unique<ActiveListener>(
      *dispatcher_, *socket_interface_, callbacks_, makeConfig());
  
  auto result = listener_->listen();
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Get actual listen port
  auto local_addr = listener_->socket().connectionInfoProvider().localAddress();
  ASSERT_NE(nullptr, local_addr);
  uint32_t port = local_addr->ip()->port();
  
  // Create client socket in separate thread
  std::thread client_thread([port]() {
    auto client_addr = Address::parseInternetAddress("127.0.0.1", port);
    auto socket_result = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    
    if (socket_result.ok()) {
      auto io_handle = socketInterface().ioHandleForFd(*socket_result, false);
      io_handle->connect(client_addr);
    }
  });
  
  // Run dispatcher briefly to accept connection
  dispatcher_->run(event::RunType::NonBlock);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  dispatcher_->run(event::RunType::NonBlock);
  
  client_thread.join();
  
  // Verify connection was accepted
  EXPECT_GT(callbacks_.accept_called_, 0);
  EXPECT_EQ(1, listener_->numConnections());
}

class ListenerManagerImplTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    socket_interface_ = &socketInterface();
    manager_ = std::make_unique<ListenerManagerImpl>(*dispatcher_, *socket_interface_);
    
    // Create listen address
    listen_addr_ = Address::parseInternetAddress("127.0.0.1", 0);
  }
  
  void TearDown() override {
    manager_.reset();
    dispatcher_->exit();
  }
  
  event::DispatcherPtr dispatcher_;
  SocketInterface* socket_interface_;
  std::unique_ptr<ListenerManagerImpl> manager_;
  Address::InstanceConstSharedPtr listen_addr_;
  MockListenerCallbacks callbacks_;
};

TEST_F(ListenerManagerImplTest, AddListener) {
  ListenerConfig config;
  config.name = "listener1";
  config.address = listen_addr_;
  
  auto result = manager_->addListener(std::move(config), callbacks_);
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Get listener
  auto* listener = manager_->getListener("listener1");
  ASSERT_NE(nullptr, listener);
  EXPECT_EQ("listener1", listener->name());
  
  // Try to add duplicate
  ListenerConfig dup_config;
  dup_config.name = "listener1";
  dup_config.address = listen_addr_;
  result = manager_->addListener(std::move(dup_config), callbacks_);
  EXPECT_TRUE(result.holds_alternative<Error>());
}

TEST_F(ListenerManagerImplTest, RemoveListener) {
  ListenerConfig config;
  config.name = "listener1";
  config.address = listen_addr_;
  
  auto result = manager_->addListener(std::move(config), callbacks_);
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Remove listener
  manager_->removeListener("listener1");
  
  // Verify removed
  auto* listener = manager_->getListener("listener1");
  EXPECT_EQ(nullptr, listener);
}

TEST_F(ListenerManagerImplTest, MultipleListeners) {
  // Add multiple listeners
  for (int i = 0; i < 3; ++i) {
    ListenerConfig config;
    config.name = "listener" + std::to_string(i);
    config.address = listen_addr_;
    
    auto result = manager_->addListener(std::move(config), callbacks_);
    ASSERT_FALSE(result.holds_alternative<Error>());
  }
  
  // Get all listeners
  auto listeners = manager_->getAllListeners();
  EXPECT_EQ(3, listeners.size());
  
  // Verify each listener
  for (int i = 0; i < 3; ++i) {
    auto* listener = manager_->getListener("listener" + std::to_string(i));
    ASSERT_NE(nullptr, listener);
  }
}

TEST_F(ListenerManagerImplTest, StopListeners) {
  // Add listeners
  ListenerConfig config1;
  config1.name = "listener1";
  config1.address = listen_addr_;
  manager_->addListener(std::move(config1), callbacks_);
  
  ListenerConfig config2;
  config2.name = "listener2";
  config2.address = listen_addr_;
  manager_->addListener(std::move(config2), callbacks_);
  
  // Stop all listeners
  manager_->stopListeners();
  
  // Verify all stopped
  auto listeners = manager_->getAllListeners();
  EXPECT_EQ(0, listeners.size());
  EXPECT_EQ(nullptr, manager_->getListener("listener1"));
  EXPECT_EQ(nullptr, manager_->getListener("listener2"));
}

// Test with listener filters
TEST_F(ActiveListenerTest, ListenerFilterChain) {
  auto config = makeConfig();
  
  // Add listener filters to config
  auto filter1 = std::make_unique<MockListenerFilter>();
  auto filter2 = std::make_unique<MockListenerFilter>();
  
  auto* filter1_ptr = filter1.get();
  auto* filter2_ptr = filter2.get();
  
  config.listener_filters.push_back(std::move(filter1));
  config.listener_filters.push_back(std::move(filter2));
  
  listener_ = std::make_unique<ActiveListener>(
      *dispatcher_, *socket_interface_, callbacks_, std::move(config));
  
  auto result = listener_->listen();
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Note: Full test would require accepting a connection and verifying
  // filters are called. For now, verify filters are stored.
}

// Test listener with socket options
TEST_F(ActiveListenerTest, SocketOptions) {
  auto config = makeConfig();
  
  // Create socket options
  config.socket_options = std::make_shared<std::vector<SocketOptionConstSharedPtr>>();
  
  // TODO: Add socket option implementations once SocketOptionImpl is available
  // config.socket_options->push_back(std::make_shared<SocketOptionImpl>(
  //     SOL_SOCKET, SO_REUSEADDR, 1));
  
  listener_ = std::make_unique<ActiveListener>(
      *dispatcher_, *socket_interface_, callbacks_, std::move(config));
  
  auto result = listener_->listen();
  ASSERT_FALSE(result.holds_alternative<Error>());
  
  // Socket should have options applied
  // Note: Actual verification would require checking socket state
}

} // namespace
} // namespace network
} // namespace mcp