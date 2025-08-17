/**
 * HTTP Server Codec Filter Implementation
 * 
 * Following production architecture:
 * - Handles HTTP/1.1 protocol processing
 * - Completely separate from transport layer
 * - Works with any transport socket that provides raw I/O
 * - Integrates with HttpCodecStateMachine for state management
 */

#include "mcp/filter/http_server_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/http/llhttp_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace mcp {
namespace filter {

// Constructor
HttpServerCodecFilter::HttpServerCodecFilter(RequestCallbacks& callbacks,
                                             event::Dispatcher& dispatcher)
    : request_callbacks_(callbacks),
      dispatcher_(dispatcher) {
  // Initialize HTTP parser callbacks
  parser_callbacks_ = std::make_unique<ParserCallbacks>(*this);
  
  // Create HTTP/1.1 parser using llhttp
  // We directly create an LLHttpParser since we know we're handling HTTP/1.1
  parser_ = std::make_unique<http::LLHttpParser>(
      http::HttpParserType::REQUEST, 
      parser_callbacks_.get(),
      http::HttpVersion::HTTP_1_1);
  
  // Initialize response encoder
  response_encoder_ = std::make_unique<ResponseEncoderImpl>(*this);
  
  // Initialize HTTP codec state machine
  HttpCodecStateMachineConfig config;
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

HttpServerCodecFilter::~HttpServerCodecFilter() = default;

// network::ReadFilter interface
network::FilterStatus HttpServerCodecFilter::onNewConnection() {
  // State machine starts in WaitingForRequest state by default
  return network::FilterStatus::Continue;
}

network::FilterStatus HttpServerCodecFilter::onData(Buffer& data, bool end_stream) {
  // Check if we need to reset for next request
  if (state_machine_->currentState() == HttpCodecState::Closed) {
    // Reset for next request if connection was closed
    state_machine_->resetForNextRequest();
    current_headers_.clear();
    current_body_.clear();
  }
  
  // Process HTTP data
  dispatch(data);
  
  return network::FilterStatus::Continue;
}

// network::WriteFilter interface
network::FilterStatus HttpServerCodecFilter::onWrite(Buffer& data, bool end_stream) {
  // Pass through response data
  return network::FilterStatus::Continue;
}

// Process incoming HTTP data
void HttpServerCodecFilter::dispatch(Buffer& data) {
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

void HttpServerCodecFilter::handleParserError(const std::string& error) {
  state_machine_->handleEvent(HttpCodecEvent::ParseError);
  request_callbacks_.onError(error);
}

void HttpServerCodecFilter::sendResponseData(Buffer& data) {
  if (write_callbacks_) {
    write_callbacks_->injectWriteDataToFilterChain(data, false);
  }
}

// ParserCallbacks implementation
http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onMessageBegin() {
  parent_.state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  parent_.current_headers_.clear();
  parent_.current_body_.clear();
  current_header_field_.clear();
  current_header_value_.clear();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onUrl(
    const char* data, size_t length) {
  // Store URL in headers
  parent_.current_headers_["url"] = std::string(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onStatus(
    const char* data, size_t length) {
  // Not used for request parsing
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onHeaderField(
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

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onHeaderValue(
    const char* data, size_t length) {
  current_header_value_.append(data, length);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onHeadersComplete() {
  // Store last header
  if (!current_header_field_.empty() && !current_header_value_.empty()) {
    std::string lower_field = current_header_field_;
    std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
    parent_.current_headers_[lower_field] = current_header_value_;
  }
  
  // Check keep-alive
  parent_.keep_alive_ = parent_.parser_->shouldKeepAlive();
  
  // Determine if request has body based on Content-Length or Transfer-Encoding
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
  
  // Set body expectation for state machine
  parent_.state_machine_->setExpectRequestBody(has_body);
  
  // Trigger headers complete event
  parent_.state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  
  // Notify callbacks
  parent_.request_callbacks_.onHeaders(parent_.current_headers_, parent_.keep_alive_);
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onBody(
    const char* data, size_t length) {
  parent_.current_body_.append(data, length);
  parent_.state_machine_->handleEvent(HttpCodecEvent::BodyData);
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onMessageComplete() {
  parent_.state_machine_->handleEvent(HttpCodecEvent::MessageComplete);
  
  // Send body to callbacks
  if (!parent_.current_body_.empty()) {
    parent_.request_callbacks_.onBody(parent_.current_body_, true);
  }
  
  parent_.request_callbacks_.onMessageComplete();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onChunkHeader(
    size_t chunk_size) {
  // Handle chunked encoding if needed
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpServerCodecFilter::ParserCallbacks::onChunkComplete() {
  return http::ParserCallbackResult::Success;
}

void HttpServerCodecFilter::ParserCallbacks::onError(const std::string& error) {
  parent_.handleParserError(error);
}

// ResponseEncoderImpl implementation
void HttpServerCodecFilter::ResponseEncoderImpl::encodeHeaders(
    int status_code,
    const std::map<std::string, std::string>& headers,
    bool end_stream) {
  
  parent_.state_machine_->handleEvent(HttpCodecEvent::SendResponse);
  
  // Build HTTP response
  std::ostringstream response;
  response << "HTTP/1.1 " << status_code << " ";
  
  // Add status text
  switch (status_code) {
    case 200: response << "OK"; break;
    case 201: response << "Created"; break;
    case 204: response << "No Content"; break;
    case 400: response << "Bad Request"; break;
    case 404: response << "Not Found"; break;
    case 500: response << "Internal Server Error"; break;
    default: response << "Unknown"; break;
  }
  response << "\r\n";
  
  // Add headers
  for (const auto& header : headers) {
    response << header.first << ": " << header.second << "\r\n";
  }
  
  // End headers
  response << "\r\n";
  
  // Send response headers
  std::string response_str = response.str();
  parent_.response_buffer_.add(response_str.c_str(), response_str.length());
  parent_.sendResponseData(parent_.response_buffer_);
  
  if (end_stream) {
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  }
}

void HttpServerCodecFilter::ResponseEncoderImpl::encodeData(Buffer& data, bool end_stream) {
  // Send response body
  parent_.sendResponseData(data);
  
  if (end_stream) {
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  }
}

// State machine callback handlers
void HttpServerCodecFilter::onCodecStateChange(const HttpCodecStateTransitionContext& context) {
  // Handle state changes as needed
  // For example, logging, metrics, or connection management
}

void HttpServerCodecFilter::onCodecError(const std::string& error) {
  // Handle codec-level errors
  request_callbacks_.onError("HTTP codec error: " + error);
}

} // namespace filter
} // namespace mcp