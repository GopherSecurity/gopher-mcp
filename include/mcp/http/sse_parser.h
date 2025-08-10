#ifndef MCP_HTTP_SSE_PARSER_H
#define MCP_HTTP_SSE_PARSER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"

namespace mcp {
namespace http {

/**
 * Server-Sent Event structure
 */
struct SseEvent {
  optional<std::string> id;      // Event ID for resumption
  optional<std::string> event;   // Event type
  std::string data;              // Event data (can be multiline)
  optional<uint64_t> retry;      // Reconnection time in milliseconds
  
  SseEvent() = default;
  
  // Clear the event for reuse
  void clear() {
    id.reset();
    event.reset();
    data.clear();
    retry.reset();
  }
  
  // Check if event has content
  bool hasContent() const {
    return !data.empty() || id.has_value() || event.has_value() || retry.has_value();
  }
};

/**
 * SSE parser callbacks
 */
class SseParserCallbacks {
 public:
  virtual ~SseParserCallbacks() = default;
  
  /**
   * Called when a complete SSE event is parsed
   */
  virtual void onSseEvent(const SseEvent& event) = 0;
  
  /**
   * Called when a comment is encountered
   */
  virtual void onSseComment(const std::string& comment) = 0;
  
  /**
   * Called on parser error
   */
  virtual void onSseError(const std::string& error) = 0;
};

/**
 * SSE parser state
 */
enum class SseParserState {
  FieldStart,     // Beginning of a field
  FieldName,      // Parsing field name
  FieldValue,     // Parsing field value
  Comment,        // Parsing comment
  EventEnd        // End of event (empty line)
};

/**
 * Server-Sent Events parser
 * 
 * Parses SSE stream format according to the W3C specification:
 * https://www.w3.org/TR/eventsource/
 * 
 * Thread-safety: Parser instances are not thread-safe
 */
class SseParser {
 public:
  /**
   * Create SSE parser
   * @param callbacks Callbacks to invoke for events
   */
  explicit SseParser(SseParserCallbacks* callbacks);
  ~SseParser();
  
  /**
   * Parse SSE data
   * @param data Input data
   * @param length Data length
   * @return Number of bytes consumed
   */
  size_t parse(const char* data, size_t length);
  
  /**
   * Parse SSE data from buffer
   * @param buffer Input buffer (consumed as parsed)
   * @return Number of bytes consumed
   */
  size_t parse(Buffer& buffer);
  
  /**
   * Reset parser state
   */
  void reset();
  
  /**
   * Force dispatch of any pending event
   */
  void flush();
  
  /**
   * Get last event ID (for reconnection)
   */
  const std::string& lastEventId() const { return last_event_id_; }
  
  /**
   * Set last event ID (for reconnection)
   */
  void setLastEventId(const std::string& id) { last_event_id_ = id; }
  
  /**
   * Get retry time in milliseconds
   */
  uint64_t retryTime() const { return retry_time_; }
  
 private:
  // Process a complete line
  void processLine(const std::string& line);
  
  // Process a field
  void processField(const std::string& name, const std::string& value);
  
  // Dispatch current event
  void dispatchEvent();
  
  // Parser state
  SseParserCallbacks* callbacks_;
  SseParserState state_;
  std::string line_buffer_;
  SseEvent current_event_;
  std::string last_event_id_;
  uint64_t retry_time_{3000};  // Default 3 seconds
  
  // Field parsing state
  std::string field_name_;
  std::string field_value_;
};

using SseParserPtr = std::unique_ptr<SseParser>;

/**
 * SSE event builder for creating events
 */
class SseEventBuilder {
 public:
  SseEventBuilder() = default;
  
  // Builder methods
  SseEventBuilder& withId(const std::string& id) {
    event_.id = id;
    return *this;
  }
  
  SseEventBuilder& withEvent(const std::string& event) {
    event_.event = event;
    return *this;
  }
  
  SseEventBuilder& withData(const std::string& data) {
    event_.data = data;
    return *this;
  }
  
  SseEventBuilder& appendData(const std::string& data) {
    if (!event_.data.empty()) {
      event_.data += "\n";
    }
    event_.data += data;
    return *this;
  }
  
  SseEventBuilder& withRetry(uint64_t retry_ms) {
    event_.retry = retry_ms;
    return *this;
  }
  
  // Build the event
  SseEvent build() const { return event_; }
  
  // Serialize to SSE format
  std::string serialize() const;
  
  // Serialize to buffer
  void serialize(Buffer& buffer) const;
  
 private:
  SseEvent event_;
};

/**
 * SSE stream writer for generating SSE streams
 */
class SseStreamWriter {
 public:
  explicit SseStreamWriter(Buffer& buffer) : buffer_(buffer) {}
  
  /**
   * Write an event to the stream
   */
  void writeEvent(const SseEvent& event);
  
  /**
   * Write a comment
   */
  void writeComment(const std::string& comment);
  
  /**
   * Write a keep-alive comment
   */
  void writeKeepAlive() { writeComment("keep-alive"); }
  
  /**
   * Write retry time
   */
  void writeRetry(uint64_t retry_ms);
  
  /**
   * Flush any pending data
   */
  void flush();
  
 private:
  Buffer& buffer_;
};

// Factory functions

/**
 * Create SSE parser
 */
inline SseParserPtr createSseParser(SseParserCallbacks* callbacks) {
  return std::make_unique<SseParser>(callbacks);
}

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_SSE_PARSER_H