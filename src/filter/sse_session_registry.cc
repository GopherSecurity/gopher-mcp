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
  }
}

bool SseSessionRegistry::sendResponse(const std::string& session_id,
                                      const std::string& json_data) {
  assert(dispatcher_.isThreadSafe() &&
         "SseSessionRegistry::sendResponse off-dispatcher-thread");
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    GOPHER_LOG_WARN("SSE session not found for response routing: {}",
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
