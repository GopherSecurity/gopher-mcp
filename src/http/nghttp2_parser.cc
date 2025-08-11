#include "mcp/http/nghttp2_parser.h"

#include <cstring>
#include <nghttp2/nghttp2.h>

namespace mcp {
namespace http {

// Helper to convert method string to HttpMethod
static HttpMethod methodFromString(const std::string& method) {
  if (method == "GET") return HttpMethod::GET;
  if (method == "POST") return HttpMethod::POST;
  if (method == "PUT") return HttpMethod::PUT;
  if (method == "DELETE") return HttpMethod::DELETE;
  if (method == "HEAD") return HttpMethod::HEAD;
  if (method == "OPTIONS") return HttpMethod::OPTIONS;
  if (method == "PATCH") return HttpMethod::PATCH;
  if (method == "CONNECT") return HttpMethod::CONNECT;
  if (method == "TRACE") return HttpMethod::TRACE;
  return HttpMethod::UNKNOWN;
}

Nghttp2Parser::Nghttp2Parser(HttpParserType type, HttpParserCallbacks* callbacks)
    : callbacks_(callbacks),
      type_(type),
      status_(ParserStatus::Ok),
      session_(nullptr),
      nghttp2_callbacks_(nullptr),
      connection_preface_received_(false) {  // Note: Initialize preface flag
  initializeSession();
}

Nghttp2Parser::~Nghttp2Parser() {
  if (session_) {
    nghttp2_session_del(session_);
  }
  if (nghttp2_callbacks_) {
    nghttp2_session_callbacks_del(nghttp2_callbacks_);
  }
}

// Wrapper callbacks that match nghttp2's exact signatures
static int onFrameRecvWrapper(nghttp2_session* session,
                              const nghttp2_frame* frame,
                              void* user_data) {
  return Nghttp2Parser::onFrameRecvCallback(session, frame, user_data);
}

static int onDataChunkRecvWrapper(nghttp2_session* session,
                                  uint8_t flags,
                                  int32_t stream_id,
                                  const uint8_t* data,
                                  size_t len,
                                  void* user_data) {
  return Nghttp2Parser::onDataChunkRecvCallback(session, flags, stream_id,
                                                data, len, user_data);
}

static int onStreamCloseWrapper(nghttp2_session* session,
                                int32_t stream_id,
                                uint32_t error_code,
                                void* user_data) {
  return Nghttp2Parser::onStreamCloseCallback(session, stream_id,
                                              error_code, user_data);
}

static int onHeaderWrapper(nghttp2_session* session,
                           const nghttp2_frame* frame,
                           const uint8_t* name,
                           size_t namelen,
                           const uint8_t* value,
                           size_t valuelen,
                           uint8_t flags,
                           void* user_data) {
  return Nghttp2Parser::onHeaderCallback(session, frame, name, namelen,
                                         value, valuelen, flags, user_data);
}

static int onBeginHeadersWrapper(nghttp2_session* session,
                                 const nghttp2_frame* frame,
                                 void* user_data) {
  return Nghttp2Parser::onBeginHeadersCallback(session, frame, user_data);
}

static ssize_t onSendWrapper(nghttp2_session* session,
                             const uint8_t* data,
                             size_t length,
                             int flags,
                             void* user_data) {
  return Nghttp2Parser::onSendCallback(session, data, length, flags, user_data);
}

void Nghttp2Parser::initializeSession() {
  // Create callbacks
  nghttp2_session_callbacks_new(&nghttp2_callbacks_);
  
  // Set up callbacks using wrappers
  nghttp2_session_callbacks_set_on_frame_recv_callback(
      nghttp2_callbacks_, onFrameRecvWrapper);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      nghttp2_callbacks_, onDataChunkRecvWrapper);
  nghttp2_session_callbacks_set_on_stream_close_callback(
      nghttp2_callbacks_, onStreamCloseWrapper);
  nghttp2_session_callbacks_set_on_header_callback(
      nghttp2_callbacks_, onHeaderWrapper);
  nghttp2_session_callbacks_set_on_begin_headers_callback(
      nghttp2_callbacks_, onBeginHeadersWrapper);
  nghttp2_session_callbacks_set_send_callback(
      nghttp2_callbacks_, onSendWrapper);
  
  // Create session based on type
  int rv;
  if (type_ == HttpParserType::REQUEST || type_ == HttpParserType::BOTH) {
    rv = nghttp2_session_server_new(&session_, nghttp2_callbacks_, this);
  } else {
    rv = nghttp2_session_client_new(&session_, nghttp2_callbacks_, this);
  }
  
  if (rv != 0) {
    handleError(rv);
  }
}

size_t Nghttp2Parser::execute(const char* data, size_t length) {
  if (status_ == ParserStatus::Error) {
    return 0;
  }
  
  // Clear caches
  method_cached_ = false;
  status_cached_ = false;
  
  // Note: For server mode, check if this is the connection preface
  // The HTTP/2 connection preface is "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" (24 bytes)
  // nghttp2 handles this internally for server sessions
  static const char* CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
  static const size_t PREFACE_LEN = 24;
  
  size_t bytes_consumed_from_input = 0;
  const char* process_data = data;
  size_t process_length = length;
  
  if ((type_ == HttpParserType::REQUEST || type_ == HttpParserType::BOTH) &&
      !connection_preface_received_) {
    // Server mode - check for connection preface
    if (length >= PREFACE_LEN && memcmp(data, CONNECTION_PREFACE, PREFACE_LEN) == 0) {
      // This is the connection preface, nghttp2 server will handle it
      connection_preface_received_ = true;
      // Don't skip the preface - nghttp2 server expects to process it
    }
  }
  
  // Feed data to nghttp2 (including preface if present)
  // nghttp2_session_mem_recv returns the number of bytes consumed
  ssize_t consumed = nghttp2_session_mem_recv(session_, 
                                              reinterpret_cast<const uint8_t*>(process_data),
                                              process_length);
  
  if (consumed < 0) {
    handleError(static_cast<int>(consumed));
    return 0;
  }
  
  // Send any pending data
  // nghttp2_session_send returns 0 on success, not bytes sent
  ssize_t send_rv = nghttp2_session_send(session_);
  if (send_rv < 0) {
    handleError(static_cast<int>(send_rv));
    return 0;
  }
  
  // Return the number of bytes consumed from the original input
  return static_cast<size_t>(consumed);
}

void Nghttp2Parser::resume() {
  if (status_ == ParserStatus::Paused) {
    status_ = ParserStatus::Ok;
  }
}

ParserCallbackResult Nghttp2Parser::pause() {
  status_ = ParserStatus::Paused;
  return ParserCallbackResult::Pause;
}

ParserStatus Nghttp2Parser::getStatus() const {
  return status_;
}

bool Nghttp2Parser::shouldKeepAlive() const {
  // HTTP/2 connections are persistent by default
  return true;
}

bool Nghttp2Parser::isUpgrade() const {
  // HTTP/2 can be negotiated via upgrade
  return false;  // Handle separately
}

HttpMethod Nghttp2Parser::httpMethod() const {
  if (!method_cached_ && current_stream_id_ > 0) {
    auto it = streams_.find(current_stream_id_);
    if (it != streams_.end()) {
      cached_method_ = it->second.method;
      method_cached_ = true;
    }
  }
  return cached_method_;
}

HttpStatusCode Nghttp2Parser::statusCode() const {
  if (!status_cached_ && current_stream_id_ > 0) {
    auto it = streams_.find(current_stream_id_);
    if (it != streams_.end()) {
      cached_status_ = it->second.status;
      status_cached_ = true;
    }
  }
  return cached_status_;
}

std::string Nghttp2Parser::getError() const {
  return error_message_;
}

void Nghttp2Parser::reset() {
  // Clear streams
  streams_.clear();
  current_stream_id_ = 0;
  send_buffer_.clear();
  error_message_.clear();
  
  // Reset caches
  method_cached_ = false;
  status_cached_ = false;
  connection_preface_received_ = false;  // Note: Reset preface flag
  
  // Reset session
  if (session_) {
    nghttp2_session_del(session_);
    session_ = nullptr;
  }
  
  initializeSession();
  status_ = ParserStatus::Ok;
}

void Nghttp2Parser::finish() {
  nghttp2_session_terminate_session(session_, NGHTTP2_NO_ERROR);
  
  // Note: Generate GOAWAY frame after terminating session
  // This ensures the frame is available in getPendingData()
  int rv = nghttp2_session_send(session_);
  if (rv < 0) {
    handleError(rv);
  }
}

void Nghttp2Parser::submitSettings(const std::map<uint32_t, uint32_t>& settings) {
  std::vector<nghttp2_settings_entry> iv;
  for (const auto& setting : settings) {
    iv.push_back({static_cast<int32_t>(setting.first), setting.second});
  }
  
  int rv = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE,
                                   iv.data(), iv.size());
  if (rv != 0) {
    handleError(rv);
    return;
  }
  
  // Note: After submitting settings, we need to call nghttp2_session_send
  // to actually generate the frame data that will be available via getPendingData().
  // The submit functions only queue frames; send() generates the actual bytes.
  rv = nghttp2_session_send(session_);
  if (rv < 0) {
    handleError(rv);
  }
}

void Nghttp2Parser::submitPing(const uint8_t* opaque_data) {
  int rv = nghttp2_submit_ping(session_, NGHTTP2_FLAG_NONE, opaque_data);
  if (rv != 0) {
    handleError(rv);
    return;
  }
  
  // Note: Generate the actual frame bytes after queuing
  // This ensures getPendingData() will have data to return
  rv = nghttp2_session_send(session_);
  if (rv < 0) {
    handleError(rv);
  }
}

void Nghttp2Parser::submitGoaway(uint32_t last_stream_id, uint32_t error_code) {
  int rv = nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE,
                                 last_stream_id, error_code,
                                 nullptr, 0);
  if (rv != 0) {
    handleError(rv);
    return;
  }
  
  // Note: Generate frame bytes for getPendingData()
  rv = nghttp2_session_send(session_);
  if (rv < 0) {
    handleError(rv);
  }
}

void Nghttp2Parser::submitWindowUpdate(int32_t stream_id, int32_t window_size_increment) {
  int rv = nghttp2_submit_window_update(session_, NGHTTP2_FLAG_NONE,
                                        stream_id, window_size_increment);
  if (rv != 0) {
    handleError(rv);
    return;
  }
  
  // Note: Generate the window update frame bytes
  rv = nghttp2_session_send(session_);
  if (rv < 0) {
    handleError(rv);
  }
}

void Nghttp2Parser::submitPriority(int32_t stream_id, int32_t weight, uint8_t exclusive) {
  nghttp2_priority_spec pri_spec;
  nghttp2_priority_spec_init(&pri_spec, 0, weight, exclusive);
  
  int rv = nghttp2_submit_priority(session_, NGHTTP2_FLAG_NONE,
                                   stream_id, &pri_spec);
  if (rv != 0) {
    handleError(rv);
    return;
  }
  
  // Note: Generate the priority frame bytes
  rv = nghttp2_session_send(session_);
  if (rv < 0) {
    handleError(rv);
  }
}

std::vector<uint8_t> Nghttp2Parser::getPendingData() {
  std::vector<uint8_t> result = std::move(send_buffer_);
  send_buffer_.clear();
  return result;
}

void Nghttp2Parser::handleError(int error_code) {
  status_ = ParserStatus::Error;
  error_message_ = nghttp2_strerror(error_code);
  if (callbacks_) {
    callbacks_->onError(error_message_);
  }
}

// Static callbacks

int Nghttp2Parser::onFrameRecvCallback(nghttp2_session* session,
                                       const void* frame,
                                       void* user_data) {
  auto* self = static_cast<Nghttp2Parser*>(user_data);
  if (self) {
    self->processFrame(frame);
  }
  return 0;
}

int Nghttp2Parser::onDataChunkRecvCallback(nghttp2_session* session,
                                   uint8_t flags,
                                   int32_t stream_id,
                                   const uint8_t* data,
                                   size_t len,
                                   void* user_data) {
  auto* self = static_cast<Nghttp2Parser*>(user_data);
  if (self && self->callbacks_) {
    // Store data in stream
    auto& stream = self->streams_[stream_id];
    stream.body.insert(stream.body.end(), data, data + len);
    
    // Notify callbacks
    auto result = self->callbacks_->onBody(
        reinterpret_cast<const char*>(data), len);
    
    if (result == ParserCallbackResult::Error) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    } else if (result == ParserCallbackResult::Pause) {
      self->status_ = ParserStatus::Paused;
      return NGHTTP2_ERR_PAUSE;
    }
  }
  return 0;
}

int Nghttp2Parser::onStreamCloseCallback(nghttp2_session* session,
                                 int32_t stream_id,
                                 uint32_t error_code,
                                 void* user_data) {
  auto* self = static_cast<Nghttp2Parser*>(user_data);
  if (self && self->callbacks_) {
    self->current_stream_id_ = stream_id;
    
    // Notify message complete
    auto result = self->callbacks_->onMessageComplete();
    
    // Remove stream data
    self->streams_.erase(stream_id);
    
    if (result == ParserCallbackResult::Error) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

int Nghttp2Parser::onHeaderCallback(nghttp2_session* session,
                                    const void* frame,
                                    const uint8_t* name,
                                    size_t namelen,
                                    const uint8_t* value,
                                    size_t valuelen,
                                    uint8_t flags,
                                    void* user_data) {
  auto* self = static_cast<Nghttp2Parser*>(user_data);
  const auto* ng_frame = static_cast<const nghttp2_frame*>(frame);
  
  if (self && self->callbacks_) {
    int32_t stream_id = ng_frame->hd.stream_id;
    auto& stream = self->streams_[stream_id];
    
    std::string header_name(reinterpret_cast<const char*>(name), namelen);
    std::string header_value(reinterpret_cast<const char*>(value), valuelen);
    
    // Store header
    stream.headers[header_name] = header_value;
    
    // Special headers for HTTP/2
    if (header_name == ":method") {
      stream.method = methodFromString(header_value);
    } else if (header_name == ":path") {
      stream.uri = header_value;
      // Notify URL callback
      self->callbacks_->onUrl(header_value.c_str(), header_value.length());
    } else if (header_name == ":status") {
      stream.status = static_cast<HttpStatusCode>(std::stoi(header_value));
      // Notify status callback
      self->callbacks_->onStatus(header_value.c_str(), header_value.length());
    } else {
      // Regular header
      auto result = self->callbacks_->onHeaderField(
          reinterpret_cast<const char*>(name), namelen);
      if (result == ParserCallbackResult::Error) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
      }
      
      result = self->callbacks_->onHeaderValue(
          reinterpret_cast<const char*>(value), valuelen);
      if (result == ParserCallbackResult::Error) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
      }
    }
  }
  return 0;
}

int Nghttp2Parser::onBeginHeadersCallback(nghttp2_session* session,
                                  const void* frame,
                                  void* user_data) {
  auto* self = static_cast<Nghttp2Parser*>(user_data);
  const auto* ng_frame = static_cast<const nghttp2_frame*>(frame);
  
  if (self && self->callbacks_) {
    self->current_stream_id_ = ng_frame->hd.stream_id;
    
    // Initialize stream data
    self->streams_[self->current_stream_id_] = StreamData();
    
    // Notify message begin
    auto result = self->callbacks_->onMessageBegin();
    if (result == ParserCallbackResult::Error) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

ssize_t Nghttp2Parser::onSendCallback(nghttp2_session* session,
                                      const uint8_t* data,
                                      size_t length,
                                      int flags,
                                      void* user_data) {
  auto* self = static_cast<Nghttp2Parser*>(user_data);
  if (self) {
    // Store data to send
    self->send_buffer_.insert(self->send_buffer_.end(), data, data + length);
  }
  return static_cast<ssize_t>(length);
}

void Nghttp2Parser::processFrame(const void* frame) {
  const auto* ng_frame = static_cast<const nghttp2_frame*>(frame);
  
  if (ng_frame->hd.type == NGHTTP2_HEADERS) {
    if (ng_frame->headers.cat == NGHTTP2_HCAT_RESPONSE ||
        ng_frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
      // Headers complete
      if (callbacks_) {
        current_stream_id_ = ng_frame->hd.stream_id;
        streams_[current_stream_id_].headers_complete = true;
        callbacks_->onHeadersComplete();
      }
    }
  }
}

// Nghttp2ParserFactory implementation

HttpParserPtr Nghttp2ParserFactory::createParser(HttpParserType type,
                                                 HttpParserCallbacks* callbacks) {
  return std::make_unique<Nghttp2Parser>(type, callbacks);
}

std::vector<HttpVersion> Nghttp2ParserFactory::supportedVersions() const {
  return {HttpVersion::HTTP_2};
}

}  // namespace http
}  // namespace mcp