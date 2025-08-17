/**
 * @file test_sse_codec_filter.cc
 * @brief Comprehensive tests for SSE codec filter
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <memory>
#include <string>

#include "mcp/filter/sse_codec_filter.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/buffer.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::InSequence;
using ::testing::StrictMock;

// Mock event callbacks
class MockEventCallbacks : public SseCodecFilter::EventCallbacks {
public:
  MOCK_METHOD(void, onEvent, 
              (const std::string& event, const std::string& data, const optional<std::string>& id), 
              (override));
  MOCK_METHOD(void, onComment, (const std::string& comment), (override));
  MOCK_METHOD(void, onError, (const std::string& error), (override));
};

// Mock filter callbacks
class MockReadFilterCallbacks : public network::ReadFilterCallbacks {
public:
  MOCK_METHOD(network::Connection&, connection, (), (override));
  MOCK_METHOD(void, continueReading, (), (override));
  MOCK_METHOD(void, injectReadDataToFilterChain, 
              (Buffer& data, bool end_stream), (override));
  MOCK_METHOD(void, injectWriteDataToFilterChain, 
              (Buffer& data, bool end_stream), (override));
  MOCK_METHOD(void, onFilterInbound, (), (override));
  MOCK_METHOD(void, requestDecoder, (), (override));
  MOCK_METHOD(const network::ConnectionInfo&, connectionInfo, (), (const, override));
  MOCK_METHOD(event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, setDecoderBufferLimit, (uint32_t limit), (override));
  MOCK_METHOD(uint32_t, decoderBufferLimit, (), (override));
  MOCK_METHOD(bool, cannotEncodeFrame, (), (override));
  MOCK_METHOD(void, markUpstreamFilterChainComplete, (), (override));
};

class MockWriteFilterCallbacks : public network::WriteFilterCallbacks {
public:
  MOCK_METHOD(network::Connection&, connection, (), (override));
  MOCK_METHOD(void, injectWriteDataToFilterChain, 
              (Buffer& data, bool end_stream), (override));
  MOCK_METHOD(void, injectReadDataToFilterChain, 
              (Buffer& data, bool end_stream), (override));
  MOCK_METHOD(event::Dispatcher&, dispatcher, (), (override));
};

class SseCodecFilterTest : public ::testing::Test {
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
    server_filter_ = std::make_unique<SseCodecFilter>(event_callbacks_, *dispatcher_, true);
    server_filter_->initializeReadFilterCallbacks(read_callbacks_);
    server_filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  void createClientFilter() {
    client_filter_ = std::make_unique<SseCodecFilter>(event_callbacks_, *dispatcher_, false);
    client_filter_->initializeReadFilterCallbacks(read_callbacks_);
    client_filter_->initializeWriteFilterCallbacks(write_callbacks_);
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

  // Helper to run dispatcher for a duration
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  StrictMock<MockEventCallbacks> event_callbacks_;
  StrictMock<MockReadFilterCallbacks> read_callbacks_;
  StrictMock<MockWriteFilterCallbacks> write_callbacks_;
  std::unique_ptr<SseCodecFilter> server_filter_;
  std::unique_ptr<SseCodecFilter> client_filter_;
};

// ===== Server Mode Tests =====

TEST_F(SseCodecFilterTest, ServerInitialState) {
  createServerFilter();
  EXPECT_EQ(server_filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(SseCodecFilterTest, ServerSendSimpleEvent) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      // Verify SSE event format
      size_t length = data.length();
      std::vector<char> event_data(length);
      data.copyOut(0, length, event_data.data());
      std::string event(event_data.begin(), event_data.end());
      
      EXPECT_TRUE(event.find("event: message") != std::string::npos);
      EXPECT_TRUE(event.find("data: Hello, World!") != std::string::npos);
      EXPECT_TRUE(event.find("\n\n") != std::string::npos);
    });
  
  server_filter_->startEventStream();
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("message", "Hello, World!");
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ServerSendEventWithId) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> event_data(length);
      data.copyOut(0, length, event_data.data());
      std::string event(event_data.begin(), event_data.end());
      
      EXPECT_TRUE(event.find("id: 123") != std::string::npos);
      EXPECT_TRUE(event.find("event: update") != std::string::npos);
      EXPECT_TRUE(event.find("data: Status updated") != std::string::npos);
    });
  
  server_filter_->startEventStream();
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("update", "Status updated", std::string("123"));
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ServerSendMultilineData) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> event_data(length);
      data.copyOut(0, length, event_data.data());
      std::string event(event_data.begin(), event_data.end());
      
      EXPECT_TRUE(event.find("data: Line 1") != std::string::npos);
      EXPECT_TRUE(event.find("data: Line 2") != std::string::npos);
      EXPECT_TRUE(event.find("data: Line 3") != std::string::npos);
    });
  
  server_filter_->startEventStream();
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("", "Line 1\nLine 2\nLine 3");
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ServerSendComment) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> comment_data(length);
      data.copyOut(0, length, comment_data.data());
      std::string comment(comment_data.begin(), comment_data.end());
      
      EXPECT_TRUE(comment.find(": keep-alive") != std::string::npos);
      EXPECT_TRUE(comment.find("\n\n") != std::string::npos);
    });
  
  server_filter_->startEventStream();
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeComment("keep-alive");
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ServerSendRetry) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> retry_data(length);
      data.copyOut(0, length, retry_data.data());
      std::string retry(retry_data.begin(), retry_data.end());
      
      EXPECT_TRUE(retry.find("retry: 5000") != std::string::npos);
      EXPECT_TRUE(retry.find("\n\n") != std::string::npos);
    });
  
  server_filter_->startEventStream();
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeRetry(5000);
  
  runFor(10ms);
}

// ===== Client Mode Tests =====

TEST_F(SseCodecFilterTest, ClientInitialState) {
  createClientFilter();
  EXPECT_EQ(client_filter_->onNewConnection(), network::FilterStatus::Continue);
}

TEST_F(SseCodecFilterTest, ClientReceiveSimpleEvent) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  EXPECT_CALL(event_callbacks_, onEvent("message", "Hello from server", _))
    .WillOnce([](const std::string& event, const std::string& data, const auto& id) {
      EXPECT_EQ(event, "message");
      EXPECT_EQ(data, "Hello from server");
      EXPECT_FALSE(id.has_value());
    });
  
  auto event_data = createSseEvent("message", "Hello from server");
  EXPECT_EQ(client_filter_->onData(event_data, false), network::FilterStatus::Continue);
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ClientReceiveEventWithId) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  EXPECT_CALL(event_callbacks_, onEvent("update", "Data updated", _))
    .WillOnce([](const std::string& event, const std::string& data, const auto& id) {
      EXPECT_EQ(event, "update");
      EXPECT_EQ(data, "Data updated");
      EXPECT_TRUE(id.has_value());
      EXPECT_EQ(id.value(), "456");
    });
  
  auto event_data = createSseEvent("update", "Data updated", std::string("456"));
  EXPECT_EQ(client_filter_->onData(event_data, false), network::FilterStatus::Continue);
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ClientReceiveMultilineEvent) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  EXPECT_CALL(event_callbacks_, onEvent("", _, _))
    .WillOnce([](const std::string& event, const std::string& data, const auto& id) {
      EXPECT_TRUE(event.empty());
      EXPECT_TRUE(data.find("Line 1") != std::string::npos);
      EXPECT_TRUE(data.find("Line 2") != std::string::npos);
    });
  
  auto event_data = createSseEvent("", "Line 1\nLine 2");
  EXPECT_EQ(client_filter_->onData(event_data, false), network::FilterStatus::Continue);
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ClientReceiveComment) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  EXPECT_CALL(event_callbacks_, onComment("heartbeat"))
    .WillOnce([](const std::string& comment) {
      EXPECT_EQ(comment, "heartbeat");
    });
  
  auto comment_data = createSseComment("heartbeat");
  EXPECT_EQ(client_filter_->onData(comment_data, false), network::FilterStatus::Continue);
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ClientReceiveMultipleEvents) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  {
    InSequence seq;
    EXPECT_CALL(event_callbacks_, onEvent("event1", "data1", _));
    EXPECT_CALL(event_callbacks_, onEvent("event2", "data2", _));
    EXPECT_CALL(event_callbacks_, onComment("separator"));
    EXPECT_CALL(event_callbacks_, onEvent("event3", "data3", _));
  }
  
  // Send multiple events in one buffer
  OwnedBuffer multi_events;
  
  auto event1 = createSseEvent("event1", "data1");
  multi_events.move(event1);
  
  auto event2 = createSseEvent("event2", "data2");
  multi_events.move(event2);
  
  auto comment = createSseComment("separator");
  multi_events.move(comment);
  
  auto event3 = createSseEvent("event3", "data3");
  multi_events.move(event3);
  
  EXPECT_EQ(client_filter_->onData(multi_events, false), network::FilterStatus::Continue);
  runFor(10ms);
}

// ===== Stream Lifecycle Tests =====

TEST_F(SseCodecFilterTest, ServerStreamLifecycle) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  // Start stream
  server_filter_->startEventStream();
  
  // Send several events
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .Times(3);
  
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("start", "Stream started");
  encoder.encodeEvent("data", "Some data");
  encoder.encodeEvent("end", "Stream ending");
  
  runFor(20ms);
  
  // End stream
  OwnedBuffer end_data;
  EXPECT_EQ(server_filter_->onData(end_data, true), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ClientStreamLifecycle) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  // Receive events
  EXPECT_CALL(event_callbacks_, onEvent("start", "Stream started", _));
  EXPECT_CALL(event_callbacks_, onEvent("data", "Some data", _));
  EXPECT_CALL(event_callbacks_, onEvent("end", "Stream ending", _));
  
  auto start_event = createSseEvent("start", "Stream started");
  EXPECT_EQ(client_filter_->onData(start_event, false), network::FilterStatus::Continue);
  
  auto data_event = createSseEvent("data", "Some data");
  EXPECT_EQ(client_filter_->onData(data_event, false), network::FilterStatus::Continue);
  
  auto end_event = createSseEvent("end", "Stream ending");
  EXPECT_EQ(client_filter_->onData(end_event, true), network::FilterStatus::Continue);
  
  runFor(20ms);
}

// ===== Error Handling Tests =====

TEST_F(SseCodecFilterTest, ClientMalformedEvent) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  EXPECT_CALL(event_callbacks_, onError(_))
    .WillOnce([](const std::string& error) {
      EXPECT_FALSE(error.empty());
    });
  
  // Send malformed SSE data
  OwnedBuffer malformed;
  malformed.add("invalid sse data\nwithout proper format\n\n", 40);
  
  EXPECT_EQ(client_filter_->onData(malformed, false), network::FilterStatus::Continue);
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, PartialEventData) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  // Send partial event (no terminating newlines)
  OwnedBuffer partial;
  partial.add("event: partial\ndata: incomplete", 31);
  
  // Should not trigger callback yet
  EXPECT_EQ(client_filter_->onData(partial, false), network::FilterStatus::Continue);
  runFor(10ms);
  
  // Complete the event
  EXPECT_CALL(event_callbacks_, onEvent("partial", "incomplete data", _));
  
  OwnedBuffer completion;
  completion.add(" data\n\n", 7);
  
  EXPECT_EQ(client_filter_->onData(completion, false), network::FilterStatus::Continue);
  runFor(10ms);
}

// ===== Performance Tests =====

TEST_F(SseCodecFilterTest, HighFrequencyEvents) {
  createServerFilter();
  server_filter_->onNewConnection();
  server_filter_->startEventStream();
  
  const int num_events = 1000;
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .Times(num_events);
  
  auto& encoder = server_filter_->eventEncoder();
  
  for (int i = 0; i < num_events; ++i) {
    encoder.encodeEvent("data", "Event " + std::to_string(i));
    
    if (i % 100 == 0) {
      runFor(1ms);  // Periodic processing
    }
  }
  
  runFor(50ms);  // Final processing
}

TEST_F(SseCodecFilterTest, LargeEventData) {
  createServerFilter();
  server_filter_->onNewConnection();
  server_filter_->startEventStream();
  
  // Create large event data (100KB)
  std::string large_data(100 * 1024, 'X');
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([&large_data](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> event_data(length);
      data.copyOut(0, length, event_data.data());
      std::string event(event_data.begin(), event_data.end());
      
      EXPECT_TRUE(event.find("data: " + large_data) != std::string::npos);
    });
  
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("large", large_data);
  
  runFor(100ms);  // Allow more time for large data
}

// ===== Keep-Alive Tests =====

TEST_F(SseCodecFilterTest, KeepAliveComments) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  // Expect keep-alive comments to be sent periodically
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .Times(::testing::AtLeast(1))
    .WillRepeatedly([](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> comment_data(length);
      data.copyOut(0, length, comment_data.data());
      std::string comment(comment_data.begin(), comment_data.end());
      
      EXPECT_TRUE(comment.find(": keep-alive") != std::string::npos);
    });
  
  server_filter_->startEventStream();
  
  // Wait for keep-alive to trigger
  runFor(35000ms);  // Should trigger at least one keep-alive
}

// ===== JSON Event Tests =====

TEST_F(SseCodecFilterTest, JsonEventData) {
  createServerFilter();
  server_filter_->onNewConnection();
  
  std::string json_data = R"({"type":"notification","message":"Hello","timestamp":1234567890})";
  
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([&json_data](Buffer& data, bool end_stream) {
      size_t length = data.length();
      std::vector<char> event_data(length);
      data.copyOut(0, length, event_data.data());
      std::string event(event_data.begin(), event_data.end());
      
      EXPECT_TRUE(event.find("event: json") != std::string::npos);
      EXPECT_TRUE(event.find("data: " + json_data) != std::string::npos);
    });
  
  server_filter_->startEventStream();
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("json", json_data);
  
  runFor(10ms);
}

TEST_F(SseCodecFilterTest, ClientReceiveJsonEvent) {
  createClientFilter();
  client_filter_->onNewConnection();
  client_filter_->startEventStream();
  
  std::string json_data = R"({"status":"success","data":[1,2,3]})";
  
  EXPECT_CALL(event_callbacks_, onEvent("json", json_data, _))
    .WillOnce([&json_data](const std::string& event, const std::string& data, const auto& id) {
      EXPECT_EQ(event, "json");
      EXPECT_EQ(data, json_data);
    });
  
  auto event_data = createSseEvent("json", json_data);
  EXPECT_EQ(client_filter_->onData(event_data, false), network::FilterStatus::Continue);
  
  runFor(10ms);
}

// ===== Bidirectional Communication Tests =====

TEST_F(SseCodecFilterTest, ServerClientCommunication) {
  createServerFilter();
  createClientFilter();
  
  // Setup both filters
  server_filter_->onNewConnection();
  client_filter_->onNewConnection();
  
  server_filter_->startEventStream();
  client_filter_->startEventStream();
  
  // Server sends, client receives
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false))
    .WillOnce([this](Buffer& data, bool end_stream) {
      // Simulate data going from server to client
      EXPECT_CALL(event_callbacks_, onEvent("greeting", "Hello Client", _));
      EXPECT_EQ(client_filter_->onData(data, false), network::FilterStatus::Continue);
    });
  
  auto& encoder = server_filter_->eventEncoder();
  encoder.encodeEvent("greeting", "Hello Client");
  
  runFor(10ms);
}

} // namespace
} // namespace filter
} // namespace mcp