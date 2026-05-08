/**
 * @file server_connection_mode.cc
 * @brief Server-side connection mode state machine implementation
 *
 * Each accepted connection is classified into exactly one mode
 * (PlainHttp, SseStream, CallbackProxy) during HTTP header parsing.
 * The mode is immutable for the connection's lifetime.
 */

#include "mcp/filter/server_connection_mode.h"

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

ServerConnectionMode::ServerConnectionMode(event::Dispatcher& dispatcher,
                                           const ServerConnModeConfig& config)
    : dispatcher_(dispatcher), config_(config) {
  mode_entry_time_ = std::chrono::steady_clock::now();
  initializeTransitions();
}

ServerConnectionMode::~ServerConnectionMode() = default;

// ===== Core Interface =====

ServerConnTransitionResult ServerConnectionMode::handleEvent(
    ServerConnEvent event, CompletionCallback callback) {
  assertInDispatcherThread();

  ServerConnMode current = current_mode_;
  ServerConnMode new_mode = current;
  std::string reason;

  switch (current) {
    case ServerConnMode::Undetermined:
      // Mode determination — exactly one of these fires per connection.
      if (event == ServerConnEvent::PlainHttpDetected) {
        new_mode = ServerConnMode::PlainHttp;
        reason = "HTTP request on non-SSE path";
      } else if (event == ServerConnEvent::SseGetDetected) {
        new_mode = ServerConnMode::SseStream;
        reason = "GET on SSE endpoint";
      } else if (event == ServerConnEvent::CallbackPostDetected) {
        new_mode = ServerConnMode::CallbackProxy;
        reason = "POST on SSE callback endpoint";
      } else if (event == ServerConnEvent::StreamError) {
        new_mode = ServerConnMode::Error;
        reason = "Error before mode determination";
      } else if (event == ServerConnEvent::Close) {
        new_mode = ServerConnMode::Closed;
        reason = "Closed before mode determination";
      }
      break;

    case ServerConnMode::PlainHttp:
      if (event == ServerConnEvent::StreamError) {
        new_mode = ServerConnMode::Error;
        reason = "Error in PlainHttp mode";
      } else if (event == ServerConnEvent::Close) {
        new_mode = ServerConnMode::Closed;
        reason = "PlainHttp connection closed";
      }
      break;

    case ServerConnMode::SseStream:
      if (event == ServerConnEvent::SseHeadersWritten) {
        // Not a mode transition — just a sub-state flag. Mark headers
        // as written and return success without changing mode.
        sse_headers_written_ = true;
        if (callback) {
          callback(true);
        }
        return ServerConnTransitionResult::Success(current);
      } else if (event == ServerConnEvent::StreamError) {
        new_mode = ServerConnMode::Error;
        reason = "Error in SSE stream";
      } else if (event == ServerConnEvent::Close) {
        new_mode = ServerConnMode::Closed;
        reason = "SSE stream closed";
      }
      break;

    case ServerConnMode::CallbackProxy:
      if (event == ServerConnEvent::StreamError) {
        new_mode = ServerConnMode::Error;
        reason = "Error in callback proxy";
      } else if (event == ServerConnEvent::Close) {
        new_mode = ServerConnMode::Closed;
        reason = "Callback proxy connection closed";
      }
      break;

    case ServerConnMode::Error:
      if (event == ServerConnEvent::Close) {
        new_mode = ServerConnMode::Closed;
        reason = "Closed after error";
      }
      break;

    case ServerConnMode::Closed:
      // Terminal — no transitions out.
      break;
  }

  // If no transition was identified, report failure.
  if (new_mode == current) {
    GOPHER_LOG_DEBUG(
        "ServerConnectionMode: event {} ignored in mode {}",
        getEventName(event), getModeName(current));
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return ServerConnTransitionResult::Failure(
        "Event " + getEventName(event) + " not valid in mode " +
        getModeName(current));
  }

  // Validate against the transition matrix.
  if (!isTransitionValid(current, new_mode)) {
    std::string error = "Invalid transition from " +
                        getModeName(current) + " to " +
                        getModeName(new_mode);
    GOPHER_LOG_WARN("ServerConnectionMode: {}", error);
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return ServerConnTransitionResult::Failure(error);
  }

  if (transition_in_progress_) {
    // Defer — should not normally happen for server mode (mode is
    // determined once), but guard against reentrancy from callbacks.
    if (callback) {
      dispatcher_.post([callback]() { callback(false); });
    }
    return ServerConnTransitionResult::Failure("Transition in progress");
  }

  transition_in_progress_ = true;
  executeTransition(new_mode, event, reason,
                    [this, callback](bool success) {
                      transition_in_progress_ = false;
                      if (callback) {
                        callback(success);
                      }
                    });

  return ServerConnTransitionResult::Success(new_mode);
}

// ===== State Observers =====

void ServerConnectionMode::addStateChangeListener(
    StateChangeCallback callback) {
  assertInDispatcherThread();
  state_change_listeners_.push_back(callback);
}

void ServerConnectionMode::clearStateChangeListeners() {
  assertInDispatcherThread();
  state_change_listeners_.clear();
}

// ===== Metrics =====

std::chrono::milliseconds ServerConnectionMode::getTimeInCurrentMode() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now - mode_entry_time_);
}

// ===== Utility =====

std::string ServerConnectionMode::getModeName(ServerConnMode mode) {
  switch (mode) {
    case ServerConnMode::Undetermined:
      return "Undetermined";
    case ServerConnMode::PlainHttp:
      return "PlainHttp";
    case ServerConnMode::SseStream:
      return "SseStream";
    case ServerConnMode::CallbackProxy:
      return "CallbackProxy";
    case ServerConnMode::Error:
      return "Error";
    case ServerConnMode::Closed:
      return "Closed";
  }
  return "Unknown";
}

std::string ServerConnectionMode::getEventName(ServerConnEvent event) {
  switch (event) {
    case ServerConnEvent::PlainHttpDetected:
      return "PlainHttpDetected";
    case ServerConnEvent::SseGetDetected:
      return "SseGetDetected";
    case ServerConnEvent::CallbackPostDetected:
      return "CallbackPostDetected";
    case ServerConnEvent::SseHeadersWritten:
      return "SseHeadersWritten";
    case ServerConnEvent::StreamError:
      return "StreamError";
    case ServerConnEvent::Close:
      return "Close";
  }
  return "Unknown";
}

// ===== Handshake Write Guard =====

void ServerConnectionMode::beginHandshakeWrite() {
  assertInDispatcherThread();
  handshake_write_depth_++;
}

void ServerConnectionMode::endHandshakeWrite() {
  assertInDispatcherThread();
  if (handshake_write_depth_ > 0) {
    handshake_write_depth_--;
  }
}

// ===== Private Implementation =====

void ServerConnectionMode::initializeTransitions() {
  // Mode determination: Undetermined -> exactly one mode.
  valid_transitions_[ServerConnMode::Undetermined] = {
      ServerConnMode::PlainHttp, ServerConnMode::SseStream,
      ServerConnMode::CallbackProxy, ServerConnMode::Error,
      ServerConnMode::Closed};

  // Each determined mode can only go to terminal states.
  valid_transitions_[ServerConnMode::PlainHttp] = {
      ServerConnMode::Closed, ServerConnMode::Error};

  valid_transitions_[ServerConnMode::SseStream] = {
      ServerConnMode::Closed, ServerConnMode::Error};

  valid_transitions_[ServerConnMode::CallbackProxy] = {
      ServerConnMode::Closed, ServerConnMode::Error};

  // Error -> Closed is the only escape.
  valid_transitions_[ServerConnMode::Error] = {ServerConnMode::Closed};

  // Closed is terminal.
}

bool ServerConnectionMode::isTransitionValid(ServerConnMode from,
                                             ServerConnMode to) const {
  auto it = valid_transitions_.find(from);
  if (it == valid_transitions_.end()) {
    return false;
  }
  return it->second.find(to) != it->second.end();
}

void ServerConnectionMode::executeTransition(ServerConnMode new_mode,
                                             ServerConnEvent event,
                                             const std::string& reason,
                                             CompletionCallback callback) {
  ServerConnMode old_mode = current_mode_;
  auto time_in_mode = getTimeInCurrentMode();

  current_mode_ = new_mode;
  mode_entry_time_ = std::chrono::steady_clock::now();
  total_transitions_++;

  ServerConnTransitionContext context;
  context.from_mode = old_mode;
  context.to_mode = new_mode;
  context.triggering_event = event;
  context.timestamp = mode_entry_time_;
  context.reason = reason;
  context.time_in_previous_mode = time_in_mode;

  recordTransition(context);
  notifyStateChange(context);

  // Fire error callback on Error entry.
  if (new_mode == ServerConnMode::Error && config_.error_callback) {
    config_.error_callback(reason);
  }

  if (callback) {
    callback(true);
  }
}

void ServerConnectionMode::notifyStateChange(
    const ServerConnTransitionContext& context) {
  for (const auto& listener : state_change_listeners_) {
    listener(context);
  }
  if (config_.state_change_callback) {
    config_.state_change_callback(context);
  }
}

void ServerConnectionMode::recordTransition(
    const ServerConnTransitionContext& context) {
  state_history_.push_back(context);
  while (state_history_.size() > kMaxHistorySize) {
    state_history_.pop_front();
  }
}

}  // namespace filter
}  // namespace mcp
