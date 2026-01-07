/**
 * SSE Codec Filter Implementation
 *
 * Following production architecture:
 * - Handles Server-Sent Events protocol
 * - Works on top of HTTP layer
 * - Completely separate from transport
 * - Integrates with SseCodecStateMachine for state management
 */

#include "mcp/filter/sse_codec_filter.h"

#include <sstream>

#include "mcp/logging/log_macros.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace filter {

// SseFilterChainBridge implementation

SseCodecFilter::SseFilterChainBridge::SseFilterChainBridge(
    SseCodecFilter& parent_filter)
    : parent_filter_(parent_filter) {
  GOPHER_LOG_DEBUG("SseFilterChainBridge created");
}

void SseCodecFilter::SseFilterChainBridge::onEvent(
    const std::string& event,
    const std::string& data,
    const optional<std::string>& id) {
  if (id.has_value()) {
    GOPHER_LOG_DEBUG(
        "SseFilterChainBridge::onEvent - event='{}', data length={}, "
        "id='{}'",
        event, data.length(), id.value());
  } else {
    GOPHER_LOG_DEBUG(
        "SseFilterChainBridge::onEvent - event='{}', data length={}", event,
        data.length());
  }

  if (!data.empty()) {
    forwardJsonRpcToNextFilter(data, false);
  }
}

void SseCodecFilter::SseFilterChainBridge::onComment(
    const std::string& comment) {
  GOPHER_LOG_DEBUG("SseFilterChainBridge::onComment - '{}'", comment);
  // SSE comments are typically used for keep-alive, no JSON-RPC data to forward
}

void SseCodecFilter::SseFilterChainBridge::onError(const std::string& error) {
  GOPHER_LOG_ERROR("SseFilterChainBridge::onError - {}", error);
  // Error handling - could inject error into filter chain if needed
}

void SseCodecFilter::SseFilterChainBridge::forwardJsonRpcToNextFilter(
    const std::string& payload, bool end_stream) {
  if (!parent_filter_.read_callbacks_ || payload.empty()) {
    return;
  }

  std::string forwarded = payload;
  if (!forwarded.empty() && forwarded.back() != '\n') {
    forwarded.push_back('\n');
  }

  GOPHER_LOG_DEBUG("SseFilterChainBridge forwarding {} bytes of JSON-RPC data",
                   forwarded.length());

  OwnedBuffer buffer;
  buffer.add(forwarded);
  parent_filter_.read_callbacks_->injectReadDataToFilterChain(buffer,
                                                              end_stream);
}

// Constructor
SseCodecFilter::SseCodecFilter(EventCallbacks& callbacks,
                               event::Dispatcher& dispatcher,
                               bool is_server)
    : event_callbacks_(&callbacks),
      dispatcher_(dispatcher),
      is_server_(is_server) {
  if (!is_server_) {
    // Client mode - create SSE parser
    parser_callbacks_ = std::make_unique<ParserCallbacks>(*this);
    parser_ = std::make_unique<http::SseParser>(parser_callbacks_.get());
  }

  // Create event encoder
  event_encoder_ = std::make_unique<EventEncoderImpl>(*this);

  // Initialize SSE codec state machine
  SseCodecStateMachineConfig config;
  config.keep_alive_interval =
      std::chrono::milliseconds(30000);                    // 30s keep-alive
  config.event_timeout = std::chrono::milliseconds(5000);  // 5s event timeout
  config.enable_keep_alive = true;
  config.state_change_callback =
      [this](const SseCodecStateTransitionContext& ctx) {
        onCodecStateChange(ctx);
      };
  config.error_callback = [this](const std::string& error) {
    onCodecError(error);
  };

  if (is_server_) {
    // Server mode - configure keep-alive callback
    config.keep_alive_callback = [this]() { sendKeepAliveComment(); };
  }

  state_machine_ = std::make_unique<SseCodecStateMachine>(dispatcher_, config);
}

// Constructor with FilterCreationContext for config-driven filter chains
SseCodecFilter::SseCodecFilter(const filter::FilterCreationContext& context,
                               const json::JsonValue& config)
    : event_callbacks_(nullptr),
      dispatcher_(context.dispatcher),
      is_server_(context.isServer()) {
  // Create filter bridge to connect SSE event callbacks to filter chain data
  // flow
  filter_bridge_ = std::make_unique<SseFilterChainBridge>(*this);
  event_callbacks_ = filter_bridge_.get();

  if (!is_server_) {
    // Client mode - create SSE parser
    parser_callbacks_ = std::make_unique<ParserCallbacks>(*this);
    parser_ = std::make_unique<http::SseParser>(parser_callbacks_.get());
  }

  // Create event encoder
  event_encoder_ = std::make_unique<EventEncoderImpl>(*this);

  // Initialize SSE codec state machine with same defaults for now
  // TODO: Extract timeout values from config parameter
  SseCodecStateMachineConfig sm_config;
  sm_config.keep_alive_interval =
      std::chrono::milliseconds(30000);  // 30s keep-alive
  sm_config.event_timeout =
      std::chrono::milliseconds(5000);  // 5s event timeout
  sm_config.enable_keep_alive = true;
  sm_config.state_change_callback =
      [this](const SseCodecStateTransitionContext& ctx) {
        onCodecStateChange(ctx);
      };
  sm_config.error_callback = [this](const std::string& error) {
    onCodecError(error);
  };

  if (is_server_) {
    // Server mode - configure keep-alive callback
    sm_config.keep_alive_callback = [this]() { sendKeepAliveComment(); };
  }

  state_machine_ =
      std::make_unique<SseCodecStateMachine>(dispatcher_, sm_config);
}

SseCodecFilter::~SseCodecFilter() = default;

// network::ReadFilter interface
network::FilterStatus SseCodecFilter::onNewConnection() {
  // State machine starts in Idle state by default
  return network::FilterStatus::Continue;
}

network::FilterStatus SseCodecFilter::onData(Buffer& data, bool end_stream) {
  if (!state_machine_->isStreaming()) {
    // Not in SSE streaming mode yet, pass through
    return network::FilterStatus::Continue;
  }

  if (!is_server_) {
    // Client mode - parse SSE events
    dispatch(data);
  }

  if (end_stream) {
    // Note: Some servers close the connection after each response
    // For SSE, end_stream doesn't mean immediate close - it means no more data
    // We should keep the connection open for future events
    // Only close if explicitly requested or on error
    std::cerr << "[DEBUG] SSE end_stream received - keeping connection open for SSE events" << std::endl;
    // Don't trigger CloseStream here - let the connection manager handle it
  }

  return network::FilterStatus::Continue;
}

// network::WriteFilter interface
network::FilterStatus SseCodecFilter::onWrite(Buffer& data, bool end_stream) {
  // In server mode, format outgoing data as SSE events
  if (is_server_ && data.length() > 0) {
    // The data contains JSON-RPC response that needs SSE formatting
    size_t data_len = data.length();
    std::string json_data(static_cast<const char*>(data.linearize(data_len)),
                          data_len);

    // Clear the buffer
    data.drain(data_len);

    // Format as SSE event
    // Using "message" event type for JSON-RPC messages
    std::string sse_event = "event: message\n";
    sse_event += "data: " + json_data + "\n\n";

    GOPHER_LOG_TRACE("SseCodecFilter formatting SSE response: {}", sse_event);

    // Add formatted SSE event back to buffer
    data.add(sse_event.c_str(), sse_event.length());
  }

  // Pass through (formatted) SSE data
  return network::FilterStatus::Continue;
}

void SseCodecFilter::startEventStream() {
  state_machine_->handleEvent(SseCodecEvent::StartStream);

  if (is_server_) {
    // Server mode - start keep-alive timer managed by state machine
    state_machine_->startKeepAliveTimer();
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
  size_t consumed = parser_->parse(raw_data, data_len);

  // Drain consumed data from buffer
  data.drain(consumed);
}

void SseCodecFilter::sendEventData(Buffer& data) {
  if (write_callbacks_) {
    // Write directly to connection following production pattern
    // This replaces the deprecated injectWriteDataToFilterChain method
    write_callbacks_->connection().write(data, false);
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
void SseCodecFilter::ParserCallbacks::onSseEvent(const http::SseEvent& event) {
  // Convert SseEvent to our callback interface
  std::string event_type = event.event.value_or("");
  if (parent_.event_callbacks_) {
    parent_.event_callbacks_->onEvent(event_type, event.data, event.id);
  }
}

void SseCodecFilter::ParserCallbacks::onSseComment(const std::string& comment) {
  if (parent_.event_callbacks_) {
    parent_.event_callbacks_->onComment(comment);
  }
}

void SseCodecFilter::ParserCallbacks::onSseError(const std::string& error) {
  if (parent_.event_callbacks_) {
    parent_.event_callbacks_->onError(error);
  }
}

// EventEncoderImpl implementation
void SseCodecFilter::EventEncoderImpl::encodeEvent(
    const std::string& event,
    const std::string& data,
    const optional<std::string>& id) {
  // Trigger send event in state machine
  parent_.state_machine_->handleEvent(SseCodecEvent::SendEvent);

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

  // Mark event as sent
  parent_.state_machine_->handleEvent(SseCodecEvent::EventSent);
}

void SseCodecFilter::EventEncoderImpl::encodeComment(
    const std::string& comment) {
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

// State machine callback handlers
void SseCodecFilter::onCodecStateChange(
    const SseCodecStateTransitionContext& context) {
  // Handle state changes as needed
  // For example, logging, metrics, or connection management
}

void SseCodecFilter::onCodecError(const std::string& error) {
  // Handle codec-level errors
  state_machine_->handleEvent(SseCodecEvent::StreamError);
  if (event_callbacks_) {
    event_callbacks_->onError("SSE codec error: " + error);
  }
}

void SseCodecFilter::sendKeepAliveComment() {
  event_encoder_->encodeComment("keep-alive");
}

}  // namespace filter
}  // namespace mcp
