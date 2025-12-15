/**
 * @file test_filter_event_emitter.cc
 * @brief Unit tests for FilterEventEmitter
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/filter/filter_chain_callbacks.h"
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_event.h"
#include "mcp/filter/filter_event_emitter.h"
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

class FilterEventEmitterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hub_ = std::make_shared<FilterChainEventHub>();
    callbacks_ = std::make_shared<NiceMock<MockFilterChainCallbacks>>();
    observer_handle_ = hub_->registerObserver(callbacks_);
  }

  std::shared_ptr<FilterChainEventHub> hub_;
  std::shared_ptr<MockFilterChainCallbacks> callbacks_;
  FilterChainEventHub::ObserverHandle observer_handle_;
};

// Test basic event emission with context enrichment
TEST_F(FilterEventEmitterTest, BasicEventEmission) {
  FilterEventEmitter emitter(hub_, "test_filter", "instance_1", "chain_1");

  auto event_data =
      json::JsonObjectBuilder().add("key1", "value1").add("key2", 42.0).build();

  // Expect event with enriched context
  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        EXPECT_EQ(event.filter_name, "test_filter");
        EXPECT_EQ(event.filter_instance_id, "instance_1");
        EXPECT_EQ(event.context.chain_id, "chain_1");
        EXPECT_EQ(event.event_type, FilterEventType::CIRCUIT_STATE_CHANGE);
        EXPECT_EQ(event.severity, FilterEventSeverity::INFO);
        EXPECT_TRUE(event.event_data.contains("key1"));
        EXPECT_EQ(event.event_data["key1"].getString(), "value1");
      }));

  emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE, FilterEventSeverity::INFO,
               event_data);
}

// Test emit with custom stream and correlation IDs
TEST_F(FilterEventEmitterTest, EmitWithCustomIds) {
  FilterEventEmitter emitter(hub_, "test_filter", "instance_1", "chain_1");

  auto event_data = json::JsonObjectBuilder().add("test", "data").build();

  // Expect event with custom stream and correlation IDs
  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        EXPECT_EQ(event.filter_name, "test_filter");
        EXPECT_EQ(event.context.stream_id, "custom_stream");
        EXPECT_EQ(event.context.correlation_id, "custom_corr");
      }));

  emitter.emit(FilterEventType::RATE_LIMIT_EXCEEDED, FilterEventSeverity::WARN,
               event_data, "custom_stream", "custom_corr");
}

// Test setStreamId and setCorrelationId
TEST_F(FilterEventEmitterTest, SetStreamAndCorrelationIds) {
  FilterEventEmitter emitter(hub_, "test_filter", "", "chain_1");

  // Set stream and correlation IDs
  emitter.setStreamId("stream_123");
  emitter.setCorrelationId("corr_456");

  auto event_data = json::JsonObjectBuilder().build();

  // Subsequent emits should use the set IDs
  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        EXPECT_EQ(event.context.stream_id, "stream_123");
        EXPECT_EQ(event.context.correlation_id, "corr_456");
      }));

  emitter.emit(FilterEventType::CIRCUIT_HEALTH_UPDATE,
               FilterEventSeverity::DEBUG, event_data);
}

// Test setStreamId overrides previous value
TEST_F(FilterEventEmitterTest, StreamIdOverride) {
  FilterEventEmitter emitter(hub_, "test_filter", "", "chain_1");

  emitter.setStreamId("stream_1");

  auto event_data = json::JsonObjectBuilder().build();

  // First emit with stream_1
  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        EXPECT_EQ(event.context.stream_id, "stream_1");
      }));
  emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE, FilterEventSeverity::INFO,
               event_data);

  // Change stream ID
  emitter.setStreamId("stream_2");

  // Second emit with stream_2
  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        EXPECT_EQ(event.context.stream_id, "stream_2");
      }));
  emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE, FilterEventSeverity::INFO,
               event_data);
}

// Test emitEvent with fully constructed event
TEST_F(FilterEventEmitterTest, EmitFullyConstructedEvent) {
  FilterEventEmitter emitter(hub_, "test_filter", "instance_1", "chain_1");

  // Create a fully constructed event
  FilterEvent event;
  event.filter_name = "custom_filter";           // Will be preserved
  event.filter_instance_id = "custom_instance";  // Will be preserved
  event.event_type = FilterEventType::METRIC_UPDATE;
  event.severity = FilterEventSeverity::DEBUG;
  event.context.chain_id = "custom_chain";  // Will be preserved (not empty)
  event.context.stream_id = "preset_stream";
  event.context.correlation_id = "preset_corr";
  event.event_data = json::JsonObjectBuilder()
                         .add("counter", "requests")
                         .add("value", 100.0)
                         .build();

  // Expect event fields to be preserved (emitter only enriches empty context
  // fields)
  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& received_event) {
        // Filter name and instance should be from the event (preserved)
        EXPECT_EQ(received_event.filter_name, "custom_filter");
        EXPECT_EQ(received_event.filter_instance_id, "custom_instance");

        // Event type and severity should be preserved
        EXPECT_EQ(received_event.event_type, FilterEventType::METRIC_UPDATE);
        EXPECT_EQ(received_event.severity, FilterEventSeverity::DEBUG);

        // Context IDs should be preset values
        EXPECT_EQ(received_event.context.stream_id, "preset_stream");
        EXPECT_EQ(received_event.context.correlation_id, "preset_corr");

        // Event data should be preserved
        EXPECT_TRUE(received_event.event_data.contains("counter"));
        EXPECT_EQ(received_event.event_data["value"].getFloat(), 100.0);
      }));

  emitter.emitEvent(event);
}

// Test emitEvent with partial context - enrichment behavior
TEST_F(FilterEventEmitterTest, EmitEventWithPartialContext) {
  FilterEventEmitter emitter(hub_, "test_filter", "instance_1", "chain_1");

  // Set stream ID on emitter
  emitter.setStreamId("emitter_stream");

  // Create event with empty stream ID - should be enriched
  FilterEvent event;
  event.event_type = FilterEventType::CIRCUIT_STATE_CHANGE;
  event.severity = FilterEventSeverity::INFO;
  event.context.stream_id = "";  // Empty - should be filled by emitter
  event.context.correlation_id =
      "preset_corr";  // Non-empty - should be preserved
  event.event_data = json::JsonObjectBuilder().build();

  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& received_event) {
        // Should enrich empty stream_id from emitter
        EXPECT_EQ(received_event.context.stream_id, "emitter_stream");
        // Should preserve non-empty correlation_id
        EXPECT_EQ(received_event.context.correlation_id, "preset_corr");
      }));

  emitter.emitEvent(event);
}

// Test getFilterName, getFilterInstanceId, getChainId
TEST_F(FilterEventEmitterTest, Getters) {
  FilterEventEmitter emitter(hub_, "my_filter", "my_instance", "my_chain");

  EXPECT_EQ(emitter.getFilterName(), "my_filter");
  EXPECT_EQ(emitter.getFilterInstanceId(), "my_instance");
  EXPECT_EQ(emitter.getChainId(), "my_chain");
}

// Test isConnected
TEST_F(FilterEventEmitterTest, IsConnected) {
  // Emitter with valid hub
  FilterEventEmitter connected_emitter(hub_, "test_filter", "", "");
  EXPECT_TRUE(connected_emitter.isConnected());

  // Emitter with null hub
  FilterEventEmitter disconnected_emitter(nullptr, "test_filter", "", "");
  EXPECT_FALSE(disconnected_emitter.isConnected());
}

// Test emission with disconnected emitter (null hub)
TEST_F(FilterEventEmitterTest, EmitWithNullHub) {
  FilterEventEmitter emitter(nullptr, "test_filter", "", "");

  auto event_data = json::JsonObjectBuilder().build();

  // Should not crash when hub is null
  EXPECT_NO_THROW({
    emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE,
                 FilterEventSeverity::INFO, event_data);
  });

  // No event should be received (hub is null)
  EXPECT_CALL(*callbacks_, onFilterEvent(_)).Times(0);
}

// Test multiple events from same emitter
TEST_F(FilterEventEmitterTest, MultipleEventsFromSameEmitter) {
  FilterEventEmitter emitter(hub_, "test_filter", "instance_1", "chain_1");

  auto event_data = json::JsonObjectBuilder().build();

  // Emit 5 different events
  EXPECT_CALL(*callbacks_, onFilterEvent(_)).Times(5);

  emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE, FilterEventSeverity::INFO,
               event_data);
  emitter.emit(FilterEventType::CIRCUIT_REQUEST_BLOCKED,
               FilterEventSeverity::WARN, event_data);
  emitter.emit(FilterEventType::CIRCUIT_HEALTH_UPDATE,
               FilterEventSeverity::DEBUG, event_data);
  emitter.emit(FilterEventType::RATE_LIMIT_EXCEEDED, FilterEventSeverity::WARN,
               event_data);
  emitter.emit(FilterEventType::METRIC_UPDATE, FilterEventSeverity::DEBUG,
               event_data);
}

// Test event timestamp is set
TEST_F(FilterEventEmitterTest, EventTimestampIsSet) {
  FilterEventEmitter emitter(hub_, "test_filter", "", "");

  auto event_data = json::JsonObjectBuilder().build();

  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        // Timestamp should be set (non-zero)
        EXPECT_GT(event.getTimestampMs(), 0);
      }));

  emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE, FilterEventSeverity::INFO,
               event_data);
}

// Test emitter with empty filter name
TEST_F(FilterEventEmitterTest, EmptyFilterName) {
  FilterEventEmitter emitter(hub_, "", "instance_1", "chain_1");

  EXPECT_EQ(emitter.getFilterName(), "");

  auto event_data = json::JsonObjectBuilder().build();

  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke(
          [](const FilterEvent& event) { EXPECT_EQ(event.filter_name, ""); }));

  emitter.emit(FilterEventType::CIRCUIT_STATE_CHANGE, FilterEventSeverity::INFO,
               event_data);
}

// Test emitter move semantics
TEST_F(FilterEventEmitterTest, MoveSemantics) {
  FilterEventEmitter emitter1(hub_, "filter1", "instance1", "chain1");

  // Move construct
  FilterEventEmitter emitter2(std::move(emitter1));

  EXPECT_EQ(emitter2.getFilterName(), "filter1");
  EXPECT_EQ(emitter2.getFilterInstanceId(), "instance1");
  EXPECT_EQ(emitter2.getChainId(), "chain1");
  EXPECT_TRUE(emitter2.isConnected());

  auto event_data = json::JsonObjectBuilder().build();

  // Should be able to emit from moved emitter
  EXPECT_CALL(*callbacks_, onFilterEvent(_)).Times(1);
  emitter2.emit(FilterEventType::CIRCUIT_STATE_CHANGE,
                FilterEventSeverity::INFO, event_data);
}

// Test event data JSON structure preservation
TEST_F(FilterEventEmitterTest, ComplexEventDataPreservation) {
  FilterEventEmitter emitter(hub_, "test_filter", "", "");

  auto complex_data = json::JsonObjectBuilder()
                          .add("string_val", "test")
                          .add("number_val", 123.45)
                          .add("bool_val", true)
                          .add("nested", json::JsonObjectBuilder()
                                             .add("inner_key", "inner_value")
                                             .add("inner_num", 999.0)
                                             .build())
                          .build();

  EXPECT_CALL(*callbacks_, onFilterEvent(_))
      .WillOnce(Invoke([](const FilterEvent& event) {
        EXPECT_EQ(event.event_data["string_val"].getString(), "test");
        EXPECT_FLOAT_EQ(event.event_data["number_val"].getFloat(), 123.45);
        EXPECT_TRUE(event.event_data["bool_val"].getBool());
        EXPECT_TRUE(event.event_data.contains("nested"));
        EXPECT_EQ(event.event_data["nested"]["inner_key"].getString(),
                  "inner_value");
        EXPECT_FLOAT_EQ(event.event_data["nested"]["inner_num"].getFloat(),
                        999.0);
      }));

  emitter.emit(FilterEventType::REQUEST_LOGGED, FilterEventSeverity::INFO,
               complex_data);
}

}  // namespace
