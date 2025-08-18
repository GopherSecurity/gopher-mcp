/**
 * MCP JSON-RPC Protocol Filter Implementation
 * 
 * Following production architecture patterns:
 * - Pure protocol processing, no I/O or transport concerns
 * - Clean separation between protocol layers
 * - Stateless message processing
 */

#include "mcp/filter/mcp_jsonrpc_filter.h"
#include "mcp/json/json_serialization.h"
#include <iostream>

namespace mcp {
namespace filter {

// EncoderImpl - Internal implementation of the encoder interface
// Forward declare to access write_callbacks_
class McpJsonRpcFilter::EncoderImpl : public McpJsonRpcFilter::Encoder {
public:
  EncoderImpl(McpJsonRpcFilter& parent) : parent_(parent) {}
  
  VoidResult encodeRequest(const jsonrpc::Request& request) override {
    // Convert request to JSON
    auto json_val = json::to_json(request);
    std::string json_str = json_val.toString();
    
    // Add framing or delimiter based on configuration
    if (!parent_.use_framing_) {
      json_str += "\n";
    }
    
    // Create buffer and write through filter chain
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(json_str);
    
    // Trigger write through filter manager
    // This will flow through the filter chain in reverse order
    // Use write callbacks to inject data if available
    if (parent_.write_callbacks_) {
      parent_.write_callbacks_->injectWriteDataToFilterChain(*buffer, false);
    }
    
    return makeVoidSuccess();
  }
  
  VoidResult encodeNotification(const jsonrpc::Notification& notification) override {
    // Convert notification to JSON
    auto json_val = json::to_json(notification);
    std::string json_str = json_val.toString();
    
    // Add framing or delimiter
    if (!parent_.use_framing_) {
      json_str += "\n";
    }
    
    // Create buffer and write through filter chain
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(json_str);
    
    // Use write callbacks to inject data if available
    if (parent_.write_callbacks_) {
      parent_.write_callbacks_->injectWriteDataToFilterChain(*buffer, false);
    }
    
    return makeVoidSuccess();
  }
  
  VoidResult encodeResponse(const jsonrpc::Response& response) override {
    // Convert response to JSON
    auto json_val = json::to_json(response);
    std::string json_str = json_val.toString();
    
    // Add framing or delimiter
    if (!parent_.use_framing_) {
      json_str += "\n";
    }
    
    // Create buffer and write through filter chain
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(json_str);
    
    // Use write callbacks to inject data if available
    if (parent_.write_callbacks_) {
      parent_.write_callbacks_->injectWriteDataToFilterChain(*buffer, false);
    }
    
    return makeVoidSuccess();
  }
  
private:
  McpJsonRpcFilter& parent_;
};

// McpJsonRpcFilter implementation

McpJsonRpcFilter::McpJsonRpcFilter(Callbacks& callbacks,
                                   event::Dispatcher& dispatcher,
                                   bool is_server)
    : callbacks_(callbacks),
      dispatcher_(dispatcher),
      is_server_(is_server),
      encoder_(std::make_unique<EncoderImpl>(*this)) {}

McpJsonRpcFilter::~McpJsonRpcFilter() {
  // Destructor defined here where EncoderImpl is complete
}

McpJsonRpcFilter::Encoder& McpJsonRpcFilter::encoder() {
  return *encoder_;
}

void McpJsonRpcFilter::initializeReadFilterCallbacks(
    network::ReadFilterCallbacks& callbacks) {
  // Store read callbacks for potential use
  // JSON-RPC filter doesn't need special read initialization
}

void McpJsonRpcFilter::initializeWriteFilterCallbacks(
    network::WriteFilterCallbacks& callbacks) {
  // Store write callbacks for encoder use
  // This allows the encoder to inject data into the filter chain
  write_callbacks_ = &callbacks;
}

network::FilterStatus McpJsonRpcFilter::onData(Buffer& data, bool end_stream) {
  (void)end_stream;
  
  // Parse JSON-RPC messages from the data buffer
  // This data has already been processed by lower protocol layers (HTTP/SSE)
  parseMessages(data);
  
  return network::FilterStatus::Continue;
}

network::FilterStatus McpJsonRpcFilter::onNewConnection() {
  // Reset state for new connection
  partial_message_.clear();
  requests_received_ = 0;
  responses_received_ = 0;
  notifications_received_ = 0;
  protocol_errors_ = 0;
  
  return network::FilterStatus::Continue;
}

network::FilterStatus McpJsonRpcFilter::onWrite(Buffer& data, bool end_stream) {
  (void)end_stream;
  
  // Frame outgoing messages if configured
  if (use_framing_) {
    frameMessage(data);
  }
  
  return network::FilterStatus::Continue;
}

void McpJsonRpcFilter::parseMessages(Buffer& buffer) {
  // Convert buffer to string and drain it
  std::string buffer_str = buffer.toString();
  buffer.drain(buffer.length());
  
  // Add to partial message buffer
  partial_message_ += buffer_str;
  
  if (use_framing_) {
    // Parse with message framing (4-byte length prefix, big-endian)
    while (partial_message_.length() >= 4) {
      // Read length prefix
      uint32_t msg_len = 0;
      msg_len |= (static_cast<uint8_t>(partial_message_[0]) << 24);
      msg_len |= (static_cast<uint8_t>(partial_message_[1]) << 16);
      msg_len |= (static_cast<uint8_t>(partial_message_[2]) << 8);
      msg_len |= static_cast<uint8_t>(partial_message_[3]);
      
      if (partial_message_.length() < 4 + msg_len) {
        // Not enough data yet, wait for more
        break;
      }
      
      // Extract complete message
      std::string json_str = partial_message_.substr(4, msg_len);
      partial_message_.erase(0, 4 + msg_len);
      
      // Parse the JSON-RPC message
      parseMessage(json_str);
    }
  } else {
    // Parse newline-delimited JSON
    size_t pos = 0;
    while ((pos = partial_message_.find('\n')) != std::string::npos) {
      std::string line = partial_message_.substr(0, pos);
      partial_message_.erase(0, pos + 1);
      
      if (!line.empty()) {
        parseMessage(line);
      }
    }
  }
}

bool McpJsonRpcFilter::parseMessage(const std::string& json_str) {
  try {
    // Parse JSON string
    auto json_val = json::JsonValue::parse(json_str);
    
    // Determine message type and dispatch to callbacks
    if (json_val.contains("method")) {
      if (json_val.contains("id")) {
        // JSON-RPC Request
        jsonrpc::Request request = json::from_json<jsonrpc::Request>(json_val);
        requests_received_++;
        callbacks_.onRequest(request);
      } else {
        // JSON-RPC Notification
        jsonrpc::Notification notification = json::from_json<jsonrpc::Notification>(json_val);
        notifications_received_++;
        callbacks_.onNotification(notification);
      }
    } else if (json_val.contains("result") || json_val.contains("error")) {
      // JSON-RPC Response
      jsonrpc::Response response = json::from_json<jsonrpc::Response>(json_val);
      responses_received_++;
      callbacks_.onResponse(response);
    } else {
      // Invalid JSON-RPC message
      Error error;
      error.code = jsonrpc::INVALID_REQUEST;
      error.message = "Invalid JSON-RPC message format";
      protocol_errors_++;
      callbacks_.onProtocolError(error);
      return false;
    }
    
    return true;
    
  } catch (const json::JsonException& e) {
    // JSON parse error
    Error error;
    error.code = jsonrpc::PARSE_ERROR;
    error.message = "JSON parse error: " + std::string(e.what());
    protocol_errors_++;
    callbacks_.onProtocolError(error);
    return false;
    
  } catch (const std::exception& e) {
    // Other errors
    Error error;
    error.code = jsonrpc::INTERNAL_ERROR;
    error.message = "Internal error: " + std::string(e.what());
    protocol_errors_++;
    callbacks_.onProtocolError(error);
    return false;
  }
}

void McpJsonRpcFilter::frameMessage(Buffer& data) {
  if (!use_framing_ || data.length() == 0) {
    return;
  }
  
  // Get message content
  std::string message_content = data.toString();
  size_t msg_len = message_content.length();
  
  // Create new buffer with length prefix
  auto framed_buffer = std::make_unique<OwnedBuffer>();
  
  // Add 4-byte length prefix (big-endian)
  uint8_t len_bytes[4];
  len_bytes[0] = (msg_len >> 24) & 0xFF;
  len_bytes[1] = (msg_len >> 16) & 0xFF;
  len_bytes[2] = (msg_len >> 8) & 0xFF;
  len_bytes[3] = msg_len & 0xFF;
  
  framed_buffer->add(len_bytes, 4);
  framed_buffer->add(message_content);
  
  // Replace original buffer content
  data.drain(data.length());
  framed_buffer->move(data);
}

} // namespace filter
} // namespace mcp