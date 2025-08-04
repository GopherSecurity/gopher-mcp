#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>
#include <errno.h>

#include "mcp/network/transport_socket.h"
#include "mcp/network/socket_option_impl.h"
#include "mcp/buffer.h"

using namespace mcp;
using namespace mcp::network;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::NiceMock;

// Simple mock for IoHandle
class MockIoHandle : public IoHandle {
 public:
  MOCK_METHOD(IoCallResult, readv, (size_t max_length, RawSlice* slices, size_t num_slices), (override));
  MOCK_METHOD(IoCallResult, read, (Buffer& buffer, optional<size_t> max_length), (override));
  MOCK_METHOD(IoCallResult, writev, (const ConstRawSlice* slices, size_t num_slices), (override));
  MOCK_METHOD(IoCallResult, write, (Buffer& buffer), (override));
  MOCK_METHOD(IoCallResult, sendmsg, (const ConstRawSlice* slices, size_t num_slices, int flags, const Address::Ip* self_ip, const Address::Instance& peer_address), (override));
  MOCK_METHOD(IoCallResult, recvmsg, (RawSlice* slices, size_t num_slices, uint32_t self_port, const UdpSaveCmsgConfig& save_cmsg_config, RecvMsgOutput& output), (override));
  MOCK_METHOD(IoCallResult, recvmmsg, (std::vector<RawSlice>& slices, uint32_t self_port, const UdpSaveCmsgConfig& save_cmsg_config, RecvMsgOutput& output), (override));
  MOCK_METHOD(IoVoidResult, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(IoResult<int>, bind, (const Address::InstanceConstSharedPtr& address), (override));
  MOCK_METHOD(IoResult<int>, listen, (int backlog), (override));
  MOCK_METHOD(IoResult<IoHandlePtr>, accept, (), (override));
  MOCK_METHOD(IoResult<int>, connect, (const Address::InstanceConstSharedPtr& address), (override));
  MOCK_METHOD(IoResult<int>, shutdown, (int how), (override));
  MOCK_METHOD(IoResult<int>, setSocketOption, (int level, int optname, const void* optval, socklen_t optlen), (override));
  MOCK_METHOD(IoResult<int>, getSocketOption, (int level, int optname, void* optval, socklen_t* optlen), (const, override));
  MOCK_METHOD(IoResult<int>, ioctl, (unsigned long request, void* argp), (override));
  MOCK_METHOD(void, initializeFileEvent, (event::Dispatcher& dispatcher, event::FileReadyCb cb, event::FileTriggerType trigger, uint32_t events), (override));
  MOCK_METHOD(void, activateFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, enableFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, resetFileEvents, (), (override));
  MOCK_METHOD(os_fd_t, fd, (), (const, override));
  MOCK_METHOD(IoResult<Address::InstanceConstSharedPtr>, localAddress, (), (const, override));
  MOCK_METHOD(IoResult<Address::InstanceConstSharedPtr>, peerAddress, (), (const, override));
  MOCK_METHOD(optional<std::string>, interfaceName, (), (const, override));
  MOCK_METHOD(IoResult<int>, setBlocking, (bool blocking), (override));
  MOCK_METHOD(optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const, override));
  MOCK_METHOD(void, configureInitialCongestionWindow, (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt), (override));
  MOCK_METHOD(IoHandlePtr, duplicate, (), (override));
};

// Minimal mock for TransportSocketCallbacks - don't mock Connection
class MockTransportSocketCallbacks : public TransportSocketCallbacks {
 public:
  // Stub Connection to avoid forward declaration issues
  class StubConnection : public Connection {
   public:
    void close(ConnectionCloseType) override {}
    void closeSocket(ConnectionEvent) override {}
    uint64_t id() const override { return 0; }
    void noDelay(bool) override {}
    void readDisable(bool) override {}
    bool readEnabled() const override { return true; }
    const ConnectionSocketOptionsSharedPtr& socketOptions() const override {
      static ConnectionSocketOptionsSharedPtr opts;
      return opts;
    }
    std::string transportFailureReason() const override { return ""; }
    bool startSecureTransport() override { return false; }
    std::chrono::milliseconds lastRoundTripTime() override { return std::chrono::milliseconds(0); }
  };

  MOCK_METHOD(IoHandle&, ioHandle, (), (override));
  MOCK_METHOD(const IoHandle&, ioHandle, (), (const, override));
  MOCK_METHOD(bool, shouldDrainReadBuffer, (), (override));
  MOCK_METHOD(void, setTransportSocketIsReadable, (), (override));
  MOCK_METHOD(void, raiseEvent, (ConnectionEvent event), (override));
  MOCK_METHOD(void, flushWriteBuffer, (), (override));
  
  Connection& connection() override { 
    static StubConnection conn;
    return conn;
  }
};

class TransportSocketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    buffer_ = createBuffer();
  }

  std::unique_ptr<Buffer> buffer_;
};

// Test RawBufferTransportSocket basic functionality
TEST_F(TransportSocketTest, RawBufferTransportSocketBasic) {
  RawBufferTransportSocket socket;
  NiceMock<MockTransportSocketCallbacks> callbacks;
  
  socket.setTransportSocketCallbacks(callbacks);
  
  // Test protocol
  EXPECT_EQ(socket.protocol(), "");
  
  // Test failure reason (initially empty)
  EXPECT_EQ(socket.failureReason(), "");
  
  // Test can flush close
  EXPECT_TRUE(socket.canFlushClose());
  
  // Test SSL info (should be nullptr for raw socket)
  EXPECT_EQ(socket.ssl(), nullptr);
  
  // Test start secure transport (should return false)
  EXPECT_FALSE(socket.startSecureTransport());
}

// Test connection lifecycle
TEST_F(TransportSocketTest, ConnectionLifecycle) {
  RawBufferTransportSocket socket;
  NiceMock<MockTransportSocketCallbacks> callbacks;
  
  socket.setTransportSocketCallbacks(callbacks);
  
  // Test onConnected callback
  socket.onConnected();
  
  // Test close socket with different events
  EXPECT_CALL(callbacks, raiseEvent(ConnectionEvent::RemoteClose));
  socket.closeSocket(ConnectionEvent::RemoteClose);
  
  EXPECT_CALL(callbacks, raiseEvent(ConnectionEvent::LocalClose));
  socket.closeSocket(ConnectionEvent::LocalClose);
}

// Test read operations
TEST_F(TransportSocketTest, ReadOperations) {
  RawBufferTransportSocket socket;
  MockTransportSocketCallbacks callbacks;
  MockIoHandle io_handle;
  
  socket.setTransportSocketCallbacks(callbacks);
  
  // Setup expectations
  EXPECT_CALL(callbacks, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  
  // Test successful read
  const size_t read_size = 100;
  EXPECT_CALL(io_handle, readv(16384, _, 1))
      .WillOnce([](size_t, RawSlice* slices, size_t) {
        // Simulate that we read some data
        slices[0].len_ = 100;
        return IoCallResult::success(100);
      });
  
  EXPECT_CALL(callbacks, setTransportSocketIsReadable());
  
  auto result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, read_size);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_FALSE(result.error_.has_value());
  
  // Test read with would block
  EXPECT_CALL(io_handle, readv(16384, _, 1))
      .WillOnce(Return(IoCallResult::error(EAGAIN)));
  
  result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
  
  // Test read with connection reset
  EXPECT_CALL(io_handle, readv(16384, _, 1))
      .WillOnce(Return(IoCallResult::error(ECONNRESET)));
  
  result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CLOSE);
  EXPECT_EQ(result.bytes_processed_, 0);
  
  // Test read EOF (0 bytes)
  EXPECT_CALL(io_handle, readv(16384, _, 1))
      .WillOnce(Return(IoCallResult::success(0)));
  
  result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
  EXPECT_TRUE(result.end_stream_read_);
}

// Test write operations
TEST_F(TransportSocketTest, WriteOperations) {
  RawBufferTransportSocket socket;
  MockTransportSocketCallbacks callbacks;
  MockIoHandle io_handle;
  
  socket.setTransportSocketCallbacks(callbacks);
  
  // Setup expectations
  EXPECT_CALL(callbacks, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  
  // Add some data to buffer
  buffer_->add("Hello, World!", 13);
  
  // Test successful write
  EXPECT_CALL(io_handle, writev(_, 1))
      .WillOnce(Return(IoCallResult::success(13)));
  
  auto result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 13);
  EXPECT_FALSE(result.end_stream_read_);
  EXPECT_FALSE(result.error_.has_value());
  EXPECT_EQ(buffer_->length(), 0); // Buffer should be drained
  
  // Test write with would block
  buffer_->add("More data", 9);
  EXPECT_CALL(io_handle, writev(_, 1))
      .WillOnce(Return(IoCallResult::error(EAGAIN)));
  
  result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
  EXPECT_EQ(buffer_->length(), 9); // Buffer should not be drained
  
  // Test write with connection reset
  EXPECT_CALL(io_handle, writev(_, 1))
      .WillOnce(Return(IoCallResult::error(ECONNRESET)));
  
  result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CLOSE);
  EXPECT_EQ(result.bytes_processed_, 0);
  
  // Test write with end stream
  buffer_->drain(buffer_->length()); // Clear buffer
  // flushWriteBuffer is not called for empty buffer with end_stream
  // because the implementation returns early
  
  result = socket.doWrite(*buffer_, true);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
}

// Test partial writes
TEST_F(TransportSocketTest, PartialWrites) {
  RawBufferTransportSocket socket;
  MockTransportSocketCallbacks callbacks;
  MockIoHandle io_handle;
  
  socket.setTransportSocketCallbacks(callbacks);
  EXPECT_CALL(callbacks, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  
  // Add data that requires multiple slices
  std::string data(32768, 'X'); // 32KB of data
  buffer_->add(data);
  
  // Test partial write
  EXPECT_CALL(io_handle, writev(_, _))
      .WillOnce(Return(IoCallResult::success(16384))); // Only write half
  
  auto result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 16384);
  EXPECT_EQ(buffer_->length(), 16384); // Half should remain
  
  // Write the rest
  EXPECT_CALL(io_handle, writev(_, _))
      .WillOnce(Return(IoCallResult::success(16384)));
  
  result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 16384);
  EXPECT_EQ(buffer_->length(), 0); // All data written
}

// Test error handling
TEST_F(TransportSocketTest, ErrorHandling) {
  RawBufferTransportSocket socket;
  MockTransportSocketCallbacks callbacks;
  MockIoHandle io_handle;
  
  socket.setTransportSocketCallbacks(callbacks);
  EXPECT_CALL(callbacks, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  
  // Test read with generic error
  EXPECT_CALL(io_handle, readv(_, _, _))
      .WillOnce([]() {
        IoCallResult result;
        result.error_info = SystemError(EIO, "I/O error");
        return result;
      });
  
  auto result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CLOSE);
  EXPECT_TRUE(result.error_.has_value());
  EXPECT_FALSE(socket.failureReason().empty());
  
  // Test write with broken pipe
  buffer_->add("data", 4);
  EXPECT_CALL(io_handle, writev(_, _))
      .WillOnce(Return(IoCallResult::error(EPIPE)));
  
  result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CLOSE);
  EXPECT_EQ(result.bytes_processed_, 0);
}

// Test shutdown behavior
TEST_F(TransportSocketTest, ShutdownBehavior) {
  RawBufferTransportSocket socket;
  MockTransportSocketCallbacks callbacks;
  MockIoHandle io_handle;
  
  socket.setTransportSocketCallbacks(callbacks);
  EXPECT_CALL(callbacks, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  
  // Close for reading
  EXPECT_CALL(callbacks, raiseEvent(ConnectionEvent::RemoteClose));
  socket.closeSocket(ConnectionEvent::RemoteClose);
  
  // Attempt to read after shutdown - should return immediately
  auto result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
  
  // Close for writing
  EXPECT_CALL(callbacks, raiseEvent(ConnectionEvent::LocalClose));
  socket.closeSocket(ConnectionEvent::LocalClose);
  
  // Attempt to write after shutdown - should return immediately
  buffer_->add("test", 4);
  result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
}

// Test factory
TEST_F(TransportSocketTest, RawBufferTransportSocketFactory) {
  RawBufferTransportSocketFactory factory;
  
  // Test factory properties
  EXPECT_FALSE(factory.implementsSecureTransport());
  EXPECT_EQ(factory.name(), "raw_buffer");
  EXPECT_FALSE(factory.supportsAlpn());
  EXPECT_EQ(factory.defaultServerNameIndication(), "");
  
  // Test creating client socket with options
  auto options = std::make_shared<TransportSocketOptionsImpl>();
  auto socket = factory.createTransportSocket(options);
  EXPECT_NE(socket, nullptr);
  
  // Test creating server socket
  auto server_socket = factory.createTransportSocket();
  EXPECT_NE(server_socket, nullptr);
  
  // Test hash key generation
  std::vector<uint8_t> key;
  factory.hashKey(key, options);
  EXPECT_EQ(key.size(), 1);
  EXPECT_EQ(key[0], 0);
}

// Test edge cases
TEST_F(TransportSocketTest, EdgeCases) {
  RawBufferTransportSocket socket;
  
  // Test operations without callbacks set
  auto result = socket.doRead(*buffer_);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
  
  result = socket.doWrite(*buffer_, false);
  EXPECT_EQ(result.action_, TransportIoResult::CONTINUE);
  EXPECT_EQ(result.bytes_processed_, 0);
  
  // Test close without callbacks
  socket.closeSocket(ConnectionEvent::RemoteClose); // Should not crash
}

// Test transport socket options
TEST_F(TransportSocketTest, TransportSocketOptions) {
  TransportSocketOptionsImpl options;
  
  // Test setting and getting options
  options.setServerNameOverride("test.server")
         .setVerifySubjectAltNameListOverride({"san1", "san2"})
         .setApplicationProtocolListOverride({"h2", "http/1.1"})
         .setApplicationProtocolFallback({"http/1.1"});
  
  EXPECT_EQ(options.serverNameOverride().value(), "test.server");
  EXPECT_EQ(options.verifySubjectAltNameListOverride().size(), 2);
  EXPECT_EQ(options.applicationProtocolListOverride().size(), 2);
  EXPECT_EQ(options.applicationProtocolFallback().size(), 1);
  
  // Test socket options
  auto socket_options = std::make_shared<std::vector<SocketOptionConstSharedPtr>>();
  socket_options->push_back(std::make_shared<const BoolSocketOption>(SOCKET_SO_REUSEADDR, true));
  options.setSocketOptions(socket_options);
  EXPECT_EQ(options.socketOptions(), socket_options);
}