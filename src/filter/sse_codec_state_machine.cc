/**
 * SSE Codec State Machine Implementation
 */

#include "mcp/filter/sse_codec_state_machine.h"

namespace mcp {
namespace filter {

SseCodecStateMachine::SseCodecStateMachine(
    event::Dispatcher& dispatcher,
    const SseCodecStateMachineConfig& config)
    : StateMachine(dispatcher),
      config_(config) {
  
  // Initialize state
  state_ = SseCodecState::Idle;
  
  // Define state transitions
  defineTransitions();
  
  // Create timers
  if (config_.enable_keep_alive && config_.keep_alive_interval.count() > 0) {
    keep_alive_timer_ = dispatcher.createTimer([this]() { onKeepAliveTimer(); });
  }
  
  if (config_.event_timeout.count() > 0) {
    event_timeout_timer_ = dispatcher.createTimer([this]() { onEventTimeout(); });
  }
}

void SseCodecStateMachine::defineTransitions() {
  // Stream initialization
  addTransition(SseCodecState::Idle,
                SseCodecEvent::StartStream,
                SseCodecState::WaitingForHeaders);
  
  addTransition(SseCodecState::WaitingForHeaders,
                SseCodecEvent::HeadersSent,
                SseCodecState::StreamEstablished);
  
  addTransition(SseCodecState::StreamEstablished,
                SseCodecEvent::StreamReady,
                SseCodecState::WaitingForEvent);
  
  // Event sending flow
  addTransition(SseCodecState::WaitingForEvent,
                SseCodecEvent::SendEvent,
                SseCodecState::SendingEvent);
  
  addTransition(SseCodecState::StreamEstablished,
                SseCodecEvent::SendEvent,
                SseCodecState::SendingEvent);
  
  addTransition(SseCodecState::SendingEvent,
                SseCodecEvent::EventSent,
                SseCodecState::WaitingForEvent);
  
  // Comment sending (can happen from multiple states)
  for (auto state : {SseCodecState::StreamEstablished,
                     SseCodecState::WaitingForEvent}) {
    addTransition(state, SseCodecEvent::SendComment, state);  // Stay in same state
    addTransition(state, SseCodecEvent::CommentSent, state);
  }
  
  // Retry sending
  addTransition(SseCodecState::StreamEstablished,
                SseCodecEvent::SendRetry,
                SseCodecState::StreamEstablished);
  
  addTransition(SseCodecState::WaitingForEvent,
                SseCodecEvent::SendRetry,
                SseCodecState::WaitingForEvent);
  
  // Keep-alive flow
  addTransition(SseCodecState::WaitingForEvent,
                SseCodecEvent::KeepAliveTimer,
                SseCodecState::SendingKeepAlive);
  
  addTransition(SseCodecState::SendingKeepAlive,
                SseCodecEvent::KeepAliveSent,
                SseCodecState::WaitingForEvent);
  
  // Error transitions
  for (auto state : {SseCodecState::WaitingForHeaders,
                     SseCodecState::StreamEstablished,
                     SseCodecState::SendingEvent,
                     SseCodecState::WaitingForEvent,
                     SseCodecState::SendingKeepAlive}) {
    addTransition(state, SseCodecEvent::StreamError, SseCodecState::StreamError);
    addTransition(state, SseCodecEvent::ClientDisconnect, SseCodecState::StreamClosing);
    addTransition(state, SseCodecEvent::ServerClose, SseCodecState::StreamClosing);
  }
  
  // Closing flow
  addTransition(SseCodecState::StreamClosing,
                SseCodecEvent::ServerClose,
                SseCodecState::StreamClosed);
  
  // Reset transitions
  addTransition(SseCodecState::StreamError,
                SseCodecEvent::Reset,
                SseCodecState::Idle);
  
  addTransition(SseCodecState::StreamClosed,
                SseCodecEvent::Reset,
                SseCodecState::Idle);
}

void SseCodecStateMachine::onStateChange(SseCodecState old_state,
                                         SseCodecState new_state) {
  // Start keep-alive timer when stream is established
  if (new_state == SseCodecState::WaitingForEvent && 
      config_.enable_keep_alive && 
      keep_alive_timer_) {
    keep_alive_timer_->enableTimer(config_.keep_alive_interval);
  }
  
  // Stop keep-alive timer when not waiting
  if (old_state == SseCodecState::WaitingForEvent && keep_alive_timer_) {
    keep_alive_timer_->disableTimer();
  }
  
  // Start event timeout when sending
  if (new_state == SseCodecState::SendingEvent && event_timeout_timer_) {
    event_timeout_timer_->enableTimer(config_.event_timeout);
  }
  
  // Stop event timeout when done sending
  if (old_state == SseCodecState::SendingEvent && event_timeout_timer_) {
    event_timeout_timer_->disableTimer();
  }
  
  // Stop all timers on close/error
  if (new_state == SseCodecState::StreamClosing ||
      new_state == SseCodecState::StreamClosed ||
      new_state == SseCodecState::StreamError) {
    if (keep_alive_timer_) {
      keep_alive_timer_->disableTimer();
    }
    if (event_timeout_timer_) {
      event_timeout_timer_->disableTimer();
    }
  }
  
  // Notify callback
  if (state_change_callback_) {
    state_change_callback_(old_state, new_state);
  }
}

void SseCodecStateMachine::onInvalidTransition(SseCodecState state,
                                               SseCodecEvent event) {
  std::string error = "Invalid SSE state transition: state=" +
                     std::to_string(static_cast<int>(state)) +
                     " event=" + std::to_string(static_cast<int>(event));
  
  if (error_callback_) {
    error_callback_(error);
  }
  
  // Move to error state
  state_ = SseCodecState::StreamError;
}

void SseCodecStateMachine::startStream() {
  if (canStartStream()) {
    processEvent(SseCodecEvent::StartStream);
  }
}

void SseCodecStateMachine::closeStream() {
  if (isStreaming()) {
    processEvent(SseCodecEvent::ServerClose);
  }
}

void SseCodecStateMachine::resetStream() {
  processEvent(SseCodecEvent::Reset);
}

void SseCodecStateMachine::startKeepAliveTimer() {
  if (config_.enable_keep_alive && keep_alive_timer_ && 
      state_ == SseCodecState::WaitingForEvent) {
    keep_alive_timer_->enableTimer(config_.keep_alive_interval);
  }
}

void SseCodecStateMachine::stopKeepAliveTimer() {
  if (keep_alive_timer_) {
    keep_alive_timer_->disableTimer();
  }
}

void SseCodecStateMachine::sendKeepAlive() {
  if (state_ == SseCodecState::WaitingForEvent) {
    processEvent(SseCodecEvent::KeepAliveTimer);
  }
}

void SseCodecStateMachine::onKeepAliveTimer() {
  if (state_ == SseCodecState::WaitingForEvent) {
    // Trigger keep-alive send
    processEvent(SseCodecEvent::KeepAliveTimer);
    
    // Notify callback to actually send the keep-alive
    if (keep_alive_callback_) {
      keep_alive_callback_();
    }
  }
}

void SseCodecStateMachine::onEventTimeout() {
  std::string error = "SSE event send timeout after " +
                     std::to_string(config_.event_timeout.count()) + "ms";
  
  if (error_callback_) {
    error_callback_(error);
  }
  
  processEvent(SseCodecEvent::StreamError);
}

} // namespace filter
} // namespace mcp