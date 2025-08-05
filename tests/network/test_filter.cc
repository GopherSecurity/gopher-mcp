#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mcp/network/filter.h"
#include "mcp/network/connection.h"
#include "mcp/buffer.h"
#include "mcp/network/address.h"
#include <memory>

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Invoke;
using ::testing::ByMove;

namespace mcp {
namespace network {

// Mock Classes

class MockFilterManagerConnection : public FilterManagerConnection {
public:
  MOCK_METHOD(Buffer&, readBuffer, ());
  MOCK_METHOD(Buffer&, writeBuffer, ());
  MOCK_METHOD(void, close, (ConnectionCloseType type));
  MOCK_METHOD(bool, readHalfClosed, (), (const));
  MOCK_METHOD(bool, isClosed, (), (const));
  MOCK_METHOD(void, readDisable, (bool disable));
  MOCK_METHOD(bool, readDisabled, (), (const));
  MOCK_METHOD(bool, startSecureTransport, ());
  MOCK_METHOD(SslConnectionInfoConstSharedPtr, ssl, (), (const));
};

class MockConnection : public Connection {
public:
  MOCK_METHOD(void, close, (ConnectionCloseType type));
  MOCK_METHOD(void, closeSocket, (ConnectionCloseType type));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (const));
  MOCK_METHOD(uint64_t, id, (), (const));
  MOCK_METHOD(bool, connecting, (), (const));
  MOCK_METHOD(void, write, (Buffer& data, bool end_stream));
  MOCK_METHOD(void, setBufferLimits, (uint32_t limit));
  MOCK_METHOD(uint32_t, bufferLimit, (), (const));
  MOCK_METHOD(bool, localAddressRestored, (), (const));
  MOCK_METHOD(bool, aboveHighWatermark, (), (const));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, localAddress, (), (const));
  MOCK_METHOD(void, setLocalAddress, (const Address::InstanceConstSharedPtr& local_address));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, directRemoteAddress, (), (const));
  MOCK_METHOD(const Address::InstanceConstSharedPtr&, remoteAddress, (), (const));
  MOCK_METHOD(void, setRemoteAddress, (const Address::InstanceConstSharedPtr& remote_address));
  MOCK_METHOD(bool, setSocketOption, (const Socket::OptionConstSharedPtr& option));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const));
  MOCK_METHOD(SslConnectionInfoConstSharedPtr, sslConn, (), (const));
  MOCK_METHOD(void, setSslConnection, (const SslConnectionPtr& ssl));
  MOCK_METHOD(SslConnectionPtr, takeSslConnection, ());
  MOCK_METHOD(State, state, (), (const));
  MOCK_METHOD(bool, connecting, (), (const));
  MOCK_METHOD(void, addConnectionCallbacks, (ConnectionCallbacks& cb));
  MOCK_METHOD(void, removeConnectionCallbacks, (ConnectionCallbacks& cb));
  MOCK_METHOD(void, addBytesSentCallback, (Connection::BytesSentCb cb));
  MOCK_METHOD(void, enableHalfClose, (bool enabled));
  MOCK_METHOD(bool, isHalfCloseEnabled, (), (const));
  MOCK_METHOD(void, setConnectionStats, (const ConnectionStats& stats));
  MOCK_METHOD(void, setDelayedCloseTimeout, (std::chrono::milliseconds timeout));
  MOCK_METHOD(std::chrono::milliseconds, delayedCloseTimeout, (), (const));
  MOCK_METHOD(void, noDelay, (bool enable));
  MOCK_METHOD(void, readDisable, (bool disable));
  MOCK_METHOD(void, detectEarlyCloseWhenReadDisabled, (bool value));
  MOCK_METHOD(bool, readEnabled, (), (const));
  MOCK_METHOD(const ConnectionInfoSetterSharedPtr&, connectionInfoSetter, (), (const));
  MOCK_METHOD(const ConnectionInfoProviderSharedPtr&, connectionInfoProvider, (), (const));
  MOCK_METHOD(void, setConnectionInfoProvider, (ConnectionInfoProviderSharedPtr provider));
  MOCK_METHOD(std::string, nextProtocol, (), (const));
  MOCK_METHOD(void, dumpState, (std::ostream& os, int indent_level), (const));
  MOCK_METHOD(void, hashKey, (std::vector<uint8_t>& hash), (const));
  MOCK_METHOD(void, setTransportSocketIsReadable, ());
  MOCK_METHOD(FilterManager&, filterManager, ());
  MOCK_METHOD(const FilterManager&, filterManager, (), (const));
  MOCK_METHOD(IoResult, doRead, (Buffer& buffer));
  MOCK_METHOD(IoResult, doWrite, (Buffer& buffer, bool end_stream));
  MOCK_METHOD(void, onConnected, ());
  MOCK_METHOD(TransportSocketPtr, createTransportSocket, (const TransportSocketOptionsConstSharedPtr&));
  MOCK_METHOD(TransportSocket&, transportSocket, (), (const));
  MOCK_METHOD(bool, hasDataToWrite, (), (const));
  MOCK_METHOD(const ConnectionStats&, connectionStats, (), (const));
  MOCK_METHOD(void, setImmediateCloseOnReadDisable, (bool value));
  MOCK_METHOD(void, configureInitialCongestionWindow, (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt));
  MOCK_METHOD(optional<std::chrono::milliseconds>, lastRoundTripTime, (), (const));
  MOCK_METHOD(optional<uint64_t>, congestionWindowInBytes, (), (const));
  MOCK_METHOD(void, rawWrite, (const Buffer::Instance& data, bool end_stream));
};

class MockReadFilter : public ReadFilter {
public:
  MOCK_METHOD(FilterStatus, onData, (Buffer& data, bool end_stream));
  MOCK_METHOD(FilterStatus, onNewConnection, ());
  MOCK_METHOD(void, initializeReadFilterCallbacks, (ReadFilterCallbacks& callbacks));
};

class MockWriteFilter : public WriteFilter {
public:
  MOCK_METHOD(FilterStatus, onWrite, (Buffer& data, bool end_stream));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (WriteFilterCallbacks& callbacks));
};

class MockFilter : public Filter {
public:
  MOCK_METHOD(FilterStatus, onData, (Buffer& data, bool end_stream));
  MOCK_METHOD(FilterStatus, onNewConnection, ());
  MOCK_METHOD(void, initializeReadFilterCallbacks, (ReadFilterCallbacks& callbacks));
  MOCK_METHOD(FilterStatus, onWrite, (Buffer& data, bool end_stream));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (WriteFilterCallbacks& callbacks));
};

// Test implementation of a simple filter
class TestFilter : public NetworkFilterBase {
public:
  FilterStatus onData(Buffer& data, bool end_stream) override {
    data_called_++;
    last_data_size_ = data.length();
    last_end_stream_ = end_stream;
    return data_return_status_;
  }

  FilterStatus onNewConnection() override {
    new_connection_called_++;
    return FilterStatus::Continue;
  }

  FilterStatus onWrite(Buffer& data, bool end_stream) override {
    write_called_++;
    last_write_size_ = data.length();
    last_write_end_stream_ = end_stream;
    return write_return_status_;
  }

  // Test helpers
  int data_called_ = 0;
  int new_connection_called_ = 0;
  int write_called_ = 0;
  size_t last_data_size_ = 0;
  size_t last_write_size_ = 0;
  bool last_end_stream_ = false;
  bool last_write_end_stream_ = false;
  FilterStatus data_return_status_ = FilterStatus::Continue;
  FilterStatus write_return_status_ = FilterStatus::Continue;
};

class FilterTest : public testing::Test {
protected:
  void SetUp() override {
    connection_ = std::make_unique<NiceMock<MockFilterManagerConnection>>();
    filter_manager_ = std::make_unique<FilterManagerImpl>(*connection_);
    
    read_buffer_ = std::make_unique<BufferImpl>();
    write_buffer_ = std::make_unique<BufferImpl>();
    
    ON_CALL(*connection_, readBuffer()).WillByDefault(testing::ReturnRef(*read_buffer_));
    ON_CALL(*connection_, writeBuffer()).WillByDefault(testing::ReturnRef(*write_buffer_));
  }

  std::unique_ptr<NiceMock<MockFilterManagerConnection>> connection_;
  std::unique_ptr<FilterManagerImpl> filter_manager_;
  std::unique_ptr<Buffer> read_buffer_;
  std::unique_ptr<Buffer> write_buffer_;
};

// Tests

TEST_F(FilterTest, AddAndInitializeFilters) {
  auto read_filter = std::make_shared<MockReadFilter>();
  auto write_filter = std::make_shared<MockWriteFilter>();
  auto bidirectional_filter = std::make_shared<MockFilter>();

  EXPECT_CALL(*read_filter, initializeReadFilterCallbacks(_));
  EXPECT_CALL(*write_filter, initializeWriteFilterCallbacks(_));
  EXPECT_CALL(*bidirectional_filter, initializeReadFilterCallbacks(_));
  EXPECT_CALL(*bidirectional_filter, initializeWriteFilterCallbacks(_));

  filter_manager_->addReadFilter(read_filter);
  filter_manager_->addWriteFilter(write_filter);
  filter_manager_->addFilter(bidirectional_filter);

  EXPECT_TRUE(filter_manager_->initializeReadFilters());
}

TEST_F(FilterTest, ReadFilterChain) {
  auto filter1 = std::make_shared<TestFilter>();
  auto filter2 = std::make_shared<TestFilter>();
  
  filter_manager_->addReadFilter(filter1);
  filter_manager_->addReadFilter(filter2);
  filter_manager_->initializeReadFilters();

  const std::string test_data = "Hello, World!";
  read_buffer_->add(test_data);
  
  ON_CALL(*connection_, readHalfClosed()).WillByDefault(Return(false));

  filter_manager_->onRead();

  EXPECT_EQ(filter1->data_called_, 1);
  EXPECT_EQ(filter2->data_called_, 1);
  EXPECT_EQ(filter1->last_data_size_, test_data.size());
  EXPECT_EQ(filter2->last_data_size_, test_data.size());
}

TEST_F(FilterTest, ReadFilterStopIteration) {
  auto filter1 = std::make_shared<TestFilter>();
  auto filter2 = std::make_shared<TestFilter>();
  
  filter1->data_return_status_ = FilterStatus::StopIteration;
  
  filter_manager_->addReadFilter(filter1);
  filter_manager_->addReadFilter(filter2);
  filter_manager_->initializeReadFilters();

  read_buffer_->add("test data");
  ON_CALL(*connection_, readHalfClosed()).WillByDefault(Return(false));

  filter_manager_->onRead();

  EXPECT_EQ(filter1->data_called_, 1);
  EXPECT_EQ(filter2->data_called_, 0); // Should not be called
}

TEST_F(FilterTest, WriteFilterChain) {
  auto filter1 = std::make_shared<TestFilter>();
  auto filter2 = std::make_shared<TestFilter>();
  
  filter_manager_->addWriteFilter(filter1);
  filter_manager_->addWriteFilter(filter2);
  filter_manager_->initializeReadFilters();

  const std::string test_data = "Response data";
  write_buffer_->add(test_data);

  filter_manager_->onWrite();

  EXPECT_EQ(filter1->write_called_, 1);
  EXPECT_EQ(filter2->write_called_, 1);
  EXPECT_EQ(filter1->last_write_size_, test_data.size());
  EXPECT_EQ(filter2->last_write_size_, test_data.size());
}

TEST_F(FilterTest, BidirectionalFilter) {
  auto filter = std::make_shared<TestFilter>();
  
  filter_manager_->addFilter(filter);
  filter_manager_->initializeReadFilters();

  // Test read path
  read_buffer_->add("read data");
  ON_CALL(*connection_, readHalfClosed()).WillByDefault(Return(false));
  filter_manager_->onRead();
  EXPECT_EQ(filter->data_called_, 1);

  // Test write path
  write_buffer_->add("write data");
  filter_manager_->onWrite();
  EXPECT_EQ(filter->write_called_, 1);
}

TEST_F(FilterTest, ConnectionEvents) {
  auto filter1 = std::make_shared<TestFilter>();
  auto filter2 = std::make_shared<TestFilter>();
  
  filter_manager_->addReadFilter(filter1);
  filter_manager_->addReadFilter(filter2);
  filter_manager_->initializeReadFilters();

  filter_manager_->onConnectionEvent(ConnectionEvent::Connected);

  EXPECT_EQ(filter1->new_connection_called_, 1);
  EXPECT_EQ(filter2->new_connection_called_, 1);
}

TEST_F(FilterTest, RemoveFilter) {
  auto filter1 = std::make_shared<TestFilter>();
  auto filter2 = std::make_shared<TestFilter>();
  
  filter_manager_->addReadFilter(filter1);
  filter_manager_->addReadFilter(filter2);
  filter_manager_->removeReadFilter(filter1);
  filter_manager_->initializeReadFilters();

  read_buffer_->add("test data");
  ON_CALL(*connection_, readHalfClosed()).WillByDefault(Return(false));
  filter_manager_->onRead();

  EXPECT_EQ(filter1->data_called_, 0);
  EXPECT_EQ(filter2->data_called_, 1);
}

TEST_F(FilterTest, EndStreamHandling) {
  auto filter = std::make_shared<TestFilter>();
  
  filter_manager_->addReadFilter(filter);
  filter_manager_->initializeReadFilters();

  read_buffer_->add("final data");
  ON_CALL(*connection_, readHalfClosed()).WillByDefault(Return(true));
  EXPECT_CALL(*connection_, close(ConnectionCloseType::FlushWrite));

  filter_manager_->onRead();

  EXPECT_TRUE(filter->last_end_stream_);
}

TEST_F(FilterTest, ContinueReading) {
  auto filter = std::make_shared<TestFilter>();
  filter->data_return_status_ = FilterStatus::StopIteration;
  
  filter_manager_->addReadFilter(filter);
  filter_manager_->initializeReadFilters();

  read_buffer_->add("part 1");
  ON_CALL(*connection_, readHalfClosed()).WillByDefault(Return(false));
  filter_manager_->onRead();
  EXPECT_EQ(filter->data_called_, 1);

  // Continue reading
  filter->data_return_status_ = FilterStatus::Continue;
  read_buffer_->add(" part 2");
  filter_manager_->continueReading();
  EXPECT_EQ(filter->data_called_, 2);
}

TEST_F(FilterTest, InjectWriteData) {
  auto filter = std::make_shared<TestFilter>();
  
  filter_manager_->addWriteFilter(filter);
  filter_manager_->initializeReadFilters();

  // Inject data
  BufferImpl injected_data;
  injected_data.add("injected");
  filter_manager_->injectWriteDataToFilterChain(injected_data, false);

  // Trigger write with regular data
  write_buffer_->add("regular");
  filter_manager_->onWrite();

  // Filter should see both injected and regular data
  EXPECT_EQ(filter->write_called_, 1);
  EXPECT_EQ(filter->last_write_size_, 15); // "injected" + "regular"
}

TEST_F(FilterTest, HighWatermark) {
  // Test high watermark detection
  EXPECT_FALSE(filter_manager_->aboveWriteBufferHighWatermark());
  
  // Add large amount of data
  std::string large_data(2 * 1024 * 1024, 'x'); // 2MB
  write_buffer_->add(large_data);
  
  EXPECT_TRUE(filter_manager_->aboveWriteBufferHighWatermark());
}

// Test FilterChainFactory
TEST(FilterChainFactoryTest, CreateFilterChain) {
  NiceMock<MockFilterManagerConnection> connection;
  FilterManagerImpl filter_manager(connection);
  FilterChainFactoryImpl factory;

  // Add factory functions
  factory.addFilterFactory([]() { return std::make_shared<TestFilter>(); });
  factory.addFilterFactory([]() { return std::make_shared<TestFilter>(); });

  EXPECT_TRUE(factory.createFilterChain(filter_manager));
}

TEST(FilterChainFactoryTest, CreateNetworkFilterChain) {
  NiceMock<MockFilterManagerConnection> connection;
  FilterManagerImpl filter_manager(connection);
  FilterChainFactoryImpl factory;

  std::vector<FilterFactoryCb> factories;
  factories.push_back([]() { return std::make_shared<TestFilter>(); });
  factories.push_back([]() { return std::make_shared<TestFilter>(); });

  EXPECT_TRUE(factory.createNetworkFilterChain(filter_manager, factories));
}

TEST(FilterChainFactoryTest, FailedFilterCreation) {
  NiceMock<MockFilterManagerConnection> connection;
  FilterManagerImpl filter_manager(connection);
  FilterChainFactoryImpl factory;

  std::vector<FilterFactoryCb> factories;
  factories.push_back([]() { return nullptr; }); // Factory returns null

  EXPECT_FALSE(factory.createNetworkFilterChain(filter_manager, factories));
}

// Test NetworkFilterBase
TEST(NetworkFilterBaseTest, CallbackDelegation) {
  class TestNetworkFilter : public NetworkFilterBase {
  public:
    FilterStatus onData(Buffer&, bool) override { return FilterStatus::Continue; }
    FilterStatus onNewConnection() override { return FilterStatus::Continue; }
    FilterStatus onWrite(Buffer&, bool) override { return FilterStatus::Continue; }
  };

  TestNetworkFilter filter;
  NiceMock<MockConnection> connection;
  NiceMock<MockFilterManagerConnection> filter_connection;
  FilterManagerImpl read_callbacks(filter_connection);
  FilterManagerImpl write_callbacks(filter_connection);

  filter.initializeReadFilterCallbacks(read_callbacks);
  filter.initializeWriteFilterCallbacks(write_callbacks);

  // Test delegation works
  EXPECT_NO_THROW(filter.connection());
  EXPECT_NO_THROW(filter.continueReading());
  EXPECT_NO_THROW(filter.upstreamHost());
  EXPECT_NO_THROW(filter.shouldContinueFilterChain());
  EXPECT_NO_THROW(filter.aboveWriteBufferHighWatermark());
}

} // namespace network
} // namespace mcp