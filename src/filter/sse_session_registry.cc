#include "mcp/filter/sse_session_registry.h"

#include <cassert>

#include "mcp/buffer.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

SseSessionRegistry::SseSessionRegistry(event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

std::string SseSessionRegistry::registerSession(
    network::Connection* connection) {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::registerSession off-dispatcher-thread");
  std::string session_id = "client_" + std::to_string(next_id_++);
  sessions_[session_id] = connection;
  GOPHER_LOG_INFO("SSE session registered: {} (total={})", session_id,
                  sessions_.size());
  return session_id;
}

void SseSessionRegistry::removeSession(const std::string& session_id) {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::removeSession off-dispatcher-thread");
  if (sessions_.erase(session_id) > 0) {
    GOPHER_LOG_INFO("SSE session removed: {} (total={})", session_id,
                    sessions_.size());
    // Notify after the entry is gone so the observer cannot route a
    // message into the half-closed stream by calling back into us.
    if (session_closed_callback_) {
      session_closed_callback_(session_id);
    }
  }
}

void SseSessionRegistry::removeConnection(network::Connection* connection) {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::removeConnection off-dispatcher-thread");
  if (!connection) {
    return;
  }
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second == connection) {
      const std::string session_id = it->first;
      it = sessions_.erase(it);
      GOPHER_LOG_INFO("SSE session removed on connection close: {} (total={})",
                      session_id, sessions_.size());
      // Erase before notifying so the observer cannot route a message
      // into the closing stream by calling back into us.
      if (session_closed_callback_) {
        session_closed_callback_(session_id);
      }
    } else {
      ++it;
    }
  }
}

bool SseSessionRegistry::sendResponse(
    const std::string& session_id,
    const std::string& json_data,
    const network::Connection* writing_connection) {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::sendResponse off-dispatcher-thread");
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    GOPHER_LOG_WARN("SSE session not found for response routing: {}",
                    session_id);
    return false;
  }
  if (writing_connection != nullptr && it->second == writing_connection) {
    // Re-entering write() on the connection we are already writing would
    // clobber the buffer that write is holding.
    GOPHER_LOG_WARN(
        "SSE response not routed: session {} streams over the connection "
        "currently being written",
        session_id);
    return false;
  }
  OwnedBuffer buffer;
  buffer.add(json_data.c_str(), json_data.length());
  it->second->write(buffer, /*end_stream=*/false);
  GOPHER_LOG_DEBUG("SSE response routed to session {} ({} bytes)", session_id,
                   json_data.size());
  return true;
}

size_t SseSessionRegistry::sessionCount() const {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::sessionCount off-dispatcher-thread");
  return sessions_.size();
}

bool SseSessionRegistry::hasSession(const std::string& session_id) const {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::hasSession off-dispatcher-thread");
  return sessions_.find(session_id) != sessions_.end();
}

}  // namespace filter
}  // namespace mcp
