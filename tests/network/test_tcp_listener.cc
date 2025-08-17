/**
 * @file test_tcp_listener.cc
 * @brief Unit tests for TCP listener implementation using real IO and MCP abstractions
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "../integration/real_io_test_base.h"
#include "mcp/network/server_listener_impl.h"
#include "mcp/network/server_listener_manager.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/buffer.h"

namespace mcp {
namespace network {
namespace {

// Real listener callbacks that handle actual connections
class RealListenerCallbacks : public ListenerCallbacks {
public:
  void onAccept(ConnectionSocketPtr&& socket) override {
    accept_count_++;
    
    // Store accepted socket for testing
    std::lock_guard<std::mutex> lock(mutex_);
    accepted_sockets_.push_back(std::move(socket));
    accept_cv_.notify_one();
  }
  
  void onNewConnection(ConnectionPtr&& connection) override {
    connection_count_++;
    
    // Store connection for testing
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.push_back(std::move(connection));
    connection_cv_.notify_one();
  }
  
  // Wait for a connection to be accepted
  ConnectionSocketPtr waitForAcceptedSocket(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (accept_cv_.wait_for(lock, timeout, [this]() { return !accepted_sockets_.empty(); })) {
      auto socket = std::move(accepted_sockets_.front());
      accepted_sockets_.pop_front();
      return socket;
    }
    return nullptr;
  }
  
  // Wait for a connection to be created
  ConnectionPtr waitForConnection(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (connection_cv_.wait_for(lock, timeout, [this]() { return !connections_.empty(); })) {
      auto conn = std::move(connections_.front());
      connections_.pop_front();
      return conn;
    }
    return nullptr;
  }
  
  std::atomic<uint32_t> accept_count_{0};
  std::atomic<uint32_t> connection_count_{0};
  
private:
  std::mutex mutex_;
  std::condition_variable accept_cv_;
  std::condition_variable connection_cv_;
  std::deque<ConnectionSocketPtr> accepted_sockets_;
  std::deque<ConnectionPtr> connections_;
};

// Real TCP listener callbacks with connection handling
class RealTcpListenerCallbacks : public TcpListenerCallbacks {
public:
  void onAccept(ConnectionSocketPtr&& socket) override {
    accept_count_++;
    
    // Store accepted socket for testing
    std::lock_guard<std::mutex> lock(mutex_);
    accepted_sockets_.push_back(std::move(socket));
    accept_cv_.notify_one();
  }
  
  void onNewConnection(ConnectionPtr&& connection) override {
    connection_count_++;
    
    // Store connection for testing
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.push_back(std::move(connection));
    connection_cv_.notify_one();
  }
  
  void onListenerEnabled() override { enabled_count_++; }
  void onListenerDisabled() override { disabled_count_++; }
  
  // Wait for a connection to be accepted
  ConnectionSocketPtr waitForAcceptedSocket(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (accept_cv_.wait_for(lock, timeout, [this]() { return !accepted_sockets_.empty(); })) {
      auto socket = std::move(accepted_sockets_.front());
      accepted_sockets_.pop_front();
      return socket;
    }
    return nullptr;
  }
  
  // Wait for a connection to be created
  ConnectionPtr waitForConnection(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (connection_cv_.wait_for(lock, timeout, [this]() { return !connections_.empty(); })) {
      auto conn = std::move(connections_.front());
      connections_.pop_front();
      return conn;
    }
    return nullptr;
  }
  
  std::atomic<uint32_t> accept_count_{0};
  std::atomic<uint32_t> connection_count_{0};
  std::atomic<uint32_t> enabled_count_{0};
  std::atomic<uint32_t> disabled_count_{0};
  
private:
  std::mutex mutex_;
  std::condition_variable accept_cv_;
  std::condition_variable connection_cv_;
  std::deque<ConnectionSocketPtr> accepted_sockets_;
  std::deque<ConnectionPtr> connections_;
};

// Test class using RealIoTestBase for real IO operations
class TcpListenerTest : public test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Use random port for testing
    address_ = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
  }
  
  void TearDown() override {
    // Stop listener in dispatcher thread
    executeInDispatcher([this]() {
      if (listener_) {
        listener_->disable();
        listener_.reset();
      }
      if (active_listener_) {
        active_listener_->disable();
        active_listener_.reset();
      }
    });
    
    RealIoTestBase::TearDown();
  }
  
  /**
   * Create a client connection using MCP abstractions
   */
  IoHandlePtr createClientConnection(uint16_t port) {
    auto& socket_int = socketInterface();
    auto client_addr = std::make_shared<Address::Ipv4Instance>("127.0.0.1", port);
    
    // Create socket and get IoHandle
    auto fd_result = socket_int.socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    
    if (!fd_result.ok()) {
      return nullptr;
    }
    
    auto client_handle = socket_int.ioHandleForFd(*fd_result, false);
    client_handle->setBlocking(false);
    
    // Connect using IoHandle
    auto connect_result = client_handle->connect(client_addr);
    
    // Non-blocking connect may return EINPROGRESS
    if (!connect_result.ok() && connect_result.error_code() != EINPROGRESS) {
      return nullptr;
    }
    
    return client_handle;
  }
  
  /**
   * Send data using MCP buffer and IoHandle
   */
  bool sendData(IoHandle& handle, const std::string& data) {
    auto buffer = createBuffer();
    buffer->add(data);
    
    // Write buffer using IoHandle
    auto result = handle.write(*buffer);
    if (result.ok()) {
      return *result == data.size();
    }
    return false;
  }
  
  /**
   * Receive data using MCP buffer and IoHandle
   */
  std::string receiveData(IoHandle& handle) {
    auto buffer = createBuffer();
    
    // Read into buffer
    auto result = handle.read(*buffer, 256);
    
    if (result.ok() && *result > 0) {
      // Extract data as string
      std::string str_result;
      str_result.resize(buffer->length());
      buffer->copyOut(0, buffer->length(), const_cast<char*>(str_result.data()));
      return str_result;
    }
    
    return "";
  }
  
  Address::InstanceConstSharedPtr address_;
  std::unique_ptr<TcpListenerImpl> listener_;
  std::unique_ptr<TcpActiveListener> active_listener_;
  std::mt19937 random_{std::random_device{}()};
  
  // Keep callbacks alive for the lifetime of the test
  std::unique_ptr<RealTcpListenerCallbacks> tcp_callbacks_;
  std::unique_ptr<RealListenerCallbacks> callbacks_;
};

// Test filter for listener filter chain testing
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

TEST_F(TcpListenerTest, CreateAndEnable) {
  tcp_callbacks_ = std::make_unique<RealTcpListenerCallbacks>();
  
  executeInDispatcher([this]() {
    // Create socket using MCP socket interface
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
    
    // Create listener with real IO
    listener_ = std::make_unique<TcpListenerImpl>(
        *dispatcher_,
        random_,
        std::move(socket),
        *tcp_callbacks_,
        true,    // bind_to_port
        false,   // ignore_global_conn_limit
        false,   // bypass_overload_manager
        1,       // max_connections_per_event
        nullopt  // overload_state
    );
    
    // Enable listener - this sets up real file event with dispatcher
    listener_->enable();
    EXPECT_EQ(tcp_callbacks_->enabled_count_, 1);
    
    // Disable listener - this removes file event from dispatcher
    listener_->disable();
    EXPECT_EQ(tcp_callbacks_->disabled_count_, 1);
  });
}

TEST_F(TcpListenerTest, RejectFraction) {
  tcp_callbacks_ = std::make_unique<RealTcpListenerCallbacks>();
  
  executeInDispatcher([this]() {
    // Create socket using MCP abstractions
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
    
    // Create listener with real IO
    listener_ = std::make_unique<TcpListenerImpl>(
        *dispatcher_,
        random_,
        std::move(socket),
        *tcp_callbacks_,
        true, false, false, 1, nullopt
    );
    
    // Set reject fraction to 0.5 (reject 50%)
    listener_->setRejectFraction(UnitFloat(0.5f));
    
    // Enable listener with real event loop
    listener_->enable();
    
    // In a real test, we'd create connections and verify ~50% are rejected
  });
}

TEST_F(TcpListenerTest, ConnectionAcceptanceWithMcpIo) {
  tcp_callbacks_ = std::make_unique<RealTcpListenerCallbacks>();
  uint32_t port = 0;
  
  // Setup listener
  executeInDispatcher([this, &port]() {
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
    port = local_address->ip()->port();
    
    // Create listener
    listener_ = std::make_unique<TcpListenerImpl>(
        *dispatcher_,
        random_,
        std::move(listen_socket),
        *tcp_callbacks_,
        true, false, false, 10, nullopt
    );
    
    listener_->enable();
  });
  
  // Create client connection using MCP abstractions
  std::thread client_thread([this, port]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto client_handle = createClientConnection(port);
    ASSERT_NE(client_handle, nullptr);
    
    // Wait for connection to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Send test data using MCP buffer
    const std::string test_data = "Hello from MCP client!";
    EXPECT_TRUE(sendData(*client_handle, test_data));
    
    // Keep connection open briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    client_handle->close();
  });
  
  // Wait for client
  client_thread.join();
  
  // Check results
  executeInDispatcher([this]() {
    EXPECT_GT(tcp_callbacks_->accept_count_, 0);
  });
}

TEST_F(TcpListenerTest, ActiveListenerWithFilters) {
  tcp_callbacks_ = std::make_unique<RealTcpListenerCallbacks>();
  MockListenerFilter* filter_ptr = nullptr;
  
  executeInDispatcher([this, &filter_ptr]() {
    // Create config with filters
    TcpListenerConfig config;
    config.name = "test_listener";
    config.address = address_;
    config.bind_to_port = true;
    config.backlog = 128;
    config.max_connections_per_event = 5;
    
    // Add a filter
    auto filter = std::make_unique<MockListenerFilter>();
    filter_ptr = filter.get();
    config.listener_filters.push_back(std::move(filter));
    
    // Create active listener - store as member so it doesn't get destroyed
    active_listener_ = std::make_unique<TcpActiveListener>(
        *dispatcher_, std::move(config), *tcp_callbacks_);
    
    // Enable listener
    active_listener_->enable();
    
    // Verify filter is used when connections arrive
    // In a real test, we'd create connections and verify filter is called
    EXPECT_EQ(filter_ptr->filter_count_, 0);  // No connections yet
  });
}

TEST_F(TcpListenerTest, ServerListenerManager) {
  executeInDispatcher([this]() {
    // Create server listener manager
    ServerListenerManagerImpl handler(*dispatcher_, 0);  // Worker 0
    
    // Verify initial state
    EXPECT_EQ(handler.numConnections(), 0);
    EXPECT_EQ(handler.statPrefix(), "worker_0.");
    
    // Create listener config
    ListenerConfig config;
    config.name = "test_listener";
    config.address = address_;
    config.bind_to_port = true;
    
    RealTcpListenerCallbacks local_callbacks;
    
    // Add listener
    handler.addListener(std::move(config), local_callbacks);
    
    // Enable all listeners
    handler.enableListeners();
    
    // Set reject fraction
    handler.setListenerRejectFraction(UnitFloat(0.1f));
    
    // Disable all listeners
    handler.disableListeners();
    
    // Stop all listeners
    handler.stopListeners();
  });
}

TEST_F(TcpListenerTest, DataTransferUsingMcpBuffer) {
  tcp_callbacks_ = std::make_unique<RealTcpListenerCallbacks>();
  uint32_t port = 0;
  
  // Setup listener
  executeInDispatcher([this, &port]() {
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
    port = local_address->ip()->port();
    
    listener_ = std::make_unique<TcpListenerImpl>(
        *dispatcher_,
        random_,
        std::move(listen_socket),
        *tcp_callbacks_,
        true, false, false, 10, nullopt
    );
    
    listener_->enable();
  });
  
  const std::string test_data = "Testing MCP buffer transfer!";
  std::string received_data;
  
  // Server thread to handle accepted connection
  std::thread server_thread([this, &received_data]() {
    // Wait for accepted socket
    auto socket = tcp_callbacks_->waitForAcceptedSocket(std::chrono::seconds(2));
    if (socket) {
      // Create IoHandle for accepted socket
      auto& socket_int = socketInterface();
      auto server_handle = socket_int.ioHandleForFd(socket->ioHandle().fd(), false);
      
      // Wait a bit for data to arrive
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      
      // Receive data using MCP abstractions
      received_data = receiveData(*server_handle);
      
      // Echo back using MCP buffer
      if (!received_data.empty()) {
        sendData(*server_handle, received_data);
      }
    }
  });
  
  // Client thread
  std::thread client_thread([this, port, test_data]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto client_handle = createClientConnection(port);
    ASSERT_NE(client_handle, nullptr);
    
    // Wait for connection
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Send data
    EXPECT_TRUE(sendData(*client_handle, test_data));
    
    // Wait for echo
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Receive echo
    auto echo = receiveData(*client_handle);
    EXPECT_EQ(echo, test_data);
    
    client_handle->close();
  });
  
  // Wait for threads
  client_thread.join();
  server_thread.join();
  
  // Verify data was transferred
  EXPECT_EQ(received_data, test_data);
}

TEST_F(TcpListenerTest, BatchedAccepts) {
  tcp_callbacks_ = std::make_unique<RealTcpListenerCallbacks>();
  uint32_t port = 0;
  
  executeInDispatcher([this, &port]() {
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
    port = local_address->ip()->port();
    
    // Create listener that accepts up to 5 connections per event
    listener_ = std::make_unique<TcpListenerImpl>(
        *dispatcher_,
        random_,
        std::move(listen_socket),
        *tcp_callbacks_,
        true, false, false, 
        5,       // Accept up to 5 connections per socket event
        nullopt
    );
    
    listener_->enable();
  });
  
  // Create multiple client connections using MCP abstractions
  const int num_clients = 3;
  std::vector<std::thread> client_threads;
  
  for (int i = 0; i < num_clients; ++i) {
    client_threads.emplace_back([this, port, i]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100 + i * 10));
      
      auto client_handle = createClientConnection(port);
      if (client_handle) {
        // Send unique data from each client
        std::string data = "Client " + std::to_string(i);
        sendData(*client_handle, data);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client_handle->close();
      }
    });
  }
  
  // Wait for all clients
  for (auto& t : client_threads) {
    t.join();
  }
  
  // Check results in dispatcher thread
  executeInDispatcher([this, num_clients]() {
    // All connections should be accepted
    EXPECT_EQ(tcp_callbacks_->accept_count_, num_clients);
  });
}

// Test using socket pair from RealIoTestBase
TEST_F(TcpListenerTest, SocketPairDataTransfer) {
  executeInDispatcher([this]() {
    // Create socket pair using RealIoTestBase utility
    auto socket_pair = createSocketPair();
    auto client_handle = std::move(socket_pair.first);
    auto server_handle = std::move(socket_pair.second);
    
    // Send data from client to server using MCP buffer
    const std::string test_data = "Socket pair test data";
    EXPECT_TRUE(sendData(*client_handle, test_data));
    
    // Receive on server side
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto received = receiveData(*server_handle);
    EXPECT_EQ(received, test_data);
    
    // Send response back
    const std::string response = "Server response";
    EXPECT_TRUE(sendData(*server_handle, response));
    
    // Receive response on client
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto client_received = receiveData(*client_handle);
    EXPECT_EQ(client_received, response);
    
    client_handle->close();
    server_handle->close();
  });
}

}  // namespace
}  // namespace network
}  // namespace mcp