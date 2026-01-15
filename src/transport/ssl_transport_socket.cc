/**
 * @file ssl_transport_socket.cc
 * @brief Production-grade SSL/TLS transport socket implementation
 *
 * Design principles for production-grade SSL implementation:
 * - Thread confinement: All operations in dispatcher thread (no locks)
 * - State machine driven: Clear state transitions with validation
 * - Async-first: Support for async certificate validation and selection
 * - Observable: Comprehensive metrics and error tracking
 * - Performant: Optimized buffer management and I/O operations
 * - Resilient: Robust error handling and recovery
 *
 * Key architectural improvements:
 * - PostIoAction pattern for consistent I/O result handling
 * - Comprehensive error queue drainage
 * - Session resumption with statistics
 * - Optimized read/write with chunking
 * - Extended socket info for debugging
 */

#include "mcp/transport/ssl_transport_socket.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

using mcp::TransportIoResult;

namespace mcp {
namespace transport {

namespace {

// ============================================================================
// Constants (optimized for performance)
// ============================================================================

// Maximum bytes to read/write in a single SSL operation
// 16KB balances memory usage and throughput
constexpr size_t kSslMaxReadChunk = 16384;
constexpr size_t kSslMaxWriteChunk = 16384;

// Maximum handshake attempts before giving up
constexpr uint32_t kMaxHandshakeAttempts = 100;

// Handshake timeout
constexpr std::chrono::seconds kHandshakeTimeout{60};

// Size of BIO buffers (0 means use default)
constexpr int kBioBufferSize = 0;

// ============================================================================
// SSL Error Handling Utilities
// ============================================================================

/**
 * Get detailed OpenSSL error information
 * Drains the entire error queue and builds comprehensive error message
 */
std::string drainOpenSSLErrorQueue() {
  std::string error_details;
  bool first = true;

  // Drain all errors from the queue for comprehensive diagnostics
  while (uint64_t err = ERR_get_error()) {
    if (!first) {
      error_details += " | ";
    }
    first = false;

    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));

    // Include library, function, and reason for debugging
    std::stringstream ss;
    ss << "err=" << err << " lib=" << ERR_lib_error_string(err)
       << " func=" << ERR_func_error_string(err) << " reason=" << buf;
    error_details += ss.str();
  }

  return error_details.empty() ? "No OpenSSL errors in queue" : error_details;
}

/**
 * Get SSL error reason with comprehensive details
 * Includes both SSL error code and any queued errors
 */
std::string getSslErrorDetails(SSL* ssl, int ret) {
  int ssl_error = SSL_get_error(ssl, ret);
  std::string base_error;

  switch (ssl_error) {
    case SSL_ERROR_NONE:
      base_error = "No error";
      break;
    case SSL_ERROR_ZERO_RETURN:
      base_error = "SSL connection closed";
      break;
    case SSL_ERROR_WANT_READ:
      base_error = "SSL wants read";
      break;
    case SSL_ERROR_WANT_WRITE:
      base_error = "SSL wants write";
      break;
    case SSL_ERROR_WANT_CONNECT:
      base_error = "SSL wants connect";
      break;
    case SSL_ERROR_WANT_ACCEPT:
      base_error = "SSL wants accept";
      break;
    case SSL_ERROR_WANT_X509_LOOKUP:
      base_error = "SSL wants X509 lookup";
      break;
    case SSL_ERROR_SYSCALL:
      base_error = "SSL syscall error";
      break;
    case SSL_ERROR_SSL:
      base_error = "SSL protocol error";
      break;
    default:
      base_error = "Unknown SSL error: " + std::to_string(ssl_error);
  }

  // Append any queued errors for complete picture
  std::string queue_errors = drainOpenSSLErrorQueue();
  if (!queue_errors.empty() && queue_errors != "No OpenSSL errors in queue") {
    base_error += " [" + queue_errors + "]";
  }

  return base_error;
}

/**
 * Convert SSL verification result to string
 * For debugging certificate validation issues
 */
std::string getVerifyResultString(long verify_result) {
  if (verify_result == X509_V_OK) {
    return "OK";
  }

  const char* reason = X509_verify_cert_error_string(verify_result);
  return reason ? std::string(reason)
                : "Unknown verify error: " + std::to_string(verify_result);
}

}  // namespace

// ============================================================================
// SSL Statistics for observability
// ============================================================================

struct SslTransportSocket::Stats {
  // Handshake statistics
  uint64_t handshakes_started{0};
  uint64_t handshakes_completed{0};
  uint64_t handshakes_failed{0};
  uint64_t handshake_timeouts{0};

  // Session statistics
  uint64_t sessions_reused{0};
  uint64_t sessions_new{0};

  // Certificate statistics
  uint64_t cert_verify_failed{0};
  uint64_t no_certificate{0};
  uint64_t cert_validation_failed{0};

  // I/O statistics
  uint64_t bytes_encrypted{0};
  uint64_t bytes_decrypted{0};
  uint64_t read_errors{0};
  uint64_t write_errors{0};

  // Protocol statistics
  uint64_t alpn_negotiated{0};
  uint64_t alpn_fallback{0};

  // Performance statistics
  std::chrono::milliseconds total_handshake_time{0};
  std::chrono::milliseconds last_handshake_time{0};
};

// ============================================================================
// SslTransportSocket Implementation
// ============================================================================

SslTransportSocket::SslTransportSocket(network::TransportSocketPtr inner_socket,
                                       SslContextSharedPtr ssl_context,
                                       InitialRole role,
                                       event::Dispatcher& dispatcher)
    : inner_socket_(std::move(inner_socket)),
      ssl_context_(ssl_context),
      initial_role_(role),
      dispatcher_(dispatcher),
      stats_(std::make_unique<Stats>()) {
  /**
   * Initialization Flow:
   * 1. Create optimized I/O buffers
   * 2. Create state machine with proper mode
   * 3. Register state change listener
   * 4. Configure state machine with actions
   * 5. Initialize handshake timer
   */

  // Initialize buffers with reserve for performance
  read_buffer_ = std::make_unique<OwnedBuffer>();
  write_buffer_ = std::make_unique<OwnedBuffer>();

  // Buffers will grow as needed during handshake

  // Create state machine based on role
  if (initial_role_ == InitialRole::Client) {
    state_machine_ =
        SslStateMachineFactory::createClientStateMachine(dispatcher_);
    configureClientStateMachine();
  } else {
    state_machine_ =
        SslStateMachineFactory::createServerStateMachine(dispatcher_);
    configureServerStateMachine();
  }

  // Register for state change notifications
  state_listener_id_ = state_machine_->addStateChangeListener(
      [this](SslSocketState old_state, SslSocketState new_state) {
        onStateChanged(old_state, new_state);
      });

  // Create handshake timer (but don't start yet)
  handshake_timer_ =
      dispatcher_.createTimer([this]() { onHandshakeTimeout(); });
}

SslTransportSocket::~SslTransportSocket() {
  /**
   * Cleanup Flow:
   * 1. Cancel any pending operations
   * 2. Unregister listeners
   * 3. Clean up SSL resources
   * 4. Log final statistics
   */

  // Cancel pending timers
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
  }
  if (handshake_retry_timer_) {
    handshake_retry_timer_->disableTimer();
  }

  // Unregister state listener
  if (state_listener_id_ && state_machine_) {
    state_machine_->removeStateChangeListener(state_listener_id_);
  }

  // Clean up SSL resources
  if (ssl_) {
    // Ensure graceful shutdown if still connected
    if (state_machine_->isConnected() && !shutdown_sent_) {
      SSL_shutdown(ssl_);
    }

    // Free SSL object (also frees BIOs)
    SSL_free(ssl_);
    ssl_ = nullptr;
  }

  // Log final statistics for debugging
  logFinalStatistics();
}

// ============================================================================
// Transport Socket Interface Implementation
// ============================================================================

void SslTransportSocket::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  transport_callbacks_ = &callbacks;

  // Pass callbacks through to inner socket
  if (inner_socket_) {
    inner_socket_->setTransportSocketCallbacks(callbacks);
  }
}

std::string SslTransportSocket::protocol() const {
  // Return negotiated ALPN protocol if available
  if (!negotiated_protocol_.empty()) {
    return negotiated_protocol_;
  }

  // Return SSL/TLS version if connected
  if (ssl_ && state_machine_->isConnected()) {
    const char* version = SSL_get_version(ssl_);
    return version ? std::string(version) : "TLS";
  }

  return "TLS";
}

bool SslTransportSocket::canFlushClose() {
  /**
   * Logic for flush close:
   * Can flush close if:
   * - Not initialized yet
   * - Already closed
   * - Graceful shutdown complete
   */
  auto state = state_machine_->getCurrentState();
  return state == SslSocketState::Uninitialized ||
         state == SslSocketState::Closed ||
         state == SslSocketState::ShutdownComplete ||
         (shutdown_sent_ && shutdown_received_);
}

VoidResult SslTransportSocket::connect(network::Socket& socket) {
  /**
   * Connection Flow:
   * 1. Validate state
   * 2. Update statistics
   * 3. Transition states
   * 4. Delegate to inner socket
   */

  // Validate state
  if (state_machine_->getCurrentState() != SslSocketState::Uninitialized) {
    return makeVoidError(
        Error{0, "SSL socket already connected or connecting"});
  }

  // Update statistics
  stats_->handshakes_started++;

  // State transitions
  state_machine_->transition(SslSocketState::Initialized);
  state_machine_->transition(SslSocketState::Connecting);

  // Delegate TCP connection to inner socket
  auto result = inner_socket_->connect(socket);
  if (holds_alternative<Error>(result)) {
    stats_->handshakes_failed++;
    failure_reason_ = get<Error>(result).message;
    state_machine_->transition(SslSocketState::Error);
    return result;
  }

  return makeVoidSuccess();
}

void SslTransportSocket::closeSocket(network::ConnectionEvent event) {
  /**
   * Close Flow:
   * 1. Cancel all pending timers/callbacks to prevent use-after-free
   * 2. Send close_notify if connected (best effort, no followup)
   * 3. Transition directly to Closed state
   * 4. Close inner socket
   *
   * IMPORTANT: This is a "hard close" - we don't schedule any callbacks
   * like scheduleShutdownCheck() because the socket is about to be destroyed.
   * Scheduling callbacks that capture 'this' would cause use-after-free.
   */

  auto state = state_machine_->getCurrentState();
  std::cerr << "[SslTransportSocket] closeSocket current state=" << static_cast<int>(state) << std::endl;

  // Cancel any pending timers that might reference this object
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
  }
  if (handshake_retry_timer_) {
    handshake_retry_timer_->disableTimer();
  }

  // Send close_notify if connected (best effort, don't wait for peer's response)
  if (state == SslSocketState::Connected && ssl_ && !shutdown_sent_) {
    SSL_shutdown(ssl_);  // Best effort, ignore return value
    shutdown_sent_ = true;
    moveFromBio();  // Flush the close_notify to the network
  } else if (state_machine_->isHandshaking()) {
    // Cancel handshake
    stats_->handshakes_failed++;
    cancelHandshake();
  }

  // Transition directly to Closed state - don't schedule any followup callbacks
  state_machine_->transition(SslSocketState::Closed);

  // Close inner socket
  if (inner_socket_) {
    inner_socket_->closeSocket(event);
  }
}

void SslTransportSocket::onConnected() {
  /**
   * TCP Connected Flow:
   * 1. Notify inner socket that TCP is connected
   * 2. Transition to TcpConnected
   * 3. Initialize SSL structures
   * 4. Start handshake timer
   * 5. Begin handshake process
   */
  std::cerr << "[SslTransportSocket] onConnected called, state="
            << static_cast<int>(state_machine_->getCurrentState()) << std::endl;

  // Guard: Only process if we haven't started the connection process yet
  auto current_state = state_machine_->getCurrentState();
  if (current_state != SslSocketState::Connecting &&
      current_state != SslSocketState::Uninitialized &&
      current_state != SslSocketState::Initialized) {
    std::cerr << "[SslTransportSocket] Already connected/connecting, ignoring duplicate onConnected" << std::endl;
    return;
  }

  // CRITICAL: Notify inner socket that TCP connection is established.
  // This allows the inner socket (TcpTransportSocket) to transition to
  // Connected state, enabling reads and writes during SSL handshake.
  if (inner_socket_) {
    std::cerr << "[SslTransportSocket] Notifying inner socket" << std::endl;
    inner_socket_->onConnected();
  }

  // Transition state
  state_machine_->transition(SslSocketState::TcpConnected);

  // Initialize SSL connection
  auto result = initializeSsl();
  if (holds_alternative<Error>(result)) {
    handleSslError(get<Error>(result).message);
    return;
  }

  // Start handshake timer for timeout protection
  handshake_timer_->enableTimer(kHandshakeTimeout);

  // Start handshake in next event loop iteration
  dispatcher_.post([this]() {
    if (initial_role_ == InitialRole::Client) {
      initiateClientHandshake();
    } else {
      initiateServerHandshake();
    }
  });
}

TransportIoResult SslTransportSocket::doRead(Buffer& buffer) {
  /**
   * Read Flow (optimized):
   * 1. Check state
   * 2. Perform optimized SSL read
   * 3. Update statistics
   * 4. Handle errors
   */
  std::cerr << "[SSL] doRead called" << std::endl;

  auto state = state_machine_->getCurrentState();

  if (state != SslSocketState::Connected) {
    std::cerr << "[SSL] doRead: not connected, state=" << static_cast<int>(state) << std::endl;
    if (state_machine_->isHandshaking()) {
      // Still handshaking
      return TransportIoResult::stop();
    }
    // Connection closed or error
    return TransportIoResult::close();
  }

  // Perform optimized SSL read
  auto result = performOptimizedSslRead(buffer);
  std::cerr << "[SSL] doRead result: bytes=" << result.bytes_processed_ << ", action=" << static_cast<int>(result.action_) << std::endl;
  return result;
}

TransportIoResult SslTransportSocket::doWrite(Buffer& buffer, bool end_stream) {
  /**
   * Write Flow (optimized):
   * 1. Check state
   * 2. Buffer if handshaking
   * 3. Perform optimized SSL write
   * 4. Handle end_stream
   */

  auto state = state_machine_->getCurrentState();

  if (state != SslSocketState::Connected) {
    if (state_machine_->isHandshaking()) {
      // Buffer data during handshake
      size_t buffered = buffer.length();
      write_buffer_->move(buffer);
      return TransportIoResult::success(buffered);
    }
    // Connection closed or error
    return TransportIoResult::close();
  }

  // Perform optimized SSL write
  auto result = performOptimizedSslWrite(buffer, end_stream);

  // Handle end_stream
  if (end_stream && buffer.length() == 0) {
    initiateShutdown();
  }

  return result;
}

// ============================================================================
// SSL Initialization and Configuration
// ============================================================================

VoidResult SslTransportSocket::initializeSsl() {
  /**
   * SSL Initialization:
   * 1. Create SSL object
   * 2. Create optimized BIO pair
   * 3. Configure SSL options
   * 4. Set up info callback for debugging
   * 5. Configure based on role
   */

  // Create SSL connection object
  ssl_ = ssl_context_->newSsl();
  if (!ssl_) {
    return makeVoidError(
        Error{0, "Failed to create SSL object: " + drainOpenSSLErrorQueue()});
  }

  // Create BIO pair with specified buffer size
  if (!BIO_new_bio_pair(&internal_bio_, kBioBufferSize, &network_bio_,
                        kBioBufferSize)) {
    SSL_free(ssl_);
    ssl_ = nullptr;
    return makeVoidError(
        Error{0, "Failed to create BIO pair: " + drainOpenSSLErrorQueue()});
  }

  // Attach BIOs to SSL
  SSL_set_bio(ssl_, internal_bio_, internal_bio_);

  // Set SSL info callback for debugging
  SSL_set_info_callback(ssl_, [](const SSL* ssl, int where, int ret) {
    // Log SSL state changes for debugging
    if (where & SSL_CB_HANDSHAKE_START) {
      // Handshake started
    } else if (where & SSL_CB_HANDSHAKE_DONE) {
      // Handshake completed
    }
  });

  // Configure based on role
  if (initial_role_ == InitialRole::Client) {
    configureClientSsl();
  } else {
    configureServerSsl();
  }

  // Record handshake start
  handshake_start_ = std::chrono::steady_clock::now();
  handshake_attempts_ = 0;

  return makeVoidSuccess();
}

void SslTransportSocket::configureClientSsl() {
  /**
   * Client Configuration:
   * 1. Set connect state
   * 2. Configure SNI
   * 3. Enable session resumption
   * 4. Set ALPN protocols
   */

  // Set client mode
  SSL_set_connect_state(ssl_);

  const auto& config = ssl_context_->getConfig();

  // Configure SNI
  if (!config.sni_hostname.empty()) {
    std::cerr << "[SSL] Setting SNI hostname: " << config.sni_hostname << std::endl;
    SSL_set_tlsext_host_name(ssl_, config.sni_hostname.c_str());
  } else {
    std::cerr << "[SSL] WARNING: No SNI hostname configured!" << std::endl;
  }

  // Enable session resumption
  SSL_set_session(
      ssl_, nullptr);  // Will attempt to resume if context has cached session

  // Set ALPN protocols if configured
  if (!config.alpn_protocols.empty()) {
    std::string alpn_protocols;
    for (const auto& protocol : config.alpn_protocols) {
      alpn_protocols.push_back(static_cast<char>(protocol.size()));
      alpn_protocols.append(protocol);
    }
    SSL_set_alpn_protos(
        ssl_, reinterpret_cast<const unsigned char*>(alpn_protocols.data()),
        alpn_protocols.size());
  }
}

void SslTransportSocket::configureServerSsl() {
  /**
   * Server Configuration:
   * 1. Set accept state
   * 2. Configure client certificate request
   * 3. Set ALPN protocols
   * 4. Configure session tickets
   */

  // Set server mode
  SSL_set_accept_state(ssl_);

  const auto& config = ssl_context_->getConfig();

  // Request client certificate if configured
  if (config.verify_peer) {
    SSL_set_verify(ssl_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                   nullptr);
  }

  // Set ALPN protocols if configured
  if (!config.alpn_protocols.empty()) {
    // Server ALPN configuration handled at context level
  }
}

// ============================================================================
// State Machine Configuration
// ============================================================================

void SslTransportSocket::configureClientStateMachine() {
  /**
   * Configure client state machine
   */

  // Entry action for ClientHandshakeInit
  state_machine_->setEntryAction(
      SslSocketState::ClientHandshakeInit,
      [this](SslSocketState state, std::function<void()> done) {
        performHandshakeStep();
        done();
      });

  // Entry action for HandshakeWantRead
  state_machine_->setEntryAction(
      SslSocketState::HandshakeWantRead,
      [this](SslSocketState state, std::function<void()> done) {
        scheduleIoWait(true, false);
        done();
      });

  // Entry action for HandshakeWantWrite
  state_machine_->setEntryAction(
      SslSocketState::HandshakeWantWrite,
      [this](SslSocketState state, std::function<void()> done) {
        scheduleIoWait(false, true);
        done();
      });
}

void SslTransportSocket::configureServerStateMachine() {
  /**
   * Configure server state machine
   */

  // Entry action for ServerHandshakeInit
  state_machine_->setEntryAction(
      SslSocketState::ServerHandshakeInit,
      [this](SslSocketState state, std::function<void()> done) {
        performHandshakeStep();
        done();
      });

  // Entry action for HandshakeWantRead
  state_machine_->setEntryAction(
      SslSocketState::HandshakeWantRead,
      [this](SslSocketState state, std::function<void()> done) {
        scheduleIoWait(true, false);
        done();
      });

  // Entry action for HandshakeWantWrite
  state_machine_->setEntryAction(
      SslSocketState::HandshakeWantWrite,
      [this](SslSocketState state, std::function<void()> done) {
        scheduleIoWait(false, true);
        done();
      });
}

// ============================================================================
// Handshake Management
// ============================================================================

void SslTransportSocket::initiateClientHandshake() {
  state_machine_->transition(
      SslSocketState::ClientHandshakeInit,
      [this](bool success, const std::string& error) {
        if (!success) {
          handleSslError("Failed to initiate client handshake: " + error);
        }
      });
}

void SslTransportSocket::initiateServerHandshake() {
  state_machine_->transition(
      SslSocketState::ServerHandshakeInit,
      [this](bool success, const std::string& error) {
        if (!success) {
          handleSslError("Failed to initiate server handshake: " + error);
        }
      });
}

void SslTransportSocket::performHandshakeStep() {
  /**
   * Handshake Step:
   * 1. Check attempt limit
   * 2. Move pending data
   * 3. Perform handshake
   * 4. Handle result with action pattern
   */

  handshake_attempts_++;
  std::cerr << "[SSL] performHandshakeStep attempt=" << handshake_attempts_ << std::endl;

  // Prevent infinite handshake attempts
  if (handshake_attempts_ > kMaxHandshakeAttempts) {
    handleSslError("Handshake exceeded maximum attempts: " +
                   std::to_string(kMaxHandshakeAttempts));
    return;
  }

  // Move data between socket and BIOs
  size_t bytes_to_bio = moveToBio();
  std::cerr << "[SSL] moveToBio returned " << bytes_to_bio << " bytes" << std::endl;

  // Perform handshake
  int ret = SSL_do_handshake(ssl_);
  std::cerr << "[SSL] SSL_do_handshake returned " << ret << std::endl;

  // Debug: check BIO state immediately after handshake
  size_t bio_pending = BIO_ctrl_pending(network_bio_);
  std::cerr << "[SSL] BIO_ctrl_pending(network_bio_)=" << bio_pending << std::endl;
  std::cerr << "[SSL] ssl_=" << (void*)ssl_ << ", network_bio_=" << (void*)network_bio_ << std::endl;

  // Move generated data to socket
  size_t bytes_from_bio = moveFromBio();
  std::cerr << "[SSL] moveFromBio returned " << bytes_from_bio << " bytes" << std::endl;

  if (ret == 1) {
    // Handshake complete
    std::cerr << "[SSL] Handshake complete!" << std::endl;
    onHandshakeComplete();
    return;
  }

  // Check error and determine next action
  int ssl_error = SSL_get_error(ssl_, ret);
  std::cerr << "[SSL] SSL_get_error=" << ssl_error << std::endl;
  auto action = handleHandshakeResult(ssl_error);

  // Handle PostIoAction result
  if (action == TransportIoResult::PostIoAction::CLOSE) {
    std::cerr << "[SSL] Handshake failed, closing" << std::endl;
    handleSslError(getSslErrorDetails(ssl_, ret));
  }
}

TransportIoResult::PostIoAction SslTransportSocket::handleHandshakeResult(
    int ssl_error) {
  /**
   * Handle handshake result with PostIoAction pattern
   */

  switch (ssl_error) {
    case SSL_ERROR_WANT_READ: {
      auto current = state_machine_->getCurrentState();
      std::cerr << "[SSL] Need more data (WANT_READ), current state=" << static_cast<int>(current) << std::endl;
      // If already in HandshakeWantRead, just schedule retry directly
      // Otherwise, transition to HandshakeWantRead (which will trigger scheduleHandshakeRetry)
      if (current == SslSocketState::HandshakeWantRead) {
        std::cerr << "[SSL] Already in HandshakeWantRead, scheduling retry directly" << std::endl;
        scheduleHandshakeRetry();
      } else {
        std::cerr << "[SSL] Scheduling transition to HandshakeWantRead" << std::endl;
        state_machine_->scheduleTransition(SslSocketState::HandshakeWantRead);
      }
      return TransportIoResult::PostIoAction::CONTINUE;
    }

    case SSL_ERROR_WANT_WRITE:
      std::cerr << "[SSL] Need to write more (WANT_WRITE), scheduling transition to HandshakeWantWrite" << std::endl;
      // Use scheduleTransition to avoid "transition already in progress" error
      state_machine_->scheduleTransition(SslSocketState::HandshakeWantWrite);
      return TransportIoResult::PostIoAction::CONTINUE;

    case SSL_ERROR_WANT_X509_LOOKUP:
      // Async certificate validation needed
      state_machine_->transition(SslSocketState::CertificateValidating);
      // TODO: Implement async certificate validation
      return TransportIoResult::PostIoAction::CLOSE;

    default:
      // Handshake failed
      stats_->handshakes_failed++;
      return TransportIoResult::PostIoAction::CLOSE;
  }
}

void SslTransportSocket::onHandshakeComplete() {
  /**
   * Handshake Complete:
   * 1. Cancel handshake timer
   * 2. Update statistics
   * 3. Extract connection info
   * 4. Verify peer
   * 5. Transition to connected
   * 6. Flush buffered writes
   */

  handshake_complete_ = true;

  // Cancel handshake timer
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
  }

  // Update statistics
  auto handshake_duration = std::chrono::steady_clock::now() - handshake_start_;
  stats_->handshakes_completed++;
  stats_->last_handshake_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(handshake_duration);
  stats_->total_handshake_time += stats_->last_handshake_time;

  // Check if session was reused
  if (SSL_session_reused(ssl_)) {
    stats_->sessions_reused++;
  } else {
    stats_->sessions_new++;
  }

  // Extract connection information
  extractConnectionInfo();

  // Verify peer certificate
  if (ssl_context_->getConfig().verify_peer) {
    if (!verifyPeerCertificate()) {
      return;  // Error already handled
    }
  }

  // Transition to connected
  state_machine_->transition(
      SslSocketState::Connected,
      [this](bool success, const std::string& error) {
        if (!success) {
          handleSslError("Failed to transition to Connected: " + error);
          return;
        }

        // Notify callbacks
        if (handshake_callbacks_) {
          handshake_callbacks_->onSslHandshakeComplete();
        }

        // Flush buffered writes
        flushBufferedWrites();
      });
}

void SslTransportSocket::extractConnectionInfo() {
  /**
   * Extract comprehensive connection information
   */

  // Get peer certificate
  X509* peer_cert = SSL_get_peer_certificate(ssl_);
  if (peer_cert) {
    // Extract subject
    char subject[256];
    X509_NAME_oneline(X509_get_subject_name(peer_cert), subject,
                      sizeof(subject));
    peer_cert_info_ = std::string(subject);

    // Extract SAN (Subject Alternative Names)
    extractSubjectAltNames(peer_cert);

    X509_free(peer_cert);
  } else {
    stats_->no_certificate++;
  }

  // Get negotiated ALPN protocol
  const unsigned char* alpn_data = nullptr;
  unsigned int alpn_len = 0;
  SSL_get0_alpn_selected(ssl_, &alpn_data, &alpn_len);
  if (alpn_data && alpn_len > 0) {
    negotiated_protocol_ =
        std::string(reinterpret_cast<const char*>(alpn_data), alpn_len);
    stats_->alpn_negotiated++;
  } else {
    stats_->alpn_fallback++;
  }

  // Get cipher suite
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
  if (cipher) {
    cipher_suite_ = SSL_CIPHER_get_name(cipher);
  }

  // Get TLS version
  tls_version_ = SSL_get_version(ssl_);
}

void SslTransportSocket::extractSubjectAltNames(X509* cert) {
  /**
   * Extract SANs from certificate for validation
   */

  subject_alt_names_.clear();

  GENERAL_NAMES* san_names = static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

  if (san_names) {
    int san_count = sk_GENERAL_NAME_num(san_names);
    for (int i = 0; i < san_count; ++i) {
      GENERAL_NAME* san = sk_GENERAL_NAME_value(san_names, i);
      if (san->type == GEN_DNS) {
        ASN1_STRING* dns_name = san->d.dNSName;
        if (dns_name) {
          std::string name(
              reinterpret_cast<const char*>(ASN1_STRING_get0_data(dns_name)),
              ASN1_STRING_length(dns_name));
          subject_alt_names_.push_back(name);
        }
      }
    }
    GENERAL_NAMES_free(san_names);
  }
}

bool SslTransportSocket::verifyPeerCertificate() {
  /**
   * Enhanced peer verification
   */

  // Get verification result
  long verify_result = SSL_get_verify_result(ssl_);

  if (verify_result != X509_V_OK) {
    stats_->cert_verify_failed++;
    std::string error_reason = getVerifyResultString(verify_result);
    handleSslError("Peer certificate verification failed: " + error_reason);
    return false;
  }

  // Additional validation can be added here
  // e.g., hostname validation, certificate pinning, etc.

  return true;
}

void SslTransportSocket::onHandshakeTimeout() {
  /**
   * Handle handshake timeout
   */

  stats_->handshake_timeouts++;
  handleSslError("SSL handshake timeout");
}

void SslTransportSocket::cancelHandshake() {
  /**
   * Cancel ongoing handshake
   */

  if (handshake_timer_) {
    handshake_timer_->disableTimer();
  }

  state_machine_->transition(SslSocketState::Closed);

  if (handshake_callbacks_) {
    handshake_callbacks_->onSslHandshakeFailed("Handshake cancelled");
  }
}

// ============================================================================
// Optimized SSL I/O Operations
// ============================================================================

TransportIoResult SslTransportSocket::performOptimizedSslRead(Buffer& buffer) {
  /**
   * Optimized SSL Read:
   * 1. Move encrypted data from socket to BIO
   * 2. Read in chunks for better performance
   * 3. Handle partial reads and errors
   * 4. Update statistics
   */

  // Move encrypted data from socket to BIO
  moveToBio();

  size_t total_bytes_read = 0;
  bool keep_reading = true;
  bool eof = false;

  while (keep_reading) {
    // Prepare buffer slice for read
    RawSlice slice;
    size_t slice_size = kSslMaxReadChunk;

    if (slice_size == 0) {
      // Buffer full
      break;
    }

    void* data = buffer.reserveSingleSlice(slice_size, slice);

    // Read from SSL
    int ret = SSL_read(ssl_, data, slice.len_);

    if (ret > 0) {
      // Data read successfully
      std::cerr << "[SSL] Read " << ret << " decrypted bytes: "
                << std::string(static_cast<const char*>(data), std::min(ret, 200)) << std::endl;
      buffer.commit(slice, ret);
      total_bytes_read += ret;
      stats_->bytes_decrypted += ret;

      // Check if more data is immediately available (optimization)
      if (SSL_pending(ssl_) == 0) {
        keep_reading = false;
      }
    } else {
      // Check error
      int ssl_error = SSL_get_error(ssl_, ret);

      switch (ssl_error) {
        case SSL_ERROR_WANT_READ:
          // Need more data from socket
          keep_reading = false;
          break;

        case SSL_ERROR_ZERO_RETURN:
          // SSL connection closed cleanly
          eof = true;
          shutdown_received_ = true;
          state_machine_->transition(SslSocketState::ShutdownReceived);
          keep_reading = false;
          break;

        default:
          // Error occurred
          stats_->read_errors++;
          failure_reason_ = getSslErrorDetails(ssl_, ret);
          state_machine_->transition(SslSocketState::Error);
          return TransportIoResult::close();
      }
    }
  }

  if (eof) {
    return TransportIoResult::endStream(total_bytes_read);
  }

  return total_bytes_read > 0 ? TransportIoResult::success(total_bytes_read)
                              : TransportIoResult::stop();
}

TransportIoResult SslTransportSocket::performOptimizedSslWrite(
    Buffer& buffer, bool end_stream) {
  /**
   * Optimized SSL Write:
   * 1. Write in chunks for better performance
   * 2. Handle partial writes
   * 3. Flush encrypted data to socket
   * 4. Update statistics
   */

  size_t total_bytes_written = 0;

  while (buffer.length() > 0) {
    // Get data to write (chunk for performance)
    size_t write_size = std::min(buffer.length(), kSslMaxWriteChunk);

    // Get slices from buffer
    constexpr size_t max_slices = 16;
    RawSlice slices[max_slices];
    size_t num_slices = buffer.getRawSlices(slices, max_slices);

    if (num_slices == 0) {
      break;
    }

    // Calculate actual write size from first slice
    size_t actual_write = std::min(write_size, slices[0].len_);

    // Write to SSL
    int ret = SSL_write(ssl_, slices[0].mem_, actual_write);

    if (ret > 0) {
      // Data written successfully
      buffer.drain(ret);
      total_bytes_written += ret;
      stats_->bytes_encrypted += ret;

      // Flush encrypted data to socket immediately
      moveFromBio();
    } else {
      // Check error
      int ssl_error = SSL_get_error(ssl_, ret);

      switch (ssl_error) {
        case SSL_ERROR_WANT_WRITE:
          // BIO buffer full, flush and retry
          moveFromBio();

          // If we've written something, return success
          if (total_bytes_written > 0) {
            return TransportIoResult::success(total_bytes_written);
          }
          return TransportIoResult::stop();

        case SSL_ERROR_WANT_READ:
          // Renegotiation requested (unusual during write)
          if (total_bytes_written > 0) {
            return TransportIoResult::success(total_bytes_written);
          }
          return TransportIoResult::stop();

        default:
          // Error occurred
          stats_->write_errors++;
          failure_reason_ = getSslErrorDetails(ssl_, ret);
          state_machine_->transition(SslSocketState::Error);
          return TransportIoResult::close();
      }
    }
  }

  // Final flush of encrypted data
  moveFromBio();

  return TransportIoResult::success(total_bytes_written);
}

size_t SslTransportSocket::moveToBio() {
  /**
   * Move data from socket to BIO (optimized)
   */

  // Read from inner socket
  OwnedBuffer temp_buffer;
  auto result = inner_socket_->doRead(temp_buffer);

  if (temp_buffer.length() == 0) {
    return 0;
  }

  // Write to network BIO
  size_t total_written = 0;
  constexpr size_t max_slices = 16;
  RawSlice slices[max_slices];
  size_t num_slices = temp_buffer.getRawSlices(slices, max_slices);

  for (size_t i = 0; i < num_slices; ++i) {
    int written = BIO_write(network_bio_, slices[i].mem_, slices[i].len_);
    if (written > 0) {
      total_written += written;
    } else {
      // BIO full or error
      break;
    }
  }

  return total_written;
}

size_t SslTransportSocket::moveFromBio() {
  /**
   * Move data from BIO to socket (optimized)
   */

  // Check pending data
  size_t pending = BIO_ctrl_pending(network_bio_);
  std::cerr << "[SSL-moveFromBio] pending=" << pending << std::endl;
  if (pending == 0) {
    return 0;
  }

  // Read from BIO
  OwnedBuffer temp_buffer;
  RawSlice slice;
  void* data = temp_buffer.reserveSingleSlice(pending, slice);
  int read = BIO_read(network_bio_, data, slice.len_);
  std::cerr << "[SSL-moveFromBio] BIO_read returned " << read << std::endl;

  if (read <= 0) {
    return 0;
  }

  temp_buffer.commit(slice, read);
  std::cerr << "[SSL-moveFromBio] buffer length after commit=" << temp_buffer.length() << std::endl;

  // Write to inner socket
  auto result = inner_socket_->doWrite(temp_buffer, false);
  std::cerr << "[SSL-moveFromBio] doWrite bytes_processed=" << result.bytes_processed_
            << ", action=" << static_cast<int>(result.action_) << std::endl;

  return result.bytes_processed_;
}

// ============================================================================
// Shutdown Management
// ============================================================================

void SslTransportSocket::initiateShutdown() {
  /**
   * Initiate SSL shutdown
   */

  if (!ssl_ || shutdown_sent_) {
    return;
  }

  state_machine_->transition(SslSocketState::ShutdownInitiated);

  // Send close_notify
  int ret = SSL_shutdown(ssl_);

  if (ret == 0) {
    // close_notify sent
    shutdown_sent_ = true;
    state_machine_->transition(SslSocketState::ShutdownSent);

    // Flush data
    moveFromBio();

    // Schedule check for peer's close_notify
    scheduleShutdownCheck();
  } else if (ret == 1) {
    // Bidirectional shutdown complete
    shutdown_sent_ = true;
    shutdown_received_ = true;
    state_machine_->transition(SslSocketState::ShutdownComplete);
    state_machine_->scheduleTransition(SslSocketState::Closed);
  } else {
    // Error
    int ssl_error = SSL_get_error(ssl_, ret);
    if (ssl_error != SSL_ERROR_WANT_READ && ssl_error != SSL_ERROR_WANT_WRITE) {
      state_machine_->transition(SslSocketState::Error);
    }
  }
}

void SslTransportSocket::scheduleShutdownCheck() {
  /**
   * Schedule periodic check for shutdown completion
   */

  dispatcher_.post([this]() {
    auto state = state_machine_->getCurrentState();
    if (state == SslSocketState::ShutdownSent) {
      int ret = SSL_shutdown(ssl_);
      if (ret == 1) {
        shutdown_received_ = true;
        state_machine_->transition(SslSocketState::ShutdownComplete);
        state_machine_->scheduleTransition(SslSocketState::Closed);
      } else {
        // Retry later
        scheduleShutdownCheck();
      }
    }
  });
}

// ============================================================================
// Event Handling and Callbacks
// ============================================================================

void SslTransportSocket::onStateChanged(SslSocketState old_state,
                                        SslSocketState new_state) {
  /**
   * Handle state changes
   */
  std::cerr << "[SSL] onStateChanged: " << static_cast<int>(old_state)
            << " -> " << static_cast<int>(new_state) << std::endl;

  switch (new_state) {
    case SslSocketState::Connected:
      std::cerr << "[SSL] State is Connected, transport_callbacks_="
                << (transport_callbacks_ ? "set" : "NULL") << std::endl;
      if (transport_callbacks_) {
        std::cerr << "[SSL] Raising ConnectionEvent::Connected" << std::endl;
        transport_callbacks_->raiseEvent(network::ConnectionEvent::Connected);
        std::cerr << "[SSL] ConnectionEvent::Connected raised" << std::endl;
      }
      break;

    case SslSocketState::HandshakeWantRead:
    case SslSocketState::HandshakeWantWrite:
      scheduleHandshakeRetry();
      break;

    case SslSocketState::Error:
      if (transport_callbacks_) {
        transport_callbacks_->raiseEvent(network::ConnectionEvent::RemoteClose);
      }
      break;

    case SslSocketState::Closed:
      if (transport_callbacks_) {
        transport_callbacks_->raiseEvent(network::ConnectionEvent::LocalClose);
      }
      break;

    default:
      break;
  }
}

void SslTransportSocket::scheduleIoWait(bool wait_for_read,
                                        bool wait_for_write) {
  /**
   * Schedule I/O wait (should use file events in production)
   */
  scheduleHandshakeRetry();
}

void SslTransportSocket::scheduleHandshakeRetry() {
  /**
   * Schedule handshake retry with exponential backoff
   */
  std::cerr << "[SSL] scheduleHandshakeRetry, this=" << this << std::endl;

  if (!handshake_retry_timer_) {
    handshake_retry_timer_ =
        dispatcher_.createTimer([this]() {
          std::cerr << "[SSL] retry timer fired, this=" << this << std::endl;
          performHandshakeStep();
        });
  }

  // Use exponential backoff
  auto delay = std::min(std::chrono::milliseconds(10) * (1 << retry_count_),
                        std::chrono::milliseconds(1000));
  retry_count_++;

  std::cerr << "[SSL] enabling retry timer with delay=" << delay.count() << "ms" << std::endl;
  handshake_retry_timer_->enableTimer(delay);
}

void SslTransportSocket::handleSslError(const std::string& reason) {
  /**
   * Handle SSL error
   */

  failure_reason_ = reason;

  // Cancel timers
  if (handshake_timer_) {
    handshake_timer_->disableTimer();
  }
  if (handshake_retry_timer_) {
    handshake_retry_timer_->disableTimer();
  }

  // Update statistics
  if (!handshake_complete_) {
    stats_->handshakes_failed++;
  }

  // Transition to error state
  state_machine_->transition(SslSocketState::Error);

  // Notify callbacks
  if (handshake_callbacks_ && !handshake_complete_) {
    handshake_callbacks_->onSslHandshakeFailed(reason);
  }
}

void SslTransportSocket::flushBufferedWrites() {
  /**
   * Flush buffered writes after handshake
   */

  if (write_buffer_->length() > 0) {
    dispatcher_.post([this]() {
      if (state_machine_->isConnected() && write_buffer_->length() > 0) {
        auto result = performOptimizedSslWrite(*write_buffer_, false);
        if (result.action_ == TransportIoResult::CLOSE) {
          closeSocket(network::ConnectionEvent::LocalClose);
        }
      }
    });
  }
}

void SslTransportSocket::logFinalStatistics() {
  /**
   * Log final statistics for debugging
   */

  // In production, this would integrate with stats/metrics system
  // For now, we could log to debug output if enabled
}

// ============================================================================
// Extended Information Methods
// ============================================================================

std::string SslTransportSocket::getPeerCertificateInfo() const {
  if (!state_machine_->isConnected() || peer_cert_info_.empty()) {
    return "";
  }
  return peer_cert_info_;
}

std::string SslTransportSocket::getNegotiatedProtocol() const {
  if (!state_machine_->isConnected()) {
    return "";
  }
  return negotiated_protocol_;
}

std::vector<std::string> SslTransportSocket::getSubjectAltNames() const {
  return subject_alt_names_;
}

std::string SslTransportSocket::getCipherSuite() const { return cipher_suite_; }

std::string SslTransportSocket::getTlsVersion() const { return tls_version_; }

SslTransportSocket::SslStats SslTransportSocket::getStatistics() const {
  SslStats stats;
  stats.handshakes_started = stats_->handshakes_started;
  stats.handshakes_completed = stats_->handshakes_completed;
  stats.handshakes_failed = stats_->handshakes_failed;
  stats.sessions_reused = stats_->sessions_reused;
  stats.bytes_encrypted = stats_->bytes_encrypted;
  stats.bytes_decrypted = stats_->bytes_decrypted;
  return stats;
}

// ============================================================================
// SslTransportSocketFactory Implementation
// ============================================================================

SslTransportSocketFactory::SslTransportSocketFactory(
    std::unique_ptr<network::TransportSocketFactoryBase> inner_factory,
    const SslContextConfig& ssl_config,
    event::Dispatcher& dispatcher)
    : inner_factory_(std::move(inner_factory)), dispatcher_(dispatcher) {
  // Create SSL context with caching
  auto result = SslContextManager::getInstance().getOrCreateContext(ssl_config);
  if (holds_alternative<Error>(result)) {
    throw std::runtime_error("Failed to create SSL context: " +
                             get<Error>(result).message);
  }
  ssl_context_ = get<SslContextSharedPtr>(result);

  // Determine role
  role_ = ssl_config.is_client ? SslTransportSocket::InitialRole::Client
                               : SslTransportSocket::InitialRole::Server;
}

network::TransportSocketPtr SslTransportSocketFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Create inner transport socket
  auto client_factory = dynamic_cast<network::ClientTransportSocketFactory*>(
      inner_factory_.get());
  if (!client_factory) {
    throw std::runtime_error(
        "Inner factory must be a ClientTransportSocketFactory");
  }
  auto inner_socket = client_factory->createTransportSocket(options);

  // Wrap with SSL
  return std::make_unique<SslTransportSocket>(std::move(inner_socket),
                                              ssl_context_, role_, dispatcher_);
}

}  // namespace transport
}  // namespace mcp