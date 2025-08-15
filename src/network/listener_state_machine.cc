/**
 * @file listener_state_machine.cc
 * @brief Implementation of the listener state machine
 */

#include "mcp/network/listener_state_machine.h"

#include <algorithm>
#include <sstream>

namespace mcp {
namespace network {

// Constructor
ListenerStateMachine::ListenerStateMachine(event::Dispatcher& dispatcher,
                                         const ListenerStateMachineConfig& config)
    : dispatcher_(dispatcher),
      config_(config),
      state_history_(kMaxHistorySize) {
  // Initialize timers
  retry_timer_ = dispatcher_.createTimer([this]() {
    // Retry logic after errors
    if (state_ == ListenerState::AcceptError) {
      enable();
    }
  });
  
  drain_timer_ = dispatcher_.createTimer([this]() {
    // Force close if drain timeout exceeded
    if (state_ == ListenerState::Draining) {
      forceClose();
    }
  });
  
  metrics_timer_ = dispatcher_.createTimer([this]() {
    // Periodic metrics updates
    updateStateTimer();
    for (auto& callback : callbacks_) {
      callback->onMetricsUpdate(metrics_);
    }
    metrics_timer_->enableTimer(std::chrono::seconds(1));
  });
  
  // Start metrics timer
  metrics_timer_->enableTimer(std::chrono::seconds(1));
  
  // Record initial state
  state_enter_time_ = std::chrono::steady_clock::now();
}

ListenerStateMachine::~ListenerStateMachine() {
  // Clean shutdown if not already done
  if (state_ != ListenerState::ShutdownComplete && 
      state_ != ListenerState::Error) {
    forceClose();
  }
}

// Check if can accept connections
bool ListenerStateMachine::canAcceptConnections() const {
  return ListenerStateHelper::canAcceptInState(state_);
}

// Check if shutting down
bool ListenerStateMachine::isShuttingDown() const {
  return state_ == ListenerState::ShutdownInitiated ||
         state_ == ListenerState::Draining ||
         state_ == ListenerState::ShutdownComplete;
}

// Initialize the listener
ListenerStateTransition ListenerStateMachine::initialize(
    const Address::InstanceConstSharedPtr& address,
    const SocketOptionsSharedPtr& options) {
  // Validate we're in correct state
  if (state_ != ListenerState::Uninitialized) {
    return transitionTo(ListenerState::Error, ListenerEvent::ConfigurationError,
                       "Cannot initialize from state: " + 
                       ListenerStateHelper::stateToString(state_));
  }
  
  local_address_ = address;
  socket_options_ = options;
  
  return transitionTo(ListenerState::Initialized, ListenerEvent::Configure,
                     "Listener initialized with address: " + address->asString());
}

// Bind to address
ListenerStateTransition ListenerStateMachine::bind() {
  if (state_ != ListenerState::Initialized) {
    return transitionTo(ListenerState::BindError, ListenerEvent::BindFailed,
                       "Cannot bind from state: " + 
                       ListenerStateHelper::stateToString(state_));
  }
  
  // Transition to binding state
  auto result = transitionTo(ListenerState::Binding, ListenerEvent::BindRequested,
                            "Starting bind to " + local_address_->asString());
  
  // In real implementation, this would trigger actual socket bind
  // For now, simulate success
  return transitionTo(ListenerState::Bound, ListenerEvent::BindSucceeded,
                     "Successfully bound to " + local_address_->asString());
}

// Start listening
ListenerStateTransition ListenerStateMachine::listen(int backlog) {
  if (state_ != ListenerState::Bound) {
    return transitionTo(ListenerState::ListenError, ListenerEvent::ListenFailed,
                       "Cannot listen from state: " + 
                       ListenerStateHelper::stateToString(state_));
  }
  
  // Transition through listen pending
  transitionTo(ListenerState::ListenPending, ListenerEvent::ListenRequested,
              "Starting listen with backlog " + std::to_string(backlog));
  
  // Success - now actively listening
  return transitionTo(ListenerState::Listening, ListenerEvent::ListenSucceeded,
                     "Listening with backlog " + std::to_string(backlog));
}

// Enable the listener
ListenerStateTransition ListenerStateMachine::enable() {
  // Can enable from various states
  if (state_ == ListenerState::Disabled ||
      state_ == ListenerState::Paused ||
      state_ == ListenerState::AcceptError) {
    
    consecutive_errors_ = 0;  // Reset error counter
    return transitionTo(ListenerState::Listening, ListenerEvent::EnableRequested,
                       "Listener enabled");
  }
  
  return ListenerStateTransition{false, state_, state_, 
                                 ListenerEvent::EnableRequested,
                                 "Cannot enable from current state", 
                                 std::chrono::steady_clock::now()};
}

// Disable the listener
ListenerStateTransition ListenerStateMachine::disable() {
  if (!canAcceptConnections()) {
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::DisableRequested,
                                  "Already disabled or shutting down",
                                  std::chrono::steady_clock::now()};
  }
  
  return transitionTo(ListenerState::Disabled, ListenerEvent::DisableRequested,
                     "Listener disabled");
}

// Pause accepting connections (backpressure)
ListenerStateTransition ListenerStateMachine::pause() {
  if (state_ != ListenerState::Listening &&
      state_ != ListenerState::AcceptReady) {
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::BackpressureApplied,
                                  "Cannot pause from current state",
                                  std::chrono::steady_clock::now()};
  }
  
  return transitionTo(ListenerState::Paused, ListenerEvent::BackpressureApplied,
                     "Listener paused due to backpressure");
}

// Resume from paused state
ListenerStateTransition ListenerStateMachine::resume() {
  if (state_ != ListenerState::Paused) {
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::BackpressureReleased,
                                  "Not in paused state",
                                  std::chrono::steady_clock::now()};
  }
  
  return transitionTo(ListenerState::Listening, ListenerEvent::BackpressureReleased,
                     "Listener resumed");
}

// Graceful shutdown
ListenerStateTransition ListenerStateMachine::shutdown() {
  if (isShuttingDown()) {
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::ShutdownRequested,
                                  "Already shutting down",
                                  std::chrono::steady_clock::now()};
  }
  
  // Start shutdown sequence
  auto result = transitionTo(ListenerState::ShutdownInitiated, 
                            ListenerEvent::ShutdownRequested,
                            "Graceful shutdown initiated");
  
  // If we have active connections, start draining
  if (active_connections_ > 0 && config_.enable_graceful_shutdown) {
    transitionTo(ListenerState::Draining, ListenerEvent::ShutdownRequested,
                "Draining " + std::to_string(active_connections_) + 
                " active connections");
    
    // Start drain timer
    drain_timer_->enableTimer(config_.drain_timeout);
  } else {
    // No connections to drain, complete shutdown
    transitionTo(ListenerState::ShutdownComplete, ListenerEvent::DrainComplete,
                "Shutdown complete");
  }
  
  return result;
}

// Force close
ListenerStateTransition ListenerStateMachine::forceClose() {
  cancelRetry();
  drain_timer_->disableTimer();
  
  return transitionTo(ListenerState::ShutdownComplete, ListenerEvent::ForceClose,
                     "Forced close");
}

// Handle pending connection
ListenerStateTransition ListenerStateMachine::handleConnectionPending() {
  if (!canAcceptConnections()) {
    metrics_.total_rejected++;
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::ConnectionPending,
                                  "Cannot accept in current state",
                                  std::chrono::steady_clock::now()};
  }
  
  // Check backpressure
  if (config_.enable_backpressure) {
    checkBackpressure();
    if (state_ == ListenerState::Paused) {
      metrics_.total_rejected++;
      return ListenerStateTransition{false, state_, state_,
                                    ListenerEvent::ConnectionPending,
                                    "Backpressure active",
                                    std::chrono::steady_clock::now()};
    }
  }
  
  // Check rate limiting
  if (config_.enable_rate_limiting) {
    checkRateLimit();
    if (isRateLimited()) {
      metrics_.total_rejected++;
      return transitionTo(ListenerState::Throttled, 
                         ListenerEvent::RateLimitExceeded,
                         "Rate limit exceeded");
    }
  }
  
  pending_connections_++;
  return transitionTo(ListenerState::Accepting, ListenerEvent::ConnectionPending,
                     "Accepting new connection");
}

// Handle accepted connection
ListenerStateTransition ListenerStateMachine::handleConnectionAccepted(
    ConnectionSocketPtr socket) {
  if (state_ != ListenerState::Accepting) {
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::ConnectionAccepted,
                                  "Not in accepting state",
                                  std::chrono::steady_clock::now()};
  }
  
  pending_connections_--;
  active_connections_++;
  metrics_.total_accepted++;
  
  // Notify callbacks
  if (socket) {
    auto remote_addr = socket->connectionInfoProvider().remoteAddress();
    for (auto& callback : callbacks_) {
      callback->onConnectionAccepted(remote_addr);
    }
  }
  
  // If filters are enabled, transition to filtering
  if (config_.enable_filter_chain) {
    return transitionTo(ListenerState::AcceptFiltering, 
                       ListenerEvent::ConnectionAccepted,
                       "Running filter chain");
  }
  
  // No filters, connection is ready
  return transitionTo(ListenerState::AcceptComplete,
                     ListenerEvent::ConnectionAccepted,
                     "Connection accepted");
}

// Handle filtered connection
ListenerStateTransition ListenerStateMachine::handleConnectionFiltered(
    ConnectionPtr connection) {
  if (state_ != ListenerState::AcceptFiltering) {
    return ListenerStateTransition{false, state_, state_,
                                  ListenerEvent::ConnectionFiltered,
                                  "Not in filtering state",
                                  std::chrono::steady_clock::now()};
  }
  
  metrics_.filter_chain_success++;
  
  // Notify callbacks
  for (auto& callback : callbacks_) {
    callback->onConnectionReady(std::move(connection));
  }
  
  // Back to listening
  return transitionTo(ListenerState::Listening, 
                     ListenerEvent::ConnectionFiltered,
                     "Connection ready after filtering");
}

// Handle rejected connection
ListenerStateTransition ListenerStateMachine::handleConnectionRejected(
    const std::string& reason) {
  metrics_.total_rejected++;
  metrics_.filter_chain_failure++;
  
  if (state_ == ListenerState::AcceptFiltering) {
    active_connections_--;
  }
  
  return transitionTo(ListenerState::Listening,
                     ListenerEvent::ConnectionRejected,
                     "Connection rejected: " + reason);
}

// Handle error
ListenerStateTransition ListenerStateMachine::handleError(
    ListenerEvent error_event, const std::string& error_message) {
  consecutive_errors_++;
  last_error_time_ = std::chrono::steady_clock::now();
  
  // Update metrics based on error type
  switch (error_event) {
    case ListenerEvent::SocketError:
      metrics_.socket_errors++;
      break;
    case ListenerEvent::SystemResourceExhausted:
      metrics_.resource_exhausted++;
      break;
    default:
      metrics_.accept_errors++;
      break;
  }
  
  // Notify callbacks
  notifyError(error_message);
  
  // Check if we should retry
  if (consecutive_errors_ < config_.max_accept_errors) {
    // Recoverable error - schedule retry
    scheduleRetry();
    return transitionTo(ListenerState::AcceptError, error_event,
                       error_message + " (will retry)");
  }
  
  // Too many errors - go to error state
  return transitionTo(ListenerState::Error, error_event,
                     "Fatal error after " + std::to_string(consecutive_errors_) +
                     " attempts: " + error_message);
}

// Reset metrics
void ListenerStateMachine::resetMetrics() {
  metrics_ = ListenerMetrics{};
}

// Add state callbacks
void ListenerStateMachine::addStateCallbacks(
    std::shared_ptr<ListenerStateCallbacks> callbacks) {
  callbacks_.push_back(callbacks);
}

// Remove state callbacks
void ListenerStateMachine::removeStateCallbacks(
    std::shared_ptr<ListenerStateCallbacks> callbacks) {
  callbacks_.erase(
      std::remove(callbacks_.begin(), callbacks_.end(), callbacks),
      callbacks_.end());
}

// Check if transition is valid
bool ListenerStateMachine::isValidTransition(ListenerState from, 
                                            ListenerState to) const {
  auto valid_transitions = ListenerStateHelper::getValidTransitions(from);
  return std::find(valid_transitions.begin(), valid_transitions.end(), to) !=
         valid_transitions.end();
}

// Check if can handle event
bool ListenerStateMachine::canHandleEvent(ListenerEvent event) const {
  auto valid_events = ListenerStateHelper::getValidEvents(state_);
  return std::find(valid_events.begin(), valid_events.end(), event) !=
         valid_events.end();
}

// Get state history
std::vector<ListenerStateTransition> ListenerStateMachine::getStateHistory(
    size_t max_entries) const {
  std::vector<ListenerStateTransition> history;
  size_t count = std::min(max_entries, state_history_.size());
  
  // Copy from circular buffer
  for (size_t i = 0; i < count; ++i) {
    size_t idx = (history_index_ + kMaxHistorySize - i) % kMaxHistorySize;
    if (state_history_[idx].valid) {
      history.push_back(state_history_[idx]);
    }
  }
  
  return history;
}

// Transition to new state
ListenerStateTransition ListenerStateMachine::transitionTo(
    ListenerState new_state, ListenerEvent triggering_event,
    const std::string& reason) {
  
  ListenerStateTransition transition{
    isTransitionAllowed(state_, new_state, triggering_event),
    state_,
    new_state,
    triggering_event,
    reason,
    std::chrono::steady_clock::now()
  };
  
  if (!transition.valid) {
    return transition;
  }
  
  // Notify callbacks for veto
  for (auto& callback : callbacks_) {
    if (!callback->onStateTransition(transition)) {
      transition.valid = false;
      transition.reason = "Transition vetoed by callback";
      return transition;
    }
  }
  
  // Perform transition
  onExitState(state_);
  
  // Update state duration metrics
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_enter_time_);
  recordStateTime(state_, duration);
  
  // Change state
  ListenerState old_state = state_;
  state_ = new_state;
  state_enter_time_ = now;
  
  // Record in history
  state_history_[history_index_] = transition;
  history_index_ = (history_index_ + 1) % kMaxHistorySize;
  
  onEnterState(new_state);
  
  // Notify callbacks
  notifyStateChange(transition);
  
  return transition;
}

// Check if transition is allowed
bool ListenerStateMachine::isTransitionAllowed(ListenerState from,
                                              ListenerState to,
                                              ListenerEvent event) const {
  // Implement state transition matrix
  // This is simplified - real implementation would have complete matrix
  
  // Can always force close
  if (event == ListenerEvent::ForceClose) {
    return to == ListenerState::ShutdownComplete;
  }
  
  // Can always transition to error on error events
  if (event == ListenerEvent::SocketError ||
      event == ListenerEvent::SystemResourceExhausted ||
      event == ListenerEvent::ConfigurationError) {
    return to == ListenerState::Error || to == ListenerState::AcceptError;
  }
  
  // Check specific transitions
  return isValidTransition(from, to);
}

// State entry handler
void ListenerStateMachine::onEnterState(ListenerState state) {
  switch (state) {
    case ListenerState::Listening:
      consecutive_errors_ = 0;  // Reset error counter
      break;
      
    case ListenerState::Paused:
      metrics_.time_in_paused = std::chrono::milliseconds(0);
      break;
      
    case ListenerState::Draining:
      metrics_.time_in_draining = std::chrono::milliseconds(0);
      break;
      
    default:
      break;
  }
}

// State exit handler  
void ListenerStateMachine::onExitState(ListenerState state) {
  switch (state) {
    case ListenerState::AcceptError:
      cancelRetry();
      break;
      
    case ListenerState::Draining:
      drain_timer_->disableTimer();
      break;
      
    default:
      break;
  }
}

// Schedule retry after error
void ListenerStateMachine::scheduleRetry() {
  auto backoff = config_.error_backoff * (1 << std::min(consecutive_errors_, 5u));
  retry_timer_->enableTimer(backoff);
}

// Cancel retry timer
void ListenerStateMachine::cancelRetry() {
  retry_timer_->disableTimer();
}

// Update state timer
void ListenerStateMachine::updateStateTimer() {
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_enter_time_);
  
  switch (state_.load()) {
    case ListenerState::Listening:
      metrics_.time_in_listening += std::chrono::milliseconds(1000);
      break;
    case ListenerState::Paused:
      metrics_.time_in_paused += std::chrono::milliseconds(1000);
      break;
    case ListenerState::Draining:
      metrics_.time_in_draining += std::chrono::milliseconds(1000);
      break;
    default:
      break;
  }
}

// Check backpressure
void ListenerStateMachine::checkBackpressure() {
  if (active_connections_ >= config_.pause_threshold) {
    applyBackpressure();
  } else if (state_ == ListenerState::Paused &&
             active_connections_ <= config_.resume_threshold) {
    releaseBackpressure();
  }
}

// Apply backpressure
void ListenerStateMachine::applyBackpressure() {
  if (state_ == ListenerState::Listening) {
    pause();
    metrics_.max_connections_reached++;
  }
}

// Release backpressure
void ListenerStateMachine::releaseBackpressure() {
  if (state_ == ListenerState::Paused) {
    resume();
  }
}

// Check rate limit
void ListenerStateMachine::checkRateLimit() {
  auto now = std::chrono::steady_clock::now();
  
  // Remove old timestamps outside window
  while (!accept_timestamps_.empty()) {
    auto age = now - accept_timestamps_.front();
    if (age > config_.rate_limit_window) {
      accept_timestamps_.pop();
    } else {
      break;
    }
  }
  
  // Add current timestamp
  accept_timestamps_.push(now);
}

// Check if rate limited
bool ListenerStateMachine::isRateLimited() const {
  return accept_timestamps_.size() > config_.max_accept_rate;
}

// Update rate limit window
void ListenerStateMachine::updateRateLimitWindow() {
  checkRateLimit();
  
  // If we were throttled and now under limit, transition back
  if (state_ == ListenerState::Throttled && !isRateLimited()) {
    const_cast<ListenerStateMachine*>(this)->transitionTo(
        ListenerState::Listening, ListenerEvent::RateLimitRestored,
        "Rate limit restored");
  }
}

// Update metrics
void ListenerStateMachine::updateMetrics(ListenerEvent event) {
  // Already handled in specific methods
}

// Record state time
void ListenerStateMachine::recordStateTime(ListenerState state,
                                          std::chrono::milliseconds duration) {
  // Already handled in updateStateTimer
}

// Notify state change
void ListenerStateMachine::notifyStateChange(
    const ListenerStateTransition& transition) {
  for (auto& callback : callbacks_) {
    callback->onStateChanged(transition.to_state);
  }
}

// Notify error
void ListenerStateMachine::notifyError(const std::string& error_message) {
  for (auto& callback : callbacks_) {
    callback->onListenerError(error_message);
  }
}

// Factory method
std::unique_ptr<ListenerStateMachine> ListenerStateMachineFactory::create(
    event::Dispatcher& dispatcher,
    const ListenerStateMachineConfig& config) {
  return std::make_unique<ListenerStateMachine>(dispatcher, config);
}

// Helper functions implementation
namespace ListenerStateHelper {

std::string stateToString(ListenerState state) {
  switch (state) {
    case ListenerState::Uninitialized: return "Uninitialized";
    case ListenerState::Initialized: return "Initialized";
    case ListenerState::Binding: return "Binding";
    case ListenerState::Bound: return "Bound";
    case ListenerState::ListenPending: return "ListenPending";
    case ListenerState::Listening: return "Listening";
    case ListenerState::AcceptReady: return "AcceptReady";
    case ListenerState::Accepting: return "Accepting";
    case ListenerState::AcceptFiltering: return "AcceptFiltering";
    case ListenerState::AcceptComplete: return "AcceptComplete";
    case ListenerState::Paused: return "Paused";
    case ListenerState::Resuming: return "Resuming";
    case ListenerState::Throttled: return "Throttled";
    case ListenerState::ShutdownInitiated: return "ShutdownInitiated";
    case ListenerState::Draining: return "Draining";
    case ListenerState::ShutdownComplete: return "ShutdownComplete";
    case ListenerState::Error: return "Error";
    case ListenerState::BindError: return "BindError";
    case ListenerState::ListenError: return "ListenError";
    case ListenerState::AcceptError: return "AcceptError";
    case ListenerState::Disabled: return "Disabled";
    case ListenerState::Maintenance: return "Maintenance";
    default: return "Unknown";
  }
}

std::string eventToString(ListenerEvent event) {
  switch (event) {
    case ListenerEvent::Configure: return "Configure";
    case ListenerEvent::Reconfigure: return "Reconfigure";
    case ListenerEvent::BindRequested: return "BindRequested";
    case ListenerEvent::BindSucceeded: return "BindSucceeded";
    case ListenerEvent::BindFailed: return "BindFailed";
    case ListenerEvent::ListenRequested: return "ListenRequested";
    case ListenerEvent::ListenSucceeded: return "ListenSucceeded";
    case ListenerEvent::ListenFailed: return "ListenFailed";
    case ListenerEvent::ConnectionPending: return "ConnectionPending";
    case ListenerEvent::ConnectionAccepted: return "ConnectionAccepted";
    case ListenerEvent::ConnectionFiltered: return "ConnectionFiltered";
    case ListenerEvent::ConnectionRejected: return "ConnectionRejected";
    case ListenerEvent::FilterTimeout: return "FilterTimeout";
    case ListenerEvent::BackpressureApplied: return "BackpressureApplied";
    case ListenerEvent::BackpressureReleased: return "BackpressureReleased";
    case ListenerEvent::RateLimitExceeded: return "RateLimitExceeded";
    case ListenerEvent::RateLimitRestored: return "RateLimitRestored";
    case ListenerEvent::EnableRequested: return "EnableRequested";
    case ListenerEvent::DisableRequested: return "DisableRequested";
    case ListenerEvent::MaintenanceModeEntered: return "MaintenanceModeEntered";
    case ListenerEvent::MaintenanceModeExited: return "MaintenanceModeExited";
    case ListenerEvent::ShutdownRequested: return "ShutdownRequested";
    case ListenerEvent::DrainComplete: return "DrainComplete";
    case ListenerEvent::ForceClose: return "ForceClose";
    case ListenerEvent::SocketError: return "SocketError";
    case ListenerEvent::SystemResourceExhausted: return "SystemResourceExhausted";
    case ListenerEvent::ConfigurationError: return "ConfigurationError";
    default: return "Unknown";
  }
}

bool isTerminalState(ListenerState state) {
  return state == ListenerState::ShutdownComplete ||
         state == ListenerState::Error;
}

bool canAcceptInState(ListenerState state) {
  return state == ListenerState::Listening ||
         state == ListenerState::AcceptReady ||
         state == ListenerState::Accepting ||
         state == ListenerState::AcceptFiltering ||
         state == ListenerState::AcceptComplete;
}

std::vector<ListenerEvent> getValidEvents(ListenerState state) {
  std::vector<ListenerEvent> events;
  
  // Common events available in most states
  if (!isTerminalState(state)) {
    events.push_back(ListenerEvent::ForceClose);
    events.push_back(ListenerEvent::SocketError);
    events.push_back(ListenerEvent::SystemResourceExhausted);
  }
  
  // State-specific events
  switch (state) {
    case ListenerState::Uninitialized:
      events.push_back(ListenerEvent::Configure);
      break;
      
    case ListenerState::Initialized:
      events.push_back(ListenerEvent::BindRequested);
      break;
      
    case ListenerState::Bound:
      events.push_back(ListenerEvent::ListenRequested);
      break;
      
    case ListenerState::Listening:
      events.push_back(ListenerEvent::ConnectionPending);
      events.push_back(ListenerEvent::DisableRequested);
      events.push_back(ListenerEvent::BackpressureApplied);
      events.push_back(ListenerEvent::ShutdownRequested);
      break;
      
    case ListenerState::Accepting:
      events.push_back(ListenerEvent::ConnectionAccepted);
      events.push_back(ListenerEvent::ConnectionRejected);
      break;
      
    case ListenerState::AcceptFiltering:
      events.push_back(ListenerEvent::ConnectionFiltered);
      events.push_back(ListenerEvent::ConnectionRejected);
      events.push_back(ListenerEvent::FilterTimeout);
      break;
      
    case ListenerState::Paused:
      events.push_back(ListenerEvent::BackpressureReleased);
      events.push_back(ListenerEvent::ShutdownRequested);
      break;
      
    case ListenerState::Disabled:
      events.push_back(ListenerEvent::EnableRequested);
      events.push_back(ListenerEvent::ShutdownRequested);
      break;
      
    case ListenerState::Draining:
      events.push_back(ListenerEvent::DrainComplete);
      events.push_back(ListenerEvent::ForceClose);
      break;
      
    default:
      break;
  }
  
  return events;
}

std::vector<ListenerState> getValidTransitions(ListenerState from) {
  std::vector<ListenerState> states;
  
  // Terminal states can't transition
  if (isTerminalState(from)) {
    return states;
  }
  
  // Error states can always be reached
  states.push_back(ListenerState::Error);
  states.push_back(ListenerState::ShutdownComplete);
  
  // State-specific transitions
  switch (from) {
    case ListenerState::Uninitialized:
      states.push_back(ListenerState::Initialized);
      break;
      
    case ListenerState::Initialized:
      states.push_back(ListenerState::Binding);
      states.push_back(ListenerState::BindError);
      break;
      
    case ListenerState::Binding:
      states.push_back(ListenerState::Bound);
      states.push_back(ListenerState::BindError);
      break;
      
    case ListenerState::Bound:
      states.push_back(ListenerState::ListenPending);
      states.push_back(ListenerState::ListenError);
      break;
      
    case ListenerState::ListenPending:
      states.push_back(ListenerState::Listening);
      states.push_back(ListenerState::ListenError);
      break;
      
    case ListenerState::Listening:
      states.push_back(ListenerState::Accepting);
      states.push_back(ListenerState::Paused);
      states.push_back(ListenerState::Disabled);
      states.push_back(ListenerState::Throttled);
      states.push_back(ListenerState::ShutdownInitiated);
      states.push_back(ListenerState::AcceptError);
      break;
      
    case ListenerState::Accepting:
      states.push_back(ListenerState::AcceptFiltering);
      states.push_back(ListenerState::AcceptComplete);
      states.push_back(ListenerState::Listening);
      states.push_back(ListenerState::AcceptError);
      break;
      
    case ListenerState::AcceptFiltering:
      states.push_back(ListenerState::AcceptComplete);
      states.push_back(ListenerState::Listening);
      states.push_back(ListenerState::AcceptError);
      break;
      
    case ListenerState::AcceptComplete:
      states.push_back(ListenerState::Listening);
      break;
      
    case ListenerState::Paused:
      states.push_back(ListenerState::Listening);
      states.push_back(ListenerState::ShutdownInitiated);
      break;
      
    case ListenerState::Throttled:
      states.push_back(ListenerState::Listening);
      break;
      
    case ListenerState::Disabled:
      states.push_back(ListenerState::Listening);
      states.push_back(ListenerState::ShutdownInitiated);
      break;
      
    case ListenerState::ShutdownInitiated:
      states.push_back(ListenerState::Draining);
      states.push_back(ListenerState::ShutdownComplete);
      break;
      
    case ListenerState::Draining:
      states.push_back(ListenerState::ShutdownComplete);
      break;
      
    case ListenerState::AcceptError:
      states.push_back(ListenerState::Listening);
      states.push_back(ListenerState::Error);
      break;
      
    default:
      break;
  }
  
  return states;
}

}  // namespace ListenerStateHelper

}  // namespace network
}  // namespace mcp