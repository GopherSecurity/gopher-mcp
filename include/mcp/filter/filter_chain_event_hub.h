/**
 * @file filter_chain_event_hub.h
 * @brief Event hub for chain-level observer registration and event fan-out
 *
 * The FilterChainEventHub provides thread-safe registration of observers
 * and efficient event distribution to all registered callbacks. It supports
 * optional filtering by event type and severity.
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "filter_chain_callbacks.h"
#include "filter_event.h"

namespace mcp {
namespace filter {

/**
 * @brief Event hub for managing observers and distributing events
 *
 * The FilterChainEventHub is owned by each FilterChain and provides a
 * centralized registration point for chain-level observers. It handles
 * event fan-out to all registered callbacks with optional filtering.
 *
 * Thread Safety: All methods are thread-safe and can be called from any
 * thread. Event emission is typically done from the dispatcher thread,
 * but registration can happen from other threads.
 */
class FilterChainEventHub {
 public:
  /**
   * @brief Observer filter configuration
   *
   * Allows observers to specify which events they want to receive.
   */
  struct ObserverFilter {
    /// Event type filter (empty = receive all types)
    std::vector<FilterEventType> event_types;

    /// Minimum severity level (events below this severity are filtered)
    FilterEventSeverity min_severity;

    /// Filter name filter (empty = receive from all filters)
    std::vector<std::string> filter_names;

    /**
     * @brief Default constructor
     */
    ObserverFilter() : min_severity(FilterEventSeverity::TRACE) {}

    /**
     * @brief Check if event matches filter criteria
     */
    bool matches(const FilterEvent& event) const;
  };

  /**
   * @brief Observer registration handle
   *
   * RAII handle for observer registration. Automatically unregisters
   * the observer when destroyed.
   */
  class ObserverHandle {
   public:
    ObserverHandle() = default;

    ObserverHandle(FilterChainEventHub* hub, size_t id)
        : hub_(hub), observer_id_(id) {}

    ~ObserverHandle() {
      if (hub_ && observer_id_ != SIZE_MAX) {
        hub_->unregisterObserver(observer_id_);
      }
    }

    // Move-only semantics
    ObserverHandle(const ObserverHandle&) = delete;
    ObserverHandle& operator=(const ObserverHandle&) = delete;

    ObserverHandle(ObserverHandle&& other) noexcept
        : hub_(other.hub_), observer_id_(other.observer_id_) {
      other.hub_ = nullptr;
      other.observer_id_ = SIZE_MAX;
    }

    ObserverHandle& operator=(ObserverHandle&& other) noexcept {
      if (this != &other) {
        if (hub_ && observer_id_ != SIZE_MAX) {
          hub_->unregisterObserver(observer_id_);
        }
        hub_ = other.hub_;
        observer_id_ = other.observer_id_;
        other.hub_ = nullptr;
        other.observer_id_ = SIZE_MAX;
      }
      return *this;
    }

    bool isValid() const { return hub_ != nullptr && observer_id_ != SIZE_MAX; }

    size_t getObserverId() const { return observer_id_; }

   private:
    FilterChainEventHub* hub_ = nullptr;
    size_t observer_id_ = SIZE_MAX;
  };

  /**
   * @brief Default constructor
   */
  FilterChainEventHub() = default;

  /**
   * @brief Destructor
   */
  ~FilterChainEventHub() = default;

  // Non-copyable, non-movable
  FilterChainEventHub(const FilterChainEventHub&) = delete;
  FilterChainEventHub& operator=(const FilterChainEventHub&) = delete;
  FilterChainEventHub(FilterChainEventHub&&) = delete;
  FilterChainEventHub& operator=(FilterChainEventHub&&) = delete;

  /**
   * @brief Register an observer with optional filtering
   *
   * @param callback Observer callback interface
   * @param filter Optional filter criteria
   * @return RAII handle that unregisters on destruction
   */
  ObserverHandle registerObserver(
      std::shared_ptr<FilterChainCallbacks> callback,
      const ObserverFilter& filter = ObserverFilter());

  /**
   * @brief Unregister an observer by ID
   *
   * @param observer_id Observer ID from registration
   */
  void unregisterObserver(size_t observer_id);

  /**
   * @brief Emit an event to all registered observers
   *
   * Synchronously invokes all registered callbacks that match the event
   * filter criteria. This method is typically called from the dispatcher
   * thread.
   *
   * @param event The event to emit
   */
  void emit(const FilterEvent& event);

  /**
   * @brief Get number of registered observers
   */
  size_t getObserverCount() const;

  /**
   * @brief Clear all registered observers
   */
  void clearObservers();

  /**
   * @brief Enable/disable event emission
   *
   * When disabled, emit() calls are no-ops. Useful for testing.
   *
   * @param enabled Enable flag
   */
  void setEnabled(bool enabled);

  /**
   * @brief Check if event emission is enabled
   */
  bool isEnabled() const;

 private:
  struct ObserverEntry {
    size_t id;
    std::shared_ptr<FilterChainCallbacks> callback;
    ObserverFilter filter;
  };

  mutable std::mutex mutex_;
  std::vector<ObserverEntry> observers_;
  size_t next_observer_id_ = 0;
  bool enabled_ = true;
};

}  // namespace filter
}  // namespace mcp
