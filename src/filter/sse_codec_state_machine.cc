/**
 * @file sse_codec_state_machine.cc
 * @brief Server-Sent Events codec state machine implementation
 */

#include "mcp/filter/sse_codec_state_machine.h"
#include <sstream>

namespace mcp {
namespace filter {

SseCodecStateMachine::SseCodecStateMachine(
    event::Dispatcher& dispatcher,
    const SseCodecStateMachineConfig& config)
    : dispatcher_(dispatcher),
      config_(config) {
  
  // Initialize state
  current_state_ = SseCodecState::Idle;
  state_entry_time_ = std::chrono::steady_clock::now();
  
  // Initialize valid transitions
  initializeTransitions();
  
  // Create timers
  if (config_.enable_keep_alive && config_.keep_alive_interval.count() > 0) {
    keep_alive_timer_ = dispatcher.createTimer([this]() { onKeepAliveTimer(); });
  }
  
  if (config_.event_timeout.count() > 0) {
    event_timeout_timer_ = dispatcher.createTimer([this]() { onEventTimeout(); });
  }
}

SseCodecStateMachine::~SseCodecStateMachine() {
  // Cancel all timers
  if (keep_alive_timer_) {
    keep_alive_timer_->disableTimer();
  }
  if (event_timeout_timer_) {
    event_timeout_timer_->disableTimer();
  }
}

SseCodecStateTransitionResult SseCodecStateMachine::handleEvent(
    SseCodecEvent event,
    CompletionCallback callback) {
  
  assertInDispatcherThread();
  
  // Map event to appropriate state transition
  SseCodecState new_state = current_state_;
  std::string reason;
  
  switch (current_state_) {
    case SseCodecState::Idle:
      if (event == SseCodecEvent::StartStream) {
        new_state = SseCodecState::StreamActive;
        reason = "Stream started";
      }
      break;
      
    case SseCodecState::StreamActive:
      if (event == SseCodecEvent::SendEvent) {
        new_state = SseCodecState::SendingEvent;
        reason = "Sending event";
      } else if (event == SseCodecEvent::KeepAliveTimer) {
        // Send keep-alive but stay in same state
        reason = "Keep-alive";
        if (config_.keep_alive_callback) {
          config_.keep_alive_callback();
        }
      } else if (event == SseCodecEvent::CloseStream) {
        new_state = SseCodecState::Closed;
        reason = "Stream close requested";
      } else if (event == SseCodecEvent::StreamError) {
        new_state = SseCodecState::Error;
        reason = "Stream error";
      }
      break;
      
    case SseCodecState::SendingEvent:
      if (event == SseCodecEvent::EventSent) {
        new_state = SseCodecState::StreamActive;
        reason = "Event sent";
        events_sent_++;
      } else if (event == SseCodecEvent::StreamError) {
        new_state = SseCodecState::Error;
        reason = "Error sending event";
      }
      break;
      
    case SseCodecState::Closed:
      // Terminal state - only reset allowed
      if (event == SseCodecEvent::Reset) {
        new_state = SseCodecState::Idle;
        reason = "Reset after close";
      }
      break;
      
    case SseCodecState::Error:
      // Terminal state - only reset allowed
      if (event == SseCodecEvent::Reset) {
        new_state = SseCodecState::Idle;
        reason = "Reset after error";
      }
      break;
  }
  
  // Perform transition if state changed
  if (new_state != current_state_) {
    return transitionTo(new_state, event, reason, callback);
  }
  
  // No transition
  if (callback) {
    dispatcher_.post([callback]() { callback(true); });
  }
  return SseCodecStateTransitionResult::Success(current_state_);
}

SseCodecStateTransitionResult SseCodecStateMachine::transitionTo(
    SseCodecState new_state,
    SseCodecEvent event,
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
    return SseCodecStateTransitionResult::Failure(error);
  }
  
  // Prevent reentrancy
  if (transition_in_progress_) {
    scheduleTransition(new_state, event, reason, callback);
    return SseCodecStateTransitionResult::Success(new_state);
  }
  
  transition_in_progress_ = true;
  
  // Execute transition
  executeTransition(new_state, event, reason, [this, callback](bool success) {
    transition_in_progress_ = false;
    if (callback) {
      callback(success);
    }
  });
  
  return SseCodecStateTransitionResult::Success(new_state);
}

void SseCodecStateMachine::forceTransition(
    SseCodecState new_state,
    const std::string& reason) {
  
  assertInDispatcherThread();
  
  SseCodecState old_state = current_state_;
  current_state_ = new_state;
  state_entry_time_ = std::chrono::steady_clock::now();
  
  // Record transition
  SseCodecStateTransitionContext context;
  context.from_state = old_state;
  context.to_state = new_state;
  context.triggering_event = SseCodecEvent::Reset;  // Use reset as default
  context.timestamp = state_entry_time_;
  context.reason = reason + " (forced)";
  
  recordStateTransition(context);
  notifyStateChange(context);
}

void SseCodecStateMachine::scheduleTransition(
    SseCodecState new_state,
    SseCodecEvent event,
    const std::string& reason,
    CompletionCallback callback) {
  
  dispatcher_.post([this, new_state, event, reason, callback]() {
    transitionTo(new_state, event, reason, callback);
  });
}

void SseCodecStateMachine::startStream(CompletionCallback callback) {
  if (canStartStream()) {
    handleEvent(SseCodecEvent::StartStream, callback);
  } else if (callback) {
    dispatcher_.post([callback]() { callback(false); });
  }
}

void SseCodecStateMachine::closeStream(CompletionCallback callback) {
  if (isStreaming()) {
    handleEvent(SseCodecEvent::CloseStream, callback);
  } else if (callback) {
    dispatcher_.post([callback]() { callback(false); });
  }
}

void SseCodecStateMachine::sendKeepAlive(CompletionCallback callback) {
  if (current_state_ == SseCodecState::StreamActive) {
    handleEvent(SseCodecEvent::KeepAliveTimer, callback);
  } else if (callback) {
    dispatcher_.post([callback]() { callback(false); });
  }
}

void SseCodecStateMachine::addStateChangeListener(StateChangeCallback callback) {
  assertInDispatcherThread();
  state_change_listeners_.push_back(callback);
}

void SseCodecStateMachine::clearStateChangeListeners() {
  assertInDispatcherThread();
  state_change_listeners_.clear();
}

void SseCodecStateMachine::setEntryAction(SseCodecState state, StateAction action) {
  assertInDispatcherThread();
  entry_actions_[state] = action;
}

void SseCodecStateMachine::setExitAction(SseCodecState state, StateAction action) {
  assertInDispatcherThread();
  exit_actions_[state] = action;
}

void SseCodecStateMachine::addTransitionValidator(ValidationCallback validator) {
  assertInDispatcherThread();
  custom_validators_.push_back(validator);
}

bool SseCodecStateMachine::isTransitionValid(
    SseCodecState from,
    SseCodecState to,
    SseCodecEvent event) const {
  
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

std::chrono::milliseconds SseCodecStateMachine::getTimeInCurrentState() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_entry_time_);
}

void SseCodecStateMachine::startKeepAliveTimer() {
  if (config_.enable_keep_alive && keep_alive_timer_ && 
      current_state_ == SseCodecState::StreamActive) {
    keep_alive_timer_->enableTimer(config_.keep_alive_interval);
  }
}

void SseCodecStateMachine::stopKeepAliveTimer() {
  if (keep_alive_timer_) {
    keep_alive_timer_->disableTimer();
  }
}

void SseCodecStateMachine::setEventTimeout(std::chrono::milliseconds timeout) {
  // Not implemented in simplified version - would update config
}

std::string SseCodecStateMachine::getStateName(SseCodecState state) {
  switch (state) {
    case SseCodecState::Idle: return "Idle";
    case SseCodecState::StreamActive: return "StreamActive";
    case SseCodecState::SendingEvent: return "SendingEvent";
    case SseCodecState::Closed: return "Closed";
    case SseCodecState::Error: return "Error";
    default: return "Unknown";
  }
}

std::string SseCodecStateMachine::getEventName(SseCodecEvent event) {
  switch (event) {
    case SseCodecEvent::StartStream: return "StartStream";
    case SseCodecEvent::StreamReady: return "StreamReady";
    case SseCodecEvent::SendEvent: return "SendEvent";
    case SseCodecEvent::EventSent: return "EventSent";
    case SseCodecEvent::KeepAliveTimer: return "KeepAliveTimer";
    case SseCodecEvent::StreamError: return "StreamError";
    case SseCodecEvent::CloseStream: return "CloseStream";
    case SseCodecEvent::Reset: return "Reset";
    default: return "Unknown";
  }
}

std::string SseCodecStateMachine::formatEvent(const SseEventData& event_data) {
  std::stringstream ss;
  
  // Format SSE event according to spec
  if (event_data.id.has_value()) {
    ss << "id: " << event_data.id.value() << "\n";
  }
  
  if (event_data.event.has_value()) {
    ss << "event: " << event_data.event.value() << "\n";
  }
  
  if (event_data.retry.has_value()) {
    ss << "retry: " << event_data.retry.value() << "\n";
  }
  
  // Split data by newlines and format each line
  std::istringstream data_stream(event_data.data);
  std::string line;
  while (std::getline(data_stream, line)) {
    ss << "data: " << line << "\n";
  }
  
  // End with blank line
  ss << "\n";
  
  return ss.str();
}

void SseCodecStateMachine::onStateExit(SseCodecState state, CompletionCallback callback) {
  // Check for exit action
  auto it = exit_actions_.find(state);
  if (it != exit_actions_.end()) {
    it->second(state, callback);
  } else if (callback) {
    callback();
  }
  
  // Stop state-specific timers
  switch (state) {
    case SseCodecState::StreamActive:
      if (keep_alive_timer_) {
        keep_alive_timer_->disableTimer();
      }
      break;
    case SseCodecState::SendingEvent:
      if (event_timeout_timer_) {
        event_timeout_timer_->disableTimer();
      }
      break;
    default:
      break;
  }
}

void SseCodecStateMachine::onStateEnter(SseCodecState state, CompletionCallback callback) {
  // Check for entry action
  auto it = entry_actions_.find(state);
  if (it != entry_actions_.end()) {
    it->second(state, callback);
  } else if (callback) {
    callback();
  }
  
  // Start state-specific timers
  switch (state) {
    case SseCodecState::StreamActive:
      if (keep_alive_timer_ && config_.enable_keep_alive) {
        keep_alive_timer_->enableTimer(config_.keep_alive_interval);
      }
      break;
    case SseCodecState::SendingEvent:
      if (event_timeout_timer_ && config_.event_timeout.count() > 0) {
        event_timeout_timer_->enableTimer(config_.event_timeout);
      }
      break;
    default:
      break;
  }
}

std::unordered_set<SseCodecState> SseCodecStateMachine::getValidTransitions(
    SseCodecState from) const {
  auto it = valid_transitions_.find(from);
  if (it != valid_transitions_.end()) {
    return it->second;
  }
  return {};
}

void SseCodecStateMachine::onStateTimeout(SseCodecState state) {
  // Handle timeout based on current state
  if (state == SseCodecState::SendingEvent) {
    handleEvent(SseCodecEvent::StreamError);
  }
}

void SseCodecStateMachine::initializeTransitions() {
  // Define valid state transitions for SSE
  valid_transitions_[SseCodecState::Idle] = {
    SseCodecState::StreamActive,
    SseCodecState::Error
  };
  
  valid_transitions_[SseCodecState::StreamActive] = {
    SseCodecState::SendingEvent,
    SseCodecState::Closed,
    SseCodecState::Error
  };
  
  valid_transitions_[SseCodecState::SendingEvent] = {
    SseCodecState::StreamActive,
    SseCodecState::Error,
    SseCodecState::Closed
  };
  
  valid_transitions_[SseCodecState::Closed] = {
    SseCodecState::Idle  // Reset
  };
  
  valid_transitions_[SseCodecState::Error] = {
    SseCodecState::Idle,  // Reset
    SseCodecState::Closed
  };
}

void SseCodecStateMachine::executeTransition(
    SseCodecState new_state,
    SseCodecEvent event,
    const std::string& reason,
    CompletionCallback callback) {
  
  SseCodecState old_state = current_state_;
  
  // Calculate metrics
  auto time_in_state = getTimeInCurrentState();
  
  // Exit old state
  onStateExit(old_state, [this, old_state, new_state, event, reason, time_in_state, callback]() {
    // Update state
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    total_transitions_++;
    
    // Create transition context
    SseCodecStateTransitionContext context;
    context.from_state = old_state;
    context.to_state = new_state;
    context.triggering_event = event;
    context.timestamp = state_entry_time_;
    context.reason = reason;
    context.time_in_previous_state = time_in_state;
    context.events_sent_in_state = 0;  // Would be tracked in real implementation
    context.bytes_sent_in_state = 0;   // Would be tracked in real implementation
    
    // Record and notify
    recordStateTransition(context);
    notifyStateChange(context);
    
    // Enter new state
    onStateEnter(new_state, callback);
  });
}

void SseCodecStateMachine::executeEntryAction(
    SseCodecState state,
    std::function<void()> done) {
  auto it = entry_actions_.find(state);
  if (it != entry_actions_.end()) {
    it->second(state, done);
  } else {
    done();
  }
}

void SseCodecStateMachine::executeExitAction(
    SseCodecState state,
    std::function<void()> done) {
  auto it = exit_actions_.find(state);
  if (it != exit_actions_.end()) {
    it->second(state, done);
  } else {
    done();
  }
}

void SseCodecStateMachine::notifyStateChange(
    const SseCodecStateTransitionContext& context) {
  for (const auto& listener : state_change_listeners_) {
    listener(context);
  }
  
  // Also call configured callback
  if (config_.state_change_callback) {
    config_.state_change_callback(context);
  }
}

void SseCodecStateMachine::recordStateTransition(
    const SseCodecStateTransitionContext& context) {
  state_history_.push_back(context);
  
  // Limit history size
  while (state_history_.size() > kMaxHistorySize) {
    state_history_.pop_front();
  }
}

void SseCodecStateMachine::onKeepAliveTimer() {
  if (current_state_ == SseCodecState::StreamActive) {
    // Send keep-alive comment
    if (config_.keep_alive_callback) {
      config_.keep_alive_callback();
    }
    
    // Restart timer
    if (keep_alive_timer_) {
      keep_alive_timer_->enableTimer(config_.keep_alive_interval);
    }
  }
}

void SseCodecStateMachine::onEventTimeout() {
  if (current_state_ == SseCodecState::SendingEvent) {
    std::string error = "SSE event send timeout after " +
                       std::to_string(config_.event_timeout.count()) + "ms";
    
    if (config_.error_callback) {
      config_.error_callback(error);
    }
    
    handleEvent(SseCodecEvent::StreamError);
  }
}

} // namespace filter
} // namespace mcp