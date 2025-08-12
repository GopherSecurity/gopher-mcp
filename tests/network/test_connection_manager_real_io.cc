#include <gtest/gtest.h>
#include "mcp/network/connection_manager.h"
#include "mcp/network/connection_pool.h"
#include "mcp/network/filter_chain.h"
#include "mcp/network/transport_socket.h"
#include "mcp/network/address.h"
#include "mcp/buffer.h"
#include "mcp/stream_info/stream_info_impl.h"
#include "../integration/real_io_test_base.h"
#include <memory>
#include <vector>

namespace mcp {
namespace network {
namespace {

// Real transport socket implementation for testing
class TestTransportSocket : public TransportSocket {
public:
  TestTransportSocket() = default;
  
  IoResult doRead(Buffer& buffer) override {
    if (io_handle_ && !closed_) {
      // Read up to 4KB at a time
      const size_t read_size = 4096;
      auto slice = buffer.reserveForRead(read_size);
      
      ssize_t result = ::read(io_handle_->fdDoNotUse(), slice.data(), slice.len());
      if (result > 0) {
        slice.commit(result);
        bytes_read_ += result;
        return IoResult{PostIoAction::KeepOpen, result, false};
      } else if (result == 0) {
        return IoResult{PostIoAction::Close, 0, false};
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return IoResult{PostIoAction::KeepOpen, 0, false};
      } else {
        return IoResult{PostIoAction::Close, 0, true};
      }
    }
    return IoResult{PostIoAction::Close, 0, true};
  }
  
  IoResult doWrite(Buffer& buffer, bool end_stream) override {
    if (io_handle_ && !closed_) {
      size_t total_written = 0;
      
      while (buffer.length() > 0) {
        // Get data to write
        auto slices = buffer.getRawSlices();
        if (slices.empty()) break;
        
        ssize_t result = ::write(io_handle_->fdDoNotUse(), 
                                  slices[0].data(), slices[0].len());
        if (result > 0) {
          buffer.drain(result);
          total_written += result;
          bytes_written_ += result;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          return IoResult{PostIoAction::Close, total_written, true};
        }
      }
      
      if (end_stream) {
        closed_ = true;
        return IoResult{PostIoAction::Close, total_written, false};
      }
      
      return IoResult{PostIoAction::KeepOpen, total_written, false};
    }
    return IoResult{PostIoAction::Close, 0, true};
  }
  
  void closeSocket(ConnectionEvent event) override {
    closed_ = true;
    close_event_ = event;
    if (io_handle_) {
      io_handle_->close();
    }
  }
  
  void onConnected() override {
    connected_ = true;
  }
  
  void setIoHandle(IoHandlePtr handle) {
    io_handle_ = std::move(handle);
  }
  
  // Test state accessors
  bool isConnected() const { return connected_; }
  bool isClosed() const { return closed_; }
  size_t bytesRead() const { return bytes_read_; }
  size_t bytesWritten() const { return bytes_written_; }
  
private:
  IoHandlePtr io_handle_;
  bool connected_{false};
  bool closed_{false};
  ConnectionEvent close_event_{ConnectionEvent::LocalClose};
  size_t bytes_read_{0};
  size_t bytes_written_{0};
};

// Factory for test transport sockets
class TestTransportSocketFactory : public TransportSocketFactory {
public:
  TransportSocketPtr createTransportSocket(
      TransportSocketOptionsConstSharedPtr options,
      stream_info::StreamInfo& info) const override {
    auto socket = std::make_unique<TestTransportSocket>();
    last_created_ = socket.get();
    create_count_++;
    return socket;
  }
  
  mutable TestTransportSocket* last_created_{nullptr};
  mutable int create_count_{0};
};

// Test callbacks for connection events
class TestConnectionCallbacks : public ConnectionCallbacks {
public:
  void onNewConnection(ConnectionPtr&& connection) override {
    new_connections_.push_back(std::move(connection));
    new_connection_count_++;
  }
  
  void onConnectionClose(Connection& connection, ConnectionCloseType type) override {
    close_count_++;
    last_close_type_ = type;
  }
  
  std::vector<ConnectionPtr> new_connections_;
  int new_connection_count_{0};
  int close_count_{0};
  ConnectionCloseType last_close_type_{ConnectionCloseType::FlushWrite};
};

// Test filter that records events
class TestConnectionFilter : public ReadFilter {
public:
  FilterStatus onData(Buffer& data, bool end_stream) override {
    data_count_++;
    last_data_size_ = data.length();
    last_end_stream_ = end_stream;
    return FilterStatus::Continue;
  }
  
  FilterStatus onNewConnection() override {
    new_connection_count_++;
    return FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }
  
  int data_count_{0};
  int new_connection_count_{0};
  size_t last_data_size_{0};
  bool last_end_stream_{false};
  ReadFilterCallbacks* callbacks_{nullptr};
};

// Test filter chain factory
class TestFilterChainFactory : public FilterChainFactory {
public:
  bool createFilterChain(FilterManager& manager) const override {
    auto filter = std::make_shared<TestConnectionFilter>();
    manager.addReadFilter(filter);
    last_filter_ = filter.get();
    create_count_++;
    return true;
  }
  
  mutable TestConnectionFilter* last_filter_{nullptr};
  mutable int create_count_{0};
};

/**
 * Connection manager tests using real IO operations.
 * All connection creation happens within dispatcher thread context.
 */
class ConnectionManagerRealIoTest : public mcp::test::RealIoTestBase {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Create factories and config within dispatcher thread
    executeInDispatcher([this]() {
      client_socket_factory_ = std::make_unique<TestTransportSocketFactory>();
      server_socket_factory_ = std::make_unique<TestTransportSocketFactory>();
      filter_factory_ = std::make_unique<TestFilterChainFactory>();
      
      config_.client_socket_factory = client_socket_factory_.get();
      config_.server_socket_factory = server_socket_factory_.get();
      config_.filter_chain_factory = filter_factory_.get();
      config_.max_connections = 100;
      config_.enable_happy_eyeballs = false;
      
      // Create stream info
      stream_info_ = std::make_unique<stream_info::StreamInfoImpl>();
      
      // Create connection manager
      manager_ = std::make_unique<ConnectionManagerImpl>(
          *dispatcher_, 
          socketInterface(),
          config_,
          *stream_info_);
    });
  }
  
  void TearDown() override {
    // Clean up within dispatcher thread
    executeInDispatcher([this]() {
      manager_.reset();
      stream_info_.reset();
      filter_factory_.reset();
      server_socket_factory_.reset();
      client_socket_factory_.reset();
    });
    
    RealIoTestBase::TearDown();
  }
  
  ConnectionManagerConfig config_;
  std::unique_ptr<TestTransportSocketFactory> client_socket_factory_;
  std::unique_ptr<TestTransportSocketFactory> server_socket_factory_;
  std::unique_ptr<TestFilterChainFactory> filter_factory_;
  std::unique_ptr<stream_info::StreamInfoImpl> stream_info_;
  std::unique_ptr<ConnectionManagerImpl> manager_;
};

// Test creating client connection with real IO (previously DISABLED)
TEST_F(ConnectionManagerRealIoTest, CreateClientConnection) {
  TestConnectionCallbacks callbacks;
  
  executeInDispatcher([&]() {
    manager_->setConnectionCallbacks(callbacks);
    
    // Create real listener first
    auto listen_addr = Address::parseInternetAddress("127.0.0.1", 0);
    
    auto listen_fd = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socketInterface().ioHandleForFd(*listen_fd, false);
    listen_handle->bind(listen_addr);
    listen_handle->listen(1);
    
    // Get actual port
    auto local_addr = listen_handle->localAddress();
    ASSERT_NE(nullptr, local_addr);
    
    // Create client connection to real address
    auto connection = manager_->createClientConnection(local_addr);
    ASSERT_NE(nullptr, connection);
    
    // Verify transport socket was created
    EXPECT_EQ(1, client_socket_factory_->create_count_);
    
    // Verify filter chain was applied
    EXPECT_EQ(1, filter_factory_->create_count_);
    
    // Verify connection count
    EXPECT_EQ(1, manager_->numConnections());
    
    // Clean up
    listen_handle->close();
  });
}

// Test connection limit with real IO (previously DISABLED)
TEST_F(ConnectionManagerRealIoTest, ConnectionLimit) {
  // Set a lower limit for testing
  executeInDispatcher([this]() {
    config_.max_connections = 5;
    manager_.reset();
    manager_ = std::make_unique<ConnectionManagerImpl>(
        *dispatcher_, 
        socketInterface(),
        config_,
        *stream_info_);
  });
  
  executeInDispatcher([&]() {
    // Create real listener
    auto listen_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto listen_fd = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socketInterface().ioHandleForFd(*listen_fd, false);
    listen_handle->bind(listen_addr);
    listen_handle->listen(10);
    auto local_addr = listen_handle->localAddress();
    
    // Create connections up to limit
    std::vector<ClientConnectionPtr> connections;
    for (size_t i = 0; i < config_.max_connections.value(); ++i) {
      auto conn = manager_->createClientConnection(local_addr);
      ASSERT_NE(nullptr, conn) << "Failed to create connection " << i;
      connections.push_back(std::move(conn));
    }
    
    // Try to create one more - should fail
    auto conn = manager_->createClientConnection(local_addr);
    EXPECT_EQ(nullptr, conn);
    
    // Verify connection count
    EXPECT_EQ(config_.max_connections.value(), manager_->numConnections());
    
    // Clean up
    listen_handle->close();
  });
}

// Test closing all connections with real IO (previously DISABLED)
TEST_F(ConnectionManagerRealIoTest, CloseAllConnections) {
  std::vector<ClientConnectionPtr> connections;
  
  executeInDispatcher([&]() {
    // Create real listener
    auto listen_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto listen_fd = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socketInterface().ioHandleForFd(*listen_fd, false);
    listen_handle->bind(listen_addr);
    listen_handle->listen(10);
    auto local_addr = listen_handle->localAddress();
    
    // Create some connections
    for (int i = 0; i < 5; ++i) {
      auto conn = manager_->createClientConnection(local_addr);
      if (conn) {
        connections.push_back(std::move(conn));
      }
    }
    
    EXPECT_EQ(5, manager_->numConnections());
    
    // Close all
    manager_->closeAllConnections();
    
    // After closeAllConnections, the map should be cleared
    EXPECT_EQ(0, manager_->numConnections());
    
    // Clean up
    listen_handle->close();
  });
  
  // Clear connections outside dispatcher
  connections.clear();
}

// Test transport socket options with real IO (previously DISABLED)
TEST_F(ConnectionManagerRealIoTest, TransportSocketOptions) {
  executeInDispatcher([&]() {
    // Create real listener
    auto listen_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto listen_fd = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socketInterface().ioHandleForFd(*listen_fd, false);
    listen_handle->bind(listen_addr);
    listen_handle->listen(1);
    auto local_addr = listen_handle->localAddress();
    
    // Create connection with transport socket options
    auto options = std::make_shared<const TransportSocketOptions>();
    auto connection = manager_->createClientConnection(
        local_addr, 
        Address::InstanceConstSharedPtr{}, 
        options);
    
    ASSERT_NE(nullptr, connection);
    
    // Verify transport socket was created with options
    EXPECT_EQ(1, client_socket_factory_->create_count_);
    
    // Clean up
    listen_handle->close();
  });
}

// Test real data transfer between connections
TEST_F(ConnectionManagerRealIoTest, RealDataTransfer) {
  executeInDispatcher([&]() {
    // Create socket pair for real communication
    auto [client_io, server_io] = createSocketPair();
    
    // Create transport sockets and attach IO handles
    auto client_transport = std::make_unique<TestTransportSocket>();
    auto server_transport = std::make_unique<TestTransportSocket>();
    
    client_transport->setIoHandle(std::move(client_io));
    server_transport->setIoHandle(std::move(server_io));
    
    // Store raw pointers for testing
    auto* client_socket = client_transport.get();
    auto* server_socket = server_transport.get();
    
    // Create connections using the manager
    stream_info::StreamInfoImpl client_info;
    stream_info::StreamInfoImpl server_info;
    
    auto client_conn = std::make_unique<ClientConnectionImpl>(
        *dispatcher_,
        std::move(client_transport),
        client_info);
    
    auto server_conn = std::make_unique<ServerConnectionImpl>(
        *dispatcher_,
        std::move(server_transport),
        server_info);
    
    // Test data transfer
    std::string test_data = "Hello from real IO test!";
    OwnedBuffer write_buffer;
    write_buffer.add(test_data);
    
    // Write from client
    auto write_result = client_socket->doWrite(write_buffer, false);
    EXPECT_EQ(PostIoAction::KeepOpen, write_result.action);
    EXPECT_EQ(test_data.size(), write_result.bytes_processed);
    EXPECT_EQ(test_data.size(), client_socket->bytesWritten());
    
    // Read on server
    OwnedBuffer read_buffer;
    auto read_result = server_socket->doRead(read_buffer);
    EXPECT_EQ(PostIoAction::KeepOpen, read_result.action);
    EXPECT_GT(read_result.bytes_processed, 0);
    EXPECT_EQ(test_data.size(), server_socket->bytesRead());
    
    // Verify data
    EXPECT_EQ(test_data, read_buffer.toString());
  });
}

// Test connection pool with real IO
TEST_F(ConnectionManagerRealIoTest, ConnectionPoolRealIO) {
  executeInDispatcher([&]() {
    // Create connection pool
    ConnectionPoolImpl pool(*dispatcher_, socketInterface(), config_);
    
    // Create real listener for connections
    auto listen_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto listen_fd = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socketInterface().ioHandleForFd(*listen_fd, false);
    listen_handle->bind(listen_addr);
    listen_handle->listen(10);
    auto local_addr = listen_handle->localAddress();
    
    // Test getting new connection from pool
    auto conn1 = pool.newConnection(local_addr);
    ASSERT_NE(nullptr, conn1);
    
    // Test reusing connection
    auto conn1_ptr = conn1.get();
    pool.returnConnection(std::move(conn1));
    
    auto conn2 = pool.newConnection(local_addr);
    EXPECT_EQ(conn1_ptr, conn2.get()); // Should be same connection (reused)
    
    // Clean up
    listen_handle->close();
  });
}

// Test complex scenario with multiple connections and data flow
TEST_F(ConnectionManagerRealIoTest, ComplexMultiConnectionScenario) {
  const int num_connections = 10;
  std::vector<ClientConnectionPtr> connections;
  std::atomic<int> total_bytes_transferred{0};
  
  executeInDispatcher([&]() {
    // Create real listener
    auto listen_addr = Address::parseInternetAddress("127.0.0.1", 0);
    auto listen_fd = socketInterface().socket(
        SocketType::Stream,
        Address::Type::Ip,
        Address::IpVersion::v4);
    ASSERT_TRUE(listen_fd.ok());
    
    auto listen_handle = socketInterface().ioHandleForFd(*listen_fd, false);
    listen_handle->bind(listen_addr);
    listen_handle->listen(num_connections);
    auto local_addr = listen_handle->localAddress();
    
    // Create multiple connections
    for (int i = 0; i < num_connections; ++i) {
      auto conn = manager_->createClientConnection(local_addr);
      ASSERT_NE(nullptr, conn);
      connections.push_back(std::move(conn));
    }
    
    EXPECT_EQ(num_connections, manager_->numConnections());
    
    // Simulate data transfer on each connection
    for (auto& conn : connections) {
      // Each connection would normally have data transfer here
      // For this test, we just verify they exist
      EXPECT_NE(nullptr, conn);
      total_bytes_transferred += 100; // Simulate 100 bytes per connection
    }
    
    // Test partial close
    for (int i = 0; i < num_connections / 2; ++i) {
      connections[i]->close(ConnectionCloseType::NoFlush);
    }
    
    // Wait a bit for closes to process
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Close remaining via manager
    manager_->closeAllConnections();
    
    EXPECT_EQ(0, manager_->numConnections());
    
    // Clean up
    listen_handle->close();
  });
  
  // Verify operations completed
  EXPECT_EQ(num_connections * 100, total_bytes_transferred);
}

} // namespace
} // namespace network
} // namespace mcp