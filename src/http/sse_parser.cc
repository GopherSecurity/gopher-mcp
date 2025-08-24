#include "mcp/http/sse_parser.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace mcp {
namespace http {

namespace {

// SSE line ending
constexpr char kLF = '\n';
constexpr char kCR = '\r';

// Field separators
constexpr char kColon = ':';
constexpr char kSpace = ' ';

// Field names
constexpr const char* kDataField = "data";
constexpr const char* kEventField = "event";
constexpr const char* kIdField = "id";
constexpr const char* kRetryField = "retry";

// Trim whitespace from both ends of a string
std::string trim(const std::string& str) {
  size_t first = str.find_first_not_of(' ');
  if (first == std::string::npos) {
    return "";
  }
  size_t last = str.find_last_not_of(' ');
  return str.substr(first, (last - first + 1));
}

// Check if string is all digits
bool isAllDigits(const std::string& str) {
  return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

}  // namespace

SseParser::SseParser(SseParserCallbacks* callbacks)
    : callbacks_(callbacks),
      state_(SseParserState::FieldStart),
      retry_time_(3000) {}

SseParser::~SseParser() = default;

size_t SseParser::parse(const char* data, size_t length) {
  // Note: Skip UTF-8 BOM if present at the beginning of the stream
  // The UTF-8 BOM is the sequence 0xEF 0xBB 0xBF
  // Some SSE sources may include this at the start of the stream
  // We should silently skip it to process the actual SSE content
  size_t start = 0;
  if (length >= 3 && !bom_checked_) {
    const unsigned char* udata = reinterpret_cast<const unsigned char*>(data);
    if (udata[0] == 0xEF && udata[1] == 0xBB && udata[2] == 0xBF) {
      start = 3;  // Skip the BOM
    }
    bom_checked_ = true;
  }

  for (size_t i = start; i < length; ++i) {
    char ch = data[i];

    // Handle line endings
    if (ch == kCR) {
      // Check for CRLF
      if (i + 1 < length && data[i + 1] == kLF) {
        processLine(line_buffer_);
        line_buffer_.clear();
        ++i;  // Skip LF
      } else {
        // Just CR
        processLine(line_buffer_);
        line_buffer_.clear();
      }
    } else if (ch == kLF) {
      // Just LF
      processLine(line_buffer_);
      line_buffer_.clear();
    } else {
      // Accumulate characters
      line_buffer_ += ch;
    }
  }

  // Note: Always return the full length as all bytes were consumed
  // Previously, we only updated 'consumed' when a line ending was found,
  // returning 0 for chunks without line endings. This broke chunked parsing
  // where "data: Hello" might come in one chunk and " World\n\n" in another.
  // All bytes are either:
  // 1. Processed as complete lines (when line ending found), or
  // 2. Buffered in line_buffer_ for processing when the line is complete
  // Therefore, we always consume all input bytes.
  return length;
}

size_t SseParser::parse(Buffer& buffer) {
  // Get contiguous data from buffer
  size_t length = buffer.length();
  if (length == 0) {
    return 0;
  }

  // Create temporary buffer for parsing
  std::vector<char> data(length);
  buffer.copyOut(0, length, data.data());

  // Parse the data
  size_t consumed = parse(data.data(), length);

  // Drain consumed bytes from buffer
  if (consumed > 0) {
    buffer.drain(consumed);
  }

  return consumed;
}

void SseParser::reset() {
  state_ = SseParserState::FieldStart;
  line_buffer_.clear();
  current_event_.clear();
  field_name_.clear();
  field_value_.clear();
  // Note: Clear the last event ID on reset
  // The SSE spec states that the last event ID is persistent across events
  // to support resumption, but when the parser is reset (e.g., for a new
  // connection), we should clear it. The test expects reset() to clear all
  // state including the last event ID so subsequent events don't inherit it.
  last_event_id_.clear();
  // Note: Reset BOM check flag
  // When parser is reset for a new stream, we need to check for BOM again
  bom_checked_ = false;
}

void SseParser::flush() {
  // Process any remaining line
  if (!line_buffer_.empty()) {
    processLine(line_buffer_);
    line_buffer_.clear();
  }

  // Dispatch any pending event
  if (current_event_.hasContent()) {
    dispatchEvent();
  }
}

void SseParser::processLine(const std::string& line) {
  // Empty line dispatches the event
  if (line.empty()) {
    if (current_event_.hasContent()) {
      dispatchEvent();
    }
    return;
  }

  // Comment line starts with colon
  if (line[0] == kColon) {
    if (callbacks_) {
      callbacks_->onSseComment(line.substr(1));
    }
    return;
  }

  // Find colon separator
  size_t colon_pos = line.find(kColon);

  if (colon_pos == std::string::npos) {
    // No colon - treat entire line as field name with empty value
    processField(line, "");
  } else {
    // Extract field name and value
    std::string field = line.substr(0, colon_pos);
    std::string value;

    // Skip optional space after colon
    size_t value_start = colon_pos + 1;
    if (value_start < line.length() && line[value_start] == kSpace) {
      value_start++;
    }

    if (value_start < line.length()) {
      value = line.substr(value_start);
    }

    processField(field, value);
  }
}

void SseParser::processField(const std::string& name,
                             const std::string& value) {
  if (name == kDataField) {
    // Note: Mark that we've seen a data field (even if value is empty)
    // This ensures events with "data:" (empty data) are still dispatched
    current_event_.hasDataField = true;

    // Append data with newline if not first data field
    if (!current_event_.data.empty()) {
      current_event_.data += '\n';
    }
    current_event_.data += value;

  } else if (name == kEventField) {
    // Set event type
    current_event_.event = value;

  } else if (name == kIdField) {
    // Set event ID and update last event ID
    current_event_.id = value;
    if (!value.empty()) {
      last_event_id_ = value;
    }

  } else if (name == kRetryField) {
    // Set retry time if value is all digits
    if (isAllDigits(value)) {
      uint64_t retry_ms = std::stoull(value);
      current_event_.retry = retry_ms;
      retry_time_ = retry_ms;
    }
  }
  // Ignore unknown fields per SSE spec
}

void SseParser::dispatchEvent() {
  // Note: Dispatch if we've seen a data field (even if empty)
  // Previously checked !data.empty() which prevented empty data events
  // Now we check hasDataField to allow "data:\n\n" events
  // Per SSE spec: An event is dispatched when we see a data field,
  // regardless of whether the data is empty
  if (current_event_.hasDataField) {
    // Set ID from last event ID if not explicitly set
    if (!current_event_.id.has_value() && !last_event_id_.empty()) {
      current_event_.id = last_event_id_;
    }

    if (callbacks_) {
      callbacks_->onSseEvent(current_event_);
    }
  }

  // Clear current event for next one
  current_event_.clear();
}

// SseEventBuilder implementation

std::string SseEventBuilder::serialize() const {
  std::ostringstream oss;

  // Write id field
  if (event_.id.has_value()) {
    oss << "id: " << event_.id.value() << "\n";
  }

  // Write event field
  if (event_.event.has_value()) {
    oss << "event: " << event_.event.value() << "\n";
  }

  // Write retry field
  if (event_.retry.has_value()) {
    oss << "retry: " << event_.retry.value() << "\n";
  }

  // Write data field(s) - split on newlines
  if (!event_.data.empty()) {
    std::istringstream data_stream(event_.data);
    std::string line;
    while (std::getline(data_stream, line)) {
      oss << "data: " << line << "\n";
    }
  }

  // End with empty line
  oss << "\n";

  return oss.str();
}

void SseEventBuilder::serialize(Buffer& buffer) const {
  std::string serialized = serialize();
  buffer.add(serialized.data(), serialized.length());
}

// SseStreamWriter implementation

void SseStreamWriter::writeEvent(const SseEvent& event) {
  // Write id field
  if (event.id.has_value()) {
    std::string field = "id: " + event.id.value() + "\n";
    buffer_.add(field.data(), field.length());
  }

  // Write event field
  if (event.event.has_value()) {
    std::string field = "event: " + event.event.value() + "\n";
    buffer_.add(field.data(), field.length());
  }

  // Write retry field
  if (event.retry.has_value()) {
    std::string field = "retry: " + std::to_string(event.retry.value()) + "\n";
    buffer_.add(field.data(), field.length());
  }

  // Write data field(s) - split on newlines
  if (!event.data.empty()) {
    std::istringstream data_stream(event.data);
    std::string line;
    while (std::getline(data_stream, line)) {
      std::string field = "data: " + line + "\n";
      buffer_.add(field.data(), field.length());
    }
  }

  // End with empty line
  buffer_.add("\n", 1);
}

void SseStreamWriter::writeComment(const std::string& comment) {
  std::string field = ":" + comment + "\n";
  buffer_.add(field.data(), field.length());
}

void SseStreamWriter::writeRetry(uint64_t retry_ms) {
  std::string field = "retry: " + std::to_string(retry_ms) + "\n\n";
  buffer_.add(field.data(), field.length());
}

void SseStreamWriter::flush() {
  // Add empty line to flush any pending event
  buffer_.add("\n", 1);
}

}  // namespace http
}  // namespace mcp