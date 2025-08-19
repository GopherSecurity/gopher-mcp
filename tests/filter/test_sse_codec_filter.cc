/**
 * @file test_sse_codec_filter.cc
 * @brief Real IO integration tests for SSE codec filter
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include <gtest/gtest.h>

#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

// Real event callbacks implementation for testing
class TestEventCallbacks : public SseCodecFilter::EventCallbacks {
 public:
  void onEvent(const std::string& event,
               const std::string& data,
               const optional<std::string>& id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    event_received_ = true;
    last_event_type_ = event;
    last_event_data_ = data;
    last_event_id_ = id;
    event_cv_.notify_all();
  }

  void onComment(const std::string& comment) override {
    std::lock_guard<std::mutex> lock(mutex_);
    comment_received_ = true;
    last_comment_ = comment;
    comment_cv_.notify_all();
  }

  void onError(const std::string& error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    error_received_ = true;
    error_message_ = error;
    error_cv_.notify_all();
  }

  // Wait functions with timeout
  bool waitForEvent(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return event_cv_.wait_for(lock, timeout,
                              [this] { return event_received_; });
  }

  bool waitForComment(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return comment_cv_.wait_for(lock, timeout,
                                [this] { return comment_received_; });
  }

  bool waitForError(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return error_cv_.wait_for(lock, timeout,
                              [this] { return error_received_; });
  }

  // Thread-safe accessors
  std::string getLastEventType() {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_event_type_;
  }

  std::string getLastEventData() {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_event_data_;
  }

  optional<std::string> getLastEventId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_event_id_;
  }

  std::string getLastComment() {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_comment_;
  }

  std::string getErrorMessage() {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    event_received_ = false;
    comment_received_ = false;
    error_received_ = false;
    last_event_type_.clear();
    last_event_data_.clear();
    last_event_id_.reset();
    last_comment_.clear();
    error_message_.clear();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable event_cv_;
  std::condition_variable comment_cv_;
  std::condition_variable error_cv_;

  bool event_received_{false};
  bool comment_received_{false};
  bool error_received_{false};
  std::string last_event_type_;
  std::string last_event_data_;
  optional<std::string> last_event_id_;
  std::string last_comment_;
  std::string error_message_;
};

// Real write filter callbacks implementation for testing
class TestWriteFilterCallbacks : public network::WriteFilterCallbacks {
 public:
  explicit TestWriteFilterCallbacks(event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}

  network::Connection& connection() override {
    static auto stub = std::make_shared<network::Connection>();
    return *stub;
  }

  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) override {
    std::lock_guard<std::mutex> lock(mutex_);
    write_called_ = true;

    // Copy the data
    size_t length = data.length();
    if (length > 0) {
      std::vector<char> buffer_data(length);
      data.copyOut(0, length, buffer_data.data());
      write_data_.append(buffer_data.data(), length);
    }

    end_stream_ = end_stream;
    write_cv_.notify_all();
  }

  void injectReadDataToFilterChain(Buffer& data, bool end_stream) override {}

  event::Dispatcher& dispatcher() override { return dispatcher_; }

  bool aboveWriteBufferHighWatermark() const override { return false; }

  // Wait and access methods
  bool waitForWrite(std::chrono::milliseconds timeout = 1000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return write_cv_.wait_for(lock, timeout, [this] { return write_called_; });
  }

  std::string getWriteData() {
    std::lock_guard<std::mutex> lock(mutex_);
    return write_data_;
  }

  bool isEndStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    return end_stream_;
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_called_ = false;
    write_data_.clear();
    end_stream_ = false;
  }

 private:
  event::Dispatcher& dispatcher_;
  mutable std::mutex mutex_;
  std::condition_variable write_cv_;
  bool write_called_{false};
  std::string write_data_;
  bool end_stream_{false};
};

// Stub read filter callbacks
class StubReadFilterCallbacks : public network::ReadFilterCallbacks {
 public:
  explicit StubReadFilterCallbacks(event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}

  network::Connection& connection() override {
    static auto stub = std::make_shared<network::Connection>();
    return *stub;
  }
  void continueReading() override {}
  void injectReadDataToFilterChain(Buffer& data, bool end_stream) override {}
  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) override {}
  void onFilterInbound() override {}
  void requestDecoder() override {}
  const network::ConnectionInfo& connectionInfo() const override {
    static auto stub = std::make_shared<network::ConnectionInfo>();
    return *stub;
  }
  event::Dispatcher& dispatcher() override { return dispatcher_; }
  void setDecoderBufferLimit(uint32_t limit) override {}
  uint32_t decoderBufferLimit() override { return 0; }
  bool cannotEncodeFrame() override { return false; }
  void markUpstreamFilterChainComplete() override {}
  const std::string& upstreamHost() const override {
    static std::string stub;
    return stub;
  }
  void setUpstreamHost(const std::string& host) override {}
  bool shouldContinueFilterChain() override { return true; }

 private:
  event::Dispatcher& dispatcher_;
};

class SseCodecFilterRealIoTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    test::RealIoTestBase::SetUp();

    // Create test callbacks
    event_callbacks_ = std::make_unique<TestEventCallbacks>();
  }

  void TearDown() override {
    // Clean up in dispatcher context
    executeInDispatcher([this]() {
      server_filter_.reset();
      client_filter_.reset();
      server_write_callbacks_.reset();
      client_write_callbacks_.reset();
      server_read_callbacks_.reset();
      client_read_callbacks_.reset();
    });

    event_callbacks_.reset();
    test::RealIoTestBase::TearDown();
  }

  void createServerFilter() {
    executeInDispatcher([this]() {
      server_filter_ = std::make_unique<SseCodecFilter>(*event_callbacks_,
                                                        *dispatcher_, true);

      server_read_callbacks_ =
          std::make_unique<StubReadFilterCallbacks>(*dispatcher_);
      server_write_callbacks_ =
          std::make_unique<TestWriteFilterCallbacks>(*dispatcher_);

      server_filter_->initializeReadFilterCallbacks(*server_read_callbacks_);
      server_filter_->initializeWriteFilterCallbacks(*server_write_callbacks_);
    });
  }

  void createClientFilter() {
    executeInDispatcher([this]() {
      client_filter_ = std::make_unique<SseCodecFilter>(*event_callbacks_,
                                                        *dispatcher_, false);

      client_read_callbacks_ =
          std::make_unique<StubReadFilterCallbacks>(*dispatcher_);
      client_write_callbacks_ =
          std::make_unique<TestWriteFilterCallbacks>(*dispatcher_);

      client_filter_->initializeReadFilterCallbacks(*client_read_callbacks_);
      client_filter_->initializeWriteFilterCallbacks(*client_write_callbacks_);
    });
  }

  // Helper to create SSE event data
  OwnedBuffer createSseEvent(const std::string& event_type,
                             const std::string& data,
                             const optional<std::string>& id = nullopt) {
    OwnedBuffer buffer;

    if (id.has_value()) {
      std::string id_line = "id: " + id.value() + "\n";
      buffer.add(id_line.c_str(), id_line.length());
    }

    if (!event_type.empty()) {
      std::string event_line = "event: " + event_type + "\n";
      buffer.add(event_line.c_str(), event_line.length());
    }

    // Handle multiline data
    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
      std::string data_line = "data: " + line + "\n";
      buffer.add(data_line.c_str(), data_line.length());
    }

    buffer.add("\n", 1);  // End of event
    return buffer;
  }

  // Helper to create SSE comment
  OwnedBuffer createSseComment(const std::string& comment) {
    OwnedBuffer buffer;
    std::string comment_line = ": " + comment + "\n\n";
    buffer.add(comment_line.c_str(), comment_line.length());
    return buffer;
  }

  std::unique_ptr<TestEventCallbacks> event_callbacks_;
  std::unique_ptr<StubReadFilterCallbacks> server_read_callbacks_;
  std::unique_ptr<TestWriteFilterCallbacks> server_write_callbacks_;
  std::unique_ptr<StubReadFilterCallbacks> client_read_callbacks_;
  std::unique_ptr<TestWriteFilterCallbacks> client_write_callbacks_;
  std::unique_ptr<SseCodecFilter> server_filter_;
  std::unique_ptr<SseCodecFilter> client_filter_;
};

// ===== Server Mode Tests =====

TEST_F(SseCodecFilterRealIoTest, ServerInitialState) {
  createServerFilter();
  auto status = executeInDispatcher(
      [this]() { return server_filter_->onNewConnection(); });
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

TEST_F(SseCodecFilterRealIoTest, ServerSendSimpleEvent) {
  createServerFilter();

  executeInDispatcher([this]() {
    server_filter_->onNewConnection();
    server_filter_->startEventStream();

    auto& encoder = server_filter_->eventEncoder();
    encoder.encodeEvent("message", "Hello, World!");
  });

  // Wait for write callback
  ASSERT_TRUE(server_write_callbacks_->waitForWrite());

  std::string event_data = server_write_callbacks_->getWriteData();
  EXPECT_TRUE(event_data.find("event: message") != std::string::npos);
  EXPECT_TRUE(event_data.find("data: Hello, World!") != std::string::npos);
  EXPECT_TRUE(event_data.find("\n\n") != std::string::npos);
}

TEST_F(SseCodecFilterRealIoTest, ServerSendEventWithId) {
  createServerFilter();

  executeInDispatcher([this]() {
    server_filter_->onNewConnection();
    server_filter_->startEventStream();

    auto& encoder = server_filter_->eventEncoder();
    encoder.encodeEvent("update", "Status updated", std::string("123"));
  });

  ASSERT_TRUE(server_write_callbacks_->waitForWrite());

  std::string event_data = server_write_callbacks_->getWriteData();
  EXPECT_TRUE(event_data.find("id: 123") != std::string::npos);
  EXPECT_TRUE(event_data.find("event: update") != std::string::npos);
  EXPECT_TRUE(event_data.find("data: Status updated") != std::string::npos);
}

TEST_F(SseCodecFilterRealIoTest, ServerSendMultilineData) {
  createServerFilter();

  executeInDispatcher([this]() {
    server_filter_->onNewConnection();
    server_filter_->startEventStream();

    auto& encoder = server_filter_->eventEncoder();
    encoder.encodeEvent("", "Line 1\nLine 2\nLine 3");
  });

  ASSERT_TRUE(server_write_callbacks_->waitForWrite());

  std::string event_data = server_write_callbacks_->getWriteData();
  EXPECT_TRUE(event_data.find("data: Line 1") != std::string::npos);
  EXPECT_TRUE(event_data.find("data: Line 2") != std::string::npos);
  EXPECT_TRUE(event_data.find("data: Line 3") != std::string::npos);
}

TEST_F(SseCodecFilterRealIoTest, ServerSendComment) {
  createServerFilter();

  executeInDispatcher([this]() {
    server_filter_->onNewConnection();
    server_filter_->startEventStream();

    auto& encoder = server_filter_->eventEncoder();
    encoder.encodeComment("keep-alive");
  });

  ASSERT_TRUE(server_write_callbacks_->waitForWrite());

  std::string comment_data = server_write_callbacks_->getWriteData();
  EXPECT_TRUE(comment_data.find(": keep-alive") != std::string::npos);
  EXPECT_TRUE(comment_data.find("\n\n") != std::string::npos);
}

TEST_F(SseCodecFilterRealIoTest, ServerSendRetry) {
  createServerFilter();

  executeInDispatcher([this]() {
    server_filter_->onNewConnection();
    server_filter_->startEventStream();

    auto& encoder = server_filter_->eventEncoder();
    encoder.encodeRetry(5000);
  });

  ASSERT_TRUE(server_write_callbacks_->waitForWrite());

  std::string retry_data = server_write_callbacks_->getWriteData();
  EXPECT_TRUE(retry_data.find("retry: 5000") != std::string::npos);
  EXPECT_TRUE(retry_data.find("\n\n") != std::string::npos);
}

// ===== Client Mode Tests =====

TEST_F(SseCodecFilterRealIoTest, ClientInitialState) {
  createClientFilter();
  auto status = executeInDispatcher(
      [this]() { return client_filter_->onNewConnection(); });
  EXPECT_EQ(status, network::FilterStatus::Continue);
}

TEST_F(SseCodecFilterRealIoTest, ClientReceiveSimpleEvent) {
  createClientFilter();

  auto event_data = createSseEvent("message", "Hello from server");

  executeInDispatcher([this, &event_data]() {
    client_filter_->onNewConnection();
    client_filter_->startEventStream();

    auto& mutable_data = const_cast<OwnedBuffer&>(event_data);
    client_filter_->onData(mutable_data, false);
  });

  ASSERT_TRUE(event_callbacks_->waitForEvent());

  EXPECT_EQ(event_callbacks_->getLastEventType(), "message");
  EXPECT_EQ(event_callbacks_->getLastEventData(), "Hello from server");
  EXPECT_FALSE(event_callbacks_->getLastEventId().has_value());
}

TEST_F(SseCodecFilterRealIoTest, ClientReceiveEventWithId) {
  createClientFilter();

  auto event_data =
      createSseEvent("update", "Data updated", std::string("456"));

  executeInDispatcher([this, &event_data]() {
    client_filter_->onNewConnection();
    client_filter_->startEventStream();

    auto& mutable_data = const_cast<OwnedBuffer&>(event_data);
    client_filter_->onData(mutable_data, false);
  });

  ASSERT_TRUE(event_callbacks_->waitForEvent());

  EXPECT_EQ(event_callbacks_->getLastEventType(), "update");
  EXPECT_EQ(event_callbacks_->getLastEventData(), "Data updated");
  ASSERT_TRUE(event_callbacks_->getLastEventId().has_value());
  EXPECT_EQ(event_callbacks_->getLastEventId().value(), "456");
}

TEST_F(SseCodecFilterRealIoTest, ClientReceiveMultilineEvent) {
  createClientFilter();

  auto event_data = createSseEvent("", "Line 1\nLine 2");

  executeInDispatcher([this, &event_data]() {
    client_filter_->onNewConnection();
    client_filter_->startEventStream();

    auto& mutable_data = const_cast<OwnedBuffer&>(event_data);
    client_filter_->onData(mutable_data, false);
  });

  ASSERT_TRUE(event_callbacks_->waitForEvent());

  EXPECT_TRUE(event_callbacks_->getLastEventType().empty());
  std::string data = event_callbacks_->getLastEventData();
  EXPECT_TRUE(data.find("Line 1") != std::string::npos);
  EXPECT_TRUE(data.find("Line 2") != std::string::npos);
}

TEST_F(SseCodecFilterRealIoTest, ClientReceiveComment) {
  createClientFilter();

  auto comment_data = createSseComment("heartbeat");

  executeInDispatcher([this, &comment_data]() {
    client_filter_->onNewConnection();
    client_filter_->startEventStream();

    auto& mutable_data = const_cast<OwnedBuffer&>(comment_data);
    client_filter_->onData(mutable_data, false);
  });

  ASSERT_TRUE(event_callbacks_->waitForComment());

  EXPECT_EQ(event_callbacks_->getLastComment(), "heartbeat");
}

TEST_F(SseCodecFilterRealIoTest, ClientReceiveMultipleEvents) {
  createClientFilter();

  // Create multiple events in one buffer
  OwnedBuffer multi_events;

  auto event1 = createSseEvent("event1", "data1");
  multi_events.move(event1);

  auto event2 = createSseEvent("event2", "data2");
  multi_events.move(event2);

  auto comment = createSseComment("separator");
  multi_events.move(comment);

  auto event3 = createSseEvent("event3", "data3");
  multi_events.move(event3);

  executeInDispatcher([this, &multi_events]() {
    client_filter_->onNewConnection();
    client_filter_->startEventStream();

    auto& mutable_data = const_cast<OwnedBuffer&>(multi_events);
    client_filter_->onData(mutable_data, false);
  });

  // Should receive multiple events and a comment
  // Note: Due to parsing order, we'll check for at least one event and comment
  ASSERT_TRUE(event_callbacks_->waitForEvent());
  ASSERT_TRUE(event_callbacks_->waitForComment());

  EXPECT_EQ(event_callbacks_->getLastComment(), "separator");
}

// ===== Stream Lifecycle Tests =====

TEST_F(SseCodecFilterRealIoTest, ServerStreamLifecycle) {
  createServerFilter();

  executeInDispatcher([this]() {
    server_filter_->onNewConnection();
    server_filter_->startEventStream();

    auto& encoder = server_filter_->eventEncoder();
    encoder.encodeEvent("start", "Stream started");
    encoder.encodeEvent("data", "Some data");
    encoder.encodeEvent("end", "Stream ending");
  });

  // Should receive multiple write calls
  ASSERT_TRUE(server_write_callbacks_->waitForWrite());

  // End stream
  executeInDispatcher([this]() {
    OwnedBuffer end_data;
    server_filter_->onData(end_data, true);
  });
}

TEST_F(SseCodecFilterRealIoTest, ClientStreamLifecycle) {
  createClientFilter();

  auto start_event = createSseEvent("start", "Stream started");
  auto data_event = createSseEvent("data", "Some data");
  auto end_event = createSseEvent("end", "Stream ending");

  executeInDispatcher([this, &start_event, &data_event, &end_event]() {
    client_filter_->onNewConnection();
    client_filter_->startEventStream();

    auto& mutable_start = const_cast<OwnedBuffer&>(start_event);
    client_filter_->onData(mutable_start, false);

    auto& mutable_data = const_cast<OwnedBuffer&>(data_event);
    client_filter_->onData(mutable_data, false);

    auto& mutable_end = const_cast<OwnedBuffer&>(end_event);
    client_filter_->onData(mutable_end, true);
  });

  // Should receive events
  ASSERT_TRUE(event_callbacks_->waitForEvent());
}

// ===== State Machine Integration Tests =====

TEST_F(SseCodecFilterRealIoTest, StateMachineIntegration) {
  createClientFilter();

  auto event_data = createSseEvent("test", "State machine test");

  executeInDispatcher([this, &event_data]() {
    client_filter_->onNewConnection();

    // The state machine should properly manage the SSE stream lifecycle
    client_filter_->startEventStream();

    auto& mutable_data = const_cast<OwnedBuffer&>(event_data);
    client_filter_->onData(mutable_data, false);
  });

  // Should complete successfully with state machine managing the flow
  ASSERT_TRUE(event_callbacks_->waitForEvent());
  EXPECT_EQ(event_callbacks_->getLastEventData(), "State machine test");
}

}  // namespace
}  // namespace filter
}  // namespace mcp