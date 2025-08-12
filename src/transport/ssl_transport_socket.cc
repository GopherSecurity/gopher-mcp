/**
 * @file ssl_transport_socket.cc
 * @brief SSL/TLS transport socket implementation following some good design patterns
 */

#include "mcp/transport/ssl_transport_socket.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <sstream>

namespace mcp {
namespace transport {

namespace {

/**
 * Get OpenSSL error string for debugging
 */
std::string getOpenSSLError() {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
  return std::string(buf);
}

/**
 * Get SSL error reason from SSL connection
 */
std::string getSslErrorReason(SSL* ssl, int ret) {
  int ssl_error = SSL_get_error(ssl, ret);
  switch (ssl_error) {
    case SSL_ERROR_NONE:
      return "No error";
    case SSL_ERROR_ZERO_RETURN:
      return "SSL connection closed";
    case SSL_ERROR_WANT_READ:
      return "SSL wants read";
    case SSL_ERROR_WANT_WRITE:
      return "SSL wants write";
    case SSL_ERROR_WANT_CONNECT:
      return "SSL wants connect";
    case SSL_ERROR_WANT_ACCEPT:
      return "SSL wants accept";
    case SSL_ERROR_WANT_X509_LOOKUP:
      return "SSL wants X509 lookup";
    case SSL_ERROR_SYSCALL:
      return "SSL syscall error: " + getOpenSSLError();
    case SSL_ERROR_SSL:
      return "SSL protocol error: " + getOpenSSLError();
    default:
      return "Unknown SSL error: " + std::to_string(ssl_error);
  }
}

}  // namespace

// SslTransportSocket implementation

SslTransportSocket::SslTransportSocket(
    network::TransportSocketPtr inner_socket,
    SslContextSharedPtr ssl_context,
    InitialRole role,
    event::Dispatcher& dispatcher)
    : inner_socket_(std::move(inner_socket)),
      ssl_context_(ssl_context),
      initial_role_(role),
      dispatcher_(dispatcher) {
  
  // Initialize buffers
  read_buffer_ = std::make_unique<Buffer>();
  write_buffer_ = std::make_unique<Buffer>();
}

SslTransportSocket::~SslTransportSocket() {
  // Clean up SSL resources
  if (ssl_) {
    // Ensure we're in dispatcher thread for SSL cleanup
    dispatcher_.post([ssl = ssl_]() {
      SSL_free(ssl);
    });
    ssl_ = nullptr;
  }
}

void SslTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  transport_callbacks_ = &callbacks;
  
  // Pass callbacks to inner socket
  if (inner_socket_) {
    inner_socket_->setTransportSocketCallbacks(callbacks);
  }
}

std::string SslTransportSocket::protocol() const {
  // Return negotiated protocol if available
  if (!negotiated_protocol_.empty()) {
    return negotiated_protocol_;
  }
  
  // Otherwise return SSL/TLS version
  if (ssl_ && state_ == SslState::Connected) {
    const char* version = SSL_get_version(ssl_);
    return version ? std::string(version) : "ssl";
  }
  
  return "ssl";
}

bool SslTransportSocket::canFlushClose() {
  // Can flush close if SSL shutdown complete or not yet connected
  return state_ == SslState::Initial ||
         state_ == SslState::Closed ||
         (shutdown_sent_ && shutdown_received_);
}

VoidResult SslTransportSocket::connect(network::Socket& socket) {
  // Validate state
  if (state_ != SslState::Initial) {
    return makeError("SSL socket already connected or connecting");
  }
  
  // Transition to connecting state
  if (!transitionState(SslState::Connecting)) {
    return makeError("Invalid state transition to Connecting");
  }
  
  // Pass through to inner socket
  auto result = inner_socket_->connect(socket);
  if (!result.ok()) {
    transitionState(SslState::Error);
    failure_reason_ = result.error();
    return result;
  }
  
  return VoidResult::success();
}

void SslTransportSocket::closeSocket(network::ConnectionEvent event) {
  // Handle different close scenarios based on state
  switch (state_.load()) {
    case SslState::Connected:
      // Initiate SSL shutdown for graceful close
      if (!shutdown_sent_) {
        shutdownSsl();
      }
      break;
      
    case SslState::Handshaking:
    case SslState::WantRead:
    case SslState::WantWrite:
      // Cancel handshake
      if (handshake_timer_) {
        handshake_timer_->disableTimer();
      }
      transitionState(SslState::Closed);
      break;
      
    default:
      // Direct close for other states
      transitionState(SslState::Closed);
      break;
  }
  
  // Close inner socket
  if (inner_socket_) {
    inner_socket_->closeSocket(event);
  }
}

void SslTransportSocket::onConnected() {
  // Called when underlying TCP connection is established
  // Initialize SSL and start handshake
  
  // Initialize SSL connection
  auto result = initializeSsl();
  if (!result.ok()) {
    failure_reason_ = result.error();
    transitionState(SslState::Error);
    if (handshake_callbacks_) {
      handshake_callbacks_->onSslHandshakeFailed(failure_reason_);
    }
    return;
  }
  
  // Start handshake (will be performed in dispatcher thread)
  dispatcher_.post([this]() {
    auto action = doHandshake();
    if (action == network::PostIoAction::Close) {
      closeSocket(network::ConnectionEvent::RemoteClose);
    }
  });
}

TransportIoResult SslTransportSocket::doRead(Buffer& buffer) {
  // Validate state
  if (state_ != SslState::Connected) {
    return {network::PostIoAction::Close, 0, false};
  }
  
  // Perform SSL read
  return sslRead(buffer);
}

TransportIoResult SslTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  // Validate state
  if (state_ != SslState::Connected) {
    // Buffer data if still handshaking
    if (state_ == SslState::Handshaking ||
        state_ == SslState::WantRead ||
        state_ == SslState::WantWrite) {
      write_buffer_->move(buffer);
      return {network::PostIoAction::KeepOpen, 0, false};
    }
    return {network::PostIoAction::Close, 0, false};
  }
  
  // Perform SSL write
  return sslWrite(buffer, end_stream);
}

bool SslTransportSocket::transitionState(SslState new_state) {
  // State transition validation
  // This ensures state machine integrity
  
  SslState current = state_.load();
  
  // Define valid transitions
  bool valid = false;
  switch (current) {
    case SslState::Initial:
      valid = (new_state == SslState::Connecting ||
               new_state == SslState::Error ||
               new_state == SslState::Closed);
      break;
      
    case SslState::Connecting:
      valid = (new_state == SslState::Handshaking ||
               new_state == SslState::Error ||
               new_state == SslState::Closed);
      break;
      
    case SslState::Handshaking:
      valid = (new_state == SslState::WantRead ||
               new_state == SslState::WantWrite ||
               new_state == SslState::Connected ||
               new_state == SslState::Error ||
               new_state == SslState::Closed);
      break;
      
    case SslState::WantRead:
    case SslState::WantWrite:
      valid = (new_state == SslState::Handshaking ||
               new_state == SslState::Connected ||
               new_state == SslState::Error ||
               new_state == SslState::Closed);
      break;
      
    case SslState::Connected:
      valid = (new_state == SslState::ShuttingDown ||
               new_state == SslState::Error ||
               new_state == SslState::Closed);
      break;
      
    case SslState::ShuttingDown:
      valid = (new_state == SslState::Closed ||
               new_state == SslState::Error);
      break;
      
    case SslState::Error:
      valid = (new_state == SslState::Closed);
      break;
      
    case SslState::Closed:
      valid = false;  // Terminal state
      break;
  }
  
  if (!valid) {
    return false;
  }
  
  // Perform atomic state transition
  state_ = new_state;
  
  return true;
}

VoidResult SslTransportSocket::initializeSsl() {
  // Create SSL connection from context
  ssl_ = ssl_context_->newSsl();
  if (!ssl_) {
    return makeError("Failed to create SSL connection");
  }
  
  // Create BIO pair for memory-based I/O
  // This allows us to control when data is read/written to socket
  if (!BIO_new_bio_pair(&internal_bio_, 0, &network_bio_, 0)) {
    SSL_free(ssl_);
    ssl_ = nullptr;
    return makeError("Failed to create BIO pair");
  }
  
  // Attach BIOs to SSL
  SSL_set_bio(ssl_, internal_bio_, internal_bio_);
  
  // Set SSL mode based on role
  if (initial_role_ == InitialRole::Client) {
    SSL_set_connect_state(ssl_);
    
    // Set SNI if configured
    const auto& config = ssl_context_->getConfig();
    if (!config.sni_hostname.empty()) {
      SSL_set_tlsext_host_name(ssl_, config.sni_hostname.c_str());
    }
  } else {
    SSL_set_accept_state(ssl_);
  }
  
  // Transition to handshaking state
  if (!transitionState(SslState::Handshaking)) {
    SSL_free(ssl_);
    ssl_ = nullptr;
    return makeError("Failed to transition to Handshaking state");
  }
  
  // Record handshake start time
  handshake_start_ = std::chrono::steady_clock::now();
  handshake_attempts_ = 0;
  
  return VoidResult::success();
}

network::PostIoAction SslTransportSocket::doHandshake() {
  // Increment attempt counter
  handshake_attempts_++;
  
  // Move any pending data between socket and BIO
  moveToBio();
  
  // Perform SSL handshake
  int ret = SSL_do_handshake(ssl_);
  
  // Move any generated data to socket
  moveFromBio();
  
  if (ret == 1) {
    // Handshake complete
    onHandshakeComplete();
    return network::PostIoAction::KeepOpen;
  }
  
  // Check error
  int ssl_error = SSL_get_error(ssl_, ret);
  
  switch (ssl_error) {
    case SSL_ERROR_WANT_READ:
      // SSL needs to read more data
      transitionState(SslState::WantRead);
      scheduleHandshakeRetry();
      return network::PostIoAction::KeepOpen;
      
    case SSL_ERROR_WANT_WRITE:
      // SSL needs to write more data
      transitionState(SslState::WantWrite);
      scheduleHandshakeRetry();
      return network::PostIoAction::KeepOpen;
      
    case SSL_ERROR_WANT_X509_LOOKUP:
      // Certificate callback needed (not supported yet)
      onHandshakeFailed("X509 lookup required but not supported");
      return network::PostIoAction::Close;
      
    default:
      // Handshake failed
      onHandshakeFailed(getSslErrorReason(ssl_, ret));
      return network::PostIoAction::Close;
  }
}

void SslTransportSocket::resumeHandshake() {
  // Called when socket becomes ready after WantRead/WantWrite
  
  // Transition back to handshaking state
  if (!transitionState(SslState::Handshaking)) {
    onHandshakeFailed("Invalid state transition during handshake resume");
    return;
  }
  
  // Retry handshake
  auto action = doHandshake();
  if (action == network::PostIoAction::Close) {
    closeSocket(network::ConnectionEvent::RemoteClose);
  }
}

void SslTransportSocket::onHandshakeComplete() {
  // Mark handshake as complete
  handshake_complete_ = true;
  
  // Cancel handshake timer if active
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
    handshake_timer_.reset();
  }
  
  // Extract connection information
  
  // Get peer certificate info
  X509* peer_cert = SSL_get_peer_certificate(ssl_);
  if (peer_cert) {
    char subject[256];
    X509_NAME_oneline(X509_get_subject_name(peer_cert), subject, sizeof(subject));
    peer_cert_info_ = std::string(subject);
    X509_free(peer_cert);
  }
  
  // Get negotiated protocol (ALPN)
  const unsigned char* alpn_data = nullptr;
  unsigned int alpn_len = 0;
  SSL_get0_alpn_selected(ssl_, &alpn_data, &alpn_len);
  if (alpn_data && alpn_len > 0) {
    negotiated_protocol_ = std::string(reinterpret_cast<const char*>(alpn_data), alpn_len);
  }
  
  // Get cipher suite
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
  if (cipher) {
    cipher_suite_ = SSL_CIPHER_get_name(cipher);
  }
  
  // Verify peer if configured
  if (ssl_context_->getConfig().verify_peer) {
    auto verify_result = SslContext::verifyPeer(ssl_);
    if (!verify_result.ok() || !verify_result.value()) {
      onHandshakeFailed("Peer verification failed: " + 
                       (verify_result.ok() ? "Certificate invalid" : verify_result.error()));
      return;
    }
  }
  
  // Transition to connected state
  if (!transitionState(SslState::Connected)) {
    onHandshakeFailed("Failed to transition to Connected state");
    return;
  }
  
  // Notify callbacks
  if (handshake_callbacks_) {
    handshake_callbacks_->onSslHandshakeComplete();
  }
  
  // Process any buffered write data
  if (write_buffer_->length() > 0) {
    dispatcher_.post([this]() {
      if (state_ == SslState::Connected && write_buffer_->length() > 0) {
        auto result = sslWrite(*write_buffer_, false);
        if (result.action_ == network::PostIoAction::Close) {
          closeSocket(network::ConnectionEvent::LocalClose);
        }
      }
    });
  }
}

void SslTransportSocket::onHandshakeFailed(const std::string& reason) {
  // Record failure reason
  failure_reason_ = reason;
  
  // Cancel handshake timer if active
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
    handshake_timer_.reset();
  }
  
  // Transition to error state
  transitionState(SslState::Error);
  
  // Notify callbacks
  if (handshake_callbacks_) {
    handshake_callbacks_->onSslHandshakeFailed(reason);
  }
}

TransportIoResult SslTransportSocket::sslRead(Buffer& buffer) {
  // Move data from socket to BIO
  size_t bytes_from_socket = moveToBio();
  
  // Read decrypted data from SSL
  size_t total_bytes_read = 0;
  bool eof = false;
  
  while (true) {
    // Prepare buffer for read
    constexpr size_t read_size = 16384;  // 16KB chunks
    auto slice = buffer.reserve(read_size);
    
    // Read from SSL
    int ret = SSL_read(ssl_, slice.data(), slice.size());
    
    if (ret > 0) {
      // Data read successfully
      buffer.commit(ret);
      total_bytes_read += ret;
      bytes_decrypted_ += ret;
      
      // Continue reading if more data available
      if (SSL_pending(ssl_) > 0) {
        continue;
      }
    } else {
      // Check error
      int ssl_error = SSL_get_error(ssl_, ret);
      
      if (ssl_error == SSL_ERROR_WANT_READ) {
        // Need more data from socket
        break;
      } else if (ssl_error == SSL_ERROR_ZERO_RETURN) {
        // SSL connection closed cleanly
        eof = true;
        shutdown_received_ = true;
        break;
      } else {
        // Error occurred
        failure_reason_ = getSslErrorReason(ssl_, ret);
        return {network::PostIoAction::Close, total_bytes_read, eof};
      }
    }
  }
  
  return {network::PostIoAction::KeepOpen, total_bytes_read, eof};
}

TransportIoResult SslTransportSocket::sslWrite(Buffer& buffer, bool end_stream) {
  size_t total_bytes_written = 0;
  
  // Write data to SSL
  while (buffer.length() > 0) {
    // Get data to write
    auto slices = buffer.getRawSlices();
    if (slices.empty()) {
      break;
    }
    
    // Write to SSL
    int ret = SSL_write(ssl_, slices[0].data(), slices[0].size());
    
    if (ret > 0) {
      // Data written successfully
      buffer.drain(ret);
      total_bytes_written += ret;
      bytes_encrypted_ += ret;
    } else {
      // Check error
      int ssl_error = SSL_get_error(ssl_, ret);
      
      if (ssl_error == SSL_ERROR_WANT_WRITE) {
        // BIO buffer full, flush to socket
        moveFromBio();
        continue;
      } else if (ssl_error == SSL_ERROR_WANT_READ) {
        // Renegotiation? Need to read first
        break;
      } else {
        // Error occurred
        failure_reason_ = getSslErrorReason(ssl_, ret);
        return {network::PostIoAction::Close, total_bytes_written, false};
      }
    }
  }
  
  // Flush encrypted data to socket
  moveFromBio();
  
  // Handle end_stream
  if (end_stream && buffer.length() == 0) {
    shutdownSsl();
  }
  
  return {network::PostIoAction::KeepOpen, total_bytes_written, false};
}

size_t SslTransportSocket::moveToBio() {
  // Read data from socket and write to network BIO
  
  // Read from inner socket
  Buffer temp_buffer;
  auto result = inner_socket_->doRead(temp_buffer);
  
  if (temp_buffer.length() == 0) {
    return 0;
  }
  
  // Write to network BIO
  size_t total_written = 0;
  auto slices = temp_buffer.getRawSlices();
  
  for (const auto& slice : slices) {
    int written = BIO_write(network_bio_, slice.data(), slice.size());
    if (written > 0) {
      total_written += written;
    } else {
      break;
    }
  }
  
  return total_written;
}

size_t SslTransportSocket::moveFromBio() {
  // Read data from network BIO and write to socket
  
  // Check if BIO has pending data
  size_t pending = BIO_ctrl_pending(network_bio_);
  if (pending == 0) {
    return 0;
  }
  
  // Read from BIO
  Buffer temp_buffer;
  auto slice = temp_buffer.reserve(pending);
  int read = BIO_read(network_bio_, slice.data(), slice.size());
  
  if (read <= 0) {
    return 0;
  }
  
  temp_buffer.commit(read);
  
  // Write to inner socket
  auto result = inner_socket_->doWrite(temp_buffer, false);
  
  return result.bytes_processed_;
}

void SslTransportSocket::shutdownSsl() {
  // Initiate SSL shutdown
  if (!ssl_ || shutdown_sent_) {
    return;
  }
  
  // Transition to shutting down state
  transitionState(SslState::ShuttingDown);
  
  // Send close_notify alert
  int ret = SSL_shutdown(ssl_);
  
  if (ret == 0) {
    // close_notify sent, waiting for peer's close_notify
    shutdown_sent_ = true;
    
    // Flush any pending data
    moveFromBio();
    
    // Schedule check for peer's close_notify
    dispatcher_.post([this]() {
      if (state_ == SslState::ShuttingDown) {
        // Try to receive peer's close_notify
        int ret = SSL_shutdown(ssl_);
        if (ret == 1) {
          // Shutdown complete
          shutdown_received_ = true;
          transitionState(SslState::Closed);
        }
      }
    });
  } else if (ret == 1) {
    // Shutdown complete (both directions)
    shutdown_sent_ = true;
    shutdown_received_ = true;
    transitionState(SslState::Closed);
  } else {
    // Error during shutdown
    int ssl_error = SSL_get_error(ssl_, ret);
    if (ssl_error != SSL_ERROR_WANT_READ && ssl_error != SSL_ERROR_WANT_WRITE) {
      // Fatal error
      transitionState(SslState::Error);
    }
  }
}

void SslTransportSocket::scheduleHandshakeRetry() {
  // Schedule handshake retry when socket becomes ready
  
  // Create timer if not exists
  if (!handshake_timer_) {
    handshake_timer_ = dispatcher_.createTimer([this]() {
      resumeHandshake();
    });
  }
  
  // Schedule retry after brief delay
  // This allows socket to become ready
  handshake_timer_->enableTimer(std::chrono::milliseconds(10));
}

std::string SslTransportSocket::getPeerCertificateInfo() const {
  if (state_ != SslState::Connected || peer_cert_info_.empty()) {
    return "";
  }
  return peer_cert_info_;
}

std::string SslTransportSocket::getNegotiatedProtocol() const {
  if (state_ != SslState::Connected) {
    return "";
  }
  return negotiated_protocol_;
}

// SslTransportSocketFactory implementation

SslTransportSocketFactory::SslTransportSocketFactory(
    std::unique_ptr<network::TransportSocketFactoryBase> inner_factory,
    const SslContextConfig& ssl_config,
    event::Dispatcher& dispatcher)
    : inner_factory_(std::move(inner_factory)),
      dispatcher_(dispatcher) {
  
  // Create SSL context
  auto result = SslContextManager::getInstance().getOrCreateContext(ssl_config);
  if (!result.ok()) {
    throw std::runtime_error("Failed to create SSL context: " + result.error());
  }
  ssl_context_ = result.value();
  
  // Determine role based on configuration
  role_ = ssl_config.is_client ? 
          SslTransportSocket::InitialRole::Client :
          SslTransportSocket::InitialRole::Server;
}

network::TransportSocketPtr SslTransportSocketFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Create inner transport socket
  auto inner_socket = inner_factory_->createTransportSocket(options);
  
  // Wrap with SSL
  return std::make_unique<SslTransportSocket>(
      std::move(inner_socket),
      ssl_context_,
      role_,
      dispatcher_);
}

}  // namespace transport
}  // namespace mcp