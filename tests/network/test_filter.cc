#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mcp/network/filter.h"
#include "mcp/buffer.h"
#include <memory>

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::NiceMock;

namespace mcp {
namespace network {

// Mock Classes

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

// Test FilterChainFactory
TEST(FilterChainFactoryTest, CreateFilterChain) {
  FilterChainFactoryImpl factory;

  // Add factory functions
  factory.addFilterFactory([]() { return std::make_shared<TestFilter>(); });
  factory.addFilterFactory([]() { return std::make_shared<TestFilter>(); });

  // Create a minimal filter manager for testing
  class TestFilterManager : public FilterManager {
  public:
    void addReadFilter(ReadFilterSharedPtr) override {}
    void addWriteFilter(WriteFilterSharedPtr) override {}
    void addFilter(FilterSharedPtr) override {}
    bool initializeReadFilters() override { return true; }
    void onRead() override {}
    void onWrite() override {}
    void onConnectionEvent(ConnectionEvent) override {}
    void removeReadFilter(ReadFilterSharedPtr) override {}
  };

  TestFilterManager manager;
  EXPECT_TRUE(factory.createFilterChain(manager));
}

TEST(FilterChainFactoryTest, CreateNetworkFilterChain) {
  FilterChainFactoryImpl factory;

  std::vector<FilterFactoryCb> factories;
  factories.push_back([]() { return std::make_shared<TestFilter>(); });
  factories.push_back([]() { return std::make_shared<TestFilter>(); });

  class TestFilterManager : public FilterManager {
  public:
    void addReadFilter(ReadFilterSharedPtr) override {}
    void addWriteFilter(WriteFilterSharedPtr) override {}
    void addFilter(FilterSharedPtr) override {}
    bool initializeReadFilters() override { return true; }
    void onRead() override {}
    void onWrite() override {}
    void onConnectionEvent(ConnectionEvent) override {}
    void removeReadFilter(ReadFilterSharedPtr) override {}
  };

  TestFilterManager manager;
  EXPECT_TRUE(factory.createNetworkFilterChain(manager, factories));
}

TEST(FilterChainFactoryTest, FailedFilterCreation) {
  FilterChainFactoryImpl factory;

  std::vector<FilterFactoryCb> factories;
  factories.push_back([]() { return nullptr; }); // Factory returns null

  class TestFilterManager : public FilterManager {
  public:
    void addReadFilter(ReadFilterSharedPtr) override {}
    void addWriteFilter(WriteFilterSharedPtr) override {}
    void addFilter(FilterSharedPtr) override {}
    bool initializeReadFilters() override { return true; }
    void onRead() override {}
    void onWrite() override {}
    void onConnectionEvent(ConnectionEvent) override {}
    void removeReadFilter(ReadFilterSharedPtr) override {}
  };

  TestFilterManager manager;
  EXPECT_FALSE(factory.createNetworkFilterChain(manager, factories));
}

// Test NetworkFilterBase
TEST(NetworkFilterBaseTest, BasicUsage) {
  TestFilter filter;
  
  // Test data callback
  OwnedBuffer buffer;
  buffer.add("test data");
  
  EXPECT_EQ(FilterStatus::Continue, filter.onData(buffer, false));
  EXPECT_EQ(1, filter.data_called_);
  EXPECT_EQ(9, filter.last_data_size_);
  EXPECT_FALSE(filter.last_end_stream_);
  
  // Test write callback
  OwnedBuffer write_buffer;
  write_buffer.add("write data");
  
  EXPECT_EQ(FilterStatus::Continue, filter.onWrite(write_buffer, true));
  EXPECT_EQ(1, filter.write_called_);
  EXPECT_EQ(10, filter.last_write_size_);
  EXPECT_TRUE(filter.last_write_end_stream_);
  
  // Test new connection
  EXPECT_EQ(FilterStatus::Continue, filter.onNewConnection());
  EXPECT_EQ(1, filter.new_connection_called_);
}

TEST(NetworkFilterBaseTest, StopIteration) {
  TestFilter filter;
  filter.data_return_status_ = FilterStatus::StopIteration;
  
  OwnedBuffer buffer;
  buffer.add("test");
  
  EXPECT_EQ(FilterStatus::StopIteration, filter.onData(buffer, false));
}

} // namespace network
} // namespace mcp