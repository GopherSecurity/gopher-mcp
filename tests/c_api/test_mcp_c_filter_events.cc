/**
 * @file test_mcp_c_filter_events.cc
 * @brief Unit tests for MCP Filter Events C API
 *
 * Tests chain-level event callback registration and event emission
 * through the C API layer.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_filter_chain.h"
#include "mcp/c_api/mcp_c_filter_events.h"

// Include C++ headers for event emission in tests
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_event.h"
#include "mcp/filter/filter_event_emitter.h"
#include "mcp/json/json_bridge.h"

// Forward declare internal getter for accessing event hub
namespace mcp {
namespace filter_chain {
class AdvancedFilterChain;
namespace internal {
std::shared_ptr<filter::FilterChainEventHub> getEventHub(AdvancedFilterChain& chain);
}
}
}

namespace {

// ============================================================================
// RAII Wrappers for Resource Management
// ============================================================================

class DispatcherGuard {
 public:
  explicit DispatcherGuard(mcp_dispatcher_t dispatcher = 0)
      : dispatcher_(dispatcher) {}

  ~DispatcherGuard() {
    if (dispatcher_) {
      mcp_dispatcher_destroy(dispatcher_);
    }
  }

  DispatcherGuard(DispatcherGuard&& other) noexcept
      : dispatcher_(other.dispatcher_) {
    other.dispatcher_ = 0;
  }

  DispatcherGuard& operator=(DispatcherGuard&& other) noexcept {
    if (this != &other) {
      if (dispatcher_) {
        mcp_dispatcher_destroy(dispatcher_);
      }
      dispatcher_ = other.dispatcher_;
      other.dispatcher_ = 0;
    }
    return *this;
  }

  DispatcherGuard(const DispatcherGuard&) = delete;
  DispatcherGuard& operator=(const DispatcherGuard&) = delete;

  mcp_dispatcher_t get() const { return dispatcher_; }
  operator mcp_dispatcher_t() const { return dispatcher_; }

 private:
  mcp_dispatcher_t dispatcher_;
};

class FilterChainGuard {
 public:
  explicit FilterChainGuard(mcp_filter_chain_t chain = 0) : chain_(chain) {}

  ~FilterChainGuard() {
    if (chain_) {
      mcp_filter_chain_release(chain_);
    }
  }

  FilterChainGuard(FilterChainGuard&& other) noexcept : chain_(other.chain_) {
    other.chain_ = 0;
  }

  FilterChainGuard& operator=(FilterChainGuard&& other) noexcept {
    if (this != &other) {
      if (chain_) {
        mcp_filter_chain_release(chain_);
      }
      chain_ = other.chain_;
      other.chain_ = 0;
    }
    return *this;
  }

  FilterChainGuard(const FilterChainGuard&) = delete;
  FilterChainGuard& operator=(const FilterChainGuard&) = delete;

  mcp_filter_chain_t get() const { return chain_; }
  operator mcp_filter_chain_t() const { return chain_; }

 private:
  mcp_filter_chain_t chain_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class FilterEventsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dispatcher_ = DispatcherGuard(mcp_dispatcher_create());
    ASSERT_NE(dispatcher_.get(), static_cast<mcp_dispatcher_t>(0));
  }

  void TearDown() override {
    // Clean up chains
    chains_.clear();
    // Clean up dispatcher
    dispatcher_ = DispatcherGuard(0);
  }

  // Helper to create a filter chain from JSON
  mcp_filter_chain_t createFilterChain(const char* name = "test_chain") {
    // Create minimal JSON configuration for filter chain
    std::string json_str = R"({"filters": []})";

    // Parse JSON string to JSON handle
    auto json_config = mcp_json_parse(json_str.c_str());
    if (!json_config) {
      return 0;
    }

    // Create chain from JSON
    auto chain = mcp_chain_create_from_json(dispatcher_.get(), json_config);

    // Free JSON config
    mcp_json_free(json_config);

    if (chain) {
      chains_.emplace_back(chain);
    }
    return chain;
  }

  DispatcherGuard dispatcher_;
  std::vector<FilterChainGuard> chains_;
};

// ============================================================================
// Event Callback Test State
// ============================================================================

struct EventCallbackState {
  std::atomic<int> event_count{0};
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<mcp_filter_event_type_t> event_types;
  std::vector<mcp_filter_event_severity_t> severities;
  std::vector<std::string> filter_names;
  std::vector<std::string> filter_instance_ids;
  std::vector<std::string> event_data_json;
  std::vector<int64_t> timestamps;

  // Context storage
  struct ContextData {
    std::string chain_id;
    std::string stream_id;
    std::string correlation_id;
  };
  std::vector<ContextData> contexts;

  // Helper to wait for a specific number of events with timeout
  bool waitForEventCount(int expected_count, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [&]() {
      return event_count.load() >= expected_count;
    });
  }

  // Helper to reset state
  void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    event_count = 0;
    event_types.clear();
    severities.clear();
    filter_names.clear();
    filter_instance_ids.clear();
    event_data_json.clear();
    timestamps.clear();
    contexts.clear();
  }
};

// C callback function
void eventCallback(const char* filter_name,
                   const char* filter_instance_id,
                   mcp_filter_event_type_t event_type,
                   mcp_filter_event_severity_t severity,
                   const char* event_data_json,
                   const mcp_filter_event_context_t* context,
                   int64_t timestamp_ms,
                   void* user_data) {
  auto* state = static_cast<EventCallbackState*>(user_data);
  if (!state) return;

  std::lock_guard<std::mutex> lock(state->mutex);
  state->event_count++;
  state->event_types.push_back(event_type);
  state->severities.push_back(severity);

  if (filter_name) {
    state->filter_names.push_back(filter_name);
  } else {
    state->filter_names.push_back("");
  }

  if (filter_instance_id) {
    state->filter_instance_ids.push_back(filter_instance_id);
  } else {
    state->filter_instance_ids.push_back("");
  }

  if (event_data_json) {
    state->event_data_json.push_back(event_data_json);
  } else {
    state->event_data_json.push_back("");
  }

  state->timestamps.push_back(timestamp_ms);

  // Capture context
  EventCallbackState::ContextData ctx;
  if (context) {
    if (context->chain_id) ctx.chain_id = context->chain_id;
    if (context->stream_id) ctx.stream_id = context->stream_id;
    if (context->correlation_id) ctx.correlation_id = context->correlation_id;
  }
  state->contexts.push_back(ctx);

  state->cv.notify_all();
}

// ============================================================================
// Test Helper to Emit Events
// ============================================================================

// Helper function to emit a test event directly to event hub
// This simulates what filters do when they emit events
void emitTestEvent(std::shared_ptr<mcp::filter::FilterChainEventHub> hub,
                   const std::string& filter_name,
                   const std::string& filter_instance_id,
                   mcp::filter::FilterEventType event_type,
                   mcp::filter::FilterEventSeverity severity,
                   const mcp::json::JsonValue& event_data,
                   const std::string& chain_id = "",
                   const std::string& stream_id = "",
                   const std::string& correlation_id = "") {
  if (!hub) return;

  mcp::filter::FilterEvent event;
  event.filter_name = filter_name;
  event.filter_instance_id = filter_instance_id;
  event.event_type = event_type;
  event.severity = severity;
  event.event_data = event_data;
  event.context.chain_id = chain_id;
  event.context.stream_id = stream_id;
  event.context.correlation_id = correlation_id;

  hub->emit(event);
}

// ============================================================================
// Tests
// ============================================================================

TEST_F(FilterEventsTest, SetAndClearEventCallback) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;

  // Set callback
  int result = mcp_filter_chain_set_event_callback(
      chain, eventCallback, &state);
  EXPECT_EQ(result, 0);  // Success

  // Clear callback
  result = mcp_filter_chain_clear_event_callback(chain);
  EXPECT_EQ(result, 0);  // Success
}

TEST_F(FilterEventsTest, NullChainHandling) {
  EventCallbackState state;

  // Set callback on null chain - should fail
  int result = mcp_filter_chain_set_event_callback(
      0, eventCallback, &state);
  EXPECT_EQ(result, -1);  // Invalid arguments

  // Clear callback on null chain - should fail
  result = mcp_filter_chain_clear_event_callback(0);
  EXPECT_EQ(result, -1);  // Invalid arguments
}

TEST_F(FilterEventsTest, NullCallbackHandling) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  // Set null callback - should fail
  int result = mcp_filter_chain_set_event_callback(chain, nullptr, nullptr);
  EXPECT_EQ(result, -1);  // Invalid arguments
}

TEST_F(FilterEventsTest, EventTypeToString) {
  // Test all event types
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_CIRCUIT_STATE_CHANGE),
               "CIRCUIT_STATE_CHANGE");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_CIRCUIT_REQUEST_BLOCKED),
               "CIRCUIT_REQUEST_BLOCKED");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_CIRCUIT_HEALTH_UPDATE),
               "CIRCUIT_HEALTH_UPDATE");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_RATE_LIMIT_EXCEEDED),
               "RATE_LIMIT_EXCEEDED");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_METRIC_UPDATE),
               "METRIC_UPDATE");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_METRIC_FLUSH),
               "METRIC_FLUSH");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_REQUEST_LOGGED),
               "REQUEST_LOGGED");
  EXPECT_STREQ(mcp_filter_event_type_to_string(MCP_FILTER_EVENT_RESPONSE_LOGGED),
               "RESPONSE_LOGGED");
}

TEST_F(FilterEventsTest, SeverityToString) {
  // Test all severities
  EXPECT_STREQ(mcp_filter_event_severity_to_string(MCP_FILTER_EVENT_SEVERITY_DEBUG),
               "DEBUG");
  EXPECT_STREQ(mcp_filter_event_severity_to_string(MCP_FILTER_EVENT_SEVERITY_INFO),
               "INFO");
  EXPECT_STREQ(mcp_filter_event_severity_to_string(MCP_FILTER_EVENT_SEVERITY_WARN),
               "WARN");
  EXPECT_STREQ(mcp_filter_event_severity_to_string(MCP_FILTER_EVENT_SEVERITY_ERROR),
               "ERROR");
}

TEST_F(FilterEventsTest, CallbackRegistryStorage) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;

  // Register callback
  int result = mcp_filter_chain_set_event_callback(
      chain, eventCallback, &state);
  EXPECT_EQ(result, 0);

  // The callback should remain registered until we clear it
  // (No way to directly verify this without emitting an event,
  // which we can't do from C API alone)

  // Clear callback
  result = mcp_filter_chain_clear_event_callback(chain);
  EXPECT_EQ(result, 0);
}

TEST_F(FilterEventsTest, MultipleChainCallbacks) {
  auto chain1 = createFilterChain("chain1");
  auto chain2 = createFilterChain("chain2");
  ASSERT_NE(chain1, static_cast<mcp_filter_chain_t>(0));
  ASSERT_NE(chain2, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state1;
  EventCallbackState state2;

  // Register callbacks on both chains
  int result1 = mcp_filter_chain_set_event_callback(
      chain1, eventCallback, &state1);
  int result2 = mcp_filter_chain_set_event_callback(
      chain2, eventCallback, &state2);

  EXPECT_EQ(result1, 0);
  EXPECT_EQ(result2, 0);

  // Clear one callback
  result1 = mcp_filter_chain_clear_event_callback(chain1);
  EXPECT_EQ(result1, 0);

  // Other callback should still be registered
  // (No way to verify without event emission)

  // Clear other callback
  result2 = mcp_filter_chain_clear_event_callback(chain2);
  EXPECT_EQ(result2, 0);
}

TEST_F(FilterEventsTest, CallbackReplacement) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state1;
  EventCallbackState state2;

  // Register first callback
  int result = mcp_filter_chain_set_event_callback(
      chain, eventCallback, &state1);
  EXPECT_EQ(result, 0);

  // Replace with second callback
  result = mcp_filter_chain_set_event_callback(
      chain, eventCallback, &state2);
  EXPECT_EQ(result, 0);

  // Only second callback should be registered now
  // (No way to verify without event emission)

  // Clear callback
  result = mcp_filter_chain_clear_event_callback(chain);
  EXPECT_EQ(result, 0);
}

TEST_F(FilterEventsTest, ClearNonexistentCallback) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  // Clear callback that was never set - should succeed (no-op)
  int result = mcp_filter_chain_clear_event_callback(chain);
  EXPECT_EQ(result, 0);
}

TEST_F(FilterEventsTest, ThreadSafety) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;

  // Register callback from multiple threads
  constexpr int kNumThreads = 10;
  std::vector<std::thread> threads;

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      int result = mcp_filter_chain_set_event_callback(
          chain, eventCallback, &state);
      // Should succeed even with concurrent access
      EXPECT_GE(result, -2);  // Either success (0) or failure (-2)
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Clean up
  int result = mcp_filter_chain_clear_event_callback(chain);
  EXPECT_EQ(result, 0);
}

TEST_F(FilterEventsTest, CallbackLifetimeAfterChainDestruction) {
  EventCallbackState state;

  {
    auto chain = createFilterChain();
    ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

    // Register callback
    int result = mcp_filter_chain_set_event_callback(
        chain, eventCallback, &state);
    EXPECT_EQ(result, 0);

    // Chain will be destroyed here
  }

  // State should still be valid (callback shouldn't access it after chain dies)
  EXPECT_EQ(state.event_count.load(), 0);
}

// ============================================================================
// Event Emission Tests
// ============================================================================

TEST_F(FilterEventsTest, BasicEventEmission) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;

  // Register callback
  int result = mcp_filter_chain_set_event_callback(
      chain, eventCallback, &state);
  EXPECT_EQ(result, 0);

  // Access the internal event hub (test-only)
  // Note: This requires accessing C++ internals for testing
  // In production, events are emitted by filters
  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  // Emit a test event
  auto event_data = mcp::json::JsonObjectBuilder()
      .add("metric_name", "test_metric")
      .add("value", 42.0)
      .build();

  emitTestEvent(hub, "test_filter", "instance_1",
                mcp::filter::FilterEventType::METRIC_UPDATE,
                mcp::filter::FilterEventSeverity::INFO,
                event_data,
                "chain_1", "stream_1", "corr_1");

  // Wait for callback
  ASSERT_TRUE(state.waitForEventCount(1));

  // Verify event was received
  EXPECT_EQ(state.event_count.load(), 1);
  EXPECT_EQ(state.event_types[0], MCP_FILTER_EVENT_METRIC_UPDATE);
  EXPECT_EQ(state.severities[0], MCP_FILTER_EVENT_SEVERITY_INFO);
  EXPECT_EQ(state.filter_names[0], "test_filter");
  EXPECT_EQ(state.filter_instance_ids[0], "instance_1");
}

TEST_F(FilterEventsTest, EventDataJsonParsing) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;
  mcp_filter_chain_set_event_callback(chain, eventCallback, &state);

  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  // Create complex event data with nested objects
  auto nested_obj = mcp::json::JsonObjectBuilder()
      .add("inner_key", "inner_value")
      .add("inner_num", 999.0)
      .build();

  auto event_data = mcp::json::JsonObjectBuilder()
      .add("string_val", "test_string")
      .add("number_val", 123.45)
      .add("bool_val", true)
      .add("nested", nested_obj)
      .build();

  emitTestEvent(hub, "metrics_filter", "",
                mcp::filter::FilterEventType::METRIC_FLUSH,
                mcp::filter::FilterEventSeverity::DEBUG,
                event_data);

  ASSERT_TRUE(state.waitForEventCount(1));

  // Verify JSON string is populated
  EXPECT_FALSE(state.event_data_json[0].empty());

  // Verify we can find expected keys in JSON string
  const std::string& json_str = state.event_data_json[0];
  EXPECT_NE(json_str.find("string_val"), std::string::npos);
  EXPECT_NE(json_str.find("test_string"), std::string::npos);
  EXPECT_NE(json_str.find("nested"), std::string::npos);
}

TEST_F(FilterEventsTest, ContextPropagationInEvents) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;
  mcp_filter_chain_set_event_callback(chain, eventCallback, &state);

  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  // Emit event with specific context
  auto event_data = mcp::json::JsonObjectBuilder().build();

  emitTestEvent(hub, "test_filter", "",
                mcp::filter::FilterEventType::REQUEST_LOGGED,
                mcp::filter::FilterEventSeverity::INFO,
                event_data,
                "test_chain_id", "test_stream_id", "test_correlation_id");

  ASSERT_TRUE(state.waitForEventCount(1));

  // Verify context was propagated correctly
  EXPECT_EQ(state.contexts[0].chain_id, "test_chain_id");
  EXPECT_EQ(state.contexts[0].stream_id, "test_stream_id");
  EXPECT_EQ(state.contexts[0].correlation_id, "test_correlation_id");
}

TEST_F(FilterEventsTest, MultipleSequentialEvents) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;
  mcp_filter_chain_set_event_callback(chain, eventCallback, &state);

  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  auto event_data = mcp::json::JsonObjectBuilder().build();

  // Emit multiple events of different types
  emitTestEvent(hub, "filter1", "",
                mcp::filter::FilterEventType::CIRCUIT_STATE_CHANGE,
                mcp::filter::FilterEventSeverity::WARN,
                event_data);

  emitTestEvent(hub, "filter2", "",
                mcp::filter::FilterEventType::RATE_LIMIT_EXCEEDED,
                mcp::filter::FilterEventSeverity::WARN,
                event_data);

  emitTestEvent(hub, "filter3", "",
                mcp::filter::FilterEventType::METRIC_UPDATE,
                mcp::filter::FilterEventSeverity::INFO,
                event_data);

  // Wait for all 3 events
  ASSERT_TRUE(state.waitForEventCount(3));

  // Verify all events received
  EXPECT_EQ(state.event_count.load(), 3);

  // Verify event types in order
  EXPECT_EQ(state.event_types[0], MCP_FILTER_EVENT_CIRCUIT_STATE_CHANGE);
  EXPECT_EQ(state.event_types[1], MCP_FILTER_EVENT_RATE_LIMIT_EXCEEDED);
  EXPECT_EQ(state.event_types[2], MCP_FILTER_EVENT_METRIC_UPDATE);

  // Verify filter names
  EXPECT_EQ(state.filter_names[0], "filter1");
  EXPECT_EQ(state.filter_names[1], "filter2");
  EXPECT_EQ(state.filter_names[2], "filter3");
}

TEST_F(FilterEventsTest, CallbackNotInvokedAfterClear) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;
  mcp_filter_chain_set_event_callback(chain, eventCallback, &state);

  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  auto event_data = mcp::json::JsonObjectBuilder().build();

  // Emit first event - should be received
  emitTestEvent(hub, "test_filter", "",
                mcp::filter::FilterEventType::METRIC_UPDATE,
                mcp::filter::FilterEventSeverity::INFO,
                event_data);

  ASSERT_TRUE(state.waitForEventCount(1));
  EXPECT_EQ(state.event_count.load(), 1);

  // Clear callback
  int result = mcp_filter_chain_clear_event_callback(chain);
  EXPECT_EQ(result, 0);

  // Emit second event - should NOT be received
  emitTestEvent(hub, "test_filter", "",
                mcp::filter::FilterEventType::METRIC_UPDATE,
                mcp::filter::FilterEventSeverity::INFO,
                event_data);

  // Wait a bit to ensure event would have been delivered if callback was still registered
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Event count should still be 1 (second event not received)
  EXPECT_EQ(state.event_count.load(), 1);
}

TEST_F(FilterEventsTest, CircuitBreakerStateChangeEvent) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;
  mcp_filter_chain_set_event_callback(chain, eventCallback, &state);

  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  // Simulate circuit breaker state change event
  auto event_data = mcp::json::JsonObjectBuilder()
      .add("old_state", "CLOSED")
      .add("new_state", "OPEN")
      .add("failure_count", 3.0)
      .add("error_rate", 0.75)
      .build();

  emitTestEvent(hub, "circuit_breaker", "cb_instance_1",
                mcp::filter::FilterEventType::CIRCUIT_STATE_CHANGE,
                mcp::filter::FilterEventSeverity::WARN,
                event_data);

  ASSERT_TRUE(state.waitForEventCount(1));

  // Verify event type and severity
  EXPECT_EQ(state.event_types[0], MCP_FILTER_EVENT_CIRCUIT_STATE_CHANGE);
  EXPECT_EQ(state.severities[0], MCP_FILTER_EVENT_SEVERITY_WARN);
  EXPECT_EQ(state.filter_names[0], "circuit_breaker");
  EXPECT_EQ(state.filter_instance_ids[0], "cb_instance_1");

  // Verify event data contains state information
  const std::string& json_str = state.event_data_json[0];
  EXPECT_NE(json_str.find("old_state"), std::string::npos);
  EXPECT_NE(json_str.find("new_state"), std::string::npos);
  EXPECT_NE(json_str.find("OPEN"), std::string::npos);
}

TEST_F(FilterEventsTest, TimestampAndMetadataValidation) {
  auto chain = createFilterChain();
  ASSERT_NE(chain, static_cast<mcp_filter_chain_t>(0));

  EventCallbackState state;
  mcp_filter_chain_set_event_callback(chain, eventCallback, &state);

  auto advanced_chain = reinterpret_cast<mcp::filter_chain::AdvancedFilterChain*>(chain);
  auto hub = mcp::filter_chain::internal::getEventHub(*advanced_chain);
  ASSERT_NE(hub, nullptr);

  auto event_data = mcp::json::JsonObjectBuilder()
      .add("test_key", "test_value")
      .build();

  // Record time before emission
  auto before_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  emitTestEvent(hub, "metrics_filter", "metrics_1",
                mcp::filter::FilterEventType::METRIC_UPDATE,
                mcp::filter::FilterEventSeverity::DEBUG,
                event_data);

  ASSERT_TRUE(state.waitForEventCount(1));

  // Record time after callback
  auto after_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  // Verify timestamp is reasonable
  EXPECT_GT(state.timestamps[0], 0);
  EXPECT_GE(state.timestamps[0], before_ms);
  EXPECT_LE(state.timestamps[0], after_ms);

  // Verify filter metadata
  EXPECT_EQ(state.filter_names[0], "metrics_filter");
  EXPECT_EQ(state.filter_instance_ids[0], "metrics_1");

  // Verify severity
  EXPECT_EQ(state.severities[0], MCP_FILTER_EVENT_SEVERITY_DEBUG);
}

}  // namespace
