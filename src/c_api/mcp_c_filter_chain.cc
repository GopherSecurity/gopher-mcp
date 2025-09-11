/**
 * @file mcp_filter_chain.cc
 * @brief Implementation of advanced filter chain management
 */

#include "mcp/c_api/mcp_c_filter_chain.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mcp/c_api/mcp_c_filter_api.h"
#include "mcp/c_api/mcp_c_filter_buffer.h"
#include "mcp/c_api/mcp_c_raii.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/network/filter.h"
#include "mcp/filter/filter_chain_builder.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/logging/log_macros.h"

#include "handle_manager.h"
#include "json_value_converter.h"

#define GOPHER_LOG_COMPONENT "capi.chain"

// Forward declare the filter manager from mcp_c_filter_api.cc
namespace mcp {
namespace filter_api {
  extern c_api_internal::HandleManager<network::Filter> g_filter_manager;
}
}

namespace mcp {
namespace filter_chain {

// ============================================================================
// Filter Node Implementation
// ============================================================================

class FilterNode {
 public:
  FilterNode(const mcp_filter_node_t& config)
      : filter_(config.filter),
        name_(config.name ? config.name : ""),
        priority_(config.priority),
        enabled_(config.enabled),
        bypass_on_error_(config.bypass_on_error) {}

  mcp_filter_t getFilter() const { return filter_; }
  const std::string& getName() const { return name_; }
  uint32_t getPriority() const { return priority_; }
  bool isEnabled() const { return enabled_.load(); }
  bool shouldBypassOnError() const { return bypass_on_error_; }

  void setEnabled(bool enabled) { enabled_.store(enabled); }

  // Statistics
  void recordProcessing(std::chrono::microseconds duration) {
    total_processed_++;
    total_duration_ += duration;
  }

  void recordError() { errors_++; }
  void recordBypass() { bypassed_++; }

  uint64_t getTotalProcessed() const { return total_processed_; }
  uint64_t getErrors() const { return errors_; }
  uint64_t getBypassed() const { return bypassed_; }
  double getAvgLatency() const {
    if (total_processed_ == 0)
      return 0;
    return total_duration_.count() / static_cast<double>(total_processed_);
  }

 private:
  mcp_filter_t filter_;
  std::string name_;
  uint32_t priority_;
  std::atomic<bool> enabled_;
  bool bypass_on_error_;

  // Statistics
  std::atomic<uint64_t> total_processed_{0};
  std::atomic<uint64_t> errors_{0};
  std::atomic<uint64_t> bypassed_{0};
  std::chrono::microseconds total_duration_{0};
};

// ============================================================================
// Advanced Filter Chain Implementation
// ============================================================================

class AdvancedFilterChain {
 public:
  AdvancedFilterChain(const mcp_chain_config_t& config, 
                      mcp_dispatcher_t dispatcher = nullptr)
      : name_(config.name ? config.name : ""),
        mode_(config.mode),
        routing_(config.routing),
        max_parallel_(config.max_parallel),
        buffer_size_(config.buffer_size),
        timeout_ms_(config.timeout_ms),
        stop_on_error_(config.stop_on_error),
        state_(MCP_CHAIN_STATE_IDLE),
        dispatcher_(dispatcher) {}

  void addNode(std::unique_ptr<FilterNode> node) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.push_back(std::move(node));
    sortNodes();
  }

  void addConditionalFilter(const mcp_filter_condition_t& condition,
                            mcp_filter_t filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    conditional_filters_.push_back({condition, filter});
  }

  void addParallelGroup(const std::vector<mcp_filter_t>& filters) {
    std::lock_guard<std::mutex> lock(mutex_);
    parallel_groups_.push_back(filters);
  }

  mcp_filter_status_t process(mcp_buffer_handle_t buffer,
                              const mcp_protocol_metadata_t* metadata) {
    setState(MCP_CHAIN_STATE_PROCESSING);
    auto start = std::chrono::steady_clock::now();

    mcp_filter_status_t status = MCP_FILTER_CONTINUE;

    switch (mode_) {
      case MCP_CHAIN_MODE_SEQUENTIAL:
        status = processSequential(buffer, metadata);
        break;
      case MCP_CHAIN_MODE_PARALLEL:
        status = processParallel(buffer, metadata);
        break;
      case MCP_CHAIN_MODE_CONDITIONAL:
        status = processConditional(buffer, metadata);
        break;
      case MCP_CHAIN_MODE_PIPELINE:
        status = processPipeline(buffer, metadata);
        break;
    }

    auto duration = std::chrono::steady_clock::now() - start;
    updateStats(duration);

    setState(status == MCP_FILTER_CONTINUE ? MCP_CHAIN_STATE_COMPLETED
                                           : MCP_CHAIN_STATE_ERROR);

    return status;
  }

  void pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == MCP_CHAIN_STATE_PROCESSING) {
      setState(MCP_CHAIN_STATE_PAUSED);
    }
  }

  void resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == MCP_CHAIN_STATE_PAUSED) {
      setState(MCP_CHAIN_STATE_PROCESSING);
      cv_.notify_all();
    }
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    setState(MCP_CHAIN_STATE_IDLE);
    // Reset statistics
    for (auto& node : nodes_) {
      // Reset node stats if needed
    }
  }

  void setFilterEnabled(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& node : nodes_) {
      if (node->getName() == name) {
        node->setEnabled(enabled);
        break;
      }
    }
  }

  mcp_chain_state_t getState() const { return state_.load(); }

  void getStats(mcp_chain_stats_t* stats) const {
    if (!stats)
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    stats->total_processed = 0;
    stats->total_errors = 0;
    stats->total_bypassed = 0;
    stats->active_filters = 0;

    double total_latency = 0;
    for (const auto& node : nodes_) {
      if (node->isEnabled()) {
        stats->active_filters++;
      }
      stats->total_processed += node->getTotalProcessed();
      stats->total_errors += node->getErrors();
      stats->total_bypassed += node->getBypassed();
      total_latency += node->getAvgLatency();
    }

    stats->avg_latency_ms = total_latency / nodes_.size() / 1000.0;
    stats->max_latency_ms = max_latency_us_.load() / 1000.0;
    stats->throughput_mbps = calculateThroughput();
  }

  void setEventCallback(mcp_chain_event_cb callback, void* user_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = callback;
    event_user_data_ = user_data;
  }

  std::string dump(const std::string& format) const {
    std::ostringstream oss;

    if (format == "json") {
      oss << "{\"name\":\"" << name_ << "\",";
      oss << "\"mode\":" << mode_ << ",";
      oss << "\"filters\":[";
      bool first = true;
      for (const auto& node : nodes_) {
        if (!first)
          oss << ",";
        oss << "{\"name\":\"" << node->getName() << "\",";
        oss << "\"priority\":" << node->getPriority() << ",";
        oss << "\"enabled\":" << node->isEnabled() << "}";
        first = false;
      }
      oss << "]}";
    } else if (format == "dot") {
      oss << "digraph " << name_ << " {" << std::endl;
      for (size_t i = 0; i < nodes_.size(); ++i) {
        oss << "  n" << i << " [label=\"" << nodes_[i]->getName() << "\"];"
            << std::endl;
        if (i > 0) {
          oss << "  n" << (i - 1) << " -> n" << i << ";" << std::endl;
        }
      }
      oss << "}" << std::endl;
    } else {  // text
      oss << "Chain: " << name_ << std::endl;
      oss << "Mode: " << mode_ << std::endl;
      oss << "Filters:" << std::endl;
      for (const auto& node : nodes_) {
        oss << "  - " << node->getName();
        oss << " (priority=" << node->getPriority();
        oss << ", enabled=" << node->isEnabled() << ")" << std::endl;
      }
    }

    return oss.str();
  }
  
  mcp_dispatcher_t getDispatcher() const { return dispatcher_; }

 private:
  mcp_filter_status_t processSequential(
      mcp_buffer_handle_t buffer, const mcp_protocol_metadata_t* metadata) {
    for (auto& node : nodes_) {
      if (!node->isEnabled()) {
        node->recordBypass();
        continue;
      }

      if (isPaused()) {
        waitForResume();
      }

      auto start = std::chrono::steady_clock::now();

      // Process through filter (simplified)
      mcp_filter_status_t status = MCP_FILTER_CONTINUE;
      // TODO: Actually call filter with buffer

      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start);
      node->recordProcessing(duration);

      if (status != MCP_FILTER_CONTINUE) {
        if (stop_on_error_ && !node->shouldBypassOnError()) {
          return status;
        }
        node->recordError();
      }
    }

    return MCP_FILTER_CONTINUE;
  }

  mcp_filter_status_t processParallel(mcp_buffer_handle_t buffer,
                                      const mcp_protocol_metadata_t* metadata) {
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    size_t batch_size =
        std::min(static_cast<size_t>(max_parallel_), nodes_.size());

    for (size_t i = 0; i < nodes_.size(); i += batch_size) {
      size_t end = std::min(i + batch_size, nodes_.size());

      for (size_t j = i; j < end; ++j) {
        threads.emplace_back([this, j, buffer, metadata, &errors]() {
          if (!nodes_[j]->isEnabled())
            return;

          // Process filter
          mcp_filter_status_t status = MCP_FILTER_CONTINUE;
          // TODO: Call filter

          if (status != MCP_FILTER_CONTINUE) {
            errors++;
          }
        });
      }

      for (auto& t : threads) {
        if (t.joinable())
          t.join();
      }
      threads.clear();
    }

    return errors > 0 ? MCP_FILTER_STOP_ITERATION : MCP_FILTER_CONTINUE;
  }

  mcp_filter_status_t processConditional(
      mcp_buffer_handle_t buffer, const mcp_protocol_metadata_t* metadata) {
    for (const auto& cond_filter : conditional_filters_) {
      if (evaluateCondition(cond_filter.first, buffer, metadata)) {
        // Process through conditional filter
        // TODO: Call filter
      }
    }

    return processSequential(buffer, metadata);
  }

  mcp_filter_status_t processPipeline(mcp_buffer_handle_t buffer,
                                      const mcp_protocol_metadata_t* metadata) {
    // Pipeline processing with intermediate buffering
    // TODO: Implement pipeline processing
    return processSequential(buffer, metadata);
  }

  bool evaluateCondition(const mcp_filter_condition_t& condition,
                         mcp_buffer_handle_t buffer,
                         const mcp_protocol_metadata_t* metadata) {
    // TODO: Evaluate condition based on buffer and metadata
    return true;
  }

  void sortNodes() {
    std::sort(nodes_.begin(), nodes_.end(), [](const auto& a, const auto& b) {
      return a->getPriority() < b->getPriority();
    });
  }

  void setState(mcp_chain_state_t new_state) {
    mcp_chain_state_t old_state = state_.exchange(new_state);
    if (event_callback_ && old_state != new_state) {
      // Note: This should be posted to dispatcher thread
      event_callback_(0, old_state, new_state, event_user_data_);
    }
  }

  bool isPaused() const { return state_ == MCP_CHAIN_STATE_PAUSED; }

  void waitForResume() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return state_ != MCP_CHAIN_STATE_PAUSED; });
  }

  void updateStats(std::chrono::steady_clock::duration duration) {
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    uint64_t current_max = max_latency_us_.load();
    while (us > current_max &&
           !max_latency_us_.compare_exchange_weak(current_max, us)) {
    }
  }

  double calculateThroughput() const {
    // TODO: Calculate actual throughput
    return 0.0;
  }

 private:
  std::string name_;
  mcp_chain_execution_mode_t mode_;
  mcp_routing_strategy_t routing_;
  uint32_t max_parallel_;
  uint32_t buffer_size_;
  uint32_t timeout_ms_;
  bool stop_on_error_;

  std::atomic<mcp_chain_state_t> state_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;

  std::vector<std::unique_ptr<FilterNode>> nodes_;
  std::vector<std::pair<mcp_filter_condition_t, mcp_filter_t>>
      conditional_filters_;
  std::vector<std::vector<mcp_filter_t>> parallel_groups_;

  mcp_chain_event_cb event_callback_{nullptr};
  void* event_user_data_{nullptr};

  std::atomic<uint64_t> max_latency_us_{0};
  
  // Store dispatcher for cloning
  mcp_dispatcher_t dispatcher_;
  
public:
  // Owned filters for lifetime management
  // Made public for direct assignment during creation
  std::vector<network::FilterSharedPtr> owned_filters_;
};

// Use HandleManager from shared header
using c_api_internal::HandleManager;

// Handle manager for chains
static HandleManager<AdvancedFilterChain> g_chain_manager;

// ============================================================================
// Chain Router Implementation
// ============================================================================

struct ChainRouter {
  mcp_router_config_t config;
  std::vector<std::pair<mcp_filter_match_cb, mcp_filter_chain_t>> routes;
  std::atomic<size_t> round_robin_index{0};

  ChainRouter(const mcp_router_config_t& cfg) : config(cfg) {}

  mcp_filter_chain_t route(mcp_buffer_handle_t buffer,
                           const mcp_protocol_metadata_t* metadata) {
    // Evaluate routes in order
    for (const auto& route : routes) {
      if (route.first && route.first(buffer, metadata, nullptr)) {
        return route.second;
      }
    }

    // Default routing strategy
    if (config.strategy == MCP_ROUTING_ROUND_ROBIN && !routes.empty()) {
      size_t index = round_robin_index.fetch_add(1) % routes.size();
      return routes[index].second;
    }

    return 0;
  }
};

// ============================================================================
// Chain Pool Implementation
// ============================================================================

struct ChainPool {
  std::vector<mcp_filter_chain_t> chains;
  std::deque<mcp_filter_chain_t> available;
  std::mutex mutex;
  mcp_routing_strategy_t strategy;
  std::atomic<size_t> round_robin_index{0};
  std::atomic<uint64_t> total_processed{0};

  ChainPool(mcp_filter_chain_t base_chain,
            size_t size,
            mcp_routing_strategy_t strat)
      : strategy(strat) {
    // Clone base chain for pool
    for (size_t i = 0; i < size; ++i) {
      // TODO: Actually clone chain
      chains.push_back(base_chain);
      available.push_back(base_chain);
    }
  }

  mcp_filter_chain_t getNext() {
    std::lock_guard<std::mutex> lock(mutex);

    if (available.empty()) {
      return 0;
    }

    mcp_filter_chain_t chain;

    switch (strategy) {
      case MCP_ROUTING_ROUND_ROBIN:
        chain = available.front();
        available.pop_front();
        break;
      case MCP_ROUTING_LEAST_LOADED:
        // TODO: Track load and select least loaded
        chain = available.front();
        available.pop_front();
        break;
      default:
        chain = available.front();
        available.pop_front();
    }

    total_processed++;
    return chain;
  }

  void returnChain(mcp_filter_chain_t chain) {
    std::lock_guard<std::mutex> lock(mutex);
    available.push_back(chain);
  }
};

}  // namespace filter_chain
}  // namespace mcp

// ============================================================================
// C API Implementation
// ============================================================================

using namespace mcp::filter_chain;

extern "C" {

// Advanced Chain Builder

MCP_API mcp_filter_chain_builder_t
mcp_chain_builder_create_ex(mcp_dispatcher_t dispatcher,
                            const mcp_chain_config_t* config) MCP_NOEXCEPT {
  if (!config)
    return nullptr;

  try {
    // Use the function from mcp_filter_api.cc to create the builder
    return mcp_filter_chain_builder_create(dispatcher);
  } catch (const std::bad_alloc&) {
    // Out of memory - no logging to avoid further allocations
    return nullptr;
  } catch (const std::exception&) {
    // Other known exception - avoid complex logging
    return nullptr;
  } catch (...) {
    // Unknown exception - return safe default
    return nullptr;
  }
}

MCP_API mcp_result_t
mcp_chain_builder_add_node(mcp_filter_chain_builder_t builder,
                           const mcp_filter_node_t* node) MCP_NOEXCEPT {
  if (!builder || !node)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    // TODO: Add node to builder
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t
mcp_chain_builder_add_conditional(mcp_filter_chain_builder_t builder,
                                  const mcp_filter_condition_t* condition,
                                  mcp_filter_t filter) MCP_NOEXCEPT {
  if (!builder || !condition)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    // TODO: Add conditional filter
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t
mcp_chain_builder_add_parallel_group(mcp_filter_chain_builder_t builder,
                                     const mcp_filter_t* filters,
                                     size_t count) MCP_NOEXCEPT {
  if (!builder || !filters)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    // TODO: Add parallel group
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t
mcp_chain_builder_set_router(mcp_filter_chain_builder_t builder,
                             mcp_routing_function_t router,
                             void* user_data) MCP_NOEXCEPT {
  if (!builder)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    // TODO: Set custom router
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

// Chain Management

MCP_API mcp_chain_state_t mcp_chain_get_state(mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_CHAIN_STATE_ERROR;

    return chain_ptr->getState();
  } catch (const std::exception&) {
    return MCP_CHAIN_STATE_ERROR;
  } catch (...) {
    return MCP_CHAIN_STATE_ERROR;
  }
}

MCP_API mcp_result_t mcp_chain_pause(mcp_filter_chain_t chain) MCP_NOEXCEPT {
  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    chain_ptr->pause();
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_resume(mcp_filter_chain_t chain) MCP_NOEXCEPT {
  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    chain_ptr->resume();
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_reset(mcp_filter_chain_t chain) MCP_NOEXCEPT {
  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    chain_ptr->reset();
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_set_filter_enabled(mcp_filter_chain_t chain,
                                                  const char* filter_name,
                                                  mcp_bool_t enabled)
    MCP_NOEXCEPT {
  if (!filter_name)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    chain_ptr->setFilterEnabled(filter_name, enabled);
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_get_stats(
    mcp_filter_chain_t chain, mcp_chain_stats_t* stats) MCP_NOEXCEPT {
  if (!stats)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    chain_ptr->getStats(stats);
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_set_event_callback(mcp_filter_chain_t chain,
                                                  mcp_chain_event_cb callback,
                                                  void* user_data)
    MCP_NOEXCEPT {
  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    chain_ptr->setEventCallback(callback, user_data);
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

// Dynamic Chain Composition

MCP_API mcp_filter_chain_t mcp_chain_create_from_json(
    mcp_dispatcher_t dispatcher, mcp_json_value_t json_config) MCP_NOEXCEPT {
  
  // 1. Validate inputs
  if (!dispatcher || !json_config) {
    GOPHER_LOG(Error, "Invalid inputs to chain creation: dispatcher={}, config={}", 
               dispatcher ? "valid" : "null", json_config ? "valid" : "null");
    return 0;
  }
  
  // 2. TODO: Check thread affinity once dispatcher thread validation is available
  // For now, we'll skip this check as the implementation isn't clear
  // if (!isOnDispatcherThread(dispatcher)) {
  //   GOPHER_LOG(Error, "Chain creation called from wrong thread");
  //   return 0;
  // }
  
  try {
    // 3. Convert JSON and normalize
    auto config = mcp::c_api::internal::convertFromCApi(json_config);
    
    // 4. Normalize the filter chain configuration
    size_t normalized_count = mcp::c_api::internal::normalizeFilterChain(config);
    
    auto& filters = config["filters"];
    size_t filter_count = filters.size();
    bool has_typed_config = (normalized_count > 0);
    
    GOPHER_LOG(Info, "Creating chain from JSON with {} filters, {} normalized from typed_config", 
               filter_count, normalized_count);
    
    // 5. Validate all filter types exist in registry
    for (size_t i = 0; i < filter_count; ++i) {
      std::string filter_type = filters[i]["type"].getString();
      
      if (!mcp::filter::FilterRegistry::instance().hasFactory(filter_type)) {
        GOPHER_LOG(Error, "Unknown filter type '{}' at index {}", filter_type, i);
        return 0;
      }
      
      GOPHER_LOG(Debug, "Filter {}: type='{}', has_config={}", 
                 i, filter_type, filters[i].contains("config"));
    }
    
    GOPHER_LOG(Info, "Chain configuration validated: {} filters, has_typed_config={}", 
               filter_count, has_typed_config);
    
    // 6. Create chain using ConfigurableFilterChainFactory
    auto factory = std::make_unique<mcp::filter::ConfigurableFilterChainFactory>(config);
    
    // Validate the chain configuration
    if (!factory->getBuilder().validate()) {
      auto errors = factory->getBuilder().getValidationErrors();
      
      // Log aggregated errors
      std::stringstream error_msg;
      for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) error_msg << "; ";
        error_msg << "[" << i << "] " << errors[i];
      }
      
      GOPHER_LOG(Error, "Chain validation failed: {}", error_msg.str());
      return 0;
    }
    
    // 7. Build real filters using the factory
    auto& builder = factory->getBuilder();
    auto created_filters = builder.buildFilters();
    
    if (created_filters.empty()) {
      GOPHER_LOG(Error, "Factory failed to create any filters");
      return 0;
    }
    
    if (created_filters.size() != filter_count) {
      GOPHER_LOG(Warning, "Factory created {} filters, expected {}", 
                 created_filters.size(), filter_count);
    }
    
    // 8. Store filters in handle manager for lifetime management
    std::vector<mcp_filter_t> filter_handles;
    filter_handles.reserve(created_filters.size());
    
    for (const auto& filter : created_filters) {
      if (!filter) {
        GOPHER_LOG(Error, "Factory created null filter");
        // Clean up already created handles
        for (auto handle : filter_handles) {
          mcp::filter_api::g_filter_manager.release(handle);
        }
        return 0;
      }
      
      // Store in filter manager to get handle
      auto handle = mcp::filter_api::g_filter_manager.store(filter);
      if (handle == 0) {
        GOPHER_LOG(Error, "Failed to store filter in handle manager");
        // Clean up already created handles
        for (auto h : filter_handles) {
          mcp::filter_api::g_filter_manager.release(h);
        }
        return 0;
      }
      filter_handles.push_back(handle);
      GOPHER_LOG(Debug, "Stored filter with handle {}", handle);
    }
    
    // 9. Create the chain instance
    mcp_chain_config_t chain_config = {
      .name = "json_configured_chain",
      .mode = MCP_CHAIN_MODE_SEQUENTIAL,
      .routing = MCP_ROUTING_ROUND_ROBIN,
      .max_parallel = 1,
      .buffer_size = 8192,
      .timeout_ms = 30000,
      .stop_on_error = true
    };
    
    auto chain = std::make_unique<AdvancedFilterChain>(chain_config, dispatcher);
    
    // Note: Filters are already stored in g_filter_manager with handles
    // We don't need to store them again in the chain
    
    // Add filter nodes with real handles
    for (size_t i = 0; i < filter_handles.size(); ++i) {
      auto& filter_config = filters[i];
      std::string filter_type = filter_config["type"].getString();
      std::string filter_name = filter_config.contains("name") ? 
                                filter_config["name"].getString() : filter_type;
      
      mcp_filter_node_t node = {
        .filter = filter_handles[i],  // Real filter handle
        .name = filter_name.c_str(),
        .priority = static_cast<uint32_t>(i),
        .enabled = true,
        .bypass_on_error = false,
        .config = nullptr
      };
      
      auto filter_node = std::make_unique<FilterNode>(node);
      chain->addNode(std::move(filter_node));
    }
    
    // 10. Register chain with handle manager
    auto handle = g_chain_manager.store(std::move(chain));
    
    GOPHER_LOG(Info, "Chain created successfully with handle {}", handle);
    
    return handle;
    
  } catch (const std::bad_alloc&) {
    // Avoid complex logging that might allocate memory
    try {
      GOPHER_LOG(Error, "Chain creation failed: out of memory");
    } catch (...) {}
    return 0;
  } catch (const std::exception& e) {
    try {
      GOPHER_LOG(Error, "Chain creation failed with exception: {}", e.what());
    } catch (...) {}
    return 0;
  } catch (...) {
    try {
      GOPHER_LOG(Error, "Chain creation failed with unknown exception");
    } catch (...) {}
    return 0;
  }
}

MCP_API mcp_json_value_t mcp_chain_export_to_json(mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  GOPHER_LOG(Debug, "Exporting chain {} to JSON", chain);
  
  auto chain_ptr = g_chain_manager.get(chain);
  if (!chain_ptr) {
    GOPHER_LOG(Warning, "Chain {} not found for export", chain);
    return mcp_json_create_null();
  }
  
  // Create a JSON representation of the chain
  try {
    auto config = mcp::json::JsonValue::object();
    config["name"] = mcp::json::JsonValue("exported_chain");
    config["filters"] = mcp::json::JsonValue::array();
    
    // TODO: Export actual filter configurations from the chain
    // For now, return a minimal structure
    
    auto c_api_json = mcp::c_api::internal::convertToCApi(config);
    GOPHER_LOG(Info, "Chain {} exported to JSON", chain);
    return c_api_json;
    
  } catch (const std::bad_alloc&) {
    try {
      GOPHER_LOG(Error, "Failed to export chain {}: out of memory", chain);
    } catch (...) {}
    return mcp_json_create_null();
  } catch (const std::exception& e) {
    try {
      GOPHER_LOG(Error, "Failed to export chain {}: {}", chain, e.what());
    } catch (...) {}
    return mcp_json_create_null();
  } catch (...) {
    try {
      GOPHER_LOG(Error, "Failed to export chain {}: unknown exception", chain);
    } catch (...) {}
    return mcp_json_create_null();
  }
}

MCP_API mcp_filter_chain_t mcp_chain_clone(mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  GOPHER_LOG(Debug, "Cloning chain {}", chain);
  
  auto chain_ptr = g_chain_manager.get(chain);
  if (!chain_ptr) {
    GOPHER_LOG(Warning, "Chain {} not found for cloning", chain);
    return 0;
  }
  
  // Get the dispatcher from the original chain
  mcp_dispatcher_t dispatcher = chain_ptr->getDispatcher();
  if (!dispatcher) {
    GOPHER_LOG(Error, "Chain {} has no dispatcher, cannot clone", chain);
    return 0;
  }
  
  mcp_json_value_t json_config = nullptr;
  
  try {
    // Export chain to JSON
    json_config = mcp_chain_export_to_json(chain);
    if (!json_config) {
      GOPHER_LOG(Error, "Failed to export chain {} for cloning", chain);
      return 0;
    }
    
    // Create new chain from the exported JSON using the stored dispatcher
    auto new_handle = mcp_chain_create_from_json(dispatcher, json_config);
    
    // Clean up the temporary JSON
    mcp_json_free(json_config);
    json_config = nullptr;
    
    if (new_handle) {
      GOPHER_LOG(Info, "Chain {} cloned to new chain {}", chain, new_handle);
    }
    
    return new_handle;
    
  } catch (const std::bad_alloc&) {
    try {
      GOPHER_LOG(Error, "Failed to clone chain {}: out of memory", chain);
    } catch (...) {}
    
    // Clean up JSON if not already freed
    if (json_config) {
      try {
        mcp_json_free(json_config);
      } catch (...) {}
    }
    
    return 0;
  } catch (const std::exception& e) {
    try {
      GOPHER_LOG(Error, "Failed to clone chain {}: {}", chain, e.what());
    } catch (...) {}
    
    // Clean up JSON if not already freed
    if (json_config) {
      try {
        mcp_json_free(json_config);
      } catch (...) {}
    }
    
    return 0;
  } catch (...) {
    try {
      GOPHER_LOG(Error, "Failed to clone chain {}: unknown exception", chain);
    } catch (...) {}
    
    // Clean up JSON if not already freed
    if (json_config) {
      try {
        mcp_json_free(json_config);
      } catch (...) {}
    }
    
    return 0;
  }
}

MCP_API mcp_filter_chain_t mcp_chain_merge(mcp_filter_chain_t chain1,
                                           mcp_filter_chain_t chain2,
                                           mcp_chain_execution_mode_t mode)
    MCP_NOEXCEPT {
  try {
    // TODO: Merge chains
    return 0;
  } catch (const std::bad_alloc&) {
    return 0;
  } catch (const std::exception&) {
    return 0;
  } catch (...) {
    return 0;
  }
}

// Chain Router

MCP_API mcp_chain_router_t
mcp_chain_router_create(const mcp_router_config_t* config) MCP_NOEXCEPT {
  if (!config)
    return nullptr;

  try {
    return reinterpret_cast<mcp_chain_router_t>(new ChainRouter(*config));
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_result_t mcp_chain_router_add_route(mcp_chain_router_t router,
                                                mcp_filter_match_cb condition,
                                                mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  if (!router)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    reinterpret_cast<ChainRouter*>(router)->routes.push_back({condition, chain});
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_filter_chain_t
mcp_chain_router_route(mcp_chain_router_t router,
                       mcp_buffer_handle_t buffer,
                       const mcp_protocol_metadata_t* metadata) MCP_NOEXCEPT {
  if (!router)
    return 0;

  try {
    return reinterpret_cast<ChainRouter*>(router)->route(buffer, metadata);
  } catch (const std::exception&) {
    return 0;
  } catch (...) {
    return 0;
  }
}

MCP_API void mcp_chain_router_destroy(mcp_chain_router_t router) MCP_NOEXCEPT {
  try {
    delete reinterpret_cast<ChainRouter*>(router);
  } catch (...) {
    // Destructor threw - nothing we can do safely
  }
}

// Chain Pool

MCP_API mcp_chain_pool_t mcp_chain_pool_create(mcp_filter_chain_t base_chain,
                                               size_t pool_size,
                                               mcp_routing_strategy_t strategy)
    MCP_NOEXCEPT {
  try {
    return reinterpret_cast<mcp_chain_pool_t>(
        new ChainPool(base_chain, pool_size, strategy));
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_filter_chain_t mcp_chain_pool_get_next(mcp_chain_pool_t pool)
    MCP_NOEXCEPT {
  if (!pool)
    return 0;
    
  try {
    return reinterpret_cast<ChainPool*>(pool)->getNext();
  } catch (const std::exception&) {
    return 0;
  } catch (...) {
    return 0;
  }
}

MCP_API void mcp_chain_pool_return(mcp_chain_pool_t pool,
                                   mcp_filter_chain_t chain) MCP_NOEXCEPT {
  if (!pool)
    return;
    
  try {
    reinterpret_cast<ChainPool*>(pool)->returnChain(chain);
  } catch (...) {
    // Best effort - cannot report error from void function
  }
}

MCP_API mcp_result_t mcp_chain_pool_get_stats(mcp_chain_pool_t pool,
                                              size_t* active,
                                              size_t* idle,
                                              uint64_t* total_processed)
    MCP_NOEXCEPT {
  if (!pool)
    return MCP_ERROR_INVALID_ARGUMENT;

  try {
    auto* pool_impl = reinterpret_cast<ChainPool*>(pool);
    if (active)
      *active = pool_impl->chains.size() - pool_impl->available.size();
    if (idle)
      *idle = pool_impl->available.size();
    if (total_processed)
      *total_processed = pool_impl->total_processed.load();

    return MCP_OK;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API void mcp_chain_pool_destroy(mcp_chain_pool_t pool) MCP_NOEXCEPT {
  try {
    delete reinterpret_cast<ChainPool*>(pool);
  } catch (...) {
    // Destructor threw - nothing we can do safely
  }
}

// Chain Optimization

MCP_API mcp_result_t mcp_chain_optimize(mcp_filter_chain_t chain) MCP_NOEXCEPT {
  try {
    // TODO: Implement optimization
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_reorder_filters(mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  try {
    // TODO: Implement reordering
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_profile(mcp_filter_chain_t chain,
                                       mcp_buffer_handle_t test_buffer,
                                       size_t iterations,
                                       mcp_json_value_t* report) MCP_NOEXCEPT {
  try {
    // TODO: Implement profiling
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

// Chain Debugging

MCP_API mcp_result_t mcp_chain_set_trace_level(
    mcp_filter_chain_t chain, uint32_t trace_level) MCP_NOEXCEPT {
  try {
    // TODO: Implement tracing
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API char* mcp_chain_dump(mcp_filter_chain_t chain,
                             const char* format) MCP_NOEXCEPT {
  try {
    auto chain_ptr = g_chain_manager.get(chain);
    if (!chain_ptr)
      return nullptr;

    std::string dump = chain_ptr->dump(format ? format : "text");
    char* result = static_cast<char*>(malloc(dump.size() + 1));
    if (result) {
      std::strcpy(result, dump.c_str());
    }
    return result;
  } catch (const std::bad_alloc&) {
    return nullptr;
  } catch (const std::exception&) {
    return nullptr;
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_result_t mcp_chain_validate(mcp_filter_chain_t chain,
                                        mcp_json_value_t* errors) MCP_NOEXCEPT {
  try {
    // TODO: Implement validation
    return MCP_OK;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception&) {
    return MCP_ERROR_UNKNOWN;
  } catch (...) {
    return MCP_ERROR_UNKNOWN;
  }
}

}  // extern "C"
