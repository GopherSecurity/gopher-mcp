/**
 * HTTP Codec Filter Implementation
 * 
 * Following production architecture:
 * - Handles HTTP/1.1 protocol processing for both client and server modes
 * - Completely separate from transport layer
 * - Works with any transport socket that provides raw I/O
 * - Integrates with HttpCodecStateMachine for state management
 */

#include "mcp/filter/http_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/http/llhttp_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace mcp {
namespace filter {

// Constructor
HttpCodecFilter::HttpCodecFilter(MessageCallbacks& callbacks,
                                 event::Dispatcher& dispatcher,
                                 bool is_server)
    : message_callbacks_(callbacks),
      dispatcher_(dispatcher),
      is_server_(is_server) {
  // Initialize HTTP parser callbacks
  parser_callbacks_ = std::make_unique<ParserCallbacks>(*this);
  
  // Create HTTP/1.1 parser using llhttp
  // Parser type depends on mode: REQUEST for server, RESPONSE for client
  http::HttpParserType parser_type = is_server_ ? 
      http::HttpParserType::REQUEST : http::HttpParserType::RESPONSE;
  parser_ = std::make_unique<http::LLHttpParser>(
      parser_type, 
      parser_callbacks_.get(),
      http::HttpVersion::HTTP_1_1);
  
  // Initialize message encoder
  message_encoder_ = std::make_unique<MessageEncoderImpl>(*this);
  
  // Initialize HTTP codec state machine
  HttpCodecStateMachineConfig config;
  config.is_server = is_server_;  // Set mode
  config.header_timeout = std::chrono::milliseconds(30000);
  config.body_timeout = std::chrono::milliseconds(60000);
  config.idle_timeout = std::chrono::milliseconds(120000);
  config.enable_keep_alive = true;
  config.state_change_callback = [this](const HttpCodecStateTransitionContext& ctx) {
    onCodecStateChange(ctx);
  };
  config.error_callback = [this](const std::string& error) {
    onCodecError(error);
  };
  
  state_machine_ = std::make_unique<HttpCodecStateMachine>(dispatcher_, config);
}

HttpCodecFilter::~HttpCodecFilter() = default;

// network::ReadFilter interface
network::FilterStatus HttpCodecFilter::onNewConnection() {
  // State machine starts in appropriate state based on mode
  // Server: WaitingForRequest, Client: Idle
  return network::FilterStatus::Continue;
}

network::FilterStatus HttpCodecFilter::onData(Buffer& data, bool end_stream) {
  // Check if we need to reset for next message
  if (state_machine_->currentState() == HttpCodecState::Closed) {
    // Reset for next message if connection was closed
    state_machine_->resetForNextRequest();
    current_headers_.clear();
    current_body_.clear();
    current_url_.clear();
    current_status_.clear();
  }
  
  // Process HTTP data
  dispatch(data);
  
  return network::FilterStatus::Continue;
}

// network::WriteFilter interface
network::FilterStatus HttpCodecFilter::onWrite(Buffer& data, bool end_stream) {
  // Pass through message data
  return network::FilterStatus::Continue;
}

// Process incoming HTTP data
void HttpCodecFilter::dispatch(Buffer& data) {
  size_t data_len = data.length();
  if (data_len == 0) {
    return;
  }
  
  // Get linearized data for parsing
  const char* raw_data = static_cast<const char*>(data.linearize(data_len));
  
  // Parse HTTP data
  size_t consumed = parser_->execute(raw_data, data_len);
  
  // Drain consumed data from buffer
  data.drain(consumed);
  
  // Check for parser errors
  if (parser_->getStatus() == http::ParserStatus::Error) {
    handleParserError(parser_->getError());
  }
}

void HttpCodecFilter::handleParserError(const std::string& error) {
  state_machine_->handleEvent(HttpCodecEvent::ParseError);
  message_callbacks_.onError(error);
}

void HttpCodecFilter::sendMessageData(Buffer& data) {
  if (write_callbacks_) {
    write_callbacks_->injectWriteDataToFilterChain(data, false);
  }
}

// ParserCallbacks implementation
http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onMessageBegin() {
  // Event depends on mode: RequestBegin for server, ResponseBegin for client
  if (parent_.is_server_) {
    parent_.state_machine_->handleEvent(HttpCodecEvent::RequestBegin);
  } else {
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseBegin);
  }
  
  parent_.current_headers_.clear();
  parent_.current_body_.clear();
  parent_.current_url_.clear();
  parent_.current_status_.clear();
  current_header_field_.clear();
  current_header_value_.clear();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onUrl(
    const char* data, size_t length) {
  // Server mode: store URL for request
  if (parent_.is_server_) {
    parent_.current_url_ = std::string(data, length);
    parent_.current_headers_["url"] = parent_.current_url_;
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onStatus(
    const char* data, size_t length) {
  // Client mode: store status for response
  if (!parent_.is_server_) {
    parent_.current_status_ = std::string(data, length);
    parent_.current_headers_["status"] = parent_.current_status_;
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onHeaderField(
    const char* data, size_t length) {
  // If we have a pending header value, store it
  if (!current_header_field_.empty() && !current_header_value_.empty()) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower_field = current_header_field_;
    std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
    parent_.current_headers_[lower_field] = current_header_value_;
    current_header_value_.clear();
  }
  
  current_header_field_ = std::string(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onHeaderValue(
    const char* data, size_t length) {
  current_header_value_.append(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onHeadersComplete() {
  // Store last header
  if (!current_header_field_.empty() && !current_header_value_.empty()) {
    std::string lower_field = current_header_field_;
    std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
    parent_.current_headers_[lower_field] = current_header_value_;
  }
  
  // Store HTTP version from the request for use in response
  parent_.current_version_ = parent_.parser_->httpVersion();
  
  // Add HTTP method to headers for routing filter
  if (parent_.is_server_) {
    http::HttpMethod method = parent_.parser_->httpMethod();
    std::string method_str;
    switch (method) {
      case http::HttpMethod::GET: method_str = "GET"; break;
      case http::HttpMethod::POST: method_str = "POST"; break;
      case http::HttpMethod::PUT: method_str = "PUT"; break;
      case http::HttpMethod::DELETE: method_str = "DELETE"; break;
      case http::HttpMethod::HEAD: method_str = "HEAD"; break;
      case http::HttpMethod::OPTIONS: method_str = "OPTIONS"; break;
      case http::HttpMethod::PATCH: method_str = "PATCH"; break;
      case http::HttpMethod::CONNECT: method_str = "CONNECT"; break;
      case http::HttpMethod::TRACE: method_str = "TRACE"; break;
      default: method_str = "UNKNOWN"; break;
    }
    parent_.current_headers_[":method"] = method_str;
  }
  
  // Check keep-alive
  parent_.keep_alive_ = parent_.parser_->shouldKeepAlive();
  
  // Determine if message has body based on Content-Length or Transfer-Encoding
  bool has_body = false;
  auto content_length_it = parent_.current_headers_.find("content-length");
  auto transfer_encoding_it = parent_.current_headers_.find("transfer-encoding");
  
  if (content_length_it != parent_.current_headers_.end()) {
    int content_length = std::stoi(content_length_it->second);
    has_body = (content_length > 0);
  } else if (transfer_encoding_it != parent_.current_headers_.end() &&
             transfer_encoding_it->second.find("chunked") != std::string::npos) {
    has_body = true;
  }
  
  // Set body expectation for state machine based on mode
  if (parent_.is_server_) {
    parent_.state_machine_->setExpectRequestBody(has_body);
  } else {
    parent_.state_machine_->setExpectResponseBody(has_body);
  }
  
  // Trigger headers complete event based on mode
  if (parent_.is_server_) {
    parent_.state_machine_->handleEvent(HttpCodecEvent::RequestHeadersComplete);
  } else {
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseHeadersComplete);
  }
  
  // Notify callbacks
  parent_.message_callbacks_.onHeaders(parent_.current_headers_, parent_.keep_alive_);
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onBody(
    const char* data, size_t length) {
  parent_.current_body_.append(data, length);
  // Trigger body data event based on mode
  if (parent_.is_server_) {
    parent_.state_machine_->handleEvent(HttpCodecEvent::RequestBodyData);
  } else {
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseBodyData);
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onMessageComplete() {
  // Trigger message complete event based on mode
  if (parent_.is_server_) {
    parent_.state_machine_->handleEvent(HttpCodecEvent::RequestComplete);
  } else {
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  }
  
  // Send body to callbacks
  if (!parent_.current_body_.empty()) {
    parent_.message_callbacks_.onBody(parent_.current_body_, true);
  }
  
  parent_.message_callbacks_.onMessageComplete();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onChunkHeader(
    size_t chunk_size) {
  // Handle chunked encoding if needed
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onChunkComplete() {
  return http::ParserCallbackResult::Success;
}

void HttpCodecFilter::ParserCallbacks::onError(const std::string& error) {
  parent_.handleParserError(error);
}

// MessageEncoderImpl implementation
void HttpCodecFilter::MessageEncoderImpl::encodeHeaders(
    const std::string& status_code_or_method,
    const std::map<std::string, std::string>& headers,
    bool end_stream,
    const std::string& path) {
  
  std::ostringstream message;
  
  if (parent_.is_server_) {
    // Server mode: encode response using the same HTTP version as the request
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseBegin);
    
    int status_code = std::stoi(status_code_or_method);
    
    // Use the HTTP version from the request for transparent protocol handling
    std::string version_str = http::httpVersionToString(parent_.current_version_);
    message << version_str << " " << status_code << " ";
    
    // Add status text
    switch (status_code) {
      case 200: message << "OK"; break;
      case 201: message << "Created"; break;
      case 204: message << "No Content"; break;
      case 400: message << "Bad Request"; break;
      case 404: message << "Not Found"; break;
      case 500: message << "Internal Server Error"; break;
      default: message << "Unknown"; break;
    }
    message << "\r\n";
  } else {
    // Client mode: encode request using configured version
    parent_.state_machine_->handleEvent(HttpCodecEvent::RequestBegin);
    
    // Use the configured HTTP version for requests
    std::string version_str = http::httpVersionToString(parent_.current_version_);
    message << status_code_or_method << " " << path << " " << version_str << "\r\n";
  }
  
  // Add headers
  for (const auto& header : headers) {
    message << header.first << ": " << header.second << "\r\n";
  }
  
  // End headers
  message << "\r\n";
  
  // Send message headers
  std::string message_str = message.str();
  parent_.message_buffer_.add(message_str.c_str(), message_str.length());
  parent_.sendMessageData(parent_.message_buffer_);
  
  if (end_stream) {
    if (parent_.is_server_) {
      parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
    } else {
      parent_.state_machine_->handleEvent(HttpCodecEvent::RequestComplete);
    }
  }
}

void HttpCodecFilter::MessageEncoderImpl::encodeData(Buffer& data, bool end_stream) {
  // Send message body
  parent_.sendMessageData(data);
  
  if (end_stream) {
    if (parent_.is_server_) {
      parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
    } else {
      parent_.state_machine_->handleEvent(HttpCodecEvent::RequestComplete);
    }
  }
}

// State machine callback handlers
void HttpCodecFilter::onCodecStateChange(const HttpCodecStateTransitionContext& context) {
  // Handle state changes as needed
  // For example, logging, metrics, or connection management
}

void HttpCodecFilter::onCodecError(const std::string& error) {
  // Handle codec-level errors
  message_callbacks_.onError("HTTP codec error: " + error);
}

} // namespace filter
} // namespace mcp