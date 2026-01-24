/**
 * @file test_sse_event_callbacks.cc
 * @brief Direct unit tests for SSE event callback functionality
 *
 * Tests the SSE codec filter's event callback mechanism for:
 * - "endpoint" events triggering callbacks
 * - "message" events triggering callbacks
 * - Default events (backwards compatibility)
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/sse_codec_filter.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::SaveArg;

/**
 * Mock SSE event callbacks
 */
class MockSseCallbacks : public SseCodecFilter::EventCallbacks {
 public:
  MOCK_METHOD(void,
              onEvent,
              (const std::string& event,
               const std::string& data,
               const optional<std::string>& id),
              (override));
  MOCK_METHOD(void, onComment, (const std::string& comment), (override));
  MOCK_METHOD(void, onError, (const std::string& error), (override));
};

/**
 * Test fixture for SSE event callbacks
 */
class SseEventCallbacksTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createPlatformDefaultDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");

    // Create mock callbacks
    callbacks_ = std::make_unique<MockSseCallbacks>();

    // Create SSE filter (client mode - decoding)
    filter_ =
        std::make_unique<SseCodecFilter>(*callbacks_, *dispatcher_, false);

    // Initialize filter
    filter_->onNewConnection();
    filter_->startEventStream();
  }

  void TearDown() override {
    filter_.reset();
    callbacks_.reset();
    dispatcher_.reset();
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  std::unique_ptr<MockSseCallbacks> callbacks_;
  std::unique_ptr<SseCodecFilter> filter_;
};

/**
 * Test: SSE "endpoint" event is parsed and callback invoked
 */
TEST_F(SseEventCallbacksTest, EndpointEventParsedAndCallbackInvoked) {
  std::string received_event;
  std::string received_data;

  EXPECT_CALL(*callbacks_, onEvent(_, _, _))
      .WillOnce([&](const std::string& event, const std::string& data,
                    const optional<std::string>& id) {
        received_event = event;
        received_data = data;
      });

  // Simulate SSE endpoint event
  std::string sse_data =
      "event: endpoint\n"
      "data: /message\n"
      "\n";

  OwnedBuffer buffer;
  buffer.add(sse_data);

  // Process through filter
  filter_->onData(buffer, false);

  // Verify callback was invoked with correct data
  EXPECT_EQ(received_event, "endpoint");
  EXPECT_EQ(received_data, "/message");
}

/**
 * Test: SSE "message" event is parsed and callback invoked
 */
TEST_F(SseEventCallbacksTest, MessageEventParsedAndCallbackInvoked) {
  std::string received_event;
  std::string received_data;

  EXPECT_CALL(*callbacks_, onEvent(_, _, _))
      .WillOnce([&](const std::string& event, const std::string& data,
                    const optional<std::string>& id) {
        received_event = event;
        received_data = data;
      });

  // Simulate SSE message event with JSON-RPC
  std::string sse_data =
      "event: message\n"
      "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"success\"}\n"
      "\n";

  OwnedBuffer buffer;
  buffer.add(sse_data);

  // Process through filter
  filter_->onData(buffer, false);

  // Verify callback was invoked
  EXPECT_EQ(received_event, "message");
  EXPECT_NE(received_data.find("jsonrpc"), std::string::npos);
}

/**
 * Test: Default SSE event (no event type) invokes callback
 */
TEST_F(SseEventCallbacksTest, DefaultEventInvokesCallback) {
  std::string received_event;
  std::string received_data;

  EXPECT_CALL(*callbacks_, onEvent(_, _, _))
      .WillOnce([&](const std::string& event, const std::string& data,
                    const optional<std::string>& id) {
        received_event = event;
        received_data = data;
      });

  // Simulate SSE data without event type
  std::string sse_data =
      "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}\n"
      "\n";

  OwnedBuffer buffer;
  buffer.add(sse_data);

  // Process through filter
  filter_->onData(buffer, false);

  // Default event type is empty string
  EXPECT_EQ(received_event, "");
  EXPECT_NE(received_data.find("jsonrpc"), std::string::npos);
}

/**
 * Test: Multiple SSE events are all processed
 */
TEST_F(SseEventCallbacksTest, MultipleEventsProcessed) {
  std::vector<std::string> received_events;
  std::vector<std::string> received_data;

  EXPECT_CALL(*callbacks_, onEvent(_, _, _))
      .Times(2)
      .WillRepeatedly([&](const std::string& event, const std::string& data,
                          const optional<std::string>& id) {
        received_events.push_back(event);
        received_data.push_back(data);
      });

  // Simulate multiple SSE events
  std::string sse_data =
      "event: endpoint\n"
      "data: /api/message\n"
      "\n"
      "event: message\n"
      "data: {\"test\":\"data\"}\n"
      "\n";

  OwnedBuffer buffer;
  buffer.add(sse_data);

  // Process through filter
  filter_->onData(buffer, false);

  // Verify both events received
  ASSERT_EQ(received_events.size(), 2);
  EXPECT_EQ(received_events[0], "endpoint");
  EXPECT_EQ(received_data[0], "/api/message");
  EXPECT_EQ(received_events[1], "message");
  EXPECT_NE(received_data[1].find("test"), std::string::npos);
}

/**
 * Test: SSE event with ID field
 */
TEST_F(SseEventCallbacksTest, EventWithIdParsed) {
  std::string received_event;
  optional<std::string> received_id;

  EXPECT_CALL(*callbacks_, onEvent(_, _, _))
      .WillOnce([&](const std::string& event, const std::string& data,
                    const optional<std::string>& id) {
        received_event = event;
        received_id = id;
      });

  // Simulate SSE event with ID
  std::string sse_data =
      "event: message\n"
      "id: 12345\n"
      "data: test\n"
      "\n";

  OwnedBuffer buffer;
  buffer.add(sse_data);

  // Process through filter
  filter_->onData(buffer, false);

  // Verify ID was captured
  EXPECT_EQ(received_event, "message");
  ASSERT_TRUE(received_id.has_value());
  EXPECT_EQ(received_id.value(), "12345");
}

/**
 * Test: Multiline SSE data is concatenated
 */
TEST_F(SseEventCallbacksTest, MultilineDataConcatenated) {
  std::string received_data;

  EXPECT_CALL(*callbacks_, onEvent(_, _, _))
      .WillOnce([&](const std::string& event, const std::string& data,
                    const optional<std::string>& id) { received_data = data; });

  // Simulate SSE event with multiline data
  std::string sse_data =
      "data: line1\n"
      "data: line2\n"
      "data: line3\n"
      "\n";

  OwnedBuffer buffer;
  buffer.add(sse_data);

  // Process through filter
  filter_->onData(buffer, false);

  // Verify lines concatenated with newlines
  EXPECT_EQ(received_data, "line1\nline2\nline3");
}

}  // namespace
}  // namespace filter
}  // namespace mcp
