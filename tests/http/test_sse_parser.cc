#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/http/sse_parser.h"

namespace mcp {
namespace http {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

class MockSseParserCallbacks : public SseParserCallbacks {
 public:
  MOCK_METHOD(void, onSseEvent, (const SseEvent&), (override));
  MOCK_METHOD(void, onSseError, (const std::string&), (override));
  MOCK_METHOD(void, onSseComment, (const std::string&), (override));
};

class SseParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    callbacks_ = std::make_unique<StrictMock<MockSseParserCallbacks>>();
    parser_ = std::make_unique<SseParser>(callbacks_.get());
  }

  std::unique_ptr<MockSseParserCallbacks> callbacks_;
  std::unique_ptr<SseParser> parser_;
};

// Test simple event with data only
TEST_F(SseParserTest, SimpleDataEvent) {
  const char* sse_stream = "data: Hello World\n\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Hello World", event.data);
    EXPECT_FALSE(event.id.has_value());
    EXPECT_FALSE(event.event.has_value());
    EXPECT_FALSE(event.retry.has_value());
  });

  size_t consumed = parser_->parse(sse_stream, strlen(sse_stream));
  EXPECT_EQ(strlen(sse_stream), consumed);
}

// Test event with all fields
TEST_F(SseParserTest, CompleteEvent) {
  const char* sse_stream =
      "id: 123\n"
      "event: message\n"
      "retry: 5000\n"
      "data: Test data\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Test data", event.data);
    ASSERT_TRUE(event.id.has_value());
    EXPECT_EQ("123", event.id.value());
    ASSERT_TRUE(event.event.has_value());
    EXPECT_EQ("message", event.event.value());
    ASSERT_TRUE(event.retry.has_value());
    EXPECT_EQ(5000, event.retry.value());
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test multi-line data
TEST_F(SseParserTest, MultiLineData) {
  const char* sse_stream =
      "data: First line\n"
      "data: Second line\n"
      "data: Third line\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("First line\nSecond line\nThird line", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test multiple events
TEST_F(SseParserTest, MultipleEvents) {
  const char* sse_stream =
      "data: Event 1\n"
      "\n"
      "data: Event 2\n"
      "\n"
      "data: Event 3\n"
      "\n";

  InSequence seq;
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Event 1", event.data);
  });
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Event 2", event.data);
  });
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Event 3", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test comments
TEST_F(SseParserTest, Comments) {
  const char* sse_stream =
      ": This is a comment\n"
      "data: Some data\n"
      ": Another comment\n"
      "\n";

  InSequence seq;
  EXPECT_CALL(*callbacks_, onSseComment(" This is a comment"));
  EXPECT_CALL(*callbacks_, onSseComment(" Another comment"));
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Some data", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test empty lines (heartbeat)
TEST_F(SseParserTest, EmptyLines) {
  const char* sse_stream = "\n\n\n";

  // Empty lines should not trigger events
  // No expectations set

  size_t consumed = parser_->parse(sse_stream, strlen(sse_stream));
  EXPECT_EQ(strlen(sse_stream), consumed);
}

// Test parsing in chunks
TEST_F(SseParserTest, ChunkedParsing) {
  const char* part1 = "data: Hello";
  const char* part2 = " World\n";
  const char* part3 = "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Hello World", event.data);
  });

  size_t consumed1 = parser_->parse(part1, strlen(part1));
  EXPECT_EQ(strlen(part1), consumed1);

  size_t consumed2 = parser_->parse(part2, strlen(part2));
  EXPECT_EQ(strlen(part2), consumed2);

  size_t consumed3 = parser_->parse(part3, strlen(part3));
  EXPECT_EQ(strlen(part3), consumed3);
}

// Test field without colon (should be ignored)
TEST_F(SseParserTest, FieldWithoutColon) {
  const char* sse_stream =
      "invalid field\n"
      "data: Valid data\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Valid data", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test field with colon but no space
TEST_F(SseParserTest, FieldWithColonNoSpace) {
  const char* sse_stream =
      "data:No space\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("No space", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test field with leading space after colon
TEST_F(SseParserTest, FieldWithLeadingSpace) {
  const char* sse_stream =
      "data:  Two spaces\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    // Leading space after colon should be trimmed (only one)
    EXPECT_EQ(" Two spaces", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test unknown field (should be ignored)
TEST_F(SseParserTest, UnknownField) {
  const char* sse_stream =
      "unknown: field\n"
      "data: Known data\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Known data", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test retry field with invalid value
TEST_F(SseParserTest, InvalidRetryValue) {
  const char* sse_stream =
      "retry: not_a_number\n"
      "data: Test\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Test", event.data);
    EXPECT_FALSE(event.retry.has_value());  // Invalid retry should be ignored
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test very long data
TEST_F(SseParserTest, LongData) {
  std::string long_data(8192, 'X');  // 8KB of data
  std::string sse_stream = "data: " + long_data + "\n\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_))
      .WillOnce([&long_data](const SseEvent& event) {
        EXPECT_EQ(long_data, event.data);
      });

  parser_->parse(sse_stream.c_str(), sse_stream.length());
}

// Test event ID persistence
TEST_F(SseParserTest, EventIdPersistence) {
  const char* sse_stream =
      "id: 1\n"
      "data: First\n"
      "\n"
      "data: Second\n"  // Should inherit ID
      "\n"
      "id: 2\n"
      "data: Third\n"
      "\n";

  InSequence seq;
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("First", event.data);
    ASSERT_TRUE(event.id.has_value());
    EXPECT_EQ("1", event.id.value());
  });
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Second", event.data);
    ASSERT_TRUE(event.id.has_value());
    EXPECT_EQ("1", event.id.value());  // Inherited
  });
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Third", event.data);
    ASSERT_TRUE(event.id.has_value());
    EXPECT_EQ("2", event.id.value());
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test CR LF line endings
TEST_F(SseParserTest, CrLfLineEndings) {
  const char* sse_stream =
      "data: Windows line\r\n"
      "\r\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Windows line", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test CR only line endings
TEST_F(SseParserTest, CrOnlyLineEndings) {
  const char* sse_stream =
      "data: Mac line\r"
      "\r";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Mac line", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test reset functionality
TEST_F(SseParserTest, Reset) {
  const char* sse_stream1 =
      "id: 1\n"
      "data: First\n"
      "\n";

  const char* sse_stream2 =
      "data: Second\n"
      "\n";

  InSequence seq;
  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("First", event.data);
    EXPECT_EQ("1", event.id.value());
  });

  parser_->parse(sse_stream1, strlen(sse_stream1));

  // Reset should clear the last event ID
  parser_->reset();

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Second", event.data);
    EXPECT_FALSE(event.id.has_value());  // ID should be cleared
  });

  parser_->parse(sse_stream2, strlen(sse_stream2));
}

// Test empty data field
TEST_F(SseParserTest, EmptyData) {
  const char* sse_stream =
      "data:\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("", event.data);
  });

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test event without data (should not fire)
TEST_F(SseParserTest, EventWithoutData) {
  const char* sse_stream =
      "id: 123\n"
      "event: test\n"
      "\n";

  // No event should be fired without data
  // No expectations set

  parser_->parse(sse_stream, strlen(sse_stream));
}

// Test very long line (edge case)
TEST_F(SseParserTest, VeryLongLine) {
  std::string long_line = "data: " + std::string(1024, 'A') + "\n\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ(1024, event.data.length());
  });

  parser_->parse(long_line.c_str(), long_line.length());
}

// Test BOM (Byte Order Mark) handling
TEST_F(SseParserTest, ByteOrderMark) {
  // UTF-8 BOM followed by SSE data
  const unsigned char sse_with_bom[] = {0xEF, 0xBB, 0xBF,  // UTF-8 BOM
                                        'd',  'a',  't',  'a', ':',  ' ',
                                        'T',  'e',  's',  't', '\n', '\n'};

  EXPECT_CALL(*callbacks_, onSseEvent(_)).WillOnce([](const SseEvent& event) {
    EXPECT_EQ("Test", event.data);
  });

  // BOM should be handled gracefully
  parser_->parse(reinterpret_cast<const char*>(sse_with_bom),
                 sizeof(sse_with_bom));
}

// Test lastEventId
TEST_F(SseParserTest, LastEventId) {
  EXPECT_EQ("", parser_->lastEventId());

  const char* sse_stream =
      "id: abc123\n"
      "data: Test\n"
      "\n";

  EXPECT_CALL(*callbacks_, onSseEvent(_));

  parser_->parse(sse_stream, strlen(sse_stream));
  EXPECT_EQ("abc123", parser_->lastEventId());
}

// Test rapid event parsing (performance/stress test)
TEST_F(SseParserTest, RapidEvents) {
  std::string sse_stream;
  const int num_events = 1000;

  for (int i = 0; i < num_events; ++i) {
    sse_stream += "data: Event " + std::to_string(i) + "\n\n";
  }

  EXPECT_CALL(*callbacks_, onSseEvent(_)).Times(num_events);

  auto start = std::chrono::high_resolution_clock::now();
  parser_->parse(sse_stream.c_str(), sse_stream.length());
  auto end = std::chrono::high_resolution_clock::now();

  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  // Should parse 1000 events quickly (< 100ms)
  EXPECT_LT(duration.count(), 100);
}

// Test malformed retry value edge cases
TEST_F(SseParserTest, RetryEdgeCases) {
  struct TestCase {
    std::string retry_value;
    bool should_parse;
    uint64_t expected_value;
  };

  std::vector<TestCase> test_cases = {
      {"0", true, 0},
      {"1000", true, 1000},
      {"4294967295", true, 4294967295},  // Max uint32
      {"-1", false, 0},                  // Negative
      {"1.5", false, 0},                 // Float
      {"1e3", false, 0},                 // Scientific notation
      {"", false, 0},                    // Empty
      {" 100", false, 0},                // Leading space
      {"100 ", false, 0},                // Trailing space
  };

  for (const auto& tc : test_cases) {
    parser_->reset();

    std::string sse_stream = "retry: " + tc.retry_value +
                             "\n"
                             "data: Test\n"
                             "\n";

    EXPECT_CALL(*callbacks_, onSseEvent(_))
        .WillOnce([&tc](const SseEvent& event) {
          if (tc.should_parse) {
            ASSERT_TRUE(event.retry.has_value());
            EXPECT_EQ(tc.expected_value, event.retry.value());
          } else {
            EXPECT_FALSE(event.retry.has_value());
          }
        });

    parser_->parse(sse_stream.c_str(), sse_stream.length());
  }
}

}  // namespace
}  // namespace http
}  // namespace mcp