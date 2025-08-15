/**
 * @file test_tcp_listener.cc
 * @brief Unit tests for simplified TCP listener implementation
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "mcp/network/listener_impl.h"
#include "mcp/network/connection_handler.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/address_impl.h"

namespace mcp {
namespace network {
namespace {

class MockListenerCallbacks : public ListenerCallbacks {
public:
  void onAccept(ConnectionSocketPtr socket, bool hand_off_restored) override {
    accept_count_++;
    last_socket_ = std::move(socket);
  }
  
  void onNewConnection(ConnectionPtr connection) override {
    connection_count_++;
    last_connection_ = std::move(connection);
  }
  
  void onListenerEnabled() override { enabled_count_++; }
  void onListenerDisabled() override { disabled_count_++; }
  
  uint32_t accept_count_{0};
  uint32_t connection_count_{0};
  uint32_t enabled_count_{0};
  uint32_t disabled_count_{0};
  ConnectionSocketPtr last_socket_;
  ConnectionPtr last_connection_;
};

class TcpListenerTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<event::LibeventDispatcher>("test");
    
    // Use random port for testing
    address_ = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  }
  
  void TearDown() override {
    if (listener_) {
      listener_->disable();
    }
    dispatcher_->shutdown();
  }
  
  std::unique_ptr<event::LibeventDispatcher> dispatcher_;
  Address::InstanceConstSharedPtr address_;
  std::unique_ptr<TcpListenerImpl> listener_;
  std::mt19937 random_{std::random_device{}()};
};

TEST_F(TcpListenerTest, CreateAndEnable) {
  MockListenerCallbacks callbacks;
  
  // Create socket
  auto socket = createListenSocket(
      address_,
      SocketCreationOptions{
          .non_blocking = true,
          .close_on_exec = true,
          .reuse_address = true
      },
      true);
  
  ASSERT_NE(socket, nullptr);
  
  // Listen on socket
  auto* listen_socket = static_cast<ListenSocketImpl*>(socket.get());
  ASSERT_TRUE(listen_socket->listen(128).ok());
  
  // Create listener
  listener_ = std::make_unique<TcpListenerImpl>(
      *dispatcher_,
      random_,
      socket,
      callbacks,
      true,    // bind_to_port
      false,   // ignore_global_conn_limit
      false,   // bypass_overload_manager
      1,       // max_connections_per_event
      nullopt  // overload_state
  );
  
  // Enable listener
  listener_->enable();
  EXPECT_EQ(callbacks.enabled_count_, 1);
  
  // Disable listener
  listener_->disable();
  EXPECT_EQ(callbacks.disabled_count_, 1);
}

TEST_F(TcpListenerTest, RejectFraction) {
  MockListenerCallbacks callbacks;
  
  // Create socket
  auto socket = createListenSocket(
      address_,
      SocketCreationOptions{
          .non_blocking = true,
          .close_on_exec = true,
          .reuse_address = true
      },
      true);
  
  ASSERT_NE(socket, nullptr);
  static_cast<ListenSocketImpl*>(socket.get())->listen(128);
  
  // Create listener
  listener_ = std::make_unique<TcpListenerImpl>(
      *dispatcher_,
      random_,
      socket,
      callbacks,
      true, false, false, 1, nullopt
  );
  
  // Set reject fraction to 0.5 (reject 50%)
  listener_->setRejectFraction(UnitFloat(0.5f));
  
  // Enable listener
  listener_->enable();
  
  // In a real test, we'd create connections and verify ~50% are rejected
}

TEST_F(TcpListenerTest, ConnectionAcceptance) {
  MockListenerCallbacks callbacks;
  
  // Create listener socket
  auto listen_socket = createListenSocket(
      address_,
      SocketCreationOptions{
          .non_blocking = true,
          .close_on_exec = true,
          .reuse_address = true
      },
      true);
  
  ASSERT_NE(listen_socket, nullptr);
  static_cast<ListenSocketImpl*>(listen_socket.get())->listen(128);
  
  // Get actual bound port
  auto local_address = listen_socket->connectionInfoProvider().localAddress();
  uint32_t port = local_address->ip()->port();
  
  // Create listener
  listener_ = std::make_unique<TcpListenerImpl>(
      *dispatcher_,
      random_,
      listen_socket,
      callbacks,
      true, false, false, 10, nullopt  // Accept up to 10 per event
  );
  
  listener_->enable();
  
  // Create client connection in another thread
  std::thread client_thread([port]() {
    // Give listener time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create client socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);
    
    // Connect to listener
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int result = connect(client_fd, (sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(result, 0);
    
    // Keep connection open briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    close(client_fd);
  });
  
  // Run event loop briefly to accept connection
  dispatcher_->run(event::RunType::NonBlock);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  dispatcher_->run(event::RunType::NonBlock);
  
  client_thread.join();
  
  // Verify connection was accepted
  EXPECT_GT(callbacks.accept_count_, 0);
  EXPECT_GT(listener_->numConnections(), 0);
}

// Test ActiveListener with filter chain
class MockListenerFilter : public ListenerFilter {
public:
  ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) override {
    filter_count_++;
    if (should_reject_) {
      return ListenerFilterStatus::StopIteration;
    }
    return ListenerFilterStatus::Continue;
  }
  
  uint32_t filter_count_{0};
  bool should_reject_{false};
};

TEST_F(TcpListenerTest, ActiveListenerWithFilters) {
  MockListenerCallbacks callbacks;
  
  // Create config with filters
  ListenerConfig config;
  config.name = "test_listener";
  config.address = address_;
  config.bind_to_port = true;
  config.backlog = 128;
  config.max_connections_per_event = 5;
  
  // Add a filter
  auto filter = std::make_unique<MockListenerFilter>();
  auto* filter_ptr = filter.get();
  config.listener_filters.push_back(std::move(filter));
  
  // Create active listener
  ActiveListener active_listener(*dispatcher_, std::move(config), callbacks);
  
  // Enable listener
  active_listener.enable();
  
  // Verify filter is used when connections arrive
  // In a real test, we'd create connections and verify filter is called
  EXPECT_EQ(filter_ptr->filter_count_, 0);  // No connections yet
}

// Test ConnectionHandler
TEST_F(TcpListenerTest, ConnectionHandler) {
  // Create connection handler
  ConnectionHandlerImpl handler(*dispatcher_, 0);  // Worker 0
  
  // Verify initial state
  EXPECT_EQ(handler.numConnections(), 0);
  EXPECT_EQ(handler.statPrefix(), "worker_0.");
  
  // Create listener config
  ListenerConfig config;
  config.name = "test_listener";
  config.address = address_;
  config.bind_to_port = true;
  config.max_connections_per_event = 5;
  
  MockListenerCallbacks callbacks;
  
  // Add listener
  handler.addListener(std::move(config), callbacks);
  
  // Enable all listeners
  handler.enableListeners();
  
  // Set reject fraction
  handler.setListenerRejectFraction(UnitFloat(0.1f));
  
  // Disable all listeners
  handler.disableListeners();
  
  // Stop all listeners
  handler.stopListeners();
}

TEST_F(TcpListenerTest, BatchedAccepts) {
  MockListenerCallbacks callbacks;
  
  // Create listener with batched accepts
  auto listen_socket = createListenSocket(
      address_,
      SocketCreationOptions{
          .non_blocking = true,
          .close_on_exec = true,
          .reuse_address = true
      },
      true);
  
  ASSERT_NE(listen_socket, nullptr);
  static_cast<ListenSocketImpl*>(listen_socket.get())->listen(128);
  
  auto local_address = listen_socket->connectionInfoProvider().localAddress();
  uint32_t port = local_address->ip()->port();
  
  // Create listener that accepts up to 5 connections per event
  listener_ = std::make_unique<TcpListenerImpl>(
      *dispatcher_,
      random_,
      listen_socket,
      callbacks,
      true, false, false, 
      5,       // Accept up to 5 connections per socket event
      nullopt
  );
  
  listener_->enable();
  
  // Create multiple client connections
  std::vector<std::thread> client_threads;
  for (int i = 0; i < 3; ++i) {
    client_threads.emplace_back([port]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      
      int client_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (client_fd < 0) return;
      
      sockaddr_in addr;
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      addr.sin_addr.s_addr = inet_addr("127.0.0.1");
      
      connect(client_fd, (sockaddr*)&addr, sizeof(addr));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      close(client_fd);
    });
  }
  
  // Run event loop to accept all connections in one batch
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  dispatcher_->run(event::RunType::NonBlock);
  
  // Wait for clients
  for (auto& t : client_threads) {
    t.join();
  }
  
  // All 3 connections should be accepted in one or two batches
  EXPECT_EQ(callbacks.accept_count_, 3);
}

}  // namespace
}  // namespace network
}  // namespace mcp