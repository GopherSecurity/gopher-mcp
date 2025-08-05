#include <gtest/gtest.h>
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/network/socket_impl.h"
#include "mcp/buffer.h"
#include <unistd.h>
#include <fcntl.h>

namespace mcp {
namespace transport {
namespace {

// Mock transport socket callbacks
class MockTransportSocketCallbacks : public network::TransportSocketCallbacks {
public:
  bool shouldDrainReadBuffer() override { 
    return should_drain_read_buffer_; 
  }
  
  void setTransportSocketIsReadable() override {
    readable_called_++;
  }
  
  void raiseEvent(network::ConnectionEvent event) override {
    events_.push_back(event);
  }
  
  void flushWriteBuffer() override {
    flush_write_called_++;
  }
  
  // Test state
  bool should_drain_read_buffer_{true};
  int readable_called_{0};
  int flush_write_called_{0};
  std::vector<network::ConnectionEvent> events_;
};

// Mock socket for testing
class MockSocket : public network::Socket {
public:
  // Socket interface implementation
  network::ConnectionInfoSetter& connectionInfoProvider() override {
    static network::ConnectionInfoSetter dummy;
    return dummy;
  }
  
  const network::ConnectionInfoProvider& connectionInfoProvider() const override {
    static network::ConnectionInfoProvider dummy;
    return dummy;
  }
  
  network::ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override {
    return nullptr;
  }
  
  network::IoHandle& ioHandle() override { return io_handle_; }
  const network::IoHandle& ioHandle() const override { return io_handle_; }
  
  Type socketType() const override { return Type::Stream; }
  
  const network::Address::InstanceConstSharedPtr& addressProvider() const override {
    static network::Address::InstanceConstSharedPtr dummy;
    return dummy;
  }
  
  void setLocalAddress(const network::Address::InstanceConstSharedPtr& address) override {
    (void)address;
  }
  
  bool isOpen() const override { return is_open_; }
  
  void close() override { is_open_ = false; }
  
  Result<void> bind(const network::Address::Instance& address) override {
    (void)address;
    return Result<void>::makeSuccess();
  }
  
  Result<void> listen(int backlog) override {
    (void)backlog;
    return Result<void>::makeSuccess();
  }
  
  Result<void> connect(const network::Address::Instance& address) override {
    (void)address;
    return Result<void>::makeSuccess();
  }
  
  Result<void> setSocketOption(const network::SocketOption& option) override {
    (void)option;
    return Result<void>::makeSuccess();
  }
  
  Result<int> getSocketOption(const network::SocketOptionName& option_name, 
                               void* value, socklen_t* len) override {
    (void)option_name;
    (void)value;
    (void)len;
    return Result<int>::makeSuccess(0);
  }
  
  Result<void> setBlockingForTest(bool blocking) override {
    (void)blocking;
    return Result<void>::makeSuccess();
  }
  
  // Test state
  network::IoHandle io_handle_{-1};
  bool is_open_{true};
};

class StdioTransportSocketTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create pipe for testing
    int pipe_fds[2];
    ASSERT_EQ(0, pipe(pipe_fds));
    
    read_fd_ = pipe_fds[0];
    write_fd_ = pipe_fds[1];
    
    // Make non-blocking
    fcntl(read_fd_, F_SETFL, O_NONBLOCK);
    fcntl(write_fd_, F_SETFL, O_NONBLOCK);
    
    // Create config using pipe
    config_.stdin_fd = read_fd_;
    config_.stdout_fd = write_fd_;
    config_.non_blocking = true;
    
    // Create transport socket
    transport_ = std::make_unique<StdioTransportSocket>(config_);
    
    // Set callbacks
    transport_->setTransportSocketCallbacks(callbacks_);
  }
  
  void TearDown() override {
    transport_.reset();
    close(read_fd_);
    close(write_fd_);
  }
  
  void writeToStdin(const std::string& data) {
    ::write(write_fd_, data.c_str(), data.size());
  }
  
  std::string readFromStdout() {
    char buffer[1024];
    ssize_t n = ::read(read_fd_, buffer, sizeof(buffer));
    if (n > 0) {
      return std::string(buffer, n);
    }
    return "";
  }
  
  StdioTransportSocketConfig config_;
  std::unique_ptr<StdioTransportSocket> transport_;
  MockTransportSocketCallbacks callbacks_;
  MockSocket socket_;
  int read_fd_;
  int write_fd_;
};

TEST_F(StdioTransportSocketTest, BasicProperties) {
  EXPECT_EQ("stdio", transport_->protocol());
  EXPECT_TRUE(transport_->failureReason().empty());
  EXPECT_TRUE(transport_->canFlushClose());
}

TEST_F(StdioTransportSocketTest, Connect) {
  auto result = transport_->connect(socket_);
  ASSERT_TRUE(result.ok());
  
  // onConnected should be called
  transport_->onConnected();
  
  // Should mark as readable
  EXPECT_EQ(1, callbacks_.readable_called_);
}

TEST_F(StdioTransportSocketTest, ReadOperation) {
  // Connect first
  transport_->connect(socket_);
  
  // Write data to stdin
  writeToStdin("test data");
  
  // Read from transport
  auto buffer = std::make_unique<OwnedBuffer>();
  auto result = transport_->doRead(*buffer);
  
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(9, result.bytes_processed_);
  EXPECT_EQ("test data", buffer->toString());
}

TEST_F(StdioTransportSocketTest, WriteOperation) {
  // Connect first
  transport_->connect(socket_);
  
  // Write through transport
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("output data");
  
  auto result = transport_->doWrite(*buffer, false);
  
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(11, result.bytes_processed_);
  EXPECT_EQ(0, buffer->length());
  
  // Read from stdout
  std::string output = readFromStdout();
  EXPECT_EQ("output data", output);
}

TEST_F(StdioTransportSocketTest, CloseSocket) {
  transport_->connect(socket_);
  
  // Close socket
  transport_->closeSocket(network::ConnectionEvent::LocalClose);
  
  // Verify event raised
  ASSERT_EQ(1, callbacks_.events_.size());
  EXPECT_EQ(network::ConnectionEvent::LocalClose, callbacks_.events_[0]);
  
  // Subsequent reads/writes should fail
  auto buffer = std::make_unique<OwnedBuffer>();
  auto result = transport_->doRead(*buffer);
  EXPECT_FALSE(result.ok());
}

TEST_F(StdioTransportSocketTest, EndStream) {
  transport_->connect(socket_);
  
  // Write with end_stream
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("final data");
  
  auto result = transport_->doWrite(*buffer, true);
  ASSERT_TRUE(result.ok());
  
  // Subsequent writes should fail
  buffer->add("more data");
  result = transport_->doWrite(*buffer, false);
  EXPECT_FALSE(result.ok());
}

TEST_F(StdioTransportSocketTest, ReadEOF) {
  transport_->connect(socket_);
  
  // Close write end to simulate EOF
  close(write_fd_);
  write_fd_ = -1;
  
  // Read should return EOF
  auto buffer = std::make_unique<OwnedBuffer>();
  auto result = transport_->doRead(*buffer);
  
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(0, result.bytes_processed_);
  EXPECT_TRUE(result.end_stream_);
}

// StdioTransportSocketFactory tests

class StdioTransportSocketFactoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_.stdin_fd = 0;
    config_.stdout_fd = 1;
    config_.non_blocking = true;
    
    factory_ = std::make_unique<StdioTransportSocketFactory>(config_);
  }
  
  StdioTransportSocketConfig config_;
  std::unique_ptr<StdioTransportSocketFactory> factory_;
};

TEST_F(StdioTransportSocketFactoryTest, BasicProperties) {
  EXPECT_FALSE(factory_->implementsSecureTransport());
  EXPECT_EQ("stdio", factory_->name());
}

TEST_F(StdioTransportSocketFactoryTest, CreateTransportSocket) {
  // Create client transport socket
  auto socket = factory_->createTransportSocket(nullptr);
  ASSERT_NE(nullptr, socket);
  EXPECT_EQ("stdio", socket->protocol());
  
  // Create server transport socket
  socket = factory_->createTransportSocket();
  ASSERT_NE(nullptr, socket);
  EXPECT_EQ("stdio", socket->protocol());
}

TEST_F(StdioTransportSocketFactoryTest, HashKey) {
  std::vector<uint8_t> key;
  factory_->hashKey(key, nullptr);
  
  // Should contain factory name and config
  EXPECT_GT(key.size(), 0);
  
  // Verify factory name is in key
  std::string key_str(key.begin(), key.end());
  EXPECT_NE(std::string::npos, key_str.find("stdio"));
}

TEST_F(StdioTransportSocketFactoryTest, FactoryFunction) {
  // Test factory function
  auto factory = createStdioTransportSocketFactory();
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("stdio", factory->name());
  
  // With custom config
  StdioTransportSocketConfig custom_config;
  custom_config.stdin_fd = 3;
  custom_config.stdout_fd = 4;
  
  factory = createStdioTransportSocketFactory(custom_config);
  ASSERT_NE(nullptr, factory);
}

// Integration test with real stdio
TEST(StdioTransportSocketIntegrationTest, DISABLED_RealStdio) {
  // This test is disabled by default as it would interfere with test output
  // Enable manually for debugging
  
  StdioTransportSocketConfig config;
  config.stdin_fd = STDIN_FILENO;
  config.stdout_fd = STDOUT_FILENO;
  config.non_blocking = false;
  
  StdioTransportSocket transport(config);
  MockTransportSocketCallbacks callbacks;
  transport.setTransportSocketCallbacks(callbacks);
  
  MockSocket socket;
  auto result = transport.connect(socket);
  ASSERT_TRUE(result.ok());
  
  // Write a prompt
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("Enter text: ");
  transport.doWrite(*buffer, false);
  
  // Read response (would block waiting for user input)
  // auto read_result = transport.doRead(*buffer);
}

} // namespace
} // namespace transport
} // namespace mcp