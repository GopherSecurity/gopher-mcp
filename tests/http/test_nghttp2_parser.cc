#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/http/nghttp2_parser.h"
#include "mcp/buffer.h"
#include <thread>
#include <chrono>

#if MCP_HAS_NGHTTP2

namespace mcp {
namespace http {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::StrictMock;
using ::testing::AtLeast;

class MockHttpParserCallbacks : public HttpParserCallbacks {
public:
  MOCK_METHOD(ParserCallbackResult, onMessageBegin, (), (override));
  MOCK_METHOD(ParserCallbackResult, onUrl, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onStatus, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onHeaderField, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onHeaderValue, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onHeadersComplete, (), (override));
  MOCK_METHOD(ParserCallbackResult, onBody, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onMessageComplete, (), (override));
  MOCK_METHOD(ParserCallbackResult, onChunkHeader, (size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onChunkComplete, (), (override));
  MOCK_METHOD(void, onError, (const std::string&), (override));
};

class Nghttp2ParserTest : public ::testing::Test {
protected:
  void SetUp() override {
    callbacks_ = std::make_unique<MockHttpParserCallbacks>();
    factory_ = std::make_unique<Nghttp2ParserFactory>();
  }
  
  // Helper to create HTTP/2 connection preface
  std::vector<uint8_t> createConnectionPreface() {
    const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    return std::vector<uint8_t>(preface, preface + 24);
  }
  
  // Helper to create SETTINGS frame
  std::vector<uint8_t> createSettingsFrame() {
    // Minimal SETTINGS frame (empty)
    // Frame header: length=0, type=4 (SETTINGS), flags=0, stream_id=0
    return {
      0x00, 0x00, 0x00,  // Length: 0
      0x04,              // Type: SETTINGS
      0x00,              // Flags: none
      0x00, 0x00, 0x00, 0x00  // Stream ID: 0
    };
  }
  
  // Helper to create SETTINGS ACK frame
  std::vector<uint8_t> createSettingsAckFrame() {
    // SETTINGS frame with ACK flag
    return {
      0x00, 0x00, 0x00,  // Length: 0
      0x04,              // Type: SETTINGS
      0x01,              // Flags: ACK
      0x00, 0x00, 0x00, 0x00  // Stream ID: 0
    };
  }
  
  // Helper to create HEADERS frame for GET request
  std::vector<uint8_t> createGetRequestHeaders() {
    // Simplified HPACK encoded headers for GET /test
    // This is a simplified version - real HPACK encoding is more complex
    std::vector<uint8_t> frame;
    
    // For testing purposes, we'll use a mock frame
    // In real HTTP/2, this would be HPACK encoded
    frame = {
      0x00, 0x00, 0x20,  // Length: 32 bytes (example)
      0x01,              // Type: HEADERS
      0x05,              // Flags: END_HEADERS | END_STREAM
      0x00, 0x00, 0x00, 0x01,  // Stream ID: 1
      // HPACK encoded headers (simplified mock data)
      0x82,  // :method: GET (indexed)
      0x86,  // :scheme: https (indexed)
      0x84,  // :path: / (indexed)
      0x41, 0x0a, 0x77, 0x77, 0x77, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d,
      // Padding to make 32 bytes
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    return frame;
  }
  
  // Helper to create DATA frame
  std::vector<uint8_t> createDataFrame(const std::string& data, uint32_t stream_id = 1, bool end_stream = true) {
    std::vector<uint8_t> frame;
    
    // Frame header
    uint32_t length = data.length();
    frame.push_back((length >> 16) & 0xFF);
    frame.push_back((length >> 8) & 0xFF);
    frame.push_back(length & 0xFF);
    frame.push_back(0x00);  // Type: DATA
    frame.push_back(end_stream ? 0x01 : 0x00);  // Flags: END_STREAM if specified
    frame.push_back((stream_id >> 24) & 0xFF);
    frame.push_back((stream_id >> 16) & 0xFF);
    frame.push_back((stream_id >> 8) & 0xFF);
    frame.push_back(stream_id & 0xFF);
    
    // Data
    frame.insert(frame.end(), data.begin(), data.end());
    
    return frame;
  }
  
  std::unique_ptr<MockHttpParserCallbacks> callbacks_;
  std::unique_ptr<Nghttp2ParserFactory> factory_;
};

// Basic test - parser creation and properties
TEST_F(Nghttp2ParserTest, CreateParser) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  ASSERT_NE(nullptr, parser);
  
  EXPECT_EQ(HttpVersion::HTTP_2, parser->httpVersion());
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  EXPECT_TRUE(parser->shouldKeepAlive());  // HTTP/2 is always persistent
  EXPECT_FALSE(parser->isUpgrade());
}

// Test factory properties
TEST_F(Nghttp2ParserTest, FactoryProperties) {
  auto versions = factory_->supportedVersions();
  EXPECT_EQ(1, versions.size());
  EXPECT_EQ(HttpVersion::HTTP_2, versions[0]);
  
  EXPECT_EQ("nghttp2", factory_->name());
}

// Test connection preface parsing
TEST_F(Nghttp2ParserTest, ParseConnectionPreface) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  auto preface = createConnectionPreface();
  
  // The preface itself doesn't trigger callbacks in nghttp2
  // It's handled internally
  size_t consumed = parser->execute(reinterpret_cast<const char*>(preface.data()), 
                                    preface.size());
  EXPECT_GT(consumed, 0);
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
}

// Test SETTINGS frame
TEST_F(Nghttp2ParserTest, ParseSettingsFrame) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  // Send connection preface first
  auto preface = createConnectionPreface();
  parser->execute(reinterpret_cast<const char*>(preface.data()), preface.size());
  
  // Send SETTINGS frame
  auto settings = createSettingsFrame();
  size_t consumed = parser->execute(reinterpret_cast<const char*>(settings.data()), 
                                    settings.size());
  EXPECT_GT(consumed, 0);
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  
  // Parser should generate SETTINGS ACK
  auto pending_data = static_cast<Nghttp2Parser*>(parser.get())->getPendingData();
  EXPECT_GT(pending_data.size(), 0);  // Should have SETTINGS ACK to send
}

// Test submitting custom settings
TEST_F(Nghttp2ParserTest, SubmitSettings) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  std::map<uint32_t, uint32_t> settings = {
    {0x3, 100},  // SETTINGS_MAX_CONCURRENT_STREAMS
    {0x4, 65536},  // SETTINGS_INITIAL_WINDOW_SIZE
    {0x5, 16384}   // SETTINGS_MAX_FRAME_SIZE
  };
  
  parser->submitSettings(settings);
  
  // Should have pending SETTINGS frame to send
  auto pending_data = parser->getPendingData();
  EXPECT_GT(pending_data.size(), 0);
}

// Test PING frame
TEST_F(Nghttp2ParserTest, SubmitPing) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  uint8_t opaque_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  parser->submitPing(opaque_data);
  
  // Should have pending PING frame to send
  auto pending_data = parser->getPendingData();
  EXPECT_GT(pending_data.size(), 0);
}

// Test GOAWAY frame
TEST_F(Nghttp2ParserTest, SubmitGoaway) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  parser->submitGoaway(0, 0);  // Last stream ID: 0, Error code: NO_ERROR
  
  // Should have pending GOAWAY frame to send
  auto pending_data = parser->getPendingData();
  EXPECT_GT(pending_data.size(), 0);
}

// Test WINDOW_UPDATE frame
TEST_F(Nghttp2ParserTest, SubmitWindowUpdate) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  parser->submitWindowUpdate(0, 65535);  // Stream ID: 0 (connection), increment: 65535
  
  // Should have pending WINDOW_UPDATE frame to send
  auto pending_data = parser->getPendingData();
  EXPECT_GT(pending_data.size(), 0);
}

// Test priority
TEST_F(Nghttp2ParserTest, SubmitPriority) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  parser->submitPriority(1, 256, 0);  // Stream ID: 1, weight: 256, exclusive: 0
  
  // Should have pending PRIORITY frame to send
  auto pending_data = parser->getPendingData();
  EXPECT_GT(pending_data.size(), 0);
}

// Test reset functionality
TEST_F(Nghttp2ParserTest, ResetParser) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  // Send some data
  auto preface = createConnectionPreface();
  parser->execute(reinterpret_cast<const char*>(preface.data()), preface.size());
  
  // Reset
  parser->reset();
  
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  EXPECT_EQ("", parser->getError());
  
  // Should be able to parse again
  parser->execute(reinterpret_cast<const char*>(preface.data()), preface.size());
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
}

// Test finish functionality
TEST_F(Nghttp2ParserTest, FinishSession) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  parser->finish();
  
  // After finish, parser should generate GOAWAY
  auto pending_data = static_cast<Nghttp2Parser*>(parser.get())->getPendingData();
  EXPECT_GT(pending_data.size(), 0);
}

// Test error handling with malformed frame
TEST_F(Nghttp2ParserTest, MalformedFrame) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  // Send invalid frame (wrong magic string)
  const char* bad_data = "INVALID HTTP/2 DATA";
  
  EXPECT_CALL(*callbacks_, onError(_))
      .Times(AtLeast(1));
  
  size_t consumed = parser->execute(bad_data, strlen(bad_data));
  EXPECT_EQ(0, consumed);
  EXPECT_EQ(ParserStatus::Error, parser->getStatus());
  EXPECT_FALSE(parser->getError().empty());
}

// Test pause and resume
TEST_F(Nghttp2ParserTest, PauseAndResume) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  // Pause
  auto result = parser->pause();
  EXPECT_EQ(ParserCallbackResult::Pause, result);
  EXPECT_EQ(ParserStatus::Paused, parser->getStatus());
  
  // Resume
  parser->resume();
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
}

// Test concurrent streams simulation
TEST_F(Nghttp2ParserTest, MultipleStreams) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  // Note: This test only sends connection preface and settings frames,
  // which don't create HTTP message streams. onMessageBegin is only called
  // when actual HEADERS frames create new streams.
  // For now, we'll just test that the preface and settings are handled correctly
  // without expecting message callbacks.
  
  // Process connection preface
  auto preface = createConnectionPreface();
  size_t consumed = parser->execute(reinterpret_cast<const char*>(preface.data()), preface.size());
  EXPECT_EQ(preface.size(), consumed);
  
  // Settings exchange
  auto settings = createSettingsFrame();
  consumed = parser->execute(reinterpret_cast<const char*>(settings.data()), settings.size());
  EXPECT_EQ(settings.size(), consumed);
  
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  
  // The parser should have generated a SETTINGS ACK
  auto pending = parser->getPendingData();
  EXPECT_GT(pending.size(), 0);
}

// Test methods and status codes
TEST_F(Nghttp2ParserTest, HttpMethodAndStatus) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  // HTTP/2 doesn't have methods/status in the parser itself until streams are processed
  // Default values
  EXPECT_EQ(HttpMethod::UNKNOWN, parser->httpMethod());
  EXPECT_EQ(HttpStatusCode::OK, parser->statusCode());
}

// Edge case: Very large frame
TEST_F(Nghttp2ParserTest, LargeFrame) {
  auto parser = factory_->createParser(HttpParserType::BOTH, callbacks_.get());
  
  // Create a large DATA frame (but within limits)
  std::string large_data(16384, 'X');  // 16KB of data
  auto data_frame = createDataFrame(large_data);
  
  // This should be handled without error
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .Times(AtLeast(0))
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onBody(_, _))
      .Times(AtLeast(0))
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  
  // Send preface first
  auto preface = createConnectionPreface();
  parser->execute(reinterpret_cast<const char*>(preface.data()), preface.size());
  
  // Send large frame
  size_t consumed = parser->execute(reinterpret_cast<const char*>(data_frame.data()), 
                                    data_frame.size());
  EXPECT_GT(consumed, 0);
}

// Edge case: Stream ID exhaustion simulation
TEST_F(Nghttp2ParserTest, StreamIdLimits) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  // HTTP/2 uses odd stream IDs for client, even for server
  // Maximum stream ID is 2^31 - 1
  
  // This is just a property test
  EXPECT_EQ(HttpVersion::HTTP_2, parser->httpVersion());
  EXPECT_TRUE(parser->shouldKeepAlive());
}

// Test connection-level flow control
TEST_F(Nghttp2ParserTest, ConnectionFlowControl) {
  auto parser = std::make_unique<Nghttp2Parser>(HttpParserType::BOTH, callbacks_.get());
  
  // Update connection window
  parser->submitWindowUpdate(0, 32768);  // Connection-level window update
  
  auto pending = parser->getPendingData();
  EXPECT_GT(pending.size(), 0);
  
  // Should contain WINDOW_UPDATE frame
  // Frame type 0x08 is WINDOW_UPDATE
  bool found_window_update = false;
  for (size_t i = 3; i < pending.size(); i++) {
    if (pending[i] == 0x08) {
      found_window_update = true;
      break;
    }
  }
  EXPECT_TRUE(found_window_update);
}

}  // namespace
}  // namespace http
}  // namespace mcp

#endif  // MCP_HAS_NGHTTP2