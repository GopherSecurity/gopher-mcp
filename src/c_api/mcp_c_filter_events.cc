/**
 * @file mcp_c_filter_events.cc
 * @brief C API implementation for chain-level filter events
 */

#include "mcp/c_api/mcp_c_filter_events.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "mcp/filter/filter_chain_callbacks.h"
#include "mcp/filter/filter_chain_event_hub.h"
#include "mcp/filter/filter_event.h"
#include "unified_filter_chain.h"
#include "handle_manager.h"

// Forward declare the external chain registry from mcp_c_filter_chain.cc
namespace mcp {
namespace c_api_internal {
extern HandleManager<UnifiedFilterChain> g_unified_chain_manager;
}
}

namespace {

// Get chain from handle using the shared registry
std::shared_ptr<mcp::c_api_internal::UnifiedFilterChain> get_chain(
    mcp_filter_chain_t handle) {
  return mcp::c_api_internal::g_unified_chain_manager.get(handle);
}

/**
 * C++ callback implementation that wraps a C function pointer
 */
class CFilterEventCallback : public mcp::filter::FilterChainCallbacks {
 public:
  CFilterEventCallback(mcp_filter_event_callback_t callback, void* user_data)
      : callback_(callback), user_data_(user_data) {}

  void onFilterEvent(const mcp::filter::FilterEvent& event) override {
    if (!callback_) {
      return;
    }

    // Convert C++ event to C API format
    mcp_filter_event_context_t context;
    context.chain_id =
        !event.context.chain_id.empty() ? event.context.chain_id.c_str() : nullptr;
    context.stream_id =
        !event.context.stream_id.empty() ? event.context.stream_id.c_str() : nullptr;
    context.correlation_id =
        !event.context.correlation_id.empty() ? event.context.correlation_id.c_str()
                                              : nullptr;

    // Convert event type
    auto event_type = static_cast<mcp_filter_event_type_t>(event.event_type);

    // Convert severity
    auto severity = static_cast<mcp_filter_event_severity_t>(event.severity);

    // Serialize event data to JSON string
    std::string event_data_json = event.event_data.toString();

    // Get timestamp in milliseconds
    int64_t timestamp_ms = event.getTimestampMs();

    // Invoke C callback
    try {
      callback_(event.filter_name.c_str(),
                !event.filter_instance_id.empty()
                    ? event.filter_instance_id.c_str()
                    : nullptr,
                event_type, severity, event_data_json.c_str(), &context,
                timestamp_ms, user_data_);
    } catch (...) {
      // Swallow exceptions from C callbacks
    }
  }

 private:
  mcp_filter_event_callback_t callback_;
  void* user_data_;
};

// Entry to store both callback and observer handle for proper lifetime management
struct CallbackRegistryEntry {
  std::shared_ptr<CFilterEventCallback> callback;
  mcp::filter::FilterChainEventHub::ObserverHandle observer_handle;

  CallbackRegistryEntry(
      std::shared_ptr<CFilterEventCallback> cb,
      mcp::filter::FilterChainEventHub::ObserverHandle handle)
      : callback(std::move(cb)), observer_handle(std::move(handle)) {}
};

// Storage for active callbacks per chain
// Stores both callback (to keep it alive) and ObserverHandle (to prevent auto-unregister)
std::mutex g_callback_mutex;
std::unordered_map<uint64_t, std::unique_ptr<CallbackRegistryEntry>>
    g_callback_registry;

}  // namespace

// Forward declarations for functions that operate on AdvancedFilterChain
namespace mcp {
namespace filter_chain {
class AdvancedFilterChain;

mcp::filter::FilterChainEventHub::ObserverHandle advanced_chain_set_event_callback(
    AdvancedFilterChain& chain,
    std::shared_ptr<mcp::filter::FilterChainCallbacks> callbacks);
}  // namespace filter_chain
}  // namespace mcp

extern "C" {

int mcp_filter_chain_set_event_callback(mcp_filter_chain_t chain,
                                        mcp_filter_event_callback_t callback,
                                        void* user_data) {
  if (chain == 0 || !callback) {
    return -1;  // Invalid arguments
  }

  auto chain_ptr = get_chain(chain);
  if (!chain_ptr) {
    return -1;  // Invalid chain handle
  }

  // Create C++ callback wrapper
  auto cpp_callback = std::make_shared<CFilterEventCallback>(callback, user_data);

  // Set callback on advanced chain if applicable
  auto advanced_chain = chain_ptr->getAdvancedChain();
  if (advanced_chain) {
    try {
      // Register callback and get ObserverHandle
      auto observer_handle = mcp::filter_chain::advanced_chain_set_event_callback(
          *advanced_chain, cpp_callback);

      // Validate that registration actually succeeded
      if (!observer_handle.isValid()) {
        return -2;  // Registration failed - invalid handle
      }

      // Store both callback and handle in registry to keep callback alive and registered
      {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        g_callback_registry[chain] = std::make_unique<CallbackRegistryEntry>(
            cpp_callback, std::move(observer_handle));
      }

      return 0;  // Success
    } catch (...) {
      return -2;  // Callback registration failed
    }
  }

  // Simple chains don't support event callbacks yet
  return -2;  // Not supported for this chain type
}

int mcp_filter_chain_clear_event_callback(mcp_filter_chain_t chain) {
  if (chain == 0) {
    return -1;  // Invalid arguments
  }

  auto chain_ptr = get_chain(chain);
  if (!chain_ptr) {
    return -1;  // Invalid chain handle
  }

  {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    g_callback_registry.erase(chain);
  }

  return 0;  // Success
}

const char* mcp_filter_event_type_to_string(
    mcp_filter_event_type_t event_type) {
  auto cpp_type = static_cast<mcp::filter::FilterEventType>(event_type);
  // Safe: toString returns const char* to string literals
  return mcp::filter::toString(cpp_type);
}

const char* mcp_filter_event_severity_to_string(
    mcp_filter_event_severity_t severity) {
  auto cpp_severity = static_cast<mcp::filter::FilterEventSeverity>(severity);
  // Safe: toString returns const char* to string literals
  return mcp::filter::toString(cpp_severity);
}

}  // extern "C"
