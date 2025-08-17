/**
 * HTTP Codec State Machine Implementation
 */

#include "mcp/filter/http_codec_state_machine.h"

namespace mcp {
namespace filter {

HttpCodecStateMachine::HttpCodecStateMachine(
    event::Dispatcher& dispatcher,
    const HttpCodecStateMachineConfig& config)
    : StateMachine(dispatcher),
      config_(config) {
  
  // Initialize state
  state_ = HttpCodecState::WaitingForRequest;
  
  // Define state transitions
  defineTransitions();
  
  // Create timers
  if (config_.header_timeout.count() > 0) {
    header_timer_ = dispatcher.createTimer([this]() { onHeaderTimeout(); });
  }
  
  if (config_.body_timeout.count() > 0) {
    body_timer_ = dispatcher.createTimer([this]() { onBodyTimeout(); });
  }
  
  if (config_.idle_timeout.count() > 0) {
    idle_timer_ = dispatcher.createTimer([this]() { onIdleTimeout(); });
    idle_timer_->enableTimer(config_.idle_timeout);
  }
}

void HttpCodecStateMachine::defineTransitions() {
  // Request flow transitions
  addTransition(HttpCodecState::WaitingForRequest, 
                HttpCodecEvent::StartHeaders,
                HttpCodecState::ReceivingHeaders);
  
  addTransition(HttpCodecState::ReceivingHeaders,
                HttpCodecEvent::HeadersComplete,
                HttpCodecState::ReceivingBody);
  
  addTransition(HttpCodecState::ReceivingHeaders,
                HttpCodecEvent::MessageComplete,  // No body
                HttpCodecState::RequestComplete);
  
  addTransition(HttpCodecState::ReceivingBody,
                HttpCodecEvent::BodyData,
                HttpCodecState::ReceivingBody);  // Stay in same state
  
  addTransition(HttpCodecState::ReceivingBody,
                HttpCodecEvent::MessageComplete,
                HttpCodecState::RequestComplete);
  
  // Response flow transitions
  addTransition(HttpCodecState::RequestComplete,
                HttpCodecEvent::SendResponse,
                HttpCodecState::SendingResponseHeaders);
  
  addTransition(HttpCodecState::SendingResponseHeaders,
                HttpCodecEvent::ResponseHeadersSent,
                HttpCodecState::SendingResponseBody);
  
  addTransition(HttpCodecState::SendingResponseHeaders,
                HttpCodecEvent::ResponseComplete,  // No body
                HttpCodecState::ResponseComplete);
  
  addTransition(HttpCodecState::SendingResponseBody,
                HttpCodecEvent::ResponseBodySent,
                HttpCodecState::SendingResponseBody);  // Stay for chunked
  
  addTransition(HttpCodecState::SendingResponseBody,
                HttpCodecEvent::ResponseComplete,
                HttpCodecState::ResponseComplete);
  
  // Keep-alive transition
  addTransition(HttpCodecState::ResponseComplete,
                HttpCodecEvent::Reset,
                HttpCodecState::WaitingForRequest);
  
  // Error transitions - from any state
  for (auto state : {HttpCodecState::WaitingForRequest,
                     HttpCodecState::ReceivingHeaders,
                     HttpCodecState::ReceivingBody,
                     HttpCodecState::RequestComplete,
                     HttpCodecState::SendingResponseHeaders,
                     HttpCodecState::SendingResponseBody,
                     HttpCodecState::ResponseComplete}) {
    addTransition(state, HttpCodecEvent::ParseError, HttpCodecState::ProtocolError);
    addTransition(state, HttpCodecEvent::ConnectionError, HttpCodecState::ProtocolError);
    addTransition(state, HttpCodecEvent::Close, HttpCodecState::Closed);
  }
  
  // Reset from error
  addTransition(HttpCodecState::ProtocolError,
                HttpCodecEvent::Reset,
                HttpCodecState::WaitingForRequest);
}

void HttpCodecStateMachine::onStateChange(HttpCodecState old_state, 
                                          HttpCodecState new_state) {
  // Stop timers when leaving timed states
  if (old_state == HttpCodecState::ReceivingHeaders && header_timer_) {
    header_timer_->disableTimer();
  }
  
  if (old_state == HttpCodecState::ReceivingBody && body_timer_) {
    body_timer_->disableTimer();
  }
  
  // Start timers when entering timed states
  if (new_state == HttpCodecState::ReceivingHeaders && header_timer_) {
    header_timer_->enableTimer(config_.header_timeout);
  }
  
  if (new_state == HttpCodecState::ReceivingBody && body_timer_) {
    body_timer_->enableTimer(config_.body_timeout);
  }
  
  // Reset idle timer on activity
  if (new_state != HttpCodecState::WaitingForRequest && idle_timer_) {
    idle_timer_->disableTimer();
  } else if (new_state == HttpCodecState::WaitingForRequest && idle_timer_) {
    idle_timer_->enableTimer(config_.idle_timeout);
  }
  
  // Clear size tracking on new request
  if (new_state == HttpCodecState::WaitingForRequest) {
    current_header_size_ = 0;
    current_body_size_ = 0;
  }
  
  // Notify callback
  if (state_change_callback_) {
    state_change_callback_(old_state, new_state);
  }
}

void HttpCodecStateMachine::onInvalidTransition(HttpCodecState state, 
                                                HttpCodecEvent event) {
  std::string error = "Invalid HTTP state transition: state=" +
                     std::to_string(static_cast<int>(state)) +
                     " event=" + std::to_string(static_cast<int>(event));
  
  if (error_callback_) {
    error_callback_(error);
  }
  
  // Move to error state
  state_ = HttpCodecState::ProtocolError;
}

void HttpCodecStateMachine::resetForNextRequest() {
  if (state_ == HttpCodecState::ResponseComplete && keep_alive_) {
    processEvent(HttpCodecEvent::Reset);
  } else {
    processEvent(HttpCodecEvent::Close);
  }
}

void HttpCodecStateMachine::onHeaderTimeout() {
  std::string error = "HTTP header timeout after " + 
                     std::to_string(config_.header_timeout.count()) + "ms";
  
  if (error_callback_) {
    error_callback_(error);
  }
  
  processEvent(HttpCodecEvent::ConnectionError);
}

void HttpCodecStateMachine::onBodyTimeout() {
  std::string error = "HTTP body timeout after " +
                     std::to_string(config_.body_timeout.count()) + "ms";
  
  if (error_callback_) {
    error_callback_(error);
  }
  
  processEvent(HttpCodecEvent::ConnectionError);
}

void HttpCodecStateMachine::onIdleTimeout() {
  if (state_ == HttpCodecState::WaitingForRequest) {
    // Idle timeout - close connection
    processEvent(HttpCodecEvent::Close);
  }
}

} // namespace filter
} // namespace mcp