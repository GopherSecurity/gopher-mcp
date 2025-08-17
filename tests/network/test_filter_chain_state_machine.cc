/**
 * @file test_filter_chain_state_machine.cc
 * @brief Unit tests for FilterChainStateMachine using real I/O
 */

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/filter_chain_state_machine.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/socket_impl.h"

namespace mcp {
namespace network {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

// ===== Test Filters =====

/**
 * Simple pass-through read filter for testing
 * Implements full Filter interface for compatibility
 */
class TestReadFilter : public Filter {
 public:
  explicit TestReadFilter(const std::string& name = "test_read")
      : name_(name) {}

  // ReadFilter interface
  FilterStatus onData(Buffer& data, bool end_stream) override {
    data_received_ += data.length();
    calls_++;

    if (stop_iteration_) {
      return FilterStatus::StopIteration;
    }

    // Modify data to show filter was applied
    if (modify_data_) {
      std::string prefix = "[" + name_ + "]";
      data.prepend(prefix.data(), prefix.length());
    }

    return FilterStatus::Continue;
  }

  FilterStatus onNewConnection() override {
    connected_ = true;
    return FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // WriteFilter interface (pass-through)
  FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return FilterStatus::Continue;
  }

  void initializeWriteFilterCallbacks(
      WriteFilterCallbacks& callbacks) override {
    // Not used
  }

  // Test helpers
  void stopIteration(bool stop) { stop_iteration_ = stop; }
  void modifyData(bool modify) { modify_data_ = modify; }
  size_t dataReceived() const { return data_received_; }
  size_t callCount() const { return calls_; }
  bool isConnected() const { return connected_; }

 private:
  std::string name_;
  ReadFilterCallbacks* callbacks_ = nullptr;
  bool stop_iteration_ = false;
  bool modify_data_ = false;
  bool connected_ = false;
  size_t data_received_ = 0;
  size_t calls_ = 0;
};

/**
 * Simple pass-through write filter for testing
 * Implements full Filter interface for compatibility
 */
class TestWriteFilter : public Filter {
 public:
  explicit TestWriteFilter(const std::string& name = "test_write")
      : name_(name) {}

  // ReadFilter interface (pass-through)
  FilterStatus onData(Buffer& data, bool end_stream) override {
    return FilterStatus::Continue;
  }

  FilterStatus onNewConnection() override {
    return FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    // Not used
  }

  // WriteFilter interface
  FilterStatus onWrite(Buffer& data, bool end_stream) override {
    data_sent_ += data.length();
    calls_++;

    if (stop_iteration_) {
      return FilterStatus::StopIteration;
    }

    // Modify data to show filter was applied
    if (modify_data_) {
      std::string suffix = "[" + name_ + "]";
      data.add(suffix.data(), suffix.length());
    }

    return FilterStatus::Continue;
  }

  void initializeWriteFilterCallbacks(
      WriteFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Test helpers
  void stopIteration(bool stop) { stop_iteration_ = stop; }
  void modifyData(bool modify) { modify_data_ = modify; }
  size_t dataSent() const { return data_sent_; }
  size_t callCount() const { return calls_; }

 private:
  std::string name_;
  WriteFilterCallbacks* callbacks_ = nullptr;
  bool stop_iteration_ = false;
  bool modify_data_ = false;
  size_t data_sent_ = 0;
  size_t calls_ = 0;
};

// ===== Test Fixture =====

class FilterChainStateMachineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Default configuration
    config_.mode = FilterChainMode::Bidirectional;
    config_.max_buffered_bytes = 1024 * 1024;
    config_.high_watermark = 768 * 1024;
    config_.low_watermark = 256 * 1024;
    config_.initialization_timeout =
        std::chrono::milliseconds(0);  // Disable for tests
    config_.drain_timeout = std::chrono::milliseconds(0);
    config_.idle_timeout = std::chrono::milliseconds(0);
    // Track state changes
    state_changes_.clear();
  }

  void TearDown() override {
    // Clean up in proper order
    state_machine_.reset();
    connection_.reset();
    dispatcher_.reset();
  }
  
  void createStateMachine() {
    // Create dispatcher and connection for this test
    dispatcher_ =
        event::createLibeventDispatcherFactory()->createDispatcher("test");
    
    // Create a test connection with real components
    auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
    auto io_handle = std::make_unique<IoSocketHandleImpl>();
    auto socket = std::make_unique<ConnectionSocketImpl>(std::move(io_handle),
                                                         nullptr, address);
    connection_ =
        std::make_unique<ConnectionImpl>(*dispatcher_, std::move(socket),
                                         nullptr,  // no transport socket
                                         false     // not connected yet
        );
    
    // Create state machine directly - it's thread-safe
    state_machine_ = std::make_unique<FilterChainStateMachine>(
        *dispatcher_, *connection_, config_);
  }

  // Helper to create test filters
  ReadFilterSharedPtr createReadFilter(const std::string& name = "read") {
    return std::make_shared<TestReadFilter>(name);
  }

  WriteFilterSharedPtr createWriteFilter(const std::string& name = "write") {
    return std::make_shared<TestWriteFilter>(name);
  }
  

 protected:
  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<Connection> connection_;
  FilterChainConfig config_;
  std::unique_ptr<FilterChainStateMachine> state_machine_;

  // State tracking
  struct StateChange {
    FilterChainState from;
    FilterChainState to;
    std::string reason;
  };
  std::vector<StateChange> state_changes_;
};

// ===== State Tests =====

TEST_F(FilterChainStateMachineTest, InitialState) {
  createStateMachine();

  EXPECT_EQ(FilterChainState::Uninitialized, state_machine_->currentState());
  EXPECT_EQ(0, state_machine_->activeFilterCount());
  EXPECT_FALSE(state_machine_->isActive());
  EXPECT_FALSE(state_machine_->isTerminal());
}

TEST_F(FilterChainStateMachineTest, Initialization) {
  createStateMachine();

  bool result = state_machine_->initialize();
  EXPECT_TRUE(result);
  EXPECT_EQ(FilterChainState::Configuring, state_machine_->currentState());

  // Should not be able to initialize twice
  result = state_machine_->initialize();
  EXPECT_FALSE(result);
}

TEST_F(FilterChainStateMachineTest, StartWithoutFilters) {
  createStateMachine();

  EXPECT_TRUE(state_machine_->initialize());
  EXPECT_TRUE(state_machine_->start());
  EXPECT_EQ(FilterChainState::Active, state_machine_->currentState());
}

TEST_F(FilterChainStateMachineTest, StartWithFilters) {
  createStateMachine();

  auto read_filter = createReadFilter("test_read");
  auto write_filter = createWriteFilter("test_write");

  EXPECT_TRUE(state_machine_->initialize());
  EXPECT_TRUE(state_machine_->addReadFilter(read_filter, "read1"));
  EXPECT_TRUE(state_machine_->addWriteFilter(write_filter, "write1"));
  EXPECT_TRUE(state_machine_->start());

  EXPECT_EQ(FilterChainState::Active, state_machine_->currentState());
  EXPECT_EQ(2, state_machine_->activeFilterCount());
}

// ===== Filter Management Tests =====

TEST_F(FilterChainStateMachineTest, AddRemoveFilters) {
  createStateMachine();
  
  auto read_filter = createReadFilter();
  auto write_filter = createWriteFilter();

  state_machine_->initialize();
  
  // Add filters
  EXPECT_TRUE(state_machine_->addReadFilter(read_filter, "read1"));
  EXPECT_TRUE(state_machine_->addWriteFilter(write_filter, "write1"));
  EXPECT_EQ(2, state_machine_->activeFilterCount());

  // Cannot add duplicate names
  EXPECT_FALSE(state_machine_->addReadFilter(read_filter, "read1"));

  // Remove filter
  EXPECT_TRUE(state_machine_->removeFilter("read1"));
  EXPECT_EQ(1, state_machine_->activeFilterCount());

  // Cannot remove non-existent filter
  EXPECT_FALSE(state_machine_->removeFilter("nonexistent"));
}

// ===== Data Flow Tests =====

TEST_F(FilterChainStateMachineTest, DataProcessingThroughFilters) {
  createStateMachine();

  auto read_filter = std::make_shared<TestReadFilter>();
  auto write_filter = std::make_shared<TestWriteFilter>();

  read_filter->modifyData(true);
  write_filter->modifyData(true);

  state_machine_->initialize();
  state_machine_->addReadFilter(read_filter, "read1");
  state_machine_->addWriteFilter(write_filter, "write1");
  state_machine_->start();

  // Process downstream data
  OwnedBuffer read_data;
  std::string test_data = "Hello";
  read_data.add(test_data.data(), test_data.size());

  auto status = state_machine_->onData(read_data, false);
  EXPECT_EQ(status, FilterStatus::Continue);
  EXPECT_GT(read_filter->dataReceived(), 0);
  EXPECT_EQ(read_filter->callCount(), 1);
}

TEST_F(FilterChainStateMachineTest, FilterStopIteration) {
  createStateMachine();

  auto read_filter = std::make_shared<TestReadFilter>();
  read_filter->stopIteration(true);

  state_machine_->initialize();
  state_machine_->addReadFilter(read_filter, "read1");
  state_machine_->start();

  // Process data - should stop at filter
  OwnedBuffer data;
  std::string test_data = "Test";
  data.add(test_data.data(), test_data.size());

  auto status = state_machine_->onData(data, false);
  EXPECT_EQ(status, FilterStatus::StopIteration);
  EXPECT_EQ(state_machine_->currentState(), FilterChainState::StoppedIteration);
}

// ===== Flow Control Tests =====

TEST_F(FilterChainStateMachineTest, PauseResume) {
  createStateMachine();

  state_machine_->initialize();
  state_machine_->start();

  // Pause the chain
  EXPECT_TRUE(state_machine_->pause());
  EXPECT_EQ(FilterChainState::Paused, state_machine_->currentState());

  // Cannot pause when already paused
  EXPECT_FALSE(state_machine_->pause());

  // Resume the chain
  EXPECT_TRUE(state_machine_->resume());
  EXPECT_EQ(FilterChainState::Active, state_machine_->currentState());
}

TEST_F(FilterChainStateMachineTest, WatermarkFlowControl) {
  config_.high_watermark = 100;
  config_.low_watermark = 50;
  createStateMachine();

  state_machine_->initialize();
  state_machine_->start();
  
  // Pause the filter chain to enable buffering (watermarks only apply to buffered data)
  state_machine_->pause();
  EXPECT_EQ(FilterChainState::Paused, state_machine_->currentState());

  // Add data to trigger high watermark
  OwnedBuffer data1;
  std::string large_data(60, 'x');
  data1.add(large_data.data(), large_data.size());

  // When paused, onData buffers the data and returns StopIteration (successful buffering)
  auto status = state_machine_->onData(data1, false);
  EXPECT_EQ(status, FilterStatus::StopIteration);
  
  // Should transition to Buffering state after first data
  EXPECT_EQ(FilterChainState::Buffering, state_machine_->currentState());

  // Add more data to exceed high watermark
  OwnedBuffer data2;
  data2.add(large_data.data(), large_data.size());

  status = state_machine_->onData(data2, false);
  EXPECT_EQ(status, FilterStatus::StopIteration);

  // Should be above high watermark now (60 + 60 = 120 > 100)
  EXPECT_EQ(FilterChainState::AboveHighWatermark, state_machine_->currentState());
}

// ===== Error Handling Tests =====

TEST_F(FilterChainStateMachineTest, HandleFilterError) {
  createStateMachine();

  // Create a filter that stops iteration to simulate an error condition
  class StopFilter : public Filter {
   public:
    FilterStatus onData(Buffer&, bool) override {
      // Stop iteration simulates a filter issue
      return FilterStatus::StopIteration;
    }
    FilterStatus onNewConnection() override {
      return FilterStatus::Continue;
    }
    void initializeReadFilterCallbacks(ReadFilterCallbacks&) override {}
    FilterStatus onWrite(Buffer&, bool) override {
      return FilterStatus::Continue;
    }
    void initializeWriteFilterCallbacks(WriteFilterCallbacks&) override {}
  };

  auto stop_filter = std::make_shared<StopFilter>();

  state_machine_->initialize();
  state_machine_->addReadFilter(stop_filter, "stop");
  state_machine_->start();

  // Process data - should trigger stop
  OwnedBuffer data;
  std::string test_data = "Test";
  data.add(test_data.data(), test_data.size());

  auto status = state_machine_->onData(data, false);
  EXPECT_EQ(status, FilterStatus::StopIteration);
  // The state machine should be in StoppedIteration state
  EXPECT_EQ(state_machine_->currentState(), FilterChainState::StoppedIteration);
}

// ===== Shutdown Tests =====

TEST_F(FilterChainStateMachineTest, GracefulClose) {
  createStateMachine();

  state_machine_->initialize();
  state_machine_->start();

  // Close gracefully
  EXPECT_TRUE(state_machine_->close());
  EXPECT_EQ(FilterChainState::Closing, state_machine_->currentState());
}

TEST_F(FilterChainStateMachineTest, AbortImmediate) {
  createStateMachine();

  state_machine_->initialize();
  state_machine_->start();

  // Abort immediately
  EXPECT_TRUE(state_machine_->abort());
  EXPECT_EQ(FilterChainState::Aborting, state_machine_->currentState());
}

// ===== State Query Tests =====

TEST_F(FilterChainStateMachineTest, StateQueries) {
  createStateMachine();

  state_machine_->initialize();

  EXPECT_FALSE(state_machine_->isActive());
  EXPECT_FALSE(state_machine_->isTerminal());

  state_machine_->start();
  EXPECT_TRUE(state_machine_->isActive());

  state_machine_->pause();
  EXPECT_FALSE(state_machine_->isActive());
  EXPECT_EQ(state_machine_->currentState(), FilterChainState::Paused);

  state_machine_->close();
  EXPECT_FALSE(state_machine_->isActive());
}

}  // namespace
}  // namespace network
}  // namespace mcp
