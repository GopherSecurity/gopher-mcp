/**
 * @file filter_chain_event_hub.cc
 * @brief Implementation of the filter chain event hub
 */

#include "mcp/filter/filter_chain_event_hub.h"

#include <algorithm>

namespace mcp {
namespace filter {

bool FilterChainEventHub::ObserverFilter::matches(
    const FilterEvent& event) const {
  // Check severity threshold
  if (event.severity < min_severity) {
    return false;
  }

  // Check event type filter
  if (!event_types.empty()) {
    bool found = false;
    for (const auto& type : event_types) {
      if (event.event_type == type) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }

  // Check filter name filter
  if (!filter_names.empty()) {
    bool found = false;
    for (const auto& name : filter_names) {
      if (event.filter_name == name) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }

  return true;
}

FilterChainEventHub::ObserverHandle FilterChainEventHub::registerObserver(
    std::shared_ptr<FilterChainCallbacks> callback,
    const ObserverFilter& filter) {
  if (!callback) {
    return ObserverHandle();
  }

  std::lock_guard<std::mutex> lock(mutex_);

  size_t observer_id = next_observer_id_++;
  observers_.push_back({observer_id, callback, filter});

  return ObserverHandle(this, observer_id);
}

void FilterChainEventHub::unregisterObserver(size_t observer_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = std::find_if(observers_.begin(), observers_.end(),
                         [observer_id](const ObserverEntry& entry) {
                           return entry.id == observer_id;
                         });

  if (it != observers_.end()) {
    observers_.erase(it);
  }
}

void FilterChainEventHub::emit(const FilterEvent& event) {
  if (!enabled_) {
    return;
  }

  // Take a snapshot of observers to avoid holding lock during callbacks
  std::vector<ObserverEntry> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = observers_;
  }

  // Invoke callbacks without holding the lock
  for (const auto& entry : snapshot) {
    if (entry.filter.matches(event)) {
      try {
        entry.callback->onFilterEvent(event);
      } catch (const std::exception& ex) {
        // Log error but don't propagate exceptions from callbacks
        // In production, this would use the logging framework
        // For now, silently catch to prevent callback errors from
        // disrupting filter operation
        (void)ex;  // Suppress unused variable warning
      } catch (...) {
        // Catch all other exceptions
      }
    }
  }
}

size_t FilterChainEventHub::getObserverCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return observers_.size();
}

void FilterChainEventHub::clearObservers() {
  std::lock_guard<std::mutex> lock(mutex_);
  observers_.clear();
}

void FilterChainEventHub::setEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
}

bool FilterChainEventHub::isEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

}  // namespace filter
}  // namespace mcp
