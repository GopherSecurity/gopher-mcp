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
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mcp/c_api/mcp_c_filter_api.h"
#include "mcp/c_api/mcp_c_filter_buffer.h"
#include "mcp/c_api/mcp_c_raii.h"
#include "mcp/c_api/mcp_c_api_json.h"
#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/network/filter.h"
#include "mcp/network/connection.h"
#include "mcp/filter/filter_chain_assembler.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/filter/filter_context.h"
#include "mcp/config/types.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_connection_manager.h"

#include "handle_manager.h"
#include "json_value_converter.h"
#include "unified_filter_chain.h"

#define GOPHER_LOG_COMPONENT "capi.chain"

// Forward declare the filter manager from mcp_c_filter_api.cc
namespace mcp {
namespace filter_api {
  extern c_api_internal::HandleManager<network::Filter> g_filter_manager;
}
}

// Forward declaration of dispatcher thread check function from mcp_c_api_core.cc
extern "C" {
  mcp_bool_t mcp_dispatcher_is_thread(mcp_dispatcher_t dispatcher) MCP_NOEXCEPT;
}

namespace mcp {
namespace filter_chain {

namespace {

class NullProtocolCallbacks : public McpProtocolCallbacks {
 public:
  void onRequest(const jsonrpc::Request&) override {}
  void onNotification(const jsonrpc::Notification&) override {}
  void onResponse(const jsonrpc::Response&) override {}
  void onConnectionEvent(network::ConnectionEvent) override {}
  void onError(const Error&) override {}
};

class CapturingFilterManager : public network::FilterManager {
 public:
  void addReadFilter(network::ReadFilterSharedPtr) override {}
  void addWriteFilter(network::WriteFilterSharedPtr) override {}
  void addFilter(network::FilterSharedPtr filter) override {
    if (filter) {
      filters_.push_back(std::move(filter));
    }
  }
  void removeReadFilter(network::ReadFilterSharedPtr) override {}
  bool initializeReadFilters() override { return true; }
  void onRead() override {}
  network::FilterStatus onWrite() override {
    return network::FilterStatus::Continue;
  }
  void onConnectionEvent(network::ConnectionEvent) override {}

  const std::vector<network::FilterSharedPtr>& filters() const {
    return filters_;
  }

 private:
 std::vector<network::FilterSharedPtr> filters_;
};

void freeStringArray(char** values, size_t count) {
  if (!values) {
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    if (values[i]) {
      mcp_free(values[i]);
    }
  }
  mcp_free(values);
}

void assignStringArray(const std::vector<std::string>& source,
                       char*** target,
                       size_t* target_count) {
  if (!target || !target_count) {
    return;
  }

  *target = nullptr;
  *target_count = 0;

  if (source.empty()) {
    return;
  }

  char** entries = static_cast<char**>(mcp_malloc(sizeof(char*) * source.size()));
  if (!entries) {
    throw std::bad_alloc();
  }

  size_t index = 0;
  try {
    for (const auto& item : source) {
      entries[index] = mcp_strdup(item.c_str());
      if (!entries[index]) {
        throw std::bad_alloc();
      }
      ++index;
    }
  } catch (...) {
    freeStringArray(entries, index);
    throw;
  }

  *target = entries;
  *target_count = source.size();
}

config::FilterChainConfig convertFilterChainConfig(
    const mcp_filter_chain_config_t& config) {
  config::FilterChainConfig cpp_config;

  if (config.name && std::strlen(config.name) > 0) {
    cpp_config.name = config.name;
  } else {
    cpp_config.name = "default";
  }

  if (config.transport_type && std::strlen(config.transport_type) > 0) {
    cpp_config.transport_type = config.transport_type;
  } else {
    cpp_config.transport_type = "tcp";
  }

  if (config.filter_count > 0 && !config.filters) {
    throw std::invalid_argument(
        "Filter chain configuration missing filter array pointer");
  }

  for (size_t i = 0; i < config.filter_count; ++i) {
    const mcp_chain_filter_config_t& entry = config.filters[i];
    config::FilterConfig filter_config;

    if (entry.type && std::strlen(entry.type) > 0) {
      filter_config.type = entry.type;
    }

    if (entry.name && std::strlen(entry.name) > 0) {
      filter_config.name = entry.name;
    } else if (!filter_config.type.empty()) {
      filter_config.name = filter_config.type + ":" + std::to_string(i);
    }

    if (entry.config) {
      filter_config.config = mcp::c_api::internal::convertFromCApi(entry.config);
    } else {
      filter_config.config = mcp::json::JsonValue::object();
    }

    filter_config.enabled = (entry.enabled != MCP_FALSE);

    if (entry.enabled_when) {
      filter_config.enabled_when =
          mcp::c_api::internal::convertFromCApi(entry.enabled_when);
    } else {
      filter_config.enabled_when = mcp::json::JsonValue::object();
    }

    cpp_config.filters.push_back(std::move(filter_config));
  }

  return cpp_config;
}

void populateValidationResult(const mcp::filter::ValidationResult& validation,
                              mcp_chain_validation_result_t* out) {
  if (!out) {
    return;
  }

  out->valid = validation.valid ? MCP_TRUE : MCP_FALSE;
  out->errors = nullptr;
  out->warnings = nullptr;
  out->error_count = 0;
  out->warning_count = 0;

  try {
    assignStringArray(validation.errors, &out->errors, &out->error_count);
    assignStringArray(validation.warnings, &out->warnings, &out->warning_count);
  } catch (...) {
    freeStringArray(out->errors, out->error_count);
    freeStringArray(out->warnings, out->warning_count);
    out->errors = nullptr;
    out->warnings = nullptr;
    out->error_count = 0;
    out->warning_count = 0;
    throw;
  }
}

void populateAssemblyResult(const mcp::filter::AssemblyResult& assembly,
                            mcp_filter_chain_t chain_handle,
                            mcp_chain_assembly_result_t* out) {
  if (!out) {
    return;
  }

  out->success = assembly.success ? MCP_TRUE : MCP_FALSE;
  out->chain = chain_handle;
  out->error_message = nullptr;
  out->created_filters = nullptr;
  out->warnings = nullptr;
  out->created_filter_count = 0;
  out->warning_count = 0;

  if (!assembly.error_message.empty()) {
    out->error_message = mcp_strdup(assembly.error_message.c_str());
  }

  try {
    assignStringArray(assembly.created_filters,
                     &out->created_filters,
                     &out->created_filter_count);
    assignStringArray(assembly.warnings, &out->warnings, &out->warning_count);
  } catch (...) {
    if (out->error_message) {
      mcp_free(out->error_message);
      out->error_message = nullptr;
    }
    freeStringArray(out->created_filters, out->created_filter_count);
    out->created_filters = nullptr;
    out->created_filter_count = 0;
    freeStringArray(out->warnings, out->warning_count);
    out->warnings = nullptr;
    out->warning_count = 0;
    throw;
  }
}

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

}  // anonymous namespace

}  // namespace filter_chain
}  // namespace mcp

// Forward declaration of unified chain manager defined later in this file.
namespace mcp {
namespace c_api_internal {
extern HandleManager<UnifiedFilterChain> g_unified_chain_manager;
}  // namespace c_api_internal
}  // namespace mcp

namespace mcp {
namespace filter_chain {

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

  ~AdvancedFilterChain() {
    for (auto handle : filter_handles_) {
      ::mcp::filter_api::g_filter_manager.release(handle);
    }
  }

  void addNode(std::unique_ptr<FilterNode> node) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (node) {
      filter_handles_.push_back(node->getFilter());
    }
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

  // Getters for export functionality
  const std::string& getName() const { return name_; }
  mcp_chain_execution_mode_t getMode() const { return mode_; }
  mcp_routing_strategy_t getRouting() const { return routing_; }
  uint32_t getMaxParallel() const { return max_parallel_; }
  uint32_t getBufferSize() const { return buffer_size_; }
  uint32_t getTimeoutMs() const { return timeout_ms_; }
  bool getStopOnError() const { return stop_on_error_; }

  // Access to nodes for export (with mutex lock)
  std::vector<std::unique_ptr<FilterNode>>& getNodes() { return nodes_; }
  const std::vector<std::unique_ptr<FilterNode>>& getNodes() const { return nodes_; }
  std::mutex& getMutex() const { return mutex_; }

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
  std::vector<mcp_filter_t> filter_handles_;
};

::mcp::filter::AssemblyResult assembleChainWithAssembler(
    const config::FilterChainConfig& chain_config,
    ::mcp::filter::FilterChainAssembler& assembler,
    const ::mcp::filter::FilterCreationContext& context,
    CapturingFilterManager& manager) {
  return assembler.assembleFilterChain(chain_config, context, manager);
}

mcp_result_t assembleChainInternal(mcp_dispatcher_t dispatcher,
                                   const config::FilterChainConfig& chain_config,
                                   mcp_chain_assembly_result_t* out) {
  if (!out) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  out->success = MCP_FALSE;
  out->chain = 0;
  out->error_message = nullptr;
  out->created_filters = nullptr;
  out->warnings = nullptr;
  out->created_filter_count = 0;
  out->warning_count = 0;

  if (!dispatcher) {
    ::mcp::filter::AssemblyResult assembly(false);
    assembly.error_message = "Dispatcher handle is required";
    populateAssemblyResult(assembly, 0, out);
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  auto* dispatcher_impl =
      reinterpret_cast<::mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
  if (!dispatcher_impl || !dispatcher_impl->dispatcher) {
    ::mcp::filter::AssemblyResult assembly(false);
    assembly.error_message = "Dispatcher is not initialized";
    populateAssemblyResult(assembly, 0, out);
    return MCP_ERROR_INVALID_STATE;
  }

  try {
    NullProtocolCallbacks callbacks;
    ::mcp::filter::TransportMetadata metadata;
    CapturingFilterManager manager;
    ::mcp::filter::FilterChainAssembler assembler(
        ::mcp::filter::FilterRegistry::instance());

    ::mcp::filter::FilterCreationContext context(
        *dispatcher_impl->dispatcher,
        callbacks,
        ::mcp::filter::ConnectionMode::Server,
        metadata);

    auto assembly =
        assembleChainWithAssembler(chain_config, assembler, context, manager);

    std::unique_ptr<filter_chain::AdvancedFilterChain> chain;
    mcp_filter_chain_t handle = 0;
    std::vector<mcp_filter_t> filter_handles;
    bool handles_adopted = false;

    if (assembly.success) {
      const auto& filters = manager.filters();
      filter_handles.reserve(filters.size());

      for (const auto& filter_instance : filters) {
        auto handle_candidate =
            ::mcp::filter_api::g_filter_manager.store(filter_instance);
        if (handle_candidate == 0) {
          assembly.success = false;
          assembly.error_message = "Failed to store filter handle";
          break;
        }
        filter_handles.push_back(handle_candidate);
      }

      if (assembly.success) {
        std::string chain_name = chain_config.name.empty()
                                     ? std::string("configurable_chain")
                                     : chain_config.name;

        mcp_chain_config_t basic_config = {
            chain_name.c_str(),
            MCP_CHAIN_MODE_SEQUENTIAL,
            MCP_ROUTING_ROUND_ROBIN,
            1,
            8192,
            30000,
            MCP_TRUE};

        chain = std::make_unique<AdvancedFilterChain>(basic_config, dispatcher);
        chain->owned_filters_ = manager.filters();

        for (size_t i = 0; i < filter_handles.size(); ++i) {
          const auto& filter_cfg = chain_config.filters[i];
          std::string filter_name = !filter_cfg.name.empty()
                                        ? filter_cfg.name
                                        : (!filter_cfg.type.empty()
                                               ? filter_cfg.type
                                               : std::string("filter") +
                                                     std::to_string(i));

          mcp_filter_node_t node = {
              filter_handles[i],
              filter_name.c_str(),
              static_cast<uint32_t>(i),
              filter_cfg.enabled ? MCP_TRUE : MCP_FALSE,
              MCP_FALSE,
              nullptr};

          chain->addNode(std::make_unique<FilterNode>(node));
        }

        handles_adopted = true;

        auto chain_shared =
            std::shared_ptr<AdvancedFilterChain>(std::move(chain));
        auto unified = std::make_shared<::mcp::c_api_internal::UnifiedFilterChain>(
            chain_shared);
        handle =
            ::mcp::c_api_internal::g_unified_chain_manager.store(unified);

        if (handle == 0) {
          assembly.success = false;
          assembly.error_message = "Failed to store chain handle";
        }
      }
    }

    if (!assembly.success && !handles_adopted) {
      for (auto handle_candidate : filter_handles) {
        ::mcp::filter_api::g_filter_manager.release(handle_candidate);
      }
    }

    populateAssemblyResult(assembly, handle, out);

    return assembly.success ? MCP_OK : MCP_ERROR_INVALID_STATE;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    ::mcp::filter::AssemblyResult assembly(false);
    assembly.error_message = e.what();
    populateAssemblyResult(assembly, 0, out);
    return MCP_ERROR_UNKNOWN;
  }
}

}  // namespace filter_chain
}  // namespace mcp

// ============================================================================
// Thread Affinity Helpers
// ============================================================================

namespace {

static inline bool isOnDispatcherThread(mcp_dispatcher_t dispatcher) {
  if (!dispatcher) {
    return false;
  }

  return mcp_dispatcher_is_thread(dispatcher) == MCP_TRUE;
}

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

}  // anonymous namespace

// Define the global unified chain manager (shared with mcp_c_filter_api.cc)
namespace mcp {
namespace c_api_internal {
HandleManager<UnifiedFilterChain> g_unified_chain_manager;
}  // namespace c_api_internal
}  // namespace mcp

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

MCP_API mcp_result_t mcp_chain_validate_json(
    mcp_json_value_t json_config,
    mcp_chain_validation_result_t* result) MCP_NOEXCEPT {
  if (!result) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto json_value = mcp::c_api::internal::convertFromCApi(json_config);
    mcp::c_api::internal::normalizeFilterChain(json_value);

    auto chain_config =
        mcp::config::FilterChainConfig::fromJson(json_value);
    mcp::filter::FilterChainAssembler assembler(
        mcp::filter::FilterRegistry::instance());
    auto validation = assembler.validateFilterChain(chain_config);
    mcp::filter_chain::populateValidationResult(validation, result);
    return validation.valid ? MCP_OK : MCP_ERROR_INVALID_ARGUMENT;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    mcp::filter::ValidationResult failure;
    failure.valid = false;
    failure.errors.push_back(e.what());
    try {
      mcp::filter_chain::populateValidationResult(failure, result);
    } catch (...) {
      mcp_chain_validation_result_free(result);
      return MCP_ERROR_OUT_OF_MEMORY;
    }
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_validate_config(
    const mcp_filter_chain_config_t* config,
    mcp_chain_validation_result_t* result) MCP_NOEXCEPT {
  if (!config || !result) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto chain_config = mcp::filter_chain::convertFilterChainConfig(*config);
    mcp::filter::FilterChainAssembler assembler(
        mcp::filter::FilterRegistry::instance());
    auto validation = assembler.validateFilterChain(chain_config);
    mcp::filter_chain::populateValidationResult(validation, result);
    return validation.valid ? MCP_OK : MCP_ERROR_INVALID_ARGUMENT;
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    mcp::filter::ValidationResult failure;
    failure.valid = false;
    failure.errors.push_back(e.what());
    try {
      mcp::filter_chain::populateValidationResult(failure, result);
    } catch (...) {
      mcp_chain_validation_result_free(result);
      return MCP_ERROR_OUT_OF_MEMORY;
    }
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API void mcp_chain_validation_result_free(
    mcp_chain_validation_result_t* result) MCP_NOEXCEPT {
  if (!result) {
    return;
  }
  mcp::filter_chain::freeStringArray(result->errors, result->error_count);
  mcp::filter_chain::freeStringArray(result->warnings, result->warning_count);
  result->errors = nullptr;
  result->warnings = nullptr;
  result->error_count = 0;
  result->warning_count = 0;
  result->valid = MCP_FALSE;
}

MCP_API mcp_result_t mcp_chain_assemble_from_json(
    mcp_dispatcher_t dispatcher,
    mcp_json_value_t json_config,
    mcp_chain_assembly_result_t* result) MCP_NOEXCEPT {
  if (!result) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto json_value = mcp::c_api::internal::convertFromCApi(json_config);
    mcp::c_api::internal::normalizeFilterChain(json_value);
    auto chain_config =
        mcp::config::FilterChainConfig::fromJson(json_value);
    return mcp::filter_chain::assembleChainInternal(dispatcher, chain_config,
                                                    result);
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    mcp::filter::AssemblyResult failure(false);
    failure.success = false;
    failure.error_message = e.what();
    mcp::filter_chain::populateAssemblyResult(failure, 0, result);
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API mcp_result_t mcp_chain_assemble_from_config(
    mcp_dispatcher_t dispatcher,
    const mcp_filter_chain_config_t* config,
    mcp_chain_assembly_result_t* result) MCP_NOEXCEPT {
  if (!config || !result) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto chain_config =
        mcp::filter_chain::convertFilterChainConfig(*config);
    return mcp::filter_chain::assembleChainInternal(dispatcher, chain_config,
                                                    result);
  } catch (const std::bad_alloc&) {
    return MCP_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    mcp::filter::AssemblyResult failure(false);
    failure.success = false;
    failure.error_message = e.what();
    mcp::filter_chain::populateAssemblyResult(failure, 0, result);
    return MCP_ERROR_UNKNOWN;
  }
}

MCP_API void mcp_chain_assembly_result_free(
    mcp_chain_assembly_result_t* result) MCP_NOEXCEPT {
  if (!result) {
    return;
  }
  if (result->error_message) {
    mcp_free(result->error_message);
    result->error_message = nullptr;
  }
  mcp::filter_chain::freeStringArray(result->created_filters, result->created_filter_count);
  mcp::filter_chain::freeStringArray(result->warnings, result->warning_count);
  result->created_filters = nullptr;
  result->warnings = nullptr;
  result->created_filter_count = 0;
  result->warning_count = 0;
  result->success = MCP_FALSE;
  // Do not modify chain handle ownership here.
}

// Chain Management

MCP_API mcp_chain_state_t mcp_chain_get_state(mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  try {
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_CHAIN_STATE_ERROR;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    // Check thread affinity - pause must be called from dispatcher thread
    if (!isOnDispatcherThread(chain_ptr->getDispatcher())) {
      try {
        GOPHER_LOG(Error, "Chain pause must be called from dispatcher thread");
      } catch (...) {}
      return MCP_ERROR_INVALID_STATE;
    }

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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    // Check thread affinity - resume must be called from dispatcher thread
    if (!isOnDispatcherThread(chain_ptr->getDispatcher())) {
      try {
        GOPHER_LOG(Error, "Chain resume must be called from dispatcher thread");
      } catch (...) {}
      return MCP_ERROR_INVALID_STATE;
    }

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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    // Check thread affinity - reset must be called from dispatcher thread
    if (!isOnDispatcherThread(chain_ptr->getDispatcher())) {
      try {
        GOPHER_LOG(Error, "Chain reset must be called from dispatcher thread");
      } catch (...) {}
      return MCP_ERROR_INVALID_STATE;
    }

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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
    if (!chain_ptr)
      return MCP_ERROR_NOT_FOUND;

    // Check thread affinity - filter enable/disable must be called from dispatcher thread
    if (!isOnDispatcherThread(chain_ptr->getDispatcher())) {
      try {
        GOPHER_LOG(Error, "Chain set_filter_enabled must be called from dispatcher thread");
      } catch (...) {}
      return MCP_ERROR_INVALID_STATE;
    }

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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return MCP_ERROR_NOT_FOUND;
    
    auto chain_ptr = unified_chain->getAdvancedChain();
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
  if (!dispatcher || !json_config) {
    try {
      GOPHER_LOG(Error, "Invalid inputs to chain creation");
    } catch (...) {}
    return 0;
  }

  if (!isOnDispatcherThread(dispatcher)) {
    try {
      GOPHER_LOG(Error, "Chain creation must be called from dispatcher thread");
    } catch (...) {}
    return 0;
  }

  mcp_chain_assembly_result_t assembly_result{};
  mcp_result_t status = mcp_chain_assemble_from_json(dispatcher, json_config,
                                                     &assembly_result);
  if (status != MCP_OK || assembly_result.success != MCP_TRUE) {
    mcp_chain_assembly_result_free(&assembly_result);
    return 0;
  }

  mcp_filter_chain_t handle = assembly_result.chain;
  assembly_result.chain = 0;
  mcp_chain_assembly_result_free(&assembly_result);
  return handle;
}

MCP_API mcp_json_value_t mcp_chain_export_to_json(mcp_filter_chain_t chain)
    MCP_NOEXCEPT {
  // Remove debug logging from hot path - export is used for cloning
  
  auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
  if (!unified_chain) {
    try {
      GOPHER_LOG(Warning, "Chain not found for export");
    } catch (...) {}
    return mcp_json_create_null();
  }
  
  auto chain_ptr = unified_chain->getAdvancedChain();
  if (!chain_ptr) {
    try {
      GOPHER_LOG(Warning, "Chain is not an advanced chain");
    } catch (...) {}
    return mcp_json_create_null();
  }
  
  // Create a JSON representation of the chain
  try {
    auto config = mcp::json::JsonValue::object();
    
    // Export chain properties
    config["name"] = mcp::json::JsonValue(chain_ptr->getName().empty() ?
                                          "exported_chain" : chain_ptr->getName());
    config["mode"] = mcp::json::JsonValue(static_cast<int64_t>(chain_ptr->getMode()));
    config["routing"] = mcp::json::JsonValue(static_cast<int64_t>(chain_ptr->getRouting()));
    config["max_parallel"] = mcp::json::JsonValue(static_cast<int64_t>(chain_ptr->getMaxParallel()));
    config["buffer_size"] = mcp::json::JsonValue(static_cast<int64_t>(chain_ptr->getBufferSize()));
    config["timeout_ms"] = mcp::json::JsonValue(static_cast<int64_t>(chain_ptr->getTimeoutMs()));
    config["stop_on_error"] = mcp::json::JsonValue(chain_ptr->getStopOnError());
    
    // Export filters array
    auto filters_array = mcp::json::JsonValue::array();
    
    // Lock to safely access nodes
    {
      std::lock_guard<std::mutex> lock(chain_ptr->getMutex());

      for (const auto& node : chain_ptr->getNodes()) {
        auto filter_obj = mcp::json::JsonValue::object();
        
        // Get filter handle from the node
        mcp_filter_t filter_handle = node->getFilter();
        
        // Try to determine filter type from registry or use a generic type
        // For now we'll use the node name as a basis for the type
        std::string filter_type = node->getName();
        
        // Check if this is one of the known filter types
        if (filter_type.find("http_codec") != std::string::npos) {
          filter_type = "http_codec";
        } else if (filter_type.find("sse_codec") != std::string::npos) {
          filter_type = "sse_codec";
        } else if (filter_type.find("json_rpc") != std::string::npos) {
          filter_type = "json_rpc";
        } else if (filter_type.find("passthrough") != std::string::npos) {
          filter_type = "passthrough";
        } else if (filter_type.find("buffer") != std::string::npos) {
          filter_type = "buffer";
        } else if (filter_type.find("router") != std::string::npos) {
          filter_type = "router";
        } else if (filter_type.find("transform") != std::string::npos) {
          filter_type = "transform";
        } else if (filter_type.find("validation") != std::string::npos) {
          filter_type = "validation";
        } else if (filter_type.find("rate_limit") != std::string::npos) {
          filter_type = "rate_limit";
        } else if (filter_type.find("metrics") != std::string::npos) {
          filter_type = "metrics";
        } else if (filter_type.find("logging") != std::string::npos) {
          filter_type = "logging";
        } else {
          // Default to passthrough if we can't determine the type
          filter_type = "passthrough";
        }
        
        filter_obj["type"] = mcp::json::JsonValue(filter_type);
        filter_obj["name"] = mcp::json::JsonValue(node->getName());
        filter_obj["priority"] = mcp::json::JsonValue(static_cast<int64_t>(node->getPriority()));
        filter_obj["enabled"] = mcp::json::JsonValue(node->isEnabled());
        filter_obj["bypass_on_error"] = mcp::json::JsonValue(node->shouldBypassOnError());
        
        // If the node has configuration data, export it
        // For now, we'll add an empty config object
        auto config_obj = mcp::json::JsonValue::object();
        filter_obj["config"] = config_obj;
        
        filters_array.push_back(filter_obj);
      }
    }
    
    config["filters"] = filters_array;
    
    auto c_api_json = mcp::c_api::internal::convertToCApi(config);
    // Remove info logging from hot path
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
  // Remove debug logging from hot path - clone is performance critical
  
  auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
  if (!unified_chain) {
    try {
      GOPHER_LOG(Warning, "Chain not found for cloning");
    } catch (...) {}
    return 0;
  }
  
  auto chain_ptr = unified_chain->getAdvancedChain();
  if (!chain_ptr) {
    try {
      GOPHER_LOG(Warning, "Chain is not advanced, cannot clone");
    } catch (...) {}
    return 0;
  }
  
  // Get the dispatcher from the original chain
  mcp_dispatcher_t dispatcher = chain_ptr->getDispatcher();
  if (!dispatcher) {
    try {
      GOPHER_LOG(Error, "Chain has no dispatcher");
    } catch (...) {}
    return 0;
  }
  
  // Check thread affinity - clone must be called from dispatcher thread
  if (!isOnDispatcherThread(dispatcher)) {
    try {
      GOPHER_LOG(Error, "Clone must be called from dispatcher thread");
    } catch (...) {}
    return 0;
  }
  
  mcp_json_value_t json_config = nullptr;
  
  try {
    // Export chain to JSON
    json_config = mcp_chain_export_to_json(chain);
    if (!json_config) {
      try {
        GOPHER_LOG(Error, "Failed to export chain for cloning");
      } catch (...) {}
      return 0;
    }
    
    // Create new chain from the exported JSON using the stored dispatcher
    auto new_handle = mcp_chain_create_from_json(dispatcher, json_config);
    
    // Clean up the temporary JSON
    mcp_json_free(json_config);
    json_config = nullptr;
    
    // Remove info logging from hot path
    
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
    auto unified_chain = mcp::c_api_internal::g_unified_chain_manager.get(chain);
    if (!unified_chain)
      return nullptr;

    std::string dump = unified_chain->dump(format ? format : "text");
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
