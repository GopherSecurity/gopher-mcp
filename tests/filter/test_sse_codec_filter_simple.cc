/**
 * @file test_sse_codec_filter_simple.cc
 * @brief Simple integration tests for SSE codec filter with state machine
 */

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/sse_codec_filter.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

// Simple event callbacks implementation
class TestEventCallbacks : public SseCodecFilter::EventCallbacks {
 public:
  void onEvent(const std::string& event,
               const std::string& data,
               const optional<std::string>& id) override {
    event_received_ = true;
    last_event_type_ = event;
    last_event_data_ = data;
    last_event_id_ = id;
  }

  void onComment(const std::string& comment) override {
    comment_received_ = true;
    last_comment_ = comment;
  }

  void onError(const std::string& error) override {
    error_received_ = true;
    error_message_ = error;
  }

  // Test state
  bool event_received_{false};
  bool comment_received_{false};
  bool error_received_{false};
  std::string last_event_type_;
  std::string last_event_data_;
  optional<std::string> last_event_id_;
  std::string last_comment_;
  std::string error_message_;
};

class SseCodecFilterIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    dispatcher_->run(event::RunType::NonBlock);
  }

  void TearDown() override {
    server_filter_.reset();
    client_filter_.reset();
    dispatcher_.reset();
  }

  void createServerFilter() {
    server_filter_ =
        std::make_unique<SseCodecFilter>(callbacks_, *dispatcher_, true);
  }

  void createClientFilter() {
    client_filter_ =
        std::make_unique<SseCodecFilter>(callbacks_, *dispatcher_, false);
  }

  // Helper to create SSE event data
  OwnedBuffer createSseEvent(const std::string& event_type,
                             const std::string& data) {
    OwnedBuffer buffer;

    if (!event_type.empty()) {
      std::string event_line = "event: " + event_type + "\n";
      buffer.add(event_line.c_str(), event_line.length());
    }

    std::string data_line = "data: " + data + "\n";
    buffer.add(data_line.c_str(), data_line.length());
    buffer.add("\n", 1);  // End of event

    return buffer;
  }

  // Helper to run dispatcher briefly
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  TestEventCallbacks callbacks_;
  std::unique_ptr<SseCodecFilter> server_filter_;
  std::unique_ptr<SseCodecFilter> client_filter_;
};

// ===== Server Mode Tests =====

TEST_F(SseCodecFilterIntegrationTest, ServerFilterCreation) {
  createServerFilter();
  EXPECT_NE(server_filter_, nullptr);
  EXPECT_EQ(server_filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(SseCodecFilterIntegrationTest, ServerStartStream) {
  createServerFilter();
  server_filter_->onNewConnection();

  // Start event stream (this integrates with state machine)
  server_filter_->startEventStream();

  runFor(10ms);

  // Should complete without error
  EXPECT_FALSE(callbacks_.error_received_);
}

// ===== Client Mode Tests =====

TEST_F(SseCodecFilterIntegrationTest, ClientFilterCreation) {
  createClientFilter();
  EXPECT_NE(client_filter_, nullptr);
  EXPECT_EQ(client_filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(SseCodecFilterIntegrationTest, ClientReceiveEvent) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();

  auto event_data = createSseEvent("message", "Hello, World!");
  EXPECT_EQ(client_filter_->onData(event_data, false),
            network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.event_received_);
  EXPECT_EQ(callbacks_.last_event_type_, "message");
  EXPECT_EQ(callbacks_.last_event_data_, "Hello, World!");
  EXPECT_FALSE(callbacks_.error_received_);
}

TEST_F(SseCodecFilterIntegrationTest, ClientReceiveComment) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();

  OwnedBuffer comment_data;
  comment_data.add(": This is a comment\n\n", 21);

  EXPECT_EQ(client_filter_->onData(comment_data, false),
            network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.comment_received_);
  EXPECT_EQ(callbacks_.last_comment_, " This is a comment");
  EXPECT_FALSE(callbacks_.error_received_);
}

// ===== State Machine Integration Tests =====

TEST_F(SseCodecFilterIntegrationTest, StateMachineIntegration) {
  createClientFilter();
  client_filter_->onNewConnection();

  // The state machine should properly manage the SSE stream lifecycle
  client_filter_->startEventStream();

  // Send multiple events
  for (int i = 0; i < 3; ++i) {
    callbacks_ = TestEventCallbacks{};  // Reset

    auto event_data = createSseEvent("test", "Event " + std::to_string(i));
    EXPECT_EQ(client_filter_->onData(event_data, false),
              network::FilterStatus::Continue);

    runFor(5ms);

    EXPECT_TRUE(callbacks_.event_received_);
    EXPECT_EQ(callbacks_.last_event_data_, "Event " + std::to_string(i));
  }

  // End stream
  OwnedBuffer empty;
  EXPECT_EQ(client_filter_->onData(empty, true),
            network::FilterStatus::Continue);

  runFor(10ms);

  // Should handle stream end without error
  EXPECT_FALSE(callbacks_.error_received_);
}

TEST_F(SseCodecFilterIntegrationTest, DefaultEventType) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();

  // Event without explicit type (should use default)
  auto event_data = createSseEvent("", "Default event");
  EXPECT_EQ(client_filter_->onData(event_data, false),
            network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.event_received_);
  EXPECT_TRUE(
      callbacks_.last_event_type_.empty());  // Default event type is empty
  EXPECT_EQ(callbacks_.last_event_data_, "Default event");
}

TEST_F(SseCodecFilterIntegrationTest, MultilineEventData) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();

  OwnedBuffer multiline_event;
  multiline_event.add("event: multiline\n", 17);
  multiline_event.add("data: Line 1\n", 13);
  multiline_event.add("data: Line 2\n", 13);
  multiline_event.add("data: Line 3\n", 13);
  multiline_event.add("\n", 1);

  EXPECT_EQ(client_filter_->onData(multiline_event, false),
            network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.event_received_);
  EXPECT_EQ(callbacks_.last_event_type_, "multiline");

  // Data should be concatenated with newlines
  std::string expected_data = "Line 1\nLine 2\nLine 3";
  EXPECT_EQ(callbacks_.last_event_data_, expected_data);
}

TEST_F(SseCodecFilterIntegrationTest, EventWithId) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();

  OwnedBuffer event_with_id;
  event_with_id.add("id: 123\n", 8);
  event_with_id.add("event: update\n", 14);
  event_with_id.add("data: Updated data\n", 19);
  event_with_id.add("\n", 1);

  EXPECT_EQ(client_filter_->onData(event_with_id, false),
            network::FilterStatus::Continue);

  runFor(10ms);

  EXPECT_TRUE(callbacks_.event_received_);
  EXPECT_EQ(callbacks_.last_event_type_, "update");
  EXPECT_EQ(callbacks_.last_event_data_, "Updated data");
  EXPECT_TRUE(callbacks_.last_event_id_.has_value());
  EXPECT_EQ(callbacks_.last_event_id_.value(), "123");
}

}  // namespace
}  // namespace filter
}  // namespace mcp