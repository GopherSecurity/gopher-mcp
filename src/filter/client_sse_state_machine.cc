/**
 * @file client_sse_state_machine.cc
 * @brief Client-side SSE connection lifecycle state machine implementation
 *
 * Manages the SSE endpoint negotiation lifecycle for MCP clients,
 * replacing scattered boolean flags with validated state transitions,
 * observable history, and negotiation timeout handling.
 */

#include "mcp/filter/client_sse_state_machine.h"

#include <sstream>

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

ClientSseStateMachine::ClientSseStateMachine(
    event::Dispatcher& dispatcher,
    const ClientSseStateMachineConfig& config,
    bool use_sse)
    : dispatcher_(dispatcher), config_(config) {
  // The initial state depends on whether SSE mode is enabled.
  // In Streamable HTTP mode the negotiation lifecycle is skipped
  // entirely — the client sends POST requests directly.
  current_state_ =
      use_sse ? ClientSseState::Idle : ClientSseState::StreamableHttp;
  state_entry_time_ = std::chrono::steady_clock::now();

  initializeTransitions();

  // Pre-create the negotiation timeout timer so it is ready when
  // the machine enters WaitingForEndpoint. The timer is only armed
  // (enableTimer) inside onStateEnter, not here.
  if (config_.negotiation_timeout.count() > 0) {
    negotiation_timer_ =
        dispatcher_.createTimer([this]() { onNegotiationTimeout(); });
  }
}

ClientSseStateMachine::~ClientSseStateMachine() {
  if (negotiation_timer_) {
    negotiation_timer_->disableTimer();
  }
}

// ===== Core State Machine Interface =====

ClientSseTransitionResult ClientSseStateMachine::handleEvent(
    ClientSseEvent event, CompletionCallback callback) {
  assertInDispatcherThread();

  ClientSseState current = current_state_;
  ClientSseState new_state = current;
  std::string reason;

  // Map (current_state, event) -> target state.
  // Only explicit, expected transitions are listed. Anything not
  // listed leaves new_state == current, which means "no transition".
  switch (current) {
    case ClientSseState::StreamableHttp:
      // Streamable HTTP is a stable mode — only close/error leave it.
      if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "Streamable HTTP connection closed";
      } else if (event == ClientSseEvent::StreamError) {
        new_state = ClientSseState::Error;
        reason = "Streamable HTTP error";
      }
      break;

    case ClientSseState::Idle:
      if (event == ClientSseEvent::ConnectionReady) {
        new_state = ClientSseState::WaitingForGetSent;
        reason = "Connection ready, preparing SSE GET";
      } else if (event == ClientSseEvent::StreamError) {
        new_state = ClientSseState::Error;
        reason = "Error before SSE GET";
      } else if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "Closed before SSE negotiation";
      }
      break;

    case ClientSseState::WaitingForGetSent:
      if (event == ClientSseEvent::GetSent) {
        new_state = ClientSseState::WaitingForEndpoint;
        reason = "SSE GET request sent";
      } else if (event == ClientSseEvent::StreamError) {
        new_state = ClientSseState::Error;
        reason = "Error sending SSE GET";
      } else if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "Closed while sending GET";
      }
      break;

    case ClientSseState::WaitingForEndpoint:
      if (event == ClientSseEvent::EndpointReceived) {
        new_state = ClientSseState::EndpointReceived;
        reason = "Received SSE endpoint event from server";
      } else if (event == ClientSseEvent::StreamStarted) {
        // Content-Type: text/event-stream was detected in the HTTP
        // response headers. This can arrive before the endpoint event
        // because the headers precede the body in the HTTP response.
        // Transition to Active so SSE body data is routed through the
        // SSE codec (which will parse the endpoint event from the body).
        new_state = ClientSseState::Active;
        reason = "SSE stream confirmed via Content-Type (endpoint pending)";
      } else if (event == ClientSseEvent::NegotiationTimeout) {
        new_state = ClientSseState::Error;
        reason = "SSE endpoint negotiation timed out";
      } else if (event == ClientSseEvent::StreamError) {
        new_state = ClientSseState::Error;
        reason = "Error waiting for endpoint";
      } else if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "Closed while waiting for endpoint";
      }
      break;

    case ClientSseState::EndpointReceived:
      if (event == ClientSseEvent::StreamStarted) {
        new_state = ClientSseState::Active;
        reason = "SSE stream confirmed via Content-Type";
      } else if (event == ClientSseEvent::StreamError) {
        new_state = ClientSseState::Error;
        reason = "Error after endpoint received";
      } else if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "Closed after endpoint received";
      }
      break;

    case ClientSseState::Active:
      if (event == ClientSseEvent::EndpointReceived) {
        // Endpoint event arrived after Content-Type already confirmed
        // the SSE stream. Stay in Active — this is a no-op transition
        // since the endpoint URL is stored by the filter independently.
        // Return success without changing state.
      } else if (event == ClientSseEvent::StreamError) {
        new_state = ClientSseState::Error;
        reason = "SSE stream error";
      } else if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "SSE stream closed";
      }
      break;

    case ClientSseState::Error:
      // Terminal — only close is allowed.
      if (event == ClientSseEvent::Close) {
        new_state = ClientSseState::Closed;
        reason = "Closed after error";
      }
      break;

    case ClientSseState::Closed:
      // Terminal — no transitions out.
      break;
  }

  // If the event did not produce a new state, report failure.
  if (new_state == current) {
    GOPHER_LOG_DEBUG("ClientSseStateMachine: event {} ignored in state {}",
                     getEventName(event), getStateName(current));
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return ClientSseTransitionResult::Failure("Event " + getEventName(event) +
                                              " not valid in state " +
                                              getStateName(current));
  }

  return transitionTo(new_state, event, reason, callback);
}

ClientSseTransitionResult ClientSseStateMachine::transitionTo(
    ClientSseState new_state,
    ClientSseEvent event,
    const std::string& reason,
    CompletionCallback callback) {
  assertInDispatcherThread();

  // Validate against the transition matrix.
  if (!isTransitionValid(current_state_, new_state, event)) {
    std::string error = "Invalid transition from " +
                        getStateName(current_state_) + " to " +
                        getStateName(new_state);
    GOPHER_LOG_WARN("ClientSseStateMachine: {}", error);
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return ClientSseTransitionResult::Failure(error);
  }

  // If we are already inside a transition (re-entrant call from a
  // state-change callback), defer to the next event-loop iteration.
  if (transition_in_progress_) {
    scheduleTransition(new_state, event, reason, callback);
    return ClientSseTransitionResult::Success(new_state);
  }

  transition_in_progress_ = true;
  executeTransition(new_state, event, reason, [this, callback](bool success) {
    transition_in_progress_ = false;
    if (callback) {
      callback(success);
    }
  });

  return ClientSseTransitionResult::Success(new_state);
}

void ClientSseStateMachine::forceTransition(ClientSseState new_state,
                                            const std::string& reason) {
  assertInDispatcherThread();

  ClientSseState old_state = current_state_;
  auto time_in_state = getTimeInCurrentState();

  // Exit the old state (cancel timers, etc.)
  onStateExit(old_state, nullptr);

  current_state_ = new_state;
  state_entry_time_ = std::chrono::steady_clock::now();
  total_transitions_++;

  ClientSseTransitionContext context;
  context.from_state = old_state;
  context.to_state = new_state;
  context.triggering_event = ClientSseEvent::Close;  // default for forced
  context.timestamp = state_entry_time_;
  context.reason = "FORCED: " + reason;
  context.time_in_previous_state = time_in_state;

  recordStateTransition(context);
  notifyStateChange(context);

  onStateEnter(new_state, nullptr);
}

void ClientSseStateMachine::scheduleTransition(ClientSseState new_state,
                                               ClientSseEvent event,
                                               const std::string& reason,
                                               CompletionCallback callback) {
  dispatcher_.post([this, new_state, event, reason, callback]() {
    transitionTo(new_state, event, reason, callback);
  });
}

// ===== State Observers =====

void ClientSseStateMachine::addStateChangeListener(
    StateChangeCallback callback) {
  assertInDispatcherThread();
  state_change_listeners_.push_back(callback);
}

void ClientSseStateMachine::clearStateChangeListeners() {
  assertInDispatcherThread();
  state_change_listeners_.clear();
}

// ===== Validation =====

void ClientSseStateMachine::addTransitionValidator(
    ValidationCallback validator) {
  assertInDispatcherThread();
  custom_validators_.push_back(validator);
}

bool ClientSseStateMachine::isTransitionValid(ClientSseState from,
                                              ClientSseState to,
                                              ClientSseEvent /*event*/) const {
  // Check the built-in transition matrix.
  auto it = valid_transitions_.find(from);
  if (it != valid_transitions_.end()) {
    if (it->second.find(to) == it->second.end()) {
      return false;
    }
  } else {
    // No outgoing transitions defined for this state — reject.
    return false;
  }

  // Check custom validators.
  for (const auto& validator : custom_validators_) {
    if (!validator(from, to)) {
      return false;
    }
  }

  return true;
}

// ===== Metrics =====

std::chrono::milliseconds ClientSseStateMachine::getTimeInCurrentState() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now - state_entry_time_);
}

// ===== Utility =====

std::string ClientSseStateMachine::getStateName(ClientSseState state) {
  switch (state) {
    case ClientSseState::StreamableHttp:
      return "StreamableHttp";
    case ClientSseState::Idle:
      return "Idle";
    case ClientSseState::WaitingForGetSent:
      return "WaitingForGetSent";
    case ClientSseState::WaitingForEndpoint:
      return "WaitingForEndpoint";
    case ClientSseState::EndpointReceived:
      return "EndpointReceived";
    case ClientSseState::Active:
      return "Active";
    case ClientSseState::Error:
      return "Error";
    case ClientSseState::Closed:
      return "Closed";
  }
  return "Unknown";
}

std::string ClientSseStateMachine::getEventName(ClientSseEvent event) {
  switch (event) {
    case ClientSseEvent::ConnectionReady:
      return "ConnectionReady";
    case ClientSseEvent::GetSent:
      return "GetSent";
    case ClientSseEvent::EndpointReceived:
      return "EndpointReceived";
    case ClientSseEvent::StreamStarted:
      return "StreamStarted";
    case ClientSseEvent::NegotiationTimeout:
      return "NegotiationTimeout";
    case ClientSseEvent::StreamError:
      return "StreamError";
    case ClientSseEvent::Close:
      return "Close";
  }
  return "Unknown";
}

// ===== Protected Methods =====

void ClientSseStateMachine::onStateExit(ClientSseState state,
                                        CompletionCallback callback) {
  // Cancel the negotiation timer when leaving WaitingForEndpoint,
  // regardless of why we are leaving (endpoint arrived, error, close).
  if (state == ClientSseState::WaitingForEndpoint) {
    if (negotiation_timer_) {
      negotiation_timer_->disableTimer();
    }
  }

  if (callback) {
    callback(true);
  }
}

void ClientSseStateMachine::onStateEnter(ClientSseState state,
                                         CompletionCallback callback) {
  switch (state) {
    case ClientSseState::WaitingForEndpoint:
      // Arm the negotiation timeout: if the server does not send the
      // "endpoint" SSE event within the configured deadline, the
      // machine transitions to Error. A timeout of 0 means "disabled".
      if (negotiation_timer_ && config_.negotiation_timeout.count() > 0) {
        negotiation_timer_->enableTimer(config_.negotiation_timeout);
      }
      break;

    case ClientSseState::Error:
      // Cancel any outstanding timers on error.
      if (negotiation_timer_) {
        negotiation_timer_->disableTimer();
      }
      // Notify the error callback so the filter can propagate the
      // error to the application layer (McpProtocolCallbacks::onError).
      if (config_.error_callback) {
        // Use the reason from the most recent transition context.
        std::string reason = "Client SSE error";
        if (!state_history_.empty()) {
          reason = state_history_.back().reason;
        }
        config_.error_callback(reason);
      }
      break;

    case ClientSseState::Closed:
      // Final cleanup.
      if (negotiation_timer_) {
        negotiation_timer_->disableTimer();
      }
      break;

    default:
      break;
  }

  if (callback) {
    callback(true);
  }
}

// ===== Private Implementation =====

void ClientSseStateMachine::initializeTransitions() {
  // StreamableHttp is a stable mode — only terminal states are reachable.
  valid_transitions_[ClientSseState::StreamableHttp] = {ClientSseState::Closed,
                                                        ClientSseState::Error};

  // SSE negotiation lifecycle — strictly ordered forward progression.
  valid_transitions_[ClientSseState::Idle] = {ClientSseState::WaitingForGetSent,
                                              ClientSseState::Error,
                                              ClientSseState::Closed};

  valid_transitions_[ClientSseState::WaitingForGetSent] = {
      ClientSseState::WaitingForEndpoint, ClientSseState::Error,
      ClientSseState::Closed};

  // WaitingForEndpoint can reach Active directly when Content-Type:
  // text/event-stream arrives before the endpoint SSE event (the
  // normal case: HTTP headers precede the body).
  valid_transitions_[ClientSseState::WaitingForEndpoint] = {
      ClientSseState::EndpointReceived, ClientSseState::Active,
      ClientSseState::Error, ClientSseState::Closed};

  valid_transitions_[ClientSseState::EndpointReceived] = {
      ClientSseState::Active, ClientSseState::Error, ClientSseState::Closed};

  valid_transitions_[ClientSseState::Active] = {ClientSseState::Error,
                                                ClientSseState::Closed};

  // Error -> Closed is the only valid terminal transition.
  valid_transitions_[ClientSseState::Error] = {ClientSseState::Closed};

  // Closed is terminal — no outgoing transitions.
  // (Explicitly not adding an entry means getValidTransitions returns empty.)
}

void ClientSseStateMachine::executeTransition(ClientSseState new_state,
                                              ClientSseEvent event,
                                              const std::string& reason,
                                              CompletionCallback callback) {
  ClientSseState old_state = current_state_;
  auto time_in_state = getTimeInCurrentState();

  // Exit old state (cancel timers, etc.)
  onStateExit(old_state, [this, old_state, new_state, event, reason,
                          time_in_state, callback](bool /*exit_success*/) {
    // Update state
    current_state_ = new_state;
    state_entry_time_ = std::chrono::steady_clock::now();
    total_transitions_++;

    // Build transition context
    ClientSseTransitionContext context;
    context.from_state = old_state;
    context.to_state = new_state;
    context.triggering_event = event;
    context.timestamp = state_entry_time_;
    context.reason = reason;
    context.time_in_previous_state = time_in_state;

    // Record and notify
    recordStateTransition(context);
    notifyStateChange(context);

    // Enter new state (arm timers, fire error callback, etc.)
    onStateEnter(new_state, callback);
  });
}

void ClientSseStateMachine::notifyStateChange(
    const ClientSseTransitionContext& context) {
  for (const auto& listener : state_change_listeners_) {
    listener(context);
  }

  if (config_.state_change_callback) {
    config_.state_change_callback(context);
  }
}

void ClientSseStateMachine::recordStateTransition(
    const ClientSseTransitionContext& context) {
  state_history_.push_back(context);

  while (state_history_.size() > kMaxHistorySize) {
    state_history_.pop_front();
  }
}

void ClientSseStateMachine::onNegotiationTimeout() {
  // Only fire the timeout if we are still waiting for the endpoint.
  // If the state has moved on (e.g. endpoint arrived just before the
  // timer fired), ignore the stale timeout.
  if (current_state_ == ClientSseState::WaitingForEndpoint) {
    GOPHER_LOG_WARN("ClientSseStateMachine: negotiation timeout after {}ms",
                    config_.negotiation_timeout.count());
    handleEvent(ClientSseEvent::NegotiationTimeout);
  }
}

}  // namespace filter
}  // namespace mcp
