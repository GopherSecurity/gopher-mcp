/**
 * @file ssl_state_machine.cc
 * @brief Async-only SSL/TLS state machine implementation
 *
 * Lock-free implementation that runs entirely in dispatcher thread.
 * No mutexes needed - uses event loop serialization for thread safety.
 */

#include "mcp/transport/ssl_state_machine.h"

#include <algorithm>
#include <sstream>

// For ASSERT macro in debug builds
#ifdef DEBUG
#define ASSERT(condition) \
  do {                    \
    if (!(condition)) {   \
      abort();            \
    }                     \
  } while (0)
#endif

namespace mcp {
namespace transport {

// =============================================================================
// SslStateMachine Implementation
// =============================================================================

SslStateMachine::SslStateMachine(SslSocketMode mode,
                                 event::Dispatcher& dispatcher)
    : mode_(mode),
      dispatcher_(dispatcher),
      current_state_(SslSocketState::Uninitialized),
      state_entry_time_(std::chrono::steady_clock::now()) {
  // Initialize valid transitions based on mode
  if (mode_ == SslSocketMode::Client) {
    initializeClientTransitions();
  } else {
    initializeServerTransitions();
  }

  // Record initial state
  recordStateTransition(current_state_);
}

SslStateMachine::~SslStateMachine() {
  // Cancel any pending timers
  if (state_timeout_timer_) {
    state_timeout_timer_->disableTimer();
  }
}

// getCurrentState() is defined inline in header

bool SslStateMachine::canTransition(SslSocketState from,
                                    SslSocketState to) const {
  assertInDispatcherThread();

  // Check basic transition map
  if (!isValidTransition(from, to)) {
    return false;
  }

  // Apply custom validators
  for (const auto& validator : custom_validators_) {
    if (!validator(from, to)) {
      return false;
    }
  }

  return true;
}

void SslStateMachine::transition(SslSocketState new_state,
                                 TransitionCompleteCallback callback) {
  assertInDispatcherThread();

  // Prevent reentrancy during async operations
  if (transition_in_progress_) {
    if (callback) {
      callback(false, "Transition already in progress");
    }
    return;
  }

  SslSocketState old_state = current_state_;

  // Validate transition
  if (!canTransition(old_state, new_state)) {
    std::stringstream ss;
    ss << "Invalid transition from " << getStateName(old_state) << " to "
       << getStateName(new_state) << " in "
       << (mode_ == SslSocketMode::Client ? "client" : "server") << " mode";

    if (callback) {
      callback(false, ss.str());
    }
    return;
  }

  // Set transition in progress flag
  transition_in_progress_ = true;

  // Execute async exit action for current state
  executeExitAction(old_state, [this, old_state, new_state, callback]() {
    // After exit action completes, update state
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    recordStateTransition(new_state);

    // Cancel any existing state timeout
    cancelStateTimeout();

    // Execute async entry action for new state
    executeEntryAction(new_state, [this, old_state, new_state, callback]() {
      // After entry action completes, notify observers
      notifyStateChange(old_state, new_state);

      // Clear transition in progress flag
      transition_in_progress_ = false;

      // Invoke completion callback
      if (callback) {
        callback(true, "");
      }
    });
  });
}

void SslStateMachine::forceTransition(SslSocketState new_state) {
  assertInDispatcherThread();

  // Force transition bypasses validation but still uses async flow
  transition_in_progress_ = true;
  SslSocketState old_state = current_state_;

  // Execute async exit action
  executeExitAction(old_state, [this, old_state, new_state]() {
    // Force state change
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    recordStateTransition(new_state);

    // Cancel any existing state timeout
    cancelStateTimeout();

    // Execute async entry action
    executeEntryAction(new_state, [this, old_state, new_state]() {
      // Notify observers
      notifyStateChange(old_state, new_state);
      transition_in_progress_ = false;
    });
  });
}

uint32_t SslStateMachine::addStateChangeListener(StateChangeCallback callback) {
  assertInDispatcherThread();
  uint32_t id = next_listener_id_++;
  state_listeners_[id] = callback;
  return id;
}

void SslStateMachine::removeStateChangeListener(uint32_t listener_id) {
  assertInDispatcherThread();
  state_listeners_.erase(listener_id);
}

// setEntryAction, setExitAction, and addTransitionValidator are defined inline
// in header

std::string SslStateMachine::getStateName(SslSocketState state) {
  switch (state) {
    case SslSocketState::Uninitialized:
      return "Uninitialized";
    case SslSocketState::Initialized:
      return "Initialized";
    case SslSocketState::Connecting:
      return "Connecting";
    case SslSocketState::TcpConnected:
      return "TcpConnected";
    case SslSocketState::ClientHandshakeInit:
      return "ClientHandshakeInit";
    case SslSocketState::ClientHelloSent:
      return "ClientHelloSent";
    case SslSocketState::ServerHelloReceived:
      return "ServerHelloReceived";
    case SslSocketState::ClientCertRequested:
      return "ClientCertRequested";
    case SslSocketState::ClientCertSent:
      return "ClientCertSent";
    case SslSocketState::ClientKeyExchange:
      return "ClientKeyExchange";
    case SslSocketState::ClientChangeCipherSpec:
      return "ClientChangeCipherSpec";
    case SslSocketState::ClientFinished:
      return "ClientFinished";
    case SslSocketState::ServerHandshakeInit:
      return "ServerHandshakeInit";
    case SslSocketState::ClientHelloReceived:
      return "ClientHelloReceived";
    case SslSocketState::ServerHelloSent:
      return "ServerHelloSent";
    case SslSocketState::ServerCertSent:
      return "ServerCertSent";
    case SslSocketState::ServerKeyExchange:
      return "ServerKeyExchange";
    case SslSocketState::ServerCertRequest:
      return "ServerCertRequest";
    case SslSocketState::ServerHelloDone:
      return "ServerHelloDone";
    case SslSocketState::ClientCertReceived:
      return "ClientCertReceived";
    case SslSocketState::ServerChangeCipherSpec:
      return "ServerChangeCipherSpec";
    case SslSocketState::ServerFinished:
      return "ServerFinished";
    case SslSocketState::HandshakeWantRead:
      return "HandshakeWantRead";
    case SslSocketState::HandshakeWantWrite:
      return "HandshakeWantWrite";
    case SslSocketState::HandshakeWantAsync:
      return "HandshakeWantAsync";
    case SslSocketState::CertificateValidating:
      return "CertificateValidating";
    case SslSocketState::Connected:
      return "Connected";
    case SslSocketState::RenegotiationRequested:
      return "RenegotiationRequested";
    case SslSocketState::RenegotiationInProgress:
      return "RenegotiationInProgress";
    case SslSocketState::ShutdownInitiated:
      return "ShutdownInitiated";
    case SslSocketState::ShutdownSent:
      return "ShutdownSent";
    case SslSocketState::ShutdownReceived:
      return "ShutdownReceived";
    case SslSocketState::ShutdownComplete:
      return "ShutdownComplete";
    case SslSocketState::Closed:
      return "Closed";
    case SslSocketState::Error:
      return "Error";
    default:
      return "Unknown";
  }
}

// isTerminalState() is defined inline in header

bool SslStateMachine::isHandshaking() const {
  assertInDispatcherThread();
  return SslStatePatterns::isHandshakeState(current_state_);
}

bool SslStateMachine::isWaitingForIo() const {
  assertInDispatcherThread();
  return SslStatePatterns::isIoBlockedState(current_state_);
}

// isConnected() is defined inline in header

std::chrono::milliseconds SslStateMachine::getTimeInCurrentState() const {
  assertInDispatcherThread();
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_entry_time_);
}

std::vector<std::pair<SslSocketState, std::chrono::steady_clock::time_point>>
SslStateMachine::getStateHistory(size_t max_entries) const {
  assertInDispatcherThread();

  size_t start = 0;
  if (state_history_.size() > max_entries) {
    start = state_history_.size() - max_entries;
  }

  return std::vector<
      std::pair<SslSocketState, std::chrono::steady_clock::time_point>>(
      state_history_.begin() + start, state_history_.end());
}

void SslStateMachine::initializeClientTransitions() {
  // Initial transitions
  valid_transitions_[SslSocketState::Uninitialized] = {
      SslSocketState::Initialized, SslSocketState::Error};

  valid_transitions_[SslSocketState::Initialized] = {SslSocketState::Connecting,
                                                     SslSocketState::Closed,
                                                     SslSocketState::Error};

  // Connection transitions
  valid_transitions_[SslSocketState::Connecting] = {
      SslSocketState::TcpConnected, SslSocketState::Closed,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::TcpConnected] = {
      SslSocketState::ClientHandshakeInit, SslSocketState::Closed,
      SslSocketState::Error};

  // Client handshake transitions
  // Note: HandshakeWantRead is valid because after sending ClientHello,
  // SSL_do_handshake returns WANT_READ to wait for server response
  valid_transitions_[SslSocketState::ClientHandshakeInit] = {
      SslSocketState::ClientHelloSent, SslSocketState::HandshakeWantWrite,
      SslSocketState::HandshakeWantRead,  // Added: wait for server response
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientHelloSent] = {
      SslSocketState::ServerHelloReceived, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerHelloReceived] = {
      SslSocketState::ClientCertRequested, SslSocketState::ClientKeyExchange,
      SslSocketState::CertificateValidating, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientCertRequested] = {
      SslSocketState::ClientCertSent, SslSocketState::ClientKeyExchange,
      SslSocketState::HandshakeWantAsync, SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientCertSent] = {
      SslSocketState::ClientKeyExchange, SslSocketState::HandshakeWantWrite,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientKeyExchange] = {
      SslSocketState::ClientChangeCipherSpec,
      SslSocketState::HandshakeWantWrite, SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientChangeCipherSpec] = {
      SslSocketState::ClientFinished, SslSocketState::HandshakeWantWrite,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientFinished] = {
      SslSocketState::Connected, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  // Async handshake states
  // HandshakeWantRead can transition to any handshake stage since
  // SSL_do_handshake will resume from wherever it left off
  valid_transitions_[SslSocketState::HandshakeWantRead] = {
      SslSocketState::ClientHandshakeInit,  // Added: retry handshake step
      SslSocketState::ClientHelloSent, SslSocketState::ServerHelloReceived,
      SslSocketState::ClientFinished, SslSocketState::Connected,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::HandshakeWantWrite] = {
      SslSocketState::ClientHandshakeInit,
      SslSocketState::ClientCertSent,
      SslSocketState::ClientKeyExchange,
      SslSocketState::ClientChangeCipherSpec,
      SslSocketState::ClientFinished,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::HandshakeWantAsync] = {
      SslSocketState::ClientCertSent, SslSocketState::ClientKeyExchange,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::CertificateValidating] = {
      SslSocketState::ClientKeyExchange, SslSocketState::Error};

  // Connected state transitions
  valid_transitions_[SslSocketState::Connected] = {
      SslSocketState::RenegotiationRequested,  // TLS 1.2 only
      SslSocketState::ShutdownInitiated, SslSocketState::ShutdownReceived,
      SslSocketState::Error};

  // Renegotiation transitions (TLS 1.2)
  valid_transitions_[SslSocketState::RenegotiationRequested] = {
      SslSocketState::RenegotiationInProgress, SslSocketState::Connected,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::RenegotiationInProgress] = {
      SslSocketState::Connected, SslSocketState::HandshakeWantRead,
      SslSocketState::HandshakeWantWrite, SslSocketState::Error};

  // Shutdown transitions
  valid_transitions_[SslSocketState::ShutdownInitiated] = {
      SslSocketState::ShutdownSent, SslSocketState::Closed,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ShutdownSent] = {
      SslSocketState::ShutdownReceived, SslSocketState::ShutdownComplete,
      SslSocketState::Closed, SslSocketState::Error};

  valid_transitions_[SslSocketState::ShutdownReceived] = {
      SslSocketState::ShutdownSent, SslSocketState::ShutdownComplete,
      SslSocketState::Closed, SslSocketState::Error};

  valid_transitions_[SslSocketState::ShutdownComplete] = {
      SslSocketState::Closed};

  // Terminal states have no valid transitions
  valid_transitions_[SslSocketState::Closed] = {};
  valid_transitions_[SslSocketState::Error] = {};
}

void SslStateMachine::initializeServerTransitions() {
  // Initial transitions
  valid_transitions_[SslSocketState::Uninitialized] = {
      SslSocketState::Initialized, SslSocketState::Error};

  valid_transitions_[SslSocketState::Initialized] = {
      SslSocketState::TcpConnected,  // Server accepts connection
      SslSocketState::Closed, SslSocketState::Error};

  // Connection transitions
  valid_transitions_[SslSocketState::TcpConnected] = {
      SslSocketState::ServerHandshakeInit, SslSocketState::Closed,
      SslSocketState::Error};

  // Server handshake transitions
  valid_transitions_[SslSocketState::ServerHandshakeInit] = {
      SslSocketState::ClientHelloReceived, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientHelloReceived] = {
      SslSocketState::ServerHelloSent,
      SslSocketState::HandshakeWantAsync,  // Certificate selection
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerHelloSent] = {
      SslSocketState::ServerCertSent, SslSocketState::HandshakeWantWrite,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerCertSent] = {
      SslSocketState::ServerKeyExchange, SslSocketState::ServerCertRequest,
      SslSocketState::ServerHelloDone, SslSocketState::HandshakeWantWrite,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerKeyExchange] = {
      SslSocketState::ServerCertRequest, SslSocketState::ServerHelloDone,
      SslSocketState::HandshakeWantWrite, SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerCertRequest] = {
      SslSocketState::ServerHelloDone, SslSocketState::HandshakeWantWrite,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerHelloDone] = {
      SslSocketState::ClientCertReceived,
      SslSocketState::ServerChangeCipherSpec, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ClientCertReceived] = {
      SslSocketState::CertificateValidating,
      SslSocketState::ServerChangeCipherSpec, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerChangeCipherSpec] = {
      SslSocketState::ServerFinished, SslSocketState::HandshakeWantRead,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ServerFinished] = {
      SslSocketState::Connected, SslSocketState::HandshakeWantWrite,
      SslSocketState::Error};

  // Async handshake states
  valid_transitions_[SslSocketState::HandshakeWantRead] = {
      SslSocketState::ClientHelloReceived, SslSocketState::ClientCertReceived,
      SslSocketState::ServerChangeCipherSpec, SslSocketState::Connected,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::HandshakeWantWrite] = {
      SslSocketState::ServerHelloSent,
      SslSocketState::ServerCertSent,
      SslSocketState::ServerKeyExchange,
      SslSocketState::ServerCertRequest,
      SslSocketState::ServerHelloDone,
      SslSocketState::ServerFinished,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::HandshakeWantAsync] = {
      SslSocketState::ServerHelloSent, SslSocketState::Error};

  valid_transitions_[SslSocketState::CertificateValidating] = {
      SslSocketState::ServerChangeCipherSpec, SslSocketState::Error};

  // Connected state transitions (same as client)
  valid_transitions_[SslSocketState::Connected] = {
      SslSocketState::RenegotiationRequested, SslSocketState::ShutdownInitiated,
      SslSocketState::ShutdownReceived, SslSocketState::Error};

  // Renegotiation transitions
  valid_transitions_[SslSocketState::RenegotiationRequested] = {
      SslSocketState::RenegotiationInProgress, SslSocketState::Connected,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::RenegotiationInProgress] = {
      SslSocketState::Connected, SslSocketState::HandshakeWantRead,
      SslSocketState::HandshakeWantWrite, SslSocketState::Error};

  // Shutdown transitions (same as client)
  valid_transitions_[SslSocketState::ShutdownInitiated] = {
      SslSocketState::ShutdownSent, SslSocketState::Closed,
      SslSocketState::Error};

  valid_transitions_[SslSocketState::ShutdownSent] = {
      SslSocketState::ShutdownReceived, SslSocketState::ShutdownComplete,
      SslSocketState::Closed, SslSocketState::Error};

  valid_transitions_[SslSocketState::ShutdownReceived] = {
      SslSocketState::ShutdownSent, SslSocketState::ShutdownComplete,
      SslSocketState::Closed, SslSocketState::Error};

  valid_transitions_[SslSocketState::ShutdownComplete] = {
      SslSocketState::Closed};

  // Terminal states
  valid_transitions_[SslSocketState::Closed] = {};
  valid_transitions_[SslSocketState::Error] = {};
}

void SslStateMachine::executeEntryAction(SslSocketState state,
                                         std::function<void()> done) {
  auto it = entry_actions_.find(state);
  if (it != entry_actions_.end() && it->second) {
    // Execute async action with completion callback
    it->second(state, done);
  } else {
    // No action, immediately call done
    done();
  }
}

void SslStateMachine::executeExitAction(SslSocketState state,
                                        std::function<void()> done) {
  auto it = exit_actions_.find(state);
  if (it != exit_actions_.end() && it->second) {
    // Execute async action with completion callback
    it->second(state, done);
  } else {
    // No action, immediately call done
    done();
  }
}

void SslStateMachine::notifyStateChange(SslSocketState old_state,
                                        SslSocketState new_state) {
  // Already in dispatcher thread, no locking needed
  for (const auto& listener : state_listeners_) {
    listener.second(old_state, new_state);
  }
}

void SslStateMachine::recordStateTransition(SslSocketState state) {
  state_history_.push_back({state, std::chrono::steady_clock::now()});

  // Limit history size
  if (state_history_.size() > kMaxHistorySize) {
    state_history_.erase(state_history_.begin());
  }
}

bool SslStateMachine::isValidTransition(SslSocketState from,
                                        SslSocketState to) const {
  auto it = valid_transitions_.find(from);
  if (it == valid_transitions_.end()) {
    return false;
  }

  return it->second.find(to) != it->second.end();
}

void SslStateMachine::handleIoReady(bool readable, bool writable) {
  assertInDispatcherThread();
  processIoEvent(readable, writable);
}

void SslStateMachine::handleSslResult(int ssl_result, int ssl_error) {
  assertInDispatcherThread();

  if (ssl_result > 0) {
    // Success - determine next state based on current state
    if (isHandshaking()) {
      // Move to next handshake state or Connected
      auto next =
          SslStatePatterns::getNextHandshakeState(current_state_, mode_);
      if (next.has_value()) {
        scheduleTransition(next.value());
      }
    }
  } else {
    // Map SSL error to state
    SslSocketState next_state = mapSslErrorToState(ssl_error);
    if (next_state != current_state_) {
      scheduleTransition(next_state);
    }
  }
}

void SslStateMachine::handleAsyncComplete(uint32_t operation_id,
                                          bool success,
                                          const std::string& error) {
  assertInDispatcherThread();

  auto it = pending_operations_.find(operation_id);
  if (it != pending_operations_.end()) {
    const auto& op = it->second;
    SslSocketState next_state = success ? op.success_state : op.failure_state;
    pending_operations_.erase(it);

    scheduleTransition(next_state);
  }
}

uint32_t SslStateMachine::startAsyncOperation(SslSocketState success_state,
                                              SslSocketState failure_state) {
  assertInDispatcherThread();

  uint32_t id = next_operation_id_++;
  pending_operations_[id] = {success_state, failure_state};
  return id;
}

void SslStateMachine::setStateTimeout(std::chrono::milliseconds timeout,
                                      SslSocketState timeout_state) {
  assertInDispatcherThread();

  // Cancel existing timeout
  cancelStateTimeout();

  // Create new timeout timer
  timeout_state_ = timeout_state;
  state_timeout_timer_ = dispatcher_.createTimer([this, timeout_state]() {
    // Timeout expired, transition to timeout state
    scheduleTransition(timeout_state);
  });
  state_timeout_timer_->enableTimer(timeout);
}

void SslStateMachine::cancelStateTimeout() {
  assertInDispatcherThread();

  if (state_timeout_timer_) {
    state_timeout_timer_->disableTimer();
    state_timeout_timer_.reset();
  }
}

void SslStateMachine::processIoEvent(bool readable, bool writable) {
  // Handle I/O events based on current state
  if (current_state_ == SslSocketState::HandshakeWantRead && readable) {
    // Resume handshake that was waiting for read
    handleSslResult(1, 0);  // Trigger retry
  } else if (current_state_ == SslSocketState::HandshakeWantWrite && writable) {
    // Resume handshake that was waiting for write
    handleSslResult(1, 0);  // Trigger retry
  }
}

SslSocketState SslStateMachine::mapSslErrorToState(int ssl_error) const {
  switch (ssl_error) {
    case 2:  // SSL_ERROR_WANT_READ
      return SslSocketState::HandshakeWantRead;
    case 3:  // SSL_ERROR_WANT_WRITE
      return SslSocketState::HandshakeWantWrite;
    case 4:  // SSL_ERROR_WANT_X509_LOOKUP
      return SslSocketState::HandshakeWantAsync;
    case 7:  // SSL_ERROR_WANT_ASYNC
      return SslSocketState::HandshakeWantAsync;
    case 8:  // SSL_ERROR_WANT_ASYNC_JOB
      return SslSocketState::HandshakeWantAsync;
    default:
      return SslSocketState::Error;
  }
}

// =============================================================================
// SslStateMachineFactory Implementation
// =============================================================================

std::unique_ptr<SslStateMachine>
SslStateMachineFactory::createClientStateMachine(
    event::Dispatcher& dispatcher) {
  auto machine =
      std::make_unique<SslStateMachine>(SslSocketMode::Client, dispatcher);

  // Set up standard client entry/exit actions
  machine->setEntryAction(SslSocketState::ClientHandshakeInit,
                          [](SslSocketState, std::function<void()> done) {
                            // Initialize SSL handshake
                            done();
                          });

  machine->setEntryAction(SslSocketState::Connected,
                          [](SslSocketState, std::function<void()> done) {
                            // Connection established
                            done();
                          });

  machine->setExitAction(SslSocketState::Connected,
                         [](SslSocketState, std::function<void()> done) {
                           // Cleanup before disconnect
                           done();
                         });

  return machine;
}

std::unique_ptr<SslStateMachine>
SslStateMachineFactory::createServerStateMachine(
    event::Dispatcher& dispatcher) {
  auto machine =
      std::make_unique<SslStateMachine>(SslSocketMode::Server, dispatcher);

  // Set up standard server entry/exit actions
  machine->setEntryAction(SslSocketState::ServerHandshakeInit,
                          [](SslSocketState, std::function<void()> done) {
                            // Prepare for client hello
                            done();
                          });

  machine->setEntryAction(SslSocketState::Connected,
                          [](SslSocketState, std::function<void()> done) {
                            // Connection established
                            done();
                          });

  return machine;
}

// Removed createStateMachine - not in async header

// =============================================================================
// SslTransitionCoordinator Implementation
// =============================================================================

void SslTransitionCoordinator::executeHandshake(
    std::function<void(bool)> callback) {
  // Determine handshake sequence based on mode
  std::vector<SslSocketState> sequence;

  if (machine_.getMode() == SslSocketMode::Client) {
    sequence = {SslSocketState::ClientHandshakeInit,
                SslSocketState::ClientHelloSent,
                SslSocketState::ServerHelloReceived,
                SslSocketState::ClientKeyExchange,
                SslSocketState::ClientChangeCipherSpec,
                SslSocketState::ClientFinished,
                SslSocketState::Connected};
  } else {
    sequence = {SslSocketState::ServerHandshakeInit,
                SslSocketState::ClientHelloReceived,
                SslSocketState::ServerHelloSent,
                SslSocketState::ServerCertSent,
                SslSocketState::ServerHelloDone,
                SslSocketState::ServerChangeCipherSpec,
                SslSocketState::ServerFinished,
                SslSocketState::Connected};
  }

  executeSequence(sequence, 0, callback);
}

void SslTransitionCoordinator::executeShutdown(
    std::function<void(bool)> callback) {
  std::vector<SslSocketState> sequence = {
      SslSocketState::ShutdownInitiated, SslSocketState::ShutdownSent,
      SslSocketState::ShutdownComplete, SslSocketState::Closed};

  executeSequence(sequence, 0, callback);
}

void SslTransitionCoordinator::executeRenegotiation(
    std::function<void(bool)> callback) {
  // Start renegotiation
  machine_.transition(
      SslSocketState::RenegotiationRequested,
      [this, callback](bool success, const std::string& error) {
        if (!success) {
          callback(false);
          return;
        }

        // Move to renegotiation in progress
        machine_.transition(
            SslSocketState::RenegotiationInProgress,
            [this, callback](bool success, const std::string& error) {
              if (!success) {
                callback(false);
                return;
              }

              // Execute handshake sequence for renegotiation
              executeHandshake(callback);
            });
      });
}

void SslTransitionCoordinator::executeSequence(
    const std::vector<SslSocketState>& states,
    size_t index,
    std::function<void(bool)> callback) {
  // Check if sequence is complete
  if (index >= states.size()) {
    callback(true);
    return;
  }

  // Check if already in target state (can skip)
  if (machine_.getCurrentState() == states[index]) {
    // Move to next state in sequence
    executeSequence(states, index + 1, callback);
    return;
  }

  // Transition to next state in sequence
  machine_.transition(
      states[index],
      [this, states, index, callback](bool success, const std::string& error) {
        if (!success) {
          // Sequence failed
          callback(false);
          return;
        }

        // Continue with next state
        executeSequence(states, index + 1, callback);
      });
}

// =============================================================================
// SslStatePatterns Implementation
// =============================================================================

bool SslStatePatterns::isHandshakeState(SslSocketState state) {
  switch (state) {
    case SslSocketState::ClientHandshakeInit:
    case SslSocketState::ClientHelloSent:
    case SslSocketState::ServerHelloReceived:
    case SslSocketState::ClientCertRequested:
    case SslSocketState::ClientCertSent:
    case SslSocketState::ClientKeyExchange:
    case SslSocketState::ClientChangeCipherSpec:
    case SslSocketState::ClientFinished:
    case SslSocketState::ServerHandshakeInit:
    case SslSocketState::ClientHelloReceived:
    case SslSocketState::ServerHelloSent:
    case SslSocketState::ServerCertSent:
    case SslSocketState::ServerKeyExchange:
    case SslSocketState::ServerCertRequest:
    case SslSocketState::ServerHelloDone:
    case SslSocketState::ClientCertReceived:
    case SslSocketState::ServerChangeCipherSpec:
    case SslSocketState::ServerFinished:
    case SslSocketState::HandshakeWantRead:
    case SslSocketState::HandshakeWantWrite:
    case SslSocketState::HandshakeWantAsync:
    case SslSocketState::CertificateValidating:
    case SslSocketState::RenegotiationInProgress:
      return true;
    default:
      return false;
  }
}

bool SslStatePatterns::isIoBlockedState(SslSocketState state) {
  switch (state) {
    case SslSocketState::HandshakeWantRead:
    case SslSocketState::HandshakeWantWrite:
    case SslSocketState::HandshakeWantAsync:
    case SslSocketState::CertificateValidating:
      return true;
    default:
      return false;
  }
}

bool SslStatePatterns::canTransferData(SslSocketState state) {
  return state == SslSocketState::Connected;
}

bool SslStatePatterns::canInitiateShutdown(SslSocketState state) {
  switch (state) {
    case SslSocketState::Connected:
    case SslSocketState::ShutdownReceived:
      return true;
    default:
      return false;
  }
}

optional<SslSocketState> SslStatePatterns::getNextHandshakeState(
    SslSocketState current, SslSocketMode mode) {
  if (mode == SslSocketMode::Client) {
    return getNextClientHandshakeState(current);
  } else {
    return getNextServerHandshakeState(current);
  }
}

// Helper method to determine if state represents an error
bool SslStatePatterns::isErrorState(SslSocketState state) {
  return state == SslSocketState::Error;
}

// Get next expected client handshake state
optional<SslSocketState> SslStatePatterns::getNextClientHandshakeState(
    SslSocketState current) {
  switch (current) {
    case SslSocketState::ClientHandshakeInit:
      return SslSocketState::ClientHelloSent;
    case SslSocketState::ClientHelloSent:
      return SslSocketState::ServerHelloReceived;
    case SslSocketState::ServerHelloReceived:
      return SslSocketState::ClientKeyExchange;
    case SslSocketState::ClientKeyExchange:
      return SslSocketState::ClientChangeCipherSpec;
    case SslSocketState::ClientChangeCipherSpec:
      return SslSocketState::ClientFinished;
    case SslSocketState::ClientFinished:
      return SslSocketState::Connected;
    default:
      return optional<SslSocketState>();
  }
}

// Get next expected server handshake state
optional<SslSocketState> SslStatePatterns::getNextServerHandshakeState(
    SslSocketState current) {
  switch (current) {
    case SslSocketState::ServerHandshakeInit:
      return SslSocketState::ClientHelloReceived;
    case SslSocketState::ClientHelloReceived:
      return SslSocketState::ServerHelloSent;
    case SslSocketState::ServerHelloSent:
      return SslSocketState::ServerCertSent;
    case SslSocketState::ServerCertSent:
      return SslSocketState::ServerHelloDone;
    case SslSocketState::ServerHelloDone:
      return SslSocketState::ClientCertReceived;
    case SslSocketState::ClientCertReceived:
      return SslSocketState::ServerChangeCipherSpec;
    case SslSocketState::ServerChangeCipherSpec:
      return SslSocketState::ServerFinished;
    case SslSocketState::ServerFinished:
      return SslSocketState::Connected;
    default:
      return optional<SslSocketState>();
  }
}

}  // namespace transport
}  // namespace mcp