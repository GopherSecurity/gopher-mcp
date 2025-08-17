/**
 * @file http_codec_state_machine.cc
 * @brief HTTP/1.1 codec state machine implementation
 */

#include "mcp/filter/http_codec_state_machine.h"
#include <sstream>

namespace mcp {
namespace filter {

HttpCodecStateMachine::HttpCodecStateMachine(
    event::Dispatcher& dispatcher,
    const HttpCodecStateMachineConfig& config)
    : dispatcher_(dispatcher),
      config_(config) {
  
  // Initialize state
  current_state_ = HttpCodecState::WaitingForRequest;
  state_entry_time_ = std::chrono::steady_clock::now();
  
  // Initialize from config
  keep_alive_enabled_ = config_.enable_keep_alive;
  
  // Initialize valid transitions
  initializeTransitions();
  
  // Create timers
  if (config_.header_timeout.count() > 0) {
    header_timer_ = dispatcher.createTimer([this]() { onHeaderTimeout(); });
  }
  
  if (config_.body_timeout.count() > 0) {
    body_timer_ = dispatcher.createTimer([this]() { onBodyTimeout(); });
  }
  
  if (config_.idle_timeout.count() > 0) {
    idle_timer_ = dispatcher.createTimer([this]() { onIdleTimeout(); });
    // Start idle timer initially
    idle_timer_->enableTimer(config_.idle_timeout);
  }
}

HttpCodecStateMachine::~HttpCodecStateMachine() {
  // Cancel all timers
  if (header_timer_) {
    header_timer_->disableTimer();
  }
  if (body_timer_) {
    body_timer_->disableTimer();
  }
  if (idle_timer_) {
    idle_timer_->disableTimer();
  }
}

HttpCodecStateTransitionResult HttpCodecStateMachine::handleEvent(
    HttpCodecEvent event,
    CompletionCallback callback) {
  
  assertInDispatcherThread();
  
  // Map event to appropriate state transition
  HttpCodecState current = current_state_.load(std::memory_order_acquire);
  HttpCodecState new_state = current;
  std::string reason;
  
  switch (current) {
    case HttpCodecState::WaitingForRequest:
      if (event == HttpCodecEvent::RequestStart) {
        new_state = HttpCodecState::ReceivingHeaders;
        reason = "Request started";
      } else if (event == HttpCodecEvent::Close) {
        new_state = HttpCodecState::Closed;
        reason = "Connection close requested";
      }
      break;
      
    case HttpCodecState::ReceivingHeaders:
      if (event == HttpCodecEvent::HeadersComplete) {
        // Check if there's a body
        if (expect_request_body_) {
          new_state = HttpCodecState::ReceivingBody;
          reason = "Headers complete, receiving body";
        } else {
          new_state = HttpCodecState::SendingResponse;
          reason = "Headers complete, no body";
        }
      } else if (event == HttpCodecEvent::ParseError) {
        new_state = HttpCodecState::Error;
        reason = "Parse error in headers";
      } else if (event == HttpCodecEvent::Timeout) {
        new_state = HttpCodecState::Error;
        reason = "Header timeout";
      }
      break;
      
    case HttpCodecState::ReceivingBody:
      if (event == HttpCodecEvent::MessageComplete) {
        new_state = HttpCodecState::SendingResponse;
        reason = "Request complete";
      } else if (event == HttpCodecEvent::BodyData) {
        // Stay in same state
        reason = "Receiving body data";
      } else if (event == HttpCodecEvent::ParseError) {
        new_state = HttpCodecState::Error;
        reason = "Parse error in body";
      } else if (event == HttpCodecEvent::Timeout) {
        new_state = HttpCodecState::Error;
        reason = "Body timeout";
      }
      break;
      
    case HttpCodecState::SendingResponse:
      if (event == HttpCodecEvent::ResponseComplete) {
        if (keep_alive_enabled_) {
          new_state = HttpCodecState::WaitingForRequest;
          reason = "Response complete, keep-alive";
        } else {
          new_state = HttpCodecState::Closed;
          reason = "Response complete, closing";
        }
      } else if (event == HttpCodecEvent::Reset) {
        new_state = HttpCodecState::WaitingForRequest;
        reason = "Reset for next request";
      } else if (event == HttpCodecEvent::Close) {
        new_state = HttpCodecState::Closed;
        reason = "Connection close during response";
      }
      break;
      
    case HttpCodecState::Closed:
      // Terminal state - no transitions except reset
      if (event == HttpCodecEvent::Reset) {
        new_state = HttpCodecState::WaitingForRequest;
        reason = "Reset after close";
      }
      break;
      
    case HttpCodecState::Error:
      // Terminal state - transitions to reset or close
      if (event == HttpCodecEvent::Reset) {
        new_state = HttpCodecState::WaitingForRequest;
        reason = "Reset after error";
      } else if (event == HttpCodecEvent::Close) {
        new_state = HttpCodecState::Closed;
        reason = "Close after error";
      }
      break;
  }
  
  // Perform transition if state changed
  if (new_state != current) {
    return transitionTo(new_state, event, reason, callback);
  }
  
  // No transition - check if this is a valid no-op or an error
  
  // Terminal states should reject non-reset events
  if ((current == HttpCodecState::Error || current == HttpCodecState::Closed) && 
      event != HttpCodecEvent::Reset) {
    // Invalid event in terminal state
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return HttpCodecStateTransitionResult::Failure("Invalid event in terminal state");
  }
  
  // Valid no-op
  if (callback) {
    dispatcher_.post([callback]() { callback(true); });
  }
  return HttpCodecStateTransitionResult::Success(current);
}

HttpCodecStateTransitionResult HttpCodecStateMachine::transitionTo(
    HttpCodecState new_state,
    HttpCodecEvent event,
    const std::string& reason,
    CompletionCallback callback) {
  
  assertInDispatcherThread();
  
  // Check if transition is valid
  if (!isTransitionValid(current_state_, new_state, event)) {
    std::string error = "Invalid transition from " + 
                       getStateName(current_state_) + " to " + 
                       getStateName(new_state);
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return HttpCodecStateTransitionResult::Failure(error);
  }
  
  // Prevent reentrancy
  if (transition_in_progress_) {
    scheduleTransition(new_state, event, reason, callback);
    return HttpCodecStateTransitionResult::Success(new_state);
  }
  
  transition_in_progress_ = true;
  
  // Execute transition
  executeTransition(new_state, event, reason, [this, callback](bool success) {
    transition_in_progress_ = false;
    if (callback) {
      callback(success);
    }
  });
  
  return HttpCodecStateTransitionResult::Success(new_state);
}

void HttpCodecStateMachine::forceTransition(
    HttpCodecState new_state,
    const std::string& reason) {
  
  assertInDispatcherThread();
  
  HttpCodecState old_state = current_state_;
  current_state_ = new_state;
  state_entry_time_ = std::chrono::steady_clock::now();
  
  // Record transition
  HttpCodecStateTransitionContext context;
  context.from_state = old_state;
  context.to_state = new_state;
  context.triggering_event = HttpCodecEvent::Reset;  // Use reset as default
  context.timestamp = state_entry_time_;
  context.reason = reason + " (forced)";
  
  recordStateTransition(context);
  notifyStateChange(context);
}

void HttpCodecStateMachine::scheduleTransition(
    HttpCodecState new_state,
    HttpCodecEvent event,
    const std::string& reason,
    CompletionCallback callback) {
  
  dispatcher_.post([this, new_state, event, reason, callback]() {
    transitionTo(new_state, event, reason, callback);
  });
}

void HttpCodecStateMachine::resetForNextRequest(CompletionCallback callback) {
  HttpCodecState current = current_state_.load(std::memory_order_acquire);
  if (current == HttpCodecState::SendingResponse ||
      current == HttpCodecState::WaitingForRequest) {
    handleEvent(HttpCodecEvent::Reset, callback);
  } else {
    handleEvent(HttpCodecEvent::Close, callback);
  }
}

void HttpCodecStateMachine::addStateChangeListener(StateChangeCallback callback) {
  assertInDispatcherThread();
  state_change_listeners_.push_back(callback);
}

void HttpCodecStateMachine::clearStateChangeListeners() {
  assertInDispatcherThread();
  state_change_listeners_.clear();
}

void HttpCodecStateMachine::setEntryAction(HttpCodecState state, StateAction action) {
  assertInDispatcherThread();
  entry_actions_[state] = action;
}

void HttpCodecStateMachine::setExitAction(HttpCodecState state, StateAction action) {
  assertInDispatcherThread();
  exit_actions_[state] = action;
}

void HttpCodecStateMachine::addTransitionValidator(ValidationCallback validator) {
  assertInDispatcherThread();
  custom_validators_.push_back(validator);
}

bool HttpCodecStateMachine::isTransitionValid(
    HttpCodecState from,
    HttpCodecState to,
    HttpCodecEvent event) const {
  
  // Check built-in valid transitions
  auto it = valid_transitions_.find(from);
  if (it != valid_transitions_.end()) {
    if (it->second.find(to) == it->second.end()) {
      return false;
    }
  }
  
  // Check custom validators
  for (const auto& validator : custom_validators_) {
    if (!validator(from, to)) {
      return false;
    }
  }
  
  return true;
}

std::chrono::milliseconds HttpCodecStateMachine::getTimeInCurrentState() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_entry_time_);
}

void HttpCodecStateMachine::setStateTimeout(
    std::chrono::milliseconds timeout,
    HttpCodecState timeout_state) {
  // Not implemented in simplified version
}

void HttpCodecStateMachine::cancelStateTimeout() {
  // Not implemented in simplified version
}

std::string HttpCodecStateMachine::getStateName(HttpCodecState state) {
  switch (state) {
    case HttpCodecState::WaitingForRequest: return "WaitingForRequest";
    case HttpCodecState::ReceivingHeaders: return "ReceivingHeaders";
    case HttpCodecState::ReceivingBody: return "ReceivingBody";
    case HttpCodecState::SendingResponse: return "SendingResponse";
    case HttpCodecState::Closed: return "Closed";
    case HttpCodecState::Error: return "Error";
    default: return "Unknown";
  }
}

std::string HttpCodecStateMachine::getEventName(HttpCodecEvent event) {
  switch (event) {
    case HttpCodecEvent::RequestStart: return "RequestStart";
    case HttpCodecEvent::HeadersComplete: return "HeadersComplete";
    case HttpCodecEvent::BodyData: return "BodyData";
    case HttpCodecEvent::MessageComplete: return "MessageComplete";
    case HttpCodecEvent::SendResponse: return "SendResponse";
    case HttpCodecEvent::ResponseComplete: return "ResponseComplete";
    case HttpCodecEvent::ParseError: return "ParseError";
    case HttpCodecEvent::Timeout: return "Timeout";
    case HttpCodecEvent::Reset: return "Reset";
    case HttpCodecEvent::Close: return "Close";
    default: return "Unknown";
  }
}

void HttpCodecStateMachine::onStateExit(HttpCodecState state, CompletionCallback callback) {
  // Check for exit action
  auto it = exit_actions_.find(state);
  if (it != exit_actions_.end()) {
    // Wrap the completion callback to match the expected signature
    it->second(state, [callback]() { 
      if (callback) callback(true); 
    });
  } else if (callback) {
    callback(true);
  }
  
  // Stop state-specific timers
  switch (state) {
    case HttpCodecState::ReceivingHeaders:
      if (header_timer_) {
        header_timer_->disableTimer();
      }
      break;
    case HttpCodecState::ReceivingBody:
      if (body_timer_) {
        body_timer_->disableTimer();
      }
      break;
    case HttpCodecState::WaitingForRequest:
      if (idle_timer_) {
        idle_timer_->disableTimer();
      }
      break;
    default:
      break;
  }
}

void HttpCodecStateMachine::onStateEnter(HttpCodecState state, CompletionCallback callback) {
  // Check for entry action
  auto it = entry_actions_.find(state);
  if (it != entry_actions_.end()) {
    // Wrap the completion callback to match the expected signature
    it->second(state, [callback]() { 
      if (callback) callback(true); 
    });
  } else if (callback) {
    callback(true);
  }
  
  // Start state-specific timers
  switch (state) {
    case HttpCodecState::ReceivingHeaders:
      if (header_timer_ && config_.header_timeout.count() > 0) {
        header_timer_->enableTimer(config_.header_timeout);
      }
      break;
    case HttpCodecState::ReceivingBody:
      if (body_timer_ && config_.body_timeout.count() > 0) {
        body_timer_->enableTimer(config_.body_timeout);
      }
      break;
    case HttpCodecState::WaitingForRequest:
      if (idle_timer_ && config_.idle_timeout.count() > 0) {
        idle_timer_->enableTimer(config_.idle_timeout);
      }
      // Reset tracking for new request
      expect_request_body_ = false;
      current_header_size_ = 0;
      current_body_size_ = 0;
      break;
    default:
      break;
  }
}

std::unordered_set<HttpCodecState> HttpCodecStateMachine::getValidTransitions(
    HttpCodecState from) const {
  auto it = valid_transitions_.find(from);
  if (it != valid_transitions_.end()) {
    return it->second;
  }
  return {};
}

void HttpCodecStateMachine::onStateTimeout(HttpCodecState state) {
  // Handle timeout based on current state
  handleEvent(HttpCodecEvent::Timeout);
}

void HttpCodecStateMachine::initializeTransitions() {
  // Define valid state transitions
  valid_transitions_[HttpCodecState::WaitingForRequest] = {
    HttpCodecState::ReceivingHeaders,
    HttpCodecState::Closed,
    HttpCodecState::Error
  };
  
  valid_transitions_[HttpCodecState::ReceivingHeaders] = {
    HttpCodecState::ReceivingBody,
    HttpCodecState::SendingResponse,
    HttpCodecState::Error,
    HttpCodecState::Closed
  };
  
  valid_transitions_[HttpCodecState::ReceivingBody] = {
    HttpCodecState::SendingResponse,
    HttpCodecState::Error,
    HttpCodecState::Closed
  };
  
  valid_transitions_[HttpCodecState::SendingResponse] = {
    HttpCodecState::WaitingForRequest,  // Keep-alive
    HttpCodecState::Closed,
    HttpCodecState::Error
  };
  
  valid_transitions_[HttpCodecState::Closed] = {
    HttpCodecState::WaitingForRequest  // Reset
  };
  
  valid_transitions_[HttpCodecState::Error] = {
    HttpCodecState::WaitingForRequest,  // Reset
    HttpCodecState::Closed
  };
}

void HttpCodecStateMachine::executeTransition(
    HttpCodecState new_state,
    HttpCodecEvent event,
    const std::string& reason,
    CompletionCallback callback) {
  
  HttpCodecState old_state = current_state_;
  
  // Calculate metrics
  auto time_in_state = getTimeInCurrentState();
  
  // Exit old state
  onStateExit(old_state, [this, old_state, new_state, event, reason, time_in_state, callback](bool exit_success) {
    // Update state
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    total_transitions_++;
    
    // Track request completion
    if (new_state == HttpCodecState::WaitingForRequest && 
        old_state == HttpCodecState::SendingResponse) {
      requests_processed_++;
    }
    
    // Create transition context
    HttpCodecStateTransitionContext context;
    context.from_state = old_state;
    context.to_state = new_state;
    context.triggering_event = event;
    context.timestamp = state_entry_time_;
    context.reason = reason;
    context.time_in_previous_state = time_in_state;
    context.bytes_processed_in_state = 0;  // Would be tracked in real implementation
    context.requests_in_state = (new_state == HttpCodecState::WaitingForRequest) ? 1 : 0;
    
    // Record and notify
    recordStateTransition(context);
    notifyStateChange(context);
    
    // Enter new state
    onStateEnter(new_state, callback);
  });
}

void HttpCodecStateMachine::executeEntryAction(
    HttpCodecState state,
    std::function<void()> done) {
  auto it = entry_actions_.find(state);
  if (it != entry_actions_.end()) {
    it->second(state, done);
  } else {
    done();
  }
}

void HttpCodecStateMachine::executeExitAction(
    HttpCodecState state,
    std::function<void()> done) {
  auto it = exit_actions_.find(state);
  if (it != exit_actions_.end()) {
    it->second(state, done);
  } else {
    done();
  }
}

void HttpCodecStateMachine::notifyStateChange(
    const HttpCodecStateTransitionContext& context) {
  for (const auto& listener : state_change_listeners_) {
    listener(context);
  }
  
  // Also call configured callback
  if (config_.state_change_callback) {
    config_.state_change_callback(context);
  }
}

void HttpCodecStateMachine::recordStateTransition(
    const HttpCodecStateTransitionContext& context) {
  state_history_.push_back(context);
  
  // Limit history size
  while (state_history_.size() > kMaxHistorySize) {
    state_history_.pop_front();
  }
}

void HttpCodecStateMachine::onHeaderTimeout() {
  if (current_state_ == HttpCodecState::ReceivingHeaders) {
    std::string error = "HTTP header timeout after " + 
                       std::to_string(config_.header_timeout.count()) + "ms";
    
    if (config_.error_callback) {
      config_.error_callback(error);
    }
    
    handleEvent(HttpCodecEvent::Timeout);
  }
}

void HttpCodecStateMachine::onBodyTimeout() {
  if (current_state_ == HttpCodecState::ReceivingBody) {
    std::string error = "HTTP body timeout after " +
                       std::to_string(config_.body_timeout.count()) + "ms";
    
    if (config_.error_callback) {
      config_.error_callback(error);
    }
    
    handleEvent(HttpCodecEvent::Timeout);
  }
}

void HttpCodecStateMachine::onIdleTimeout() {
  if (current_state_ == HttpCodecState::WaitingForRequest) {
    // Idle timeout - close connection
    handleEvent(HttpCodecEvent::Close);
  }
}

} // namespace filter
} // namespace mcp