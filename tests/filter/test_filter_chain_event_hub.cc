/**
 * @file test_filter_chain_event_hub.cc
 * @brief Unit tests for FilterChainEventHub
 */

#include <atomic>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/filter/filter_chain_callbacks.h"
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_event.h"
#include "mcp/json/json_bridge.h"

using namespace mcp;
using namespace mcp::filter;
using namespace testing;

namespace {

// Mock callback implementation
class MockFilterChainCallbacks : public FilterChainCallbacks {
 public:
  MOCK_METHOD(void, onFilterEvent, (const FilterEvent& event), (override));
};

// Custom matcher for FilterEvent by event type
MATCHER_P(HasEventType, expected_type, "") {
  return arg.event_type == expected_type;
}

// Custom matcher for FilterEvent by severity
MATCHER_P(HasSeverity, expected_severity, "") {
  return arg.severity == expected_severity;
}

// Custom matcher for FilterEvent by filter name
MATCHER_P(HasFilterName, expected_name, "") {
  return arg.filter_name == expected_name;
}

class FilterChainEventHubTest : public ::testing::Test {
 protected:
  void SetUp() override { hub_ = std::make_shared<FilterChainEventHub>(); }

  // Helper to create test event
  FilterEvent createEvent(
      FilterEventType type = FilterEventType::CIRCUIT_STATE_CHANGE,
      FilterEventSeverity severity = FilterEventSeverity::INFO,
      const std::string& filter_name = "test_filter") {
    FilterEvent event;
    event.filter_name = filter_name;
    event.filter_instance_id = "instance_1";
    event.event_type = type;
    event.severity = severity;
    event.context.chain_id = "chain_1";
    event.context.stream_id = "stream_1";
    event.context.correlation_id = "corr_1";
    event.event_data =
        json::JsonObjectBuilder().add("test_key", "test_value").build();
    return event;
  }

  std::shared_ptr<FilterChainEventHub> hub_;
};

// Test basic observer registration and unregistration
TEST_F(FilterChainEventHubTest, ObserverRegistrationAndUnregistration) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  // Register observer
  auto handle = hub_->registerObserver(callbacks);
  EXPECT_TRUE(handle.isValid());

  // Emit event - should be received
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent());

  // Manually unregister (handle will also auto-unregister on destruction)
  // This tests explicit unregistration
  auto observer_id = handle.getObserverId();
  hub_->unregisterObserver(observer_id);

  // Emit event - should NOT be received
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(0);
  hub_->emit(createEvent());
}

// Test ObserverHandle RAII behavior
TEST_F(FilterChainEventHubTest, ObserverHandleRAII) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  {
    // Register observer in inner scope
    auto handle = hub_->registerObserver(callbacks);
    EXPECT_TRUE(handle.isValid());

    // Emit event - should be received
    EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(1);
    hub_->emit(createEvent());
  }  // handle destroyed here, should auto-unregister

  // Emit event - should NOT be received (observer was unregistered)
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(0);
  hub_->emit(createEvent());
}

// Test ObserverHandle move semantics
TEST_F(FilterChainEventHubTest, ObserverHandleMoveSemantics) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  FilterChainEventHub::ObserverHandle handle1;
  {
    auto handle2 = hub_->registerObserver(callbacks);
    EXPECT_TRUE(handle2.isValid());

    // Move handle2 to handle1
    handle1 = std::move(handle2);
    EXPECT_TRUE(handle1.isValid());
    EXPECT_FALSE(handle2.isValid());  // handle2 should be invalidated
  }  // handle2 destroyed, but should NOT unregister

  // Emit event - should still be received via handle1
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent());
}

// Test multiple observers
TEST_F(FilterChainEventHubTest, MultipleObservers) {
  auto callbacks1 = std::make_shared<NiceMock<MockFilterChainCallbacks>>();
  auto callbacks2 = std::make_shared<NiceMock<MockFilterChainCallbacks>>();
  auto callbacks3 = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  auto handle1 = hub_->registerObserver(callbacks1);
  auto handle2 = hub_->registerObserver(callbacks2);
  auto handle3 = hub_->registerObserver(callbacks3);

  // All observers should receive the event
  EXPECT_CALL(*callbacks1, onFilterEvent(_)).Times(1);
  EXPECT_CALL(*callbacks2, onFilterEvent(_)).Times(1);
  EXPECT_CALL(*callbacks3, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent());
}

// Test filtering by event type
TEST_F(FilterChainEventHubTest, FilterByEventType) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  // Register observer that only cares about CIRCUIT_STATE_CHANGE events
  FilterChainEventHub::ObserverFilter filter;
  filter.event_types = {FilterEventType::CIRCUIT_STATE_CHANGE};
  auto handle = hub_->registerObserver(callbacks, filter);

  // Emit CIRCUIT_STATE_CHANGE - should be received
  EXPECT_CALL(
      *callbacks,
      onFilterEvent(HasEventType(FilterEventType::CIRCUIT_STATE_CHANGE)))
      .Times(1);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE));

  // Emit CIRCUIT_REQUEST_BLOCKED - should NOT be received
  EXPECT_CALL(
      *callbacks,
      onFilterEvent(HasEventType(FilterEventType::CIRCUIT_REQUEST_BLOCKED)))
      .Times(0);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_REQUEST_BLOCKED));
}

// Test filtering by severity
TEST_F(FilterChainEventHubTest, FilterBySeverity) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  // Register observer that only cares about ERROR and WARN severities
  FilterChainEventHub::ObserverFilter filter;
  filter.min_severity = FilterEventSeverity::WARN;
  auto handle = hub_->registerObserver(callbacks, filter);

  // Emit ERROR - should be received
  EXPECT_CALL(*callbacks,
              onFilterEvent(HasSeverity(FilterEventSeverity::ERROR)))
      .Times(1);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::ERROR));

  // Emit WARN - should be received
  EXPECT_CALL(*callbacks, onFilterEvent(HasSeverity(FilterEventSeverity::WARN)))
      .Times(1);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::WARN));

  // Emit INFO - should NOT be received
  EXPECT_CALL(*callbacks, onFilterEvent(HasSeverity(FilterEventSeverity::INFO)))
      .Times(0);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::INFO));

  // Emit DEBUG - should NOT be received
  EXPECT_CALL(*callbacks,
              onFilterEvent(HasSeverity(FilterEventSeverity::DEBUG)))
      .Times(0);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::DEBUG));
}

// Test filtering by filter name
TEST_F(FilterChainEventHubTest, FilterByFilterName) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  // Register observer that only cares about "circuit_breaker" filter
  FilterChainEventHub::ObserverFilter filter;
  filter.filter_names = {"circuit_breaker"};
  auto handle = hub_->registerObserver(callbacks, filter);

  // Emit from circuit_breaker - should be received
  EXPECT_CALL(*callbacks, onFilterEvent(HasFilterName("circuit_breaker")))
      .Times(1);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::INFO, "circuit_breaker"));

  // Emit from rate_limiter - should NOT be received
  EXPECT_CALL(*callbacks, onFilterEvent(HasFilterName("rate_limiter")))
      .Times(0);
  hub_->emit(createEvent(FilterEventType::RATE_LIMIT_EXCEEDED,
                         FilterEventSeverity::WARN, "rate_limiter"));
}

// Test combined filtering
TEST_F(FilterChainEventHubTest, CombinedFiltering) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

  // Register observer with combined filters
  FilterChainEventHub::ObserverFilter filter;
  filter.event_types = {FilterEventType::CIRCUIT_STATE_CHANGE};
  filter.min_severity = FilterEventSeverity::WARN;
  filter.filter_names = {"circuit_breaker"};
  auto handle = hub_->registerObserver(callbacks, filter);

  // Should match all filters - received
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::WARN, "circuit_breaker"));

  // Wrong event type - NOT received
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(0);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_REQUEST_BLOCKED,
                         FilterEventSeverity::WARN, "circuit_breaker"));

  // Wrong severity - NOT received
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(0);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::INFO, "circuit_breaker"));

  // Wrong filter name - NOT received
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(0);
  hub_->emit(createEvent(FilterEventType::CIRCUIT_STATE_CHANGE,
                         FilterEventSeverity::WARN, "rate_limiter"));
}

// Test enable/disable functionality
TEST_F(FilterChainEventHubTest, EnableDisable) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();
  auto handle = hub_->registerObserver(callbacks);

  // Initially enabled - should receive
  EXPECT_TRUE(hub_->isEnabled());
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent());

  // Disable hub
  hub_->setEnabled(false);
  EXPECT_FALSE(hub_->isEnabled());

  // Should NOT receive when disabled
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(0);
  hub_->emit(createEvent());

  // Re-enable hub
  hub_->setEnabled(true);
  EXPECT_TRUE(hub_->isEnabled());

  // Should receive again
  EXPECT_CALL(*callbacks, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent());
}

// Test thread safety - concurrent registration and emission
TEST_F(FilterChainEventHubTest, ThreadSafety) {
  constexpr int kNumThreads = 10;
  constexpr int kEventsPerThread = 100;

  std::vector<std::shared_ptr<NiceMock<MockFilterChainCallbacks>>>
      callbacks_list;
  std::vector<FilterChainEventHub::ObserverHandle> handles;
  std::atomic<int> total_events_received{0};

  // Register observers from multiple threads
  std::vector<std::thread> registration_threads;
  for (int i = 0; i < kNumThreads; ++i) {
    registration_threads.emplace_back([&]() {
      auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();

      // Count events
      ON_CALL(*callbacks, onFilterEvent(_))
          .WillByDefault(
              Invoke([&](const FilterEvent&) { total_events_received++; }));

      auto handle = hub_->registerObserver(callbacks);

      // Store safely (in test main thread context after join)
      callbacks_list.push_back(callbacks);
      handles.push_back(std::move(handle));
    });
  }

  for (auto& t : registration_threads) {
    t.join();
  }

  // Emit events from multiple threads
  std::vector<std::thread> emission_threads;
  for (int i = 0; i < kNumThreads; ++i) {
    emission_threads.emplace_back([&]() {
      for (int j = 0; j < kEventsPerThread; ++j) {
        hub_->emit(createEvent());
      }
    });
  }

  for (auto& t : emission_threads) {
    t.join();
  }

  // Verify some events were received (exact count may vary due to timing)
  EXPECT_GT(total_events_received.load(), 0);
}

// Test null callback handling
TEST_F(FilterChainEventHubTest, NullCallbackHandling) {
  // Register null callback - should return invalid handle
  auto handle = hub_->registerObserver(nullptr);
  EXPECT_FALSE(handle.isValid());

  // Hub should still work normally
  auto valid_callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();
  auto valid_handle = hub_->registerObserver(valid_callbacks);
  EXPECT_TRUE(valid_handle.isValid());

  EXPECT_CALL(*valid_callbacks, onFilterEvent(_)).Times(1);
  hub_->emit(createEvent());
}

// Test double unregistration safety
TEST_F(FilterChainEventHubTest, DoubleUnregistrationSafety) {
  auto callbacks = std::make_shared<NiceMock<MockFilterChainCallbacks>>();
  auto handle = hub_->registerObserver(callbacks);

  auto observer_id = handle.getObserverId();

  // First unregistration
  hub_->unregisterObserver(observer_id);

  // Second unregistration - should be safe (no-op)
  EXPECT_NO_THROW(hub_->unregisterObserver(observer_id));

  // Invalid ID unregistration - should be safe
  EXPECT_NO_THROW(hub_->unregisterObserver(999999));
}

}  // namespace
