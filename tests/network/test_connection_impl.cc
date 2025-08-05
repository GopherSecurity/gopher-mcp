#include <gtest/gtest.h>
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/stream_info/stream_info_impl.h"
#include <memory>

namespace mcp {
namespace network {
namespace {

// Mock transport socket for testing
class MockTransportSocket : public TransportSocket {
public:
  MockTransportSocket() = default;
  
  // TransportSocket interface
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }
  
  std::string protocol() const override { return protocol_; }
  std::string failureReason() const override { return failure_reason_; }
  bool canFlushClose() override { return can_flush_close_; }
  
  VoidResult connect(Socket& socket) override {
    (void)socket;
    connect_called_++;
    if (connect_should_fail_) {
      Error err;
      err.code = -1;
      err.message = "Connect failed";
      return makeVoidError(err);
    }
    return makeVoidSuccess();
  }
  
  void closeSocket(ConnectionEvent event) override {
    close_called_++;
    last_close_event_ = event;
  }
  
  IoResult doRead(Buffer& buffer) override {
    read_called_++;
    if (read_should_fail_) {
      return IoResult::makeError(read_error_);
    }
    
    if (!read_data_.empty()) {
      buffer.add(read_data_);
      size_t bytes = read_data_.size();
      read_data_.clear();
      return IoResult::success(bytes, read_end_stream_);
    }
    
    return IoResult::success(0);
  }
  
  IoResult doWrite(Buffer& buffer, bool end_stream) override {
    write_called_++;
    write_end_stream_ = end_stream;
    
    if (write_should_fail_) {
      return IoResult::makeError(write_error_);
    }
    
    size_t bytes = buffer.length();
    write_data_ += buffer.toString();
    buffer.drain(bytes);
    
    return IoResult::success(bytes);
  }
  
  void onConnected() override {
    connected_called_++;
  }
  
  // Test helpers
  void simulateRead(const std::string& data, bool end_stream = false) {
    read_data_ = data;
    read_end_stream_ = end_stream;
    if (callbacks_) {
      callbacks_->setTransportSocketIsReadable();
    }
  }
  
  void simulateEvent(ConnectionEvent event) {
    if (callbacks_) {
      callbacks_->raiseEvent(event);
    }
  }
  
  // Test state
  TransportSocketCallbacks* callbacks_{nullptr};
  std::string protocol_{"test"};
  std::string failure_reason_;
  bool can_flush_close_{true};
  
  int connect_called_{0};
  bool connect_should_fail_{false};
  
  int close_called_{0};
  ConnectionEvent last_close_event_;
  
  int read_called_{0};
  bool read_should_fail_{false};
  int read_error_{0};
  std::string read_data_;
  bool read_end_stream_{false};
  
  int write_called_{0};
  bool write_should_fail_{false};
  int write_error_{0};
  std::string write_data_;
  bool write_end_stream_{false};
  
  int connected_called_{0};
};

// Mock connection callbacks
class MockConnectionCallbacks : public ConnectionCallbacks {
public:
  void onEvent(ConnectionEvent event) override {
    events_.push_back(event);
  }
  
  void onAboveWriteBufferHighWatermark() override {
    above_watermark_called_++;
  }
  
  void onBelowWriteBufferLowWatermark() override {
    below_watermark_called_++;
  }
  
  // Test state
  std::vector<ConnectionEvent> events_;
  int above_watermark_called_{0};
  int below_watermark_called_{0};
};

// Mock filter
class MockFilter : public Filter {
public:
  // ReadFilter interface
  FilterStatus onData(Buffer& data, bool end_stream) override {
    read_data_called_++;
    last_read_data_ = data.toString();
    last_read_end_stream_ = end_stream;
    return read_return_status_;
  }
  
  FilterStatus onNewConnection() override {
    new_connection_called_++;
    return FilterStatus::Continue;
  }
  
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  
  // WriteFilter interface
  FilterStatus onWrite(Buffer& data, bool end_stream) override {
    write_data_called_++;
    last_write_data_ = data.toString();
    last_write_end_stream_ = end_stream;
    return write_return_status_;
  }
  
  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }
  
  // Test state
  ReadFilterCallbacks* read_callbacks_{nullptr};
  WriteFilterCallbacks* write_callbacks_{nullptr};
  
  int read_data_called_{0};
  std::string last_read_data_;
  bool last_read_end_stream_{false};
  FilterStatus read_return_status_{FilterStatus::Continue};
  
  int write_data_called_{0};
  std::string last_write_data_;
  bool last_write_end_stream_{false};
  FilterStatus write_return_status_{FilterStatus::Continue};
  
  int new_connection_called_{0};
};

class ConnectionImplTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = event::createLibeventDispatcher("test");
    socket_interface_ = &socketInterface();
    stream_info_ = stream_info::StreamInfoImpl::create();
    
    // Create socket
    auto socket_result = socket_interface_->socket(
        Address::Type::IPv4,
        Socket::Type::Stream,
        SocketCreationOptions{
            .non_blocking_ = true,
            .close_on_exec_ = true
        });
    ASSERT_TRUE(socket_result.ok());
    socket_ = std::move(socket_result.value());
    
    // Create mock transport socket
    transport_socket_ = std::make_unique<MockTransportSocket>();
    mock_transport_ = transport_socket_.get();
  }
  
  void TearDown() override {
    connection_.reset();
    dispatcher_->exit();
  }
  
  void createServerConnection() {
    connection_ = ConnectionImpl::createServerConnection(
        *dispatcher_,
        std::move(socket_),
        std::move(transport_socket_),
        *stream_info_);
  }
  
  void createClientConnection() {
    connection_ = ConnectionImpl::createClientConnection(
        *dispatcher_,
        std::move(socket_),
        std::move(transport_socket_),
        *stream_info_);
  }
  
  std::unique_ptr<event::Dispatcher> dispatcher_;
  SocketInterface* socket_interface_;
  stream_info::StreamInfoImpl::SharedPtr stream_info_;
  SocketPtr socket_;
  std::unique_ptr<MockTransportSocket> transport_socket_;
  MockTransportSocket* mock_transport_;
  ConnectionPtr connection_;
};

TEST_F(ConnectionImplTest, ServerConnectionCreation) {
  createServerConnection();
  ASSERT_NE(nullptr, connection_);
  
  // Verify state
  EXPECT_EQ(ConnectionState::Open, connection_->state());
  EXPECT_FALSE(connection_->connecting());
  
  // Transport socket callbacks should be set
  EXPECT_NE(nullptr, mock_transport_->callbacks_);
}

TEST_F(ConnectionImplTest, ClientConnectionCreation) {
  createClientConnection();
  ASSERT_NE(nullptr, connection_);
  
  // Verify state
  EXPECT_EQ(ConnectionState::Connecting, connection_->state());
  EXPECT_TRUE(connection_->connecting());
  
  // Transport socket callbacks should be set
  EXPECT_NE(nullptr, mock_transport_->callbacks_);
}

TEST_F(ConnectionImplTest, ConnectionClose) {
  createServerConnection();
  
  MockConnectionCallbacks callbacks;
  connection_->addConnectionCallbacks(callbacks);
  
  // Close with no flush
  connection_->close(ConnectionCloseType::NoFlush);
  
  // Verify close was called
  EXPECT_EQ(1, mock_transport_->close_called_);
  EXPECT_EQ(ConnectionEvent::LocalClose, mock_transport_->last_close_event_);
  
  // Verify callbacks
  ASSERT_EQ(1, callbacks.events_.size());
  EXPECT_EQ(ConnectionEvent::LocalClose, callbacks.events_[0]);
  
  // Verify state
  EXPECT_EQ(ConnectionState::Closed, connection_->state());
}

TEST_F(ConnectionImplTest, ConnectionCloseFlushWrite) {
  createServerConnection();
  
  // Add data to write buffer
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("test data");
  connection_->write(*buffer, false);
  
  // Close with flush
  connection_->close(ConnectionCloseType::FlushWrite);
  
  // State should be closing, not closed yet
  EXPECT_EQ(ConnectionState::Closing, connection_->state());
  
  // Run dispatcher to process write
  dispatcher_->run(event::RunType::NonBlock);
  
  // Verify data was written
  EXPECT_EQ("test data", mock_transport_->write_data_);
}

TEST_F(ConnectionImplTest, ReadOperation) {
  createServerConnection();
  
  // Add a read filter
  auto filter = std::make_shared<MockFilter>();
  connection_->addReadFilter(filter);
  connection_->initializeReadFilters();
  
  // Simulate data available
  mock_transport_->simulateRead("incoming data");
  
  // Run dispatcher
  dispatcher_->run(event::RunType::NonBlock);
  
  // Verify filter received data
  EXPECT_EQ(1, filter->read_data_called_);
  EXPECT_EQ("incoming data", filter->last_read_data_);
}

TEST_F(ConnectionImplTest, WriteOperation) {
  createServerConnection();
  
  // Add a write filter
  auto filter = std::make_shared<MockFilter>();
  connection_->addWriteFilter(filter);
  connection_->initializeReadFilters();
  
  // Write data
  auto buffer = std::make_unique<OwnedBuffer>();
  buffer->add("outgoing data");
  connection_->write(*buffer, false);
  
  // Run dispatcher
  dispatcher_->run(event::RunType::NonBlock);
  
  // Verify filter processed data
  EXPECT_EQ(1, filter->write_data_called_);
  EXPECT_EQ("outgoing data", filter->last_write_data_);
  
  // Verify transport received data
  EXPECT_EQ("outgoing data", mock_transport_->write_data_);
}

TEST_F(ConnectionImplTest, ReadDisable) {
  createServerConnection();
  
  // Initially reading should be enabled
  EXPECT_TRUE(connection_->readEnabled());
  
  // Disable reading
  auto status = connection_->readDisable(true);
  EXPECT_EQ(ReadDisableStatus::TransitionedToReadDisabled, status);
  EXPECT_FALSE(connection_->readEnabled());
  
  // Disable again should still be disabled
  status = connection_->readDisable(true);
  EXPECT_EQ(ReadDisableStatus::StillReadDisabled, status);
  
  // Enable reading
  status = connection_->readDisable(false);
  EXPECT_EQ(ReadDisableStatus::StillReadDisabled, status);
  
  // Enable again to fully enable
  status = connection_->readDisable(false);
  EXPECT_EQ(ReadDisableStatus::TransitionedToReadEnabled, status);
  EXPECT_TRUE(connection_->readEnabled());
}

TEST_F(ConnectionImplTest, BufferLimits) {
  createServerConnection();
  
  // Set buffer limit
  connection_->setBufferLimits(1024);
  EXPECT_EQ(1024, connection_->bufferLimit());
  
  // Initially not above watermark
  EXPECT_FALSE(connection_->aboveHighWatermark());
}

TEST_F(ConnectionImplTest, FilterChain) {
  createServerConnection();
  
  // Add multiple filters
  auto filter1 = std::make_shared<MockFilter>();
  auto filter2 = std::make_shared<MockFilter>();
  auto filter3 = std::make_shared<MockFilter>();
  
  connection_->addFilter(filter1);
  connection_->addFilter(filter2);
  connection_->addReadFilter(filter3);
  
  connection_->initializeReadFilters();
  
  // Simulate data
  mock_transport_->simulateRead("test");
  dispatcher_->run(event::RunType::NonBlock);
  
  // All filters should have processed data
  EXPECT_EQ(1, filter1->read_data_called_);
  EXPECT_EQ(1, filter2->read_data_called_);
  EXPECT_EQ(1, filter3->read_data_called_);
}

TEST_F(ConnectionImplTest, ConnectionEvents) {
  createServerConnection();
  
  MockConnectionCallbacks callbacks;
  connection_->addConnectionCallbacks(callbacks);
  
  // Simulate remote close
  mock_transport_->simulateEvent(ConnectionEvent::RemoteClose);
  
  // Verify callback
  ASSERT_EQ(1, callbacks.events_.size());
  EXPECT_EQ(ConnectionEvent::RemoteClose, callbacks.events_[0]);
}

TEST_F(ConnectionImplTest, ClientConnect) {
  createClientConnection();
  
  auto* client = dynamic_cast<ClientConnection*>(connection_.get());
  ASSERT_NE(nullptr, client);
  
  MockConnectionCallbacks callbacks;
  connection_->addConnectionCallbacks(callbacks);
  
  // Initiate connect
  client->connect();
  
  // Simulate connection success
  mock_transport_->simulateEvent(ConnectionEvent::Connected);
  
  // Verify state
  EXPECT_EQ(ConnectionState::Open, connection_->state());
  EXPECT_FALSE(connection_->connecting());
  
  // Verify callback
  ASSERT_EQ(1, callbacks.events_.size());
  EXPECT_EQ(ConnectionEvent::Connected, callbacks.events_[0]);
}

TEST_F(ConnectionImplTest, NoDelayOption) {
  createServerConnection();
  
  // Enable no delay
  connection_->noDelay(true);
  
  // Note: actual verification would require checking socket options
}

TEST_F(ConnectionImplTest, DetectedCloseType) {
  createServerConnection();
  
  // Initially no close detected
  EXPECT_EQ(DetectedCloseType::Normal, connection_->detectedCloseType());
  
  // Simulate remote close
  mock_transport_->simulateEvent(ConnectionEvent::RemoteClose);
  
  // Note: Full implementation would update detected close type
}

// ConnectionUtility tests

TEST(ConnectionUtilityTest, UpdateBufferStats) {
  ConnectionStats stats;
  uint64_t previous_total = 0;
  
  // Update with new data
  ConnectionUtility::updateBufferStats(100, 100, previous_total, stats);
  EXPECT_EQ(100, stats.bytes_buffered_);
  EXPECT_EQ(100, stats.bytes_read_);
  
  // Update with more data
  ConnectionUtility::updateBufferStats(50, 150, previous_total, stats);
  EXPECT_EQ(150, stats.bytes_buffered_);
  EXPECT_EQ(150, stats.bytes_read_);
  
  // Simulate write (buffer decreases)
  previous_total = 150;
  ConnectionUtility::updateBufferStats(0, 50, previous_total, stats);
  EXPECT_EQ(100, stats.bytes_written_);
}

} // namespace
} // namespace network
} // namespace mcp