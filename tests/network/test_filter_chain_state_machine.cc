/**
 * @file test_filter_chain_state_machine.cc
 * @brief Unit tests for FilterChainStateMachine using real I/O
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "mcp/network/filter_chain_state_machine.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/event/event_loop.h"
#include "mcp/buffer.h"

namespace mcp {
namespace network {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::NiceMock;

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
  
  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
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
      std::string suffix = "[/" + name_ + "]";
      data.add(suffix.data(), suffix.length());
    }
    
    return FilterStatus::Continue;
  }
  
  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
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
    // Create dispatcher using factory for thread safety
    dispatcher_ = event::createLibeventDispatcherFactory()->createDispatcher("test");
    
    // Create a test connection with real components
    auto address = std::make_shared<Address::Ipv4Instance>("127.0.0.1", 0);
    auto io_handle = std::make_unique<IoSocketHandleImpl>();
    auto socket = std::make_unique<ConnectionSocketImpl>(
        std::move(io_handle), nullptr, address);
    
    connection_ = std::make_unique<ConnectionImpl>(
        *dispatcher_,
        std::move(socket),
        nullptr,  // no transport socket
        false     // not connected yet
    );
    
    // Default configuration
    config_.mode = FilterChainMode::Bidirectional;
    config_.max_buffered_bytes = 1024 * 1024;
    config_.high_watermark = 768 * 1024;
    config_.low_watermark = 256 * 1024;
    config_.initialization_timeout = std::chrono::milliseconds(0);  // Disable for tests
    config_.drain_timeout = std::chrono::milliseconds(0);
    config_.idle_timeout = std::chrono::milliseconds(0);
    
    // Track state changes
    state_changes_.clear();
    config_.state_change_callback = 
        [this](FilterChainState from, FilterChainState to, const std::string& reason) {
          state_changes_.push_back({from, to, reason});
        };
    
    // Track errors
    errors_.clear();
    config_.error_callback = [this](const std::string& error) {
      errors_.push_back(error);
    };
  }
  
  void TearDown() override {
    state_machine_.reset();
    connection_.reset();
    if (dispatcher_) {
      dispatcher_->exit();
    }
    dispatcher_.reset();
  }
  
  void createStateMachine() {
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
  std::vector<std::string> errors_;
};

// ===== Basic State Tests =====

TEST_F(FilterChainStateMachineTest, InitialState) {
  createStateMachine();
  EXPECT_EQ(FilterChainState::Uninitialized, state_machine_->currentState());
  EXPECT_FALSE(state_machine_->isActive());
  EXPECT_FALSE(state_machine_->canProcessData());
  EXPECT_FALSE(state_machine_->isTerminal());
}

TEST_F(FilterChainStateMachineTest, Initialization) {
  createStateMachine();
  
  EXPECT_TRUE(state_machine_->initialize());
  EXPECT_EQ(FilterChainState::Configuring, state_machine_->currentState());
  
  // Should not be able to initialize twice
  EXPECT_FALSE(state_machine_->initialize());
}

TEST_F(FilterChainStateMachineTest, StartWithoutFilters) {
  createStateMachine();
  
  EXPECT_TRUE(state_machine_->initialize());
  EXPECT_TRUE(state_machine_->start());
  EXPECT_EQ(FilterChainState::Idle, state_machine_->currentState());
}

TEST_F(FilterChainStateMachineTest, StartWithFilters) {
  createStateMachine();
  
  auto read_filter = createReadFilter("test_read");
  auto write_filter = createWriteFilter("test_write");
  
  EXPECT_TRUE(state_machine_->initialize());
  EXPECT_TRUE(state_machine_->addReadFilter(read_filter, "read1"));
  EXPECT_TRUE(state_machine_->addWriteFilter(write_filter, "write1"));
  EXPECT_TRUE(state_machine_->start());
  
  EXPECT_EQ(FilterChainState::Idle, state_machine_->currentState());
  EXPECT_EQ(2, state_machine_->activeFilterCount());
}

// ===== Filter Management Tests =====

TEST_F(FilterChainStateMachineTest, AddRemoveFilters) {
  createStateMachine();
  state_machine_->initialize();
  
  auto read_filter = createReadFilter();
  auto write_filter = createWriteFilter();
  
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
  EXPECT_EQ(FilterStatus::Continue, status);
  EXPECT_GT(read_filter->dataReceived(), 0);
  EXPECT_EQ(1, read_filter->callCount());
  
  // Process upstream data
  OwnedBuffer write_data;
  write_data.add(test_data.data(), test_data.size());
  
  status = state_machine_->onWrite(write_data, false);
  EXPECT_EQ(FilterStatus::Continue, status);
  EXPECT_GT(write_filter->dataSent(), 0);
  EXPECT_EQ(1, write_filter->callCount());
}

TEST_F(FilterChainStateMachineTest, FilterStopIteration) {
  createStateMachine();
  
  auto read_filter = std::make_shared<TestReadFilter>();
  read_filter->stopIteration(true);
  
  state_machine_->initialize();
  state_machine_->addReadFilter(read_filter, "read1");
  state_machine_->start();
  
  // Process data - filter will stop iteration
  OwnedBuffer data;
  std::string test_data = "Test";
  data.add(test_data.data(), test_data.size());
  
  auto status = state_machine_->onData(data, false);
  EXPECT_EQ(FilterStatus::StopIteration, status);
  EXPECT_EQ(FilterChainState::StoppedIteration, state_machine_->currentState());
  
  // Check stats
  auto& stats = state_machine_->stats();
  EXPECT_EQ(1, stats.iterations_stopped);
}

// ===== Flow Control Tests =====

TEST_F(FilterChainStateMachineTest, PauseResume) {
  createStateMachine();
  state_machine_->initialize();
  state_machine_->start();
  
  // Should be idle
  EXPECT_EQ(FilterChainState::Idle, state_machine_->currentState());
  
  // Pause
  EXPECT_TRUE(state_machine_->pause());
  EXPECT_EQ(FilterChainState::Paused, state_machine_->currentState());
  EXPECT_FALSE(state_machine_->canProcessData());
  
  // Resume
  EXPECT_TRUE(state_machine_->resume());
  EXPECT_EQ(FilterChainState::Active, state_machine_->currentState());
  EXPECT_TRUE(state_machine_->canProcessData());
}

TEST_F(FilterChainStateMachineTest, BufferingWithWatermarks) {
  config_.max_buffered_bytes = 1024;
  config_.high_watermark = 768;
  config_.low_watermark = 256;
  
  createStateMachine();
  state_machine_->initialize();
  state_machine_->start();
  state_machine_->pause();
  
  // Buffer data while paused
  OwnedBuffer data;
  std::string large_data(512, 'X');
  data.add(large_data.data(), large_data.size());
  
  // First chunk should buffer
  auto status = state_machine_->onData(data, false);
  EXPECT_EQ(FilterStatus::StopIteration, status);
  EXPECT_EQ(512, state_machine_->bufferedBytes());
  
  // Add more to exceed high watermark
  data.add(large_data.data(), large_data.size());
  status = state_machine_->onData(data, false);
  
  // Should trigger high watermark
  bool found_watermark = false;
  for (const auto& change : state_changes_) {
    if (change.to == FilterChainState::AboveHighWatermark) {
      found_watermark = true;
      break;
    }
  }
  EXPECT_TRUE(found_watermark);
}

// ===== Shutdown Tests =====

TEST_F(FilterChainStateMachineTest, GracefulClose) {
  createStateMachine();
  state_machine_->initialize();
  state_machine_->start();
  
  EXPECT_TRUE(state_machine_->close());
  EXPECT_EQ(FilterChainState::Closing, state_machine_->currentState());
  
  // Should not be able to close again
  EXPECT_FALSE(state_machine_->close());
}

TEST_F(FilterChainStateMachineTest, AbortClose) {
  createStateMachine();
  state_machine_->initialize();
  state_machine_->start();
  
  EXPECT_TRUE(state_machine_->abort());
  EXPECT_EQ(FilterChainState::Aborting, state_machine_->currentState());
}

// ===== Error Handling Tests =====

TEST_F(FilterChainStateMachineTest, FilterErrorWithContinue) {
  config_.continue_on_filter_error = true;
  
  createStateMachine();
  state_machine_->initialize();
  state_machine_->start();
  
  // Simulate filter error
  state_machine_->handleEvent(FilterChainEvent::FilterError);
  
  // Should attempt recovery
  bool found_recovery = false;
  for (const auto& change : state_changes_) {
    if (change.to == FilterChainState::Recovering) {
      found_recovery = true;
      break;
    }
  }
  EXPECT_TRUE(found_recovery);
}

TEST_F(FilterChainStateMachineTest, FilterErrorWithoutContinue) {
  config_.continue_on_filter_error = false;
  
  createStateMachine();
  state_machine_->initialize();
  state_machine_->start();
  
  // This would normally be triggered by a filter error
  // For testing, we'd need to create a filter that throws
}

// ===== State Pattern Tests =====

TEST_F(FilterChainStateMachineTest, StatePatterns) {
  // Test canProcessData
  EXPECT_TRUE(FilterChainStatePatterns::canProcessData(FilterChainState::Active));
  EXPECT_TRUE(FilterChainStatePatterns::canProcessData(FilterChainState::ProcessingUpstream));
  EXPECT_FALSE(FilterChainStatePatterns::canProcessData(FilterChainState::Paused));
  EXPECT_FALSE(FilterChainStatePatterns::canProcessData(FilterChainState::Closed));
  
  // Test isFlowControlled
  EXPECT_TRUE(FilterChainStatePatterns::isFlowControlled(FilterChainState::Paused));
  EXPECT_TRUE(FilterChainStatePatterns::isFlowControlled(FilterChainState::Buffering));
  EXPECT_FALSE(FilterChainStatePatterns::isFlowControlled(FilterChainState::Active));
  
  // Test isErrorState
  EXPECT_TRUE(FilterChainStatePatterns::isErrorState(FilterChainState::FilterError));
  EXPECT_TRUE(FilterChainStatePatterns::isErrorState(FilterChainState::Failed));
  EXPECT_FALSE(FilterChainStatePatterns::isErrorState(FilterChainState::Active));
  
  // Test isTerminal
  EXPECT_TRUE(FilterChainStatePatterns::isTerminal(FilterChainState::Closed));
  EXPECT_TRUE(FilterChainStatePatterns::isTerminal(FilterChainState::Failed));
  EXPECT_FALSE(FilterChainStatePatterns::isTerminal(FilterChainState::Active));
  
  // Test isIterating
  EXPECT_TRUE(FilterChainStatePatterns::isIterating(FilterChainState::IteratingRead));
  EXPECT_TRUE(FilterChainStatePatterns::isIterating(FilterChainState::StoppedIteration));
  EXPECT_FALSE(FilterChainStatePatterns::isIterating(FilterChainState::Active));
}

// ===== Builder Pattern Tests =====

TEST_F(FilterChainStateMachineTest, FilterChainBuilder) {
  bool state_changed = false;
  bool error_occurred = false;
  
  auto state_machine = FilterChainBuilder()
      .withMode(FilterChainMode::Bidirectional)
      .withBufferLimits(2048)
      .withWatermarks(1536, 512)
      .withInitTimeout(std::chrono::milliseconds(0))
      .continueOnError(true)
      .withStrictOrdering(true)
      .withStats(true)
      .withStateChangeCallback(
          [&state_changed](FilterChainState from, FilterChainState to, const std::string&) {
            state_changed = true;
          })
      .withErrorCallback([&error_occurred](const std::string&) {
        error_occurred = true;
      })
      .addReadFilter("test_read", []() -> FilterSharedPtr {
        return std::make_shared<TestReadFilter>();
      })
      .addWriteFilter("test_write", []() -> FilterSharedPtr {
        return std::make_shared<TestWriteFilter>();
      })
      .build(*dispatcher_, *connection_);
  
  EXPECT_NE(nullptr, state_machine);
  EXPECT_EQ(FilterChainState::Uninitialized, state_machine->currentState());
  
  // Initialize and verify callback
  state_machine->initialize();
  EXPECT_TRUE(state_changed);
}

// ===== Multiple Filter Chain Test =====

TEST_F(FilterChainStateMachineTest, MultipleFiltersInChain) {
  createStateMachine();
  
  // Create multiple filters
  auto read1 = std::make_shared<TestReadFilter>("read1");
  auto read2 = std::make_shared<TestReadFilter>("read2");
  auto read3 = std::make_shared<TestReadFilter>("read3");
  
  read1->modifyData(true);
  read2->modifyData(true);
  read3->modifyData(true);
  
  state_machine_->initialize();
  state_machine_->addReadFilter(read1, "read1");
  state_machine_->addReadFilter(read2, "read2");
  state_machine_->addReadFilter(read3, "read3");
  state_machine_->start();
  
  // Process data through all filters
  OwnedBuffer data;
  std::string test_data = "Test";
  data.add(test_data.data(), test_data.size());
  
  auto status = state_machine_->onData(data, false);
  EXPECT_EQ(FilterStatus::Continue, status);
  
  // All filters should have processed data
  EXPECT_GT(read1->dataReceived(), 0);
  EXPECT_GT(read2->dataReceived(), 0);
  EXPECT_GT(read3->dataReceived(), 0);
}

// ===== Real I/O Integration Test =====

TEST_F(FilterChainStateMachineTest, RealIoWithConnection) {
  createStateMachine();
  
  // Create filters that actually modify data
  auto read_filter = std::make_shared<TestReadFilter>();
  auto write_filter = std::make_shared<TestWriteFilter>();
  
  read_filter->modifyData(true);
  write_filter->modifyData(true);
  
  state_machine_->initialize();
  state_machine_->addReadFilter(read_filter, "read_processor");
  state_machine_->addWriteFilter(write_filter, "write_processor");
  state_machine_->start();
  
  // Simulate real data flow
  for (int i = 0; i < 5; ++i) {
    OwnedBuffer data;
    std::string chunk = "Chunk" + std::to_string(i);
    data.add(chunk.data(), chunk.size());
    
    // Process downstream
    auto status = state_machine_->onData(data, false);
    EXPECT_EQ(FilterStatus::Continue, status);
    
    // Process upstream
    OwnedBuffer response;
    std::string resp = "Response" + std::to_string(i);
    response.add(resp.data(), resp.size());
    
    status = state_machine_->onWrite(response, false);
    EXPECT_EQ(FilterStatus::Continue, status);
  }
  
  // Verify processing
  EXPECT_EQ(5, read_filter->callCount());
  EXPECT_EQ(5, write_filter->callCount());
  
  // Check statistics
  auto& stats = state_machine_->stats();
  EXPECT_GT(stats.bytes_processed_downstream, 0);
  EXPECT_GT(stats.bytes_processed_upstream, 0);
  EXPECT_EQ(2, stats.filters_initialized);
  EXPECT_EQ(0, stats.filters_failed);
}

}  // namespace
}  // namespace network
}  // namespace mcp