/**
 * SSE Codec Filter Implementation
 * 
 * Following production architecture:
 * - Handles Server-Sent Events protocol
 * - Works on top of HTTP layer
 * - Completely separate from transport
 */

#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include <sstream>

namespace mcp {
namespace filter {

// Constructor
SseCodecFilter::SseCodecFilter(EventCallbacks& callbacks, bool is_server)
    : event_callbacks_(callbacks),
      is_server_(is_server) {
  
  if (!is_server_) {
    // Client mode - create SSE parser
    parser_ = std::make_unique<http::SseParser>();
    parser_callbacks_ = std::make_unique<ParserCallbacks>(*this);
    parser_->setCallbacks(*parser_callbacks_);
  }
  
  // Create event encoder
  event_encoder_ = std::make_unique<EventEncoderImpl>(*this);
}

SseCodecFilter::~SseCodecFilter() = default;

// network::ReadFilter interface
network::FilterStatus SseCodecFilter::onNewConnection() {
  state_ = CodecState::WAITING_FOR_STREAM;
  return network::FilterStatus::Continue;
}

network::FilterStatus SseCodecFilter::onData(Buffer& data, bool end_stream) {
  if (state_ != CodecState::STREAMING) {
    // Not in SSE mode yet, pass through
    return network::FilterStatus::Continue;
  }
  
  if (!is_server_) {
    // Client mode - parse SSE events
    dispatch(data);
  }
  
  if (end_stream) {
    state_ = CodecState::CLOSED;
  }
  
  return network::FilterStatus::Continue;
}

// network::WriteFilter interface
network::FilterStatus SseCodecFilter::onWrite(Buffer& data, bool end_stream) {
  // Pass through SSE data
  return network::FilterStatus::Continue;
}

void SseCodecFilter::startEventStream() {
  state_ = CodecState::STREAMING;
  
  if (is_server_ && write_callbacks_) {
    // Start keep-alive timer for server mode
    if (!keep_alive_timer_) {
      keep_alive_timer_ = write_callbacks_->connection().dispatcher().createTimer(
          [this]() {
            // Send keep-alive comment
            event_encoder_->encodeComment("keep-alive");
            
            // Reschedule timer
            keep_alive_timer_->enableTimer(kKeepAliveInterval);
          });
      keep_alive_timer_->enableTimer(kKeepAliveInterval);
    }
  }
}

// Process incoming SSE data
void SseCodecFilter::dispatch(Buffer& data) {
  if (!parser_) {
    return;
  }
  
  size_t data_len = data.length();
  if (data_len == 0) {
    return;
  }
  
  // Get linearized data for parsing
  const char* raw_data = static_cast<const char*>(data.linearize(data_len));
  
  // Parse SSE data
  size_t consumed = parser_->execute(raw_data, data_len);
  
  // Drain consumed data from buffer
  data.drain(consumed);
}

void SseCodecFilter::sendEventData(Buffer& data) {
  if (write_callbacks_) {
    write_callbacks_->injectWriteDataToFilterChain(data, false);
  }
}

// Static helper to format SSE fields
void SseCodecFilter::formatSseField(Buffer& buffer,
                                    const std::string& field,
                                    const std::string& value) {
  // Split value by newlines and format each line
  std::istringstream stream(value);
  std::string line;
  while (std::getline(stream, line)) {
    buffer.add(field.c_str(), field.length());
    buffer.add(": ", 2);
    buffer.add(line.c_str(), line.length());
    buffer.add("\n", 1);
  }
}

// ParserCallbacks implementation
void SseCodecFilter::ParserCallbacks::onEvent(const std::string& event,
                                              const std::string& data,
                                              const optional<std::string>& id) {
  parent_.event_callbacks_.onEvent(event, data, id);
}

void SseCodecFilter::ParserCallbacks::onComment(const std::string& comment) {
  parent_.event_callbacks_.onComment(comment);
}

void SseCodecFilter::ParserCallbacks::onRetry(uint32_t retry_ms) {
  // Could store retry value for reconnection logic
}

// EventEncoderImpl implementation
void SseCodecFilter::EventEncoderImpl::encodeEvent(const std::string& event,
                                                   const std::string& data,
                                                   const optional<std::string>& id) {
  parent_.event_buffer_.drain(parent_.event_buffer_.length());
  
  // Format SSE event
  if (!event.empty()) {
    SseCodecFilter::formatSseField(parent_.event_buffer_, "event", event);
  }
  
  if (id.has_value()) {
    SseCodecFilter::formatSseField(parent_.event_buffer_, "id", id.value());
  }
  
  SseCodecFilter::formatSseField(parent_.event_buffer_, "data", data);
  
  // End of event
  parent_.event_buffer_.add("\n", 1);
  
  // Send event
  parent_.sendEventData(parent_.event_buffer_);
}

void SseCodecFilter::EventEncoderImpl::encodeComment(const std::string& comment) {
  parent_.event_buffer_.drain(parent_.event_buffer_.length());
  
  // Format SSE comment
  parent_.event_buffer_.add(": ", 2);
  parent_.event_buffer_.add(comment.c_str(), comment.length());
  parent_.event_buffer_.add("\n\n", 2);
  
  // Send comment
  parent_.sendEventData(parent_.event_buffer_);
}

void SseCodecFilter::EventEncoderImpl::encodeRetry(uint32_t retry_ms) {
  parent_.event_buffer_.drain(parent_.event_buffer_.length());
  
  // Format retry directive
  std::string retry_str = "retry: " + std::to_string(retry_ms) + "\n\n";
  parent_.event_buffer_.add(retry_str.c_str(), retry_str.length());
  
  // Send retry
  parent_.sendEventData(parent_.event_buffer_);
}

} // namespace filter
} // namespace mcp