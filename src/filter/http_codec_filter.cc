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
#include <map>
#include <iostream>

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
  
  // Initialize stream context for first request
  // HTTP/1.1 uses stream_id 0 (only one stream per connection)
  current_stream_ = stream_manager_.getOrCreateContext(0);
}

HttpCodecFilter::~HttpCodecFilter() = default;

// network::ReadFilter interface
network::FilterStatus HttpCodecFilter::onNewConnection() {
  // State machine starts in appropriate state based on mode
  // Server: WaitingForRequest, Client: Idle
  // Initialize stream context for HTTP/1.1 (stream_id 0)
  current_stream_ = stream_manager_.getOrCreateContext(0);
  return network::FilterStatus::Continue;
}

network::FilterStatus HttpCodecFilter::onData(Buffer& data, bool end_stream) {
  std::cerr << "[DEBUG] HttpCodecFilter::onData called with " << data.length() 
            << " bytes, end_stream=" << end_stream << std::endl;
  
  // Check if we need to reset for next message
  if (state_machine_->currentState() == HttpCodecState::Closed) {
    // Reset for next message if connection was closed
    state_machine_->resetForNextRequest();
    // Get new stream context for next request
    current_stream_ = stream_manager_.getOrCreateContext(0);  // HTTP/1.1 uses stream_id 0
    current_stream_->reset();
  }
  
  // Ensure we have a stream context
  if (!current_stream_) {
    current_stream_ = stream_manager_.getOrCreateContext(0);
  }
  
  // Process HTTP data
  dispatch(data);
  
  std::cerr << "[DEBUG] HttpCodecFilter::onData completed, remaining data: " 
            << data.length() << " bytes" << std::endl;
  
  return network::FilterStatus::Continue;
}

// network::WriteFilter interface
network::FilterStatus HttpCodecFilter::onWrite(Buffer& data, bool end_stream) {
  
  std::cerr << "[DEBUG] HttpCodecFilter::onWrite called with " << data.length() 
            << " bytes, is_server=" << is_server_ << std::endl;
  
  // Following production pattern: format HTTP message in-place
  if (data.length() == 0) {
    return network::FilterStatus::Continue;
  }
  
  if (is_server_) {
    // For server mode, this is a response that needs HTTP framing
    // Check if we're in a state where we can send a response
    auto current_state = state_machine_->currentState();
    
    // Allow sending response in most server states
    if (current_state != HttpCodecState::Closed &&
        current_state != HttpCodecState::Error) {
      
      // Save the original response body
      size_t body_length = data.length();
      std::string body_data(static_cast<const char*>(data.linearize(body_length)), body_length);
      
      // Clear the buffer to build formatted HTTP response
      data.drain(body_length);
      
      // Build HTTP response with headers
      std::ostringstream response;
      
      // Use the HTTP version from the request for transparent protocol handling
      std::ostringstream version_str;
      version_str << "HTTP/" << static_cast<int>(current_stream_->http_major) 
                  << "." << static_cast<int>(current_stream_->http_minor);
      response << version_str.str() << " 200 OK\r\n";
      
      // Check if this is an SSE response based on Accept header
      bool is_sse_response = (current_stream_->accept_header == "text/event-stream");
      
      if (is_sse_response) {
        // SSE response headers
        response << "Content-Type: text/event-stream\r\n";
        response << "Cache-Control: no-cache\r\n";
        response << "Connection: keep-alive\r\n";
        response << "X-Accel-Buffering: no\r\n";  // Disable proxy buffering
        response << "\r\n";
        // SSE data is already formatted by SSE filter
        response << body_data;
      } else {
        // Regular JSON response
        response << "Content-Type: application/json\r\n";
        response << "Content-Length: " << body_length << "\r\n";
        response << "Cache-Control: no-cache\r\n";
        response << "Connection: " << (current_stream_->keep_alive ? "keep-alive" : "close") << "\r\n";
        response << "\r\n";
        response << body_data;
      }
      
      // Add formatted response to buffer
      std::string response_str = response.str();
      data.add(response_str.c_str(), response_str.length());
      
      // Update state machine
      state_machine_->handleEvent(HttpCodecEvent::ResponseBegin);
      if (end_stream) {
        state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
      }
    }
  } else {
    // Client mode: format as HTTP POST request
    auto current_state = state_machine_->currentState();
    
    // Check if we can send a request
    // Client can send when idle or after receiving a complete response
    if (current_state == HttpCodecState::Idle) {
      
      // Save the original request body (JSON-RPC)
      size_t body_length = data.length();
      std::string body_data(static_cast<const char*>(data.linearize(body_length)), body_length);
      
      // Clear the buffer to build formatted HTTP request
      data.drain(body_length);
      
      // Build HTTP POST request
      std::ostringstream request;
      request << "POST /rpc HTTP/1.1\r\n";
      request << "Host: localhost\r\n";
      request << "Content-Type: application/json\r\n";
      request << "Content-Length: " << body_length << "\r\n";
      request << "Accept: text/event-stream\r\n";  // Support SSE responses
      request << "Connection: keep-alive\r\n";
      request << "\r\n";
      request << body_data;
      
      // Add formatted request to buffer
      std::string request_str = request.str();
      data.add(request_str.c_str(), request_str.length());
      
      std::cerr << "[DEBUG] HttpCodecFilter client sending HTTP request: " 
                << request_str.substr(0, 200) << "..." << std::endl;
      
      // Update state machine
      state_machine_->handleEvent(HttpCodecEvent::RequestBegin);
      if (end_stream) {
        state_machine_->handleEvent(HttpCodecEvent::RequestComplete);
      }
    }
  }
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
    // Write directly to connection following production pattern
    // This replaces the deprecated injectWriteDataToFilterChain method
    write_callbacks_->connection().write(data, false);
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
  
  // Ensure we have a stream context
  if (!parent_.current_stream_) {
    parent_.current_stream_ = parent_.stream_manager_.getOrCreateContext(0);
  }
  
  // Reset stream context for new message
  parent_.current_stream_->reset();
  current_header_field_.clear();
  current_header_value_.clear();
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onUrl(
    const char* data, size_t length) {
  // Server mode: store URL for request
  if (parent_.is_server_ && parent_.current_stream_) {
    parent_.current_stream_->url = std::string(data, length);
    parent_.current_stream_->headers["url"] = parent_.current_stream_->url;
    // Extract path from URL
    parent_.current_stream_->path = parent_.current_stream_->url;
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onStatus(
    const char* data, size_t length) {
  // Client mode: store status for response
  if (!parent_.is_server_ && parent_.current_stream_) {
    parent_.current_stream_->status = std::string(data, length);
    parent_.current_stream_->headers["status"] = parent_.current_stream_->status;
  }
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onHeaderField(
    const char* data, size_t length) {
  // If we have a pending header value, store it
  if (!current_header_field_.empty() && !current_header_value_.empty() && parent_.current_stream_) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower_field = current_header_field_;
    std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
    parent_.current_stream_->headers[lower_field] = current_header_value_;
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
  if (!parent_.current_stream_) {
    return http::ParserCallbackResult::Error;
  }
  
  // Store last header
  if (!current_header_field_.empty() && !current_header_value_.empty()) {
    std::string lower_field = current_header_field_;
    std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
    parent_.current_stream_->headers[lower_field] = current_header_value_;
  }
  
  // Store HTTP version from the request for use in response
  auto version = parent_.parser_->httpVersion();
  parent_.current_stream_->http_major = (version == http::HttpVersion::HTTP_1_0) ? 1 : 1;
  parent_.current_stream_->http_minor = (version == http::HttpVersion::HTTP_1_0) ? 0 : 1;
  
  // Store Accept header for response formatting
  auto accept_it = parent_.current_stream_->headers.find("accept");
  if (accept_it != parent_.current_stream_->headers.end()) {
    parent_.current_stream_->accept_header = accept_it->second;
  }
  
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
    parent_.current_stream_->headers[":method"] = method_str;
    parent_.current_stream_->method = method_str;
  }
  
  // Check keep-alive
  parent_.current_stream_->keep_alive = parent_.parser_->shouldKeepAlive();
  
  // Determine if message has body based on Content-Length or Transfer-Encoding
  bool has_body = false;
  auto content_length_it = parent_.current_stream_->headers.find("content-length");
  auto transfer_encoding_it = parent_.current_stream_->headers.find("transfer-encoding");
  
  if (content_length_it != parent_.current_stream_->headers.end()) {
    int content_length = std::stoi(content_length_it->second);
    has_body = (content_length > 0);
  } else if (transfer_encoding_it != parent_.current_stream_->headers.end() &&
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
  parent_.message_callbacks_.onHeaders(parent_.current_stream_->headers, parent_.current_stream_->keep_alive);
  
  return http::ParserCallbackResult::Success;
}

http::ParserCallbackResult HttpCodecFilter::ParserCallbacks::onBody(
    const char* data, size_t length) {
  if (parent_.current_stream_) {
    parent_.current_stream_->body.append(data, length);
  }
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
  if (parent_.current_stream_ && !parent_.current_stream_->body.empty()) {
    parent_.message_callbacks_.onBody(parent_.current_stream_->body, true);
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
  
  // Ensure we have a stream context for encoding
  if (!parent_.current_stream_) {
    parent_.current_stream_ = parent_.stream_manager_.getOrCreateContext(0);
  }
  
  std::ostringstream message;
  
  if (parent_.is_server_) {
    // Server mode: encode response using the same HTTP version as the request
    parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseBegin);
    
    int status_code = std::stoi(status_code_or_method);
    
    // Use the HTTP version from the request for transparent protocol handling
    std::ostringstream version_str;
    if (parent_.current_stream_) {
      version_str << "HTTP/" << static_cast<int>(parent_.current_stream_->http_major) 
                  << "." << static_cast<int>(parent_.current_stream_->http_minor);
    } else {
      version_str << "HTTP/1.1";  // Default fallback
    }
    message << version_str.str() << " " << status_code << " ";
    
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
    std::ostringstream version_str;
    if (parent_.current_stream_) {
      version_str << "HTTP/" << static_cast<int>(parent_.current_stream_->http_major) 
                  << "." << static_cast<int>(parent_.current_stream_->http_minor);
    } else {
      version_str << "HTTP/1.1";  // Default fallback
    }
    message << status_code_or_method << " " << path << " " << version_str.str() << "\r\n";
  }
  
  // Add headers
  for (const auto& header : headers) {
    message << header.first << ": " << header.second << "\r\n";
  }
  
  // End headers
  message << "\r\n";
  
  // For server responses, send the headers immediately through write callbacks
  std::string message_str = message.str();
  if (parent_.is_server_ && parent_.write_callbacks_) {
    parent_.message_buffer_.add(message_str.c_str(), message_str.length());
    // Write directly to connection following production pattern
    // This replaces the deprecated injectWriteDataToFilterChain method
    parent_.write_callbacks_->connection().write(parent_.message_buffer_, false);
    parent_.message_buffer_.drain(parent_.message_buffer_.length());
  } else {
    // For client requests, store in buffer
    parent_.message_buffer_.add(message_str.c_str(), message_str.length());
  }
  
  if (end_stream) {
    if (parent_.is_server_) {
      parent_.state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
    } else {
      parent_.state_machine_->handleEvent(HttpCodecEvent::RequestComplete);
    }
  }
}

void HttpCodecFilter::MessageEncoderImpl::encodeData(Buffer& data, bool end_stream) {
  // DON'T call sendMessageData here - we're already in onWrite context
  // The data is already in the buffer being processed by onWrite
  // Just update state machine
  
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