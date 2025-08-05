#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/buffer.h"
#include <sstream>
#include <algorithm>

namespace mcp {
namespace transport {

namespace {

// HTTP/1.1 line endings
constexpr const char* kCRLF = "\r\n";
constexpr const char* kDoubleCRLF = "\r\n\r\n";

// SSE field prefixes
constexpr const char* kSseData = "data: ";
constexpr const char* kSseEvent = "event: ";
constexpr const char* kSseId = "id: ";
constexpr const char* kSseRetry = "retry: ";

} // namespace

// HttpSseTransportSocket implementation

HttpSseTransportSocket::HttpSseTransportSocket(const HttpSseTransportSocketConfig& config)
    : config_(config) {}

HttpSseTransportSocket::~HttpSseTransportSocket() {
  if (state_ != HttpState::Closed) {
    closeSocket(network::ConnectionEvent::LocalClose);
  }
}

void HttpSseTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

VoidResult HttpSseTransportSocket::connect(network::Socket& socket) {
  if (state_ != HttpState::Disconnected) {
    Error err;
    err.code = -1;
    err.message = "Already connected";
    return makeVoidError(err);
  }
  
  state_ = HttpState::Connecting;
  
  // For HTTP/SSE, we need to establish an HTTP connection
  // This would typically use the underlying socket to connect to the HTTP endpoint
  // For now, we'll simulate the connection
  
  // Parse URL and connect to the endpoint
  // This is a simplified implementation
  
  // Send initial HTTP request to establish SSE connection
  std::stringstream request;
  request << "GET " << config_.endpoint_url << " HTTP/1.1\r\n";
  request << "Accept: text/event-stream\r\n";
  request << "Cache-Control: no-cache\r\n";
  
  // Add custom headers
  for (const auto& header : config_.headers) {
    request << header.first << ": " << header.second << "\r\n";
  }
  
  request << "Connection: keep-alive\r\n";
  request << "\r\n";
  
  // This would be sent through the underlying transport
  // For now, mark as connected
  state_ = HttpState::Connected;
  
  return makeVoidSuccess();
}

void HttpSseTransportSocket::closeSocket(network::ConnectionEvent event) {
  if (state_ == HttpState::Closed) {
    return;
  }
  
  state_ = HttpState::Closed;
  
  // Close underlying transport if exists
  if (underlying_transport_) {
    underlying_transport_->closeSocket(event);
  }
  
  // Clear buffers
  sse_buffer_.clear();
  while (!pending_events_.empty()) {
    pending_events_.pop();
  }
  while (!pending_requests_.empty()) {
    pending_requests_.pop();
  }
  
  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

TransportIoResult HttpSseTransportSocket::doRead(Buffer& buffer) {
  if (state_ != HttpState::Connected) {
    Error err;
    err.code = ENOTCONN;
    err.message = "Not connected";
    return TransportIoResult::error(err);
  }
  
  // Process any pending SSE events
  processSseEvents(buffer);
  
  if (buffer.length() > 0) {
    return TransportIoResult::success(buffer.length());
  }
  
  // If no pending events, try to read more from underlying transport
  if (underlying_transport_) {
    auto temp_buffer = std::make_unique<OwnedBuffer>();
    auto result = underlying_transport_->doRead(*temp_buffer);
    
    if (result.ok() && result.bytes_processed_ > 0) {
      // Process HTTP response
      processHttpResponse(*temp_buffer);
      
      // Try again to get any newly parsed events
      processSseEvents(buffer);
      
      if (buffer.length() > 0) {
        return TransportIoResult::success(buffer.length());
      }
    }
    
    // Return the result from underlying transport
    return result;
  }
  
  return TransportIoResult::success(0);
}

TransportIoResult HttpSseTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  if (state_ != HttpState::Connected) {
    Error err;
    err.code = ENOTCONN;
    err.message = "Not connected";
    return TransportIoResult::error(err);
  }
  
  // For HTTP/SSE, writes are HTTP POST requests with JSON-RPC payloads
  // Extract the data from buffer
  std::string request_body = buffer.toString();
  buffer.drain(buffer.length());
  
  // Queue the request
  pending_requests_.push(request_body);
  
  // Send if no request in flight
  if (!request_in_flight_) {
    sendHttpRequest(pending_requests_.front());
    pending_requests_.pop();
    request_in_flight_ = true;
  }
  
  if (end_stream) {
    state_ = HttpState::Closing;
  }
  
  return TransportIoResult::success(request_body.length());
}

void HttpSseTransportSocket::onConnected() {
  // Called when underlying transport is connected
  if (callbacks_) {
    callbacks_->setTransportSocketIsReadable();
  }
}

void HttpSseTransportSocket::sendHttpRequest(const std::string& body) {
  std::string request = buildHttpRequest(body);
  
  // Send through underlying transport
  if (underlying_transport_) {
    auto buffer = std::make_unique<OwnedBuffer>();
    buffer->add(request);
    underlying_transport_->doWrite(*buffer, false);
  }
}

void HttpSseTransportSocket::processHttpResponse(Buffer& buffer) {
  // Add to SSE buffer
  std::string data = buffer.toString();
  sse_buffer_ += data;
  
  // Process complete lines
  size_t pos = 0;
  while ((pos = sse_buffer_.find('\n')) != std::string::npos) {
    std::string line = sse_buffer_.substr(0, pos);
    sse_buffer_.erase(0, pos + 1);
    
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    
    parseSseEvent(line);
  }
}

void HttpSseTransportSocket::parseSseEvent(const std::string& line) {
  if (line.empty()) {
    // Empty line marks end of event
    if (!pending_events_.empty() && !pending_events_.back().data.empty()) {
      // Event is complete
      return;
    }
  } else if (line.substr(0, strlen(kSseData)) == kSseData) {
    // Data field
    std::string data = line.substr(strlen(kSseData));
    
    if (pending_events_.empty()) {
      pending_events_.push(SseEvent{});
    }
    
    auto& event = pending_events_.back();
    if (!event.data.empty()) {
      event.data += "\n";
    }
    event.data += data;
  } else if (line.substr(0, strlen(kSseEvent)) == kSseEvent) {
    // Event type field
    if (pending_events_.empty()) {
      pending_events_.push(SseEvent{});
    }
    pending_events_.back().event = line.substr(strlen(kSseEvent));
  } else if (line.substr(0, strlen(kSseId)) == kSseId) {
    // Event ID field
    if (pending_events_.empty()) {
      pending_events_.push(SseEvent{});
    }
    pending_events_.back().id = line.substr(strlen(kSseId));
  } else if (line[0] == ':') {
    // Comment, ignore
  }
}

void HttpSseTransportSocket::processSseEvents(Buffer& output) {
  while (!pending_events_.empty()) {
    const auto& event = pending_events_.front();
    
    // For MCP, we only care about the data field which contains JSON-RPC messages
    if (!event.data.empty()) {
      output.add(event.data);
      output.add("\n");
    }
    
    pending_events_.pop();
  }
}

std::string HttpSseTransportSocket::buildHttpRequest(const std::string& body) {
  std::stringstream request;
  
  request << "POST " << config_.endpoint_url << " HTTP/1.1\r\n";
  request << "Content-Type: application/json\r\n";
  request << "Content-Length: " << body.length() << "\r\n";
  
  // Add custom headers
  for (const auto& header : config_.headers) {
    request << header.first << ": " << header.second << "\r\n";
  }
  
  request << "Connection: keep-alive\r\n";
  request << "\r\n";
  request << body;
  
  return request.str();
}

bool HttpSseTransportSocket::parseHttpResponse(const std::string& data, std::string& body) {
  // Simple HTTP response parser
  size_t header_end = data.find(kDoubleCRLF);
  if (header_end == std::string::npos) {
    return false;
  }
  
  // Parse status line
  size_t first_line_end = data.find(kCRLF);
  if (first_line_end == std::string::npos) {
    return false;
  }
  
  std::string status_line = data.substr(0, first_line_end);
  
  // Check if it's HTTP/1.1 200 OK or similar
  if (status_line.find("HTTP/1.1 200") == std::string::npos &&
      status_line.find("HTTP/1.0 200") == std::string::npos) {
    failure_reason_ = "HTTP request failed: " + status_line;
    return false;
  }
  
  // Extract body
  body = data.substr(header_end + strlen(kDoubleCRLF));
  
  // Mark request as complete
  request_in_flight_ = false;
  
  // Send next queued request if any
  if (!pending_requests_.empty()) {
    sendHttpRequest(pending_requests_.front());
    pending_requests_.pop();
    request_in_flight_ = true;
  }
  
  return true;
}

// HttpSseTransportSocketFactory implementation

HttpSseTransportSocketFactory::HttpSseTransportSocketFactory(
    const HttpSseTransportSocketConfig& config)
    : config_(config) {}

bool HttpSseTransportSocketFactory::implementsSecureTransport() const {
  // Check if URL starts with https://
  return config_.endpoint_url.find("https://") == 0;
}

network::TransportSocketPtr HttpSseTransportSocketFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Create HTTP/SSE transport socket
  auto socket = std::make_unique<HttpSseTransportSocket>(config_);
  
  // Apply any options
  // For HTTP/SSE, options might include additional headers, etc.
  (void)options;
  
  return socket;
}

std::string HttpSseTransportSocketFactory::defaultServerNameIndication() const {
  // Extract hostname from URL
  // This is a simplified implementation
  size_t start = config_.endpoint_url.find("://");
  if (start == std::string::npos) {
    return "";
  }
  
  start += 3; // Skip "://"
  size_t end = config_.endpoint_url.find('/', start);
  if (end == std::string::npos) {
    end = config_.endpoint_url.find(':', start);
  }
  
  if (end != std::string::npos) {
    static std::string hostname = config_.endpoint_url.substr(start, end - start);
    return hostname;
  }
  
  return "";
}

void HttpSseTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Add factory identifier
  const std::string factory_name = "http+sse";
  key.insert(key.end(), factory_name.begin(), factory_name.end());
  
  // Add endpoint URL
  key.insert(key.end(), config_.endpoint_url.begin(), config_.endpoint_url.end());
  
  // Add headers
  for (const auto& header : config_.headers) {
    key.insert(key.end(), header.first.begin(), header.first.end());
    key.push_back(0); // Separator
    key.insert(key.end(), header.second.begin(), header.second.end());
    key.push_back(0); // Separator
  }
  
  // Add options if relevant
  (void)options;
}

} // namespace transport
} // namespace mcp