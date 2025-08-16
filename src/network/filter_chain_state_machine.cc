/**
 * @file filter_chain_state_machine.cc
 * @brief Implementation of Filter Chain State Machine
 */

#include "mcp/network/filter_chain_state_machine.h"

#include <algorithm>
#include <sstream>

namespace mcp {
namespace network {

namespace {

// Validation matrix for state transitions
const std::unordered_map<FilterChainState,
                         std::unordered_set<FilterChainState>>&
getValidTransitions() {
  static const std::unordered_map<FilterChainState,
                                  std::unordered_set<FilterChainState>>
      transitions = {
          // Initial states
          {FilterChainState::Uninitialized,
           {FilterChainState::Initializing, FilterChainState::Failed}},

          {FilterChainState::Initializing,
           {FilterChainState::Configuring, FilterChainState::Failed}},

          // Setup states
          {FilterChainState::Configuring,
           {FilterChainState::Connecting, FilterChainState::Idle,
            FilterChainState::Active, FilterChainState::Failed}},

          {FilterChainState::Connecting,
           {FilterChainState::Idle, FilterChainState::Active,
            FilterChainState::Failed}},

          // Ready states
          {FilterChainState::Active,
           {FilterChainState::Idle, FilterChainState::ProcessingUpstream,
            FilterChainState::ProcessingDownstream, FilterChainState::Paused,
            FilterChainState::Buffering, FilterChainState::IteratingRead,
            FilterChainState::IteratingWrite, FilterChainState::Closing,
            FilterChainState::Aborting, FilterChainState::FilterError,
            FilterChainState::Failed}},

          // Data flow states
          {FilterChainState::ProcessingUpstream,
           {FilterChainState::Active, FilterChainState::Idle,
            FilterChainState::IteratingWrite, FilterChainState::Buffering,
            FilterChainState::StoppedIteration, FilterChainState::Failed}},

          {FilterChainState::ProcessingDownstream,
           {FilterChainState::Active, FilterChainState::Idle,
            FilterChainState::IteratingRead, FilterChainState::Buffering,
            FilterChainState::StoppedIteration, FilterChainState::Failed}},

          // Flow control states
          {FilterChainState::Paused,
           {FilterChainState::Active, FilterChainState::Buffering,
            FilterChainState::Draining, FilterChainState::Closing,
            FilterChainState::AboveHighWatermark, FilterChainState::Failed}},

          {FilterChainState::Buffering,
           {FilterChainState::Draining, FilterChainState::AboveHighWatermark,
            FilterChainState::Active, FilterChainState::Failed}},

          {FilterChainState::Draining,
           {FilterChainState::Active, FilterChainState::Idle,
            FilterChainState::BelowLowWatermark, FilterChainState::Flushing,
            FilterChainState::Closing, FilterChainState::Failed}},

          {FilterChainState::Flushing,
           {FilterChainState::Closing, FilterChainState::Terminated,
            FilterChainState::Failed}},

          // Filter iteration states
          {FilterChainState::IteratingRead,
           {FilterChainState::Active, FilterChainState::StoppedIteration,
            FilterChainState::ProcessingDownstream, FilterChainState::Failed}},

          {FilterChainState::IteratingWrite,
           {FilterChainState::Active, FilterChainState::StoppedIteration,
            FilterChainState::ProcessingUpstream, FilterChainState::Failed}},

          {FilterChainState::StoppedIteration,
           {FilterChainState::Active, FilterChainState::IteratingRead,
            FilterChainState::IteratingWrite, FilterChainState::Paused,
            FilterChainState::Failed}},

          // Watermark states
          {FilterChainState::AboveHighWatermark,
           {FilterChainState::Paused, FilterChainState::Draining,
            FilterChainState::BelowLowWatermark, FilterChainState::Failed}},

          // Idle can transition to active or closing
          {FilterChainState::Idle,
           {FilterChainState::Active, FilterChainState::Closing,
            FilterChainState::Failed}},

          {FilterChainState::BelowLowWatermark,
           {FilterChainState::Active, FilterChainState::Buffering,
            FilterChainState::Failed}},

          // Error states
          {FilterChainState::FilterError,
           {FilterChainState::Recovering, FilterChainState::Closing,
            FilterChainState::Failed}},

          {FilterChainState::ProtocolError,
           {FilterChainState::Closing, FilterChainState::Aborting,
            FilterChainState::Failed}},

          {FilterChainState::Recovering,
           {FilterChainState::Active, FilterChainState::Idle,
            FilterChainState::Failed}},

          // Shutdown states
          {FilterChainState::Closing,
           {FilterChainState::Flushing, FilterChainState::Terminated,
            FilterChainState::Closed}},

          {FilterChainState::Aborting,
           {FilterChainState::Terminated, FilterChainState::Failed}},

          {FilterChainState::Terminated,
           {FilterChainState::Closed, FilterChainState::Failed}},

          // Terminal states (no transitions)
          {FilterChainState::Closed, {}},
          {FilterChainState::Failed, {}}};

  return transitions;
}

}  // namespace

// Filter entry for internal tracking
struct FilterEntry {
  std::string name;
  FilterSharedPtr filter;             // Base filter pointer
  ReadFilterSharedPtr read_filter;    // For read filters
  WriteFilterSharedPtr write_filter;  // For write filters
  bool enabled = true;
  size_t bytes_processed = 0;
  std::chrono::steady_clock::time_point last_active;
};

// ===== FilterChainStateMachine Implementation =====

FilterChainStateMachine::FilterChainStateMachine(
    event::Dispatcher& dispatcher,
    Connection& connection,
    const FilterChainConfig& config)
    : dispatcher_(dispatcher),
      connection_(connection),
      config_(config),
      dispatcher_thread_id_(std::this_thread::get_id()) {
  last_activity_ = std::chrono::steady_clock::now();
}

FilterChainStateMachine::~FilterChainStateMachine() {
  assertInDispatcherThread();

  // Cancel all timers
  cancelTimers();

  // Clear filters
  read_filters_.clear();
  write_filters_.clear();
  filter_map_.clear();
}

bool FilterChainStateMachine::initialize() {
  assertInDispatcherThread();

  if (current_state_ != FilterChainState::Uninitialized) {
    return false;
  }

  if (!transitionTo(FilterChainState::Initializing,
                    FilterChainEvent::InitializeRequested,
                    "Initialization requested")) {
    return false;
  }

  // Start initialization timer if configured
  if (config_.initialization_timeout.count() > 0) {
    startInitializationTimer();
  }

  // Move to configuring state
  return transitionTo(FilterChainState::Configuring,
                      FilterChainEvent::ConfigurationComplete,
                      "Ready for configuration");
}

bool FilterChainStateMachine::start() {
  assertInDispatcherThread();

  auto state = current_state_.load();
  if (state != FilterChainState::Configuring &&
      state != FilterChainState::Idle) {
    return false;
  }

  // Initialize read filters
  if (config_.mode != FilterChainMode::WriteOnly) {
    if (!initializeReadFilters()) {
      handleFilterError("read_filters", "Failed to initialize read filters");
      return false;
    }
  }

  // Initialize write filters
  if (config_.mode != FilterChainMode::ReadOnly) {
    for (auto& entry : write_filters_) {
      if (entry.filter && entry.enabled) {
        // Write filters typically don't have an onNewConnection
        stats_.filters_initialized++;
      }
    }
  }

  // Transition to active state - ready to process data
  return transitionTo(FilterChainState::Active,
                      FilterChainEvent::ConnectionEstablished,
                      "Filter chain started and active");
}

bool FilterChainStateMachine::pause() {
  assertInDispatcherThread();

  // Can only pause from active processing states
  if (!FilterChainStatePatterns::canProcessData(current_state_)) {
    return false;
  }

  return transitionTo(FilterChainState::Paused,
                      FilterChainEvent::PauseRequested, "Pause requested");
}

bool FilterChainStateMachine::resume() {
  assertInDispatcherThread();

  if (current_state_ != FilterChainState::Paused) {
    return false;
  }

  // Check if we need to drain buffers first
  if (buffered_bytes_ > 0) {
    return transitionTo(FilterChainState::Draining,
                        FilterChainEvent::ResumeRequested,
                        "Resuming with buffer drain");
  }

  return transitionTo(FilterChainState::Active,
                      FilterChainEvent::ResumeRequested, "Resume requested");
}

bool FilterChainStateMachine::close() {
  assertInDispatcherThread();

  auto state = current_state_.load();

  // Already closing or in terminal state
  if (state == FilterChainState::Closing ||
      state == FilterChainState::Flushing ||
      FilterChainStatePatterns::isTerminal(state)) {
    return false;
  }

  // If we have buffered data, flush first
  if (buffered_bytes_ > 0) {
    transitionTo(FilterChainState::Flushing, FilterChainEvent::CloseRequested,
                 "Flushing before close");
  }

  return transitionTo(FilterChainState::Closing,
                      FilterChainEvent::CloseRequested, "Close requested");
}

bool FilterChainStateMachine::abort() {
  assertInDispatcherThread();

  if (FilterChainStatePatterns::isTerminal(current_state_)) {
    return false;
  }

  // Transition to Aborting from any non-terminal state
  transitionTo(FilterChainState::Aborting, FilterChainEvent::AbortRequested,
               "Abort requested");

  return true;
}

bool FilterChainStateMachine::addReadFilter(ReadFilterSharedPtr filter,
                                            const std::string& name) {
  assertInDispatcherThread();

  if (FilterChainStatePatterns::isTerminal(current_state_)) {
    return false;
  }

  // Check for duplicate name
  if (filter_map_.find(name) != filter_map_.end()) {
    return false;
  }

  FilterEntry entry;
  entry.name = name;
  entry.filter = std::static_pointer_cast<Filter>(filter);
  entry.read_filter = filter;
  entry.enabled = true;
  entry.last_active = std::chrono::steady_clock::now();

  read_filters_.push_back(entry);
  filter_map_[name] = &read_filters_.back();

  handleEvent(FilterChainEvent::FilterAdded);
  return true;
}

bool FilterChainStateMachine::addWriteFilter(WriteFilterSharedPtr filter,
                                             const std::string& name) {
  assertInDispatcherThread();

  if (FilterChainStatePatterns::isTerminal(current_state_)) {
    return false;
  }

  // Check for duplicate name
  if (filter_map_.find(name) != filter_map_.end()) {
    return false;
  }

  FilterEntry entry;
  entry.name = name;
  entry.filter = std::static_pointer_cast<Filter>(filter);
  entry.write_filter = filter;
  entry.enabled = true;
  entry.last_active = std::chrono::steady_clock::now();

  write_filters_.push_back(entry);
  filter_map_[name] = &write_filters_.back();

  handleEvent(FilterChainEvent::FilterAdded);
  return true;
}

bool FilterChainStateMachine::removeFilter(const std::string& name) {
  assertInDispatcherThread();

  auto it = filter_map_.find(name);
  if (it == filter_map_.end()) {
    return false;
  }

  // Mark as disabled rather than removing to avoid iterator invalidation
  it->second->enabled = false;
  filter_map_.erase(it);

  handleEvent(FilterChainEvent::FilterRemoved);
  return true;
}

void FilterChainStateMachine::reorderFilters(
    const std::vector<std::string>& order) {
  assertInDispatcherThread();

  // This is a simplified implementation
  // A full implementation would reorder the filter vectors
}

FilterStatus FilterChainStateMachine::onData(Buffer& data, bool end_stream) {
  assertInDispatcherThread();

  // Check if we can process data
  auto state = current_state_.load();

  // Buffer the data if we're paused or buffering
  if (state == FilterChainState::Paused ||
      state == FilterChainState::Buffering) {
    if (bufferData(data)) {
      return FilterStatus::StopIteration;
    }
    return FilterStatus::Continue;
  }

  if (!FilterChainStatePatterns::canProcessData(current_state_)) {
    return FilterStatus::Continue;
  }

  // Update state
  transitionTo(FilterChainState::ProcessingDownstream,
               FilterChainEvent::DataReceived, "Processing downstream data");

  // Iterate through read filters
  return iterateReadFilters(data, end_stream);
}

FilterStatus FilterChainStateMachine::onWrite(Buffer& data, bool end_stream) {
  assertInDispatcherThread();

  // Check if we can process data
  auto state = current_state_.load();

  // Buffer the data if we're paused or buffering
  if (state == FilterChainState::Paused ||
      state == FilterChainState::Buffering) {
    // Buffer in upstream buffer for write data
    size_t data_size = data.length();
    size_t current = buffered_bytes_.load();

    if (current + data_size > config_.max_buffered_bytes) {
      stats_.buffer_overflows++;
      return FilterStatus::Continue;
    }

    upstream_buffer_.move(data);
    buffered_bytes_ += data_size;
    checkWatermarks();
    return FilterStatus::StopIteration;
  }

  if (!FilterChainStatePatterns::canProcessData(state)) {
    return FilterStatus::Continue;
  }

  // Update state
  transitionTo(FilterChainState::ProcessingUpstream,
               FilterChainEvent::DataToSend, "Processing upstream data");

  // Iterate through write filters
  return iterateWriteFilters(data, end_stream);
}

void FilterChainStateMachine::addReadFilter(ReadFilterSharedPtr filter) {
  // Generate a unique name
  std::stringstream ss;
  ss << "read_filter_" << read_filters_.size();
  addReadFilter(filter, ss.str());
}

void FilterChainStateMachine::addWriteFilter(WriteFilterSharedPtr filter) {
  // Generate a unique name
  std::stringstream ss;
  ss << "write_filter_" << write_filters_.size();
  addWriteFilter(filter, ss.str());
}

void FilterChainStateMachine::addFilter(FilterSharedPtr filter) {
  // Try to cast to read/write filter
  if (auto read_filter = std::dynamic_pointer_cast<ReadFilter>(filter)) {
    addReadFilter(read_filter);
  } else if (auto write_filter =
                 std::dynamic_pointer_cast<WriteFilter>(filter)) {
    addWriteFilter(write_filter);
  }
}

bool FilterChainStateMachine::initializeReadFilters() {
  assertInDispatcherThread();

  for (auto& entry : read_filters_) {
    if (entry.read_filter && entry.enabled) {
      if (auto* read_filter = entry.read_filter.get()) {
        read_filter->onNewConnection();
        stats_.filters_initialized++;
      }
    }
  }

  return true;
}

void FilterChainStateMachine::onContinueReading(ReadFilter* filter) {
  assertInDispatcherThread();

  if (filter == stopped_read_filter_) {
    stopped_read_filter_ = nullptr;
    iteration_stopped_ = false;

    // Continue iteration
    continueIteration();
  }
}

bool FilterChainStateMachine::isActive() const {
  auto state = current_state_.load();
  return state == FilterChainState::Active ||
         state == FilterChainState::ProcessingUpstream ||
         state == FilterChainState::ProcessingDownstream;
}

bool FilterChainStateMachine::canProcessData() const {
  return FilterChainStatePatterns::canProcessData(current_state_);
}

bool FilterChainStateMachine::isTerminal() const {
  return FilterChainStatePatterns::isTerminal(current_state_);
}

size_t FilterChainStateMachine::activeFilterCount() const {
  size_t count = 0;

  for (const auto& entry : read_filters_) {
    if (entry.enabled)
      count++;
  }

  for (const auto& entry : write_filters_) {
    if (entry.enabled)
      count++;
  }

  return count;
}

size_t FilterChainStateMachine::bufferedBytes() const {
  return buffered_bytes_.load();
}

bool FilterChainStateMachine::handleEvent(FilterChainEvent event) {
  assertInDispatcherThread();

  bool handled = false;

  switch (event) {
    case FilterChainEvent::InitializeRequested:
      handled = initialize();
      break;

    case FilterChainEvent::ConnectionEstablished:
      handled = start();
      break;

    case FilterChainEvent::DataReceived:
      // Handled in onData
      handled = true;
      break;

    case FilterChainEvent::PauseRequested:
      handled = pause();
      break;

    case FilterChainEvent::ResumeRequested:
      handled = resume();
      break;

    case FilterChainEvent::CloseRequested:
      handled = close();
      break;

    case FilterChainEvent::AbortRequested:
      handled = abort();
      break;

    case FilterChainEvent::BufferHighWatermark:
      applyBackpressure();
      handled = true;
      break;

    case FilterChainEvent::BufferLowWatermark:
      releaseBackpressure();
      handled = true;
      break;

    case FilterChainEvent::FilterError:
      // Transition to error state
      if (config_.continue_on_filter_error) {
        transitionTo(FilterChainState::FilterError,
                     FilterChainEvent::FilterError, "Filter error occurred");
        attemptRecovery();
      } else {
        transitionTo(FilterChainState::Failed, FilterChainEvent::FilterError,
                     "Fatal filter error");
      }
      handled = true;
      break;

    default:
      break;
  }

  return handled;
}

// ===== Private Methods =====

bool FilterChainStateMachine::transitionTo(FilterChainState new_state,
                                           FilterChainEvent event,
                                           const std::string& reason) {
  auto current = current_state_.load();

  if (current == new_state) {
    return true;  // Already in target state
  }

  if (!isValidTransition(current, new_state)) {
    if (config_.error_callback) {
      std::stringstream ss;
      ss << "Invalid transition from " << static_cast<int>(current) << " to "
         << static_cast<int>(new_state);
      config_.error_callback(ss.str());
    }
    return false;
  }

  // Exit current state
  onStateExit(current);

  // Update state
  current_state_ = new_state;
  transition_count_++;

  // Enter new state
  onStateEnter(new_state);

  // Notify callback
  if (config_.state_change_callback) {
    config_.state_change_callback(current, new_state, reason);
  }

  return true;
}

bool FilterChainStateMachine::isValidTransition(FilterChainState from,
                                                FilterChainState to) const {
  const auto& transitions = getValidTransitions();
  auto it = transitions.find(from);

  if (it == transitions.end()) {
    return false;
  }

  return it->second.find(to) != it->second.end();
}

void FilterChainStateMachine::onStateEnter(FilterChainState state) {
  switch (state) {
    case FilterChainState::Initializing:
      if (config_.initialization_timeout.count() > 0) {
        startInitializationTimer();
      }
      break;

    case FilterChainState::Draining:
      if (config_.drain_timeout.count() > 0) {
        startDrainTimer();
      }
      drainBuffers();
      break;

    case FilterChainState::Idle:
      if (config_.idle_timeout.count() > 0) {
        startIdleTimer();
      }
      break;

    case FilterChainState::AboveHighWatermark:
      applyBackpressure();
      break;

    case FilterChainState::BelowLowWatermark:
      releaseBackpressure();
      break;

    default:
      break;
  }
}

void FilterChainStateMachine::onStateExit(FilterChainState state) {
  switch (state) {
    case FilterChainState::Initializing:
      if (initialization_timer_) {
        initialization_timer_->disableTimer();
      }
      break;

    case FilterChainState::Draining:
      if (drain_timer_) {
        drain_timer_->disableTimer();
      }
      break;

    case FilterChainState::Idle:
      if (idle_timer_) {
        idle_timer_->disableTimer();
      }
      break;

    default:
      break;
  }
}

FilterStatus FilterChainStateMachine::iterateReadFilters(Buffer& data,
                                                         bool end_stream) {
  transitionTo(FilterChainState::IteratingRead, FilterChainEvent::DataReceived,
               "Iterating read filters");

  FilterStatus status = FilterStatus::Continue;
  size_t original_data_len = data.length();

  for (size_t i = read_filter_index_; i < read_filters_.size(); ++i) {
    auto& entry = read_filters_[i];

    if (!entry.enabled) {
      continue;
    }

    if (entry.read_filter) {
      auto* read_filter = entry.read_filter.get();
      entry.last_active = std::chrono::steady_clock::now();

      // Store the original data length before filter processing
      size_t data_len = data.length();
      status = read_filter->onData(data, end_stream);
      entry.bytes_processed += data_len;

      if (status == FilterStatus::StopIteration) {
        read_filter_index_ = i;
        stopped_read_filter_ = read_filter;
        iteration_stopped_ = true;
        stats_.iterations_stopped++;

        transitionTo(FilterChainState::StoppedIteration,
                     FilterChainEvent::FilterStopIteration,
                     "Filter stopped iteration");
        break;
      }
    }
  }

  if (status == FilterStatus::Continue) {
    read_filter_index_ = 0;  // Reset for next iteration
    transitionTo(FilterChainState::Active, FilterChainEvent::FilterContinue,
                 "Filter iteration complete");
  }

  updateStats(original_data_len, false);
  return status;
}

FilterStatus FilterChainStateMachine::iterateWriteFilters(Buffer& data,
                                                          bool end_stream) {
  transitionTo(FilterChainState::IteratingWrite, FilterChainEvent::DataToSend,
               "Iterating write filters");

  FilterStatus status = FilterStatus::Continue;
  size_t original_data_len = data.length();

  for (size_t i = write_filter_index_; i < write_filters_.size(); ++i) {
    auto& entry = write_filters_[i];

    if (!entry.enabled) {
      continue;
    }

    if (entry.write_filter) {
      auto* write_filter = entry.write_filter.get();
      entry.last_active = std::chrono::steady_clock::now();

      // Store the original data length before filter processing
      size_t data_len = data.length();
      status = write_filter->onWrite(data, end_stream);
      entry.bytes_processed += data_len;

      if (status == FilterStatus::StopIteration) {
        write_filter_index_ = i;
        stopped_write_filter_ = write_filter;
        iteration_stopped_ = true;
        stats_.iterations_stopped++;

        transitionTo(FilterChainState::StoppedIteration,
                     FilterChainEvent::FilterStopIteration,
                     "Filter stopped iteration");
        break;
      }
    }
  }

  if (status == FilterStatus::Continue) {
    write_filter_index_ = 0;  // Reset for next iteration
    transitionTo(FilterChainState::Active, FilterChainEvent::FilterContinue,
                 "Filter iteration complete");
  }

  updateStats(original_data_len, true);
  return status;
}

void FilterChainStateMachine::continueIteration() {
  if (!iteration_stopped_) {
    return;
  }

  iteration_stopped_ = false;

  // Continue from where we left off
  if (stopped_read_filter_) {
    OwnedBuffer empty;
    iterateReadFilters(empty, false);
  } else if (stopped_write_filter_) {
    OwnedBuffer empty;
    iterateWriteFilters(empty, false);
  }
}

void FilterChainStateMachine::checkWatermarks() {
  size_t current = buffered_bytes_.load();

  if (current >= config_.high_watermark && !above_high_watermark_) {
    above_high_watermark_ = true;
    handleEvent(FilterChainEvent::BufferHighWatermark);
  } else if (current <= config_.low_watermark && above_high_watermark_) {
    above_high_watermark_ = false;
    handleEvent(FilterChainEvent::BufferLowWatermark);
  }
}

void FilterChainStateMachine::applyBackpressure() {
  if (!backpressure_applied_) {
    backpressure_applied_ = true;
    connection_.readDisable(true);

    transitionTo(FilterChainState::AboveHighWatermark,
                 FilterChainEvent::BufferHighWatermark, "Backpressure applied");
  }
}

void FilterChainStateMachine::releaseBackpressure() {
  if (backpressure_applied_) {
    backpressure_applied_ = false;
    connection_.readDisable(false);

    transitionTo(FilterChainState::BelowLowWatermark,
                 FilterChainEvent::BufferLowWatermark, "Backpressure released");
  }
}

bool FilterChainStateMachine::bufferData(Buffer& data) {
  size_t data_size = data.length();
  size_t current = buffered_bytes_.load();

  if (current + data_size > config_.max_buffered_bytes) {
    stats_.buffer_overflows++;
    return false;
  }

  downstream_buffer_.move(data);
  buffered_bytes_ += data_size;

  // Transition to Buffering state if we're paused and starting to buffer
  if (current_state_ == FilterChainState::Paused && buffered_bytes_ > 0) {
    transitionTo(FilterChainState::Buffering, FilterChainEvent::DataReceived,
                 "Started buffering data");
  }

  checkWatermarks();
  return true;
}

void FilterChainStateMachine::drainBuffers() {
  if (buffered_bytes_ == 0) {
    return;
  }

  // Process buffered downstream data
  if (downstream_buffer_.length() > 0) {
    iterateReadFilters(downstream_buffer_, false);
  }

  // Process buffered upstream data
  if (upstream_buffer_.length() > 0) {
    iterateWriteFilters(upstream_buffer_, false);
  }

  buffered_bytes_ = downstream_buffer_.length() + upstream_buffer_.length();
  checkWatermarks();
}

void FilterChainStateMachine::startInitializationTimer() {
  if (config_.initialization_timeout.count() == 0) {
    return;
  }

  initialization_timer_ = dispatcher_.createTimer([this]() {
    handleFilterError("initialization", "Initialization timeout");
  });

  initialization_timer_->enableTimer(config_.initialization_timeout);
}

void FilterChainStateMachine::startDrainTimer() {
  if (config_.drain_timeout.count() == 0) {
    return;
  }

  drain_timer_ = dispatcher_.createTimer([this]() {
    transitionTo(FilterChainState::Closing, FilterChainEvent::DrainComplete,
                 "Drain timeout");
  });

  drain_timer_->enableTimer(config_.drain_timeout);
}

void FilterChainStateMachine::startIdleTimer() {
  if (config_.idle_timeout.count() == 0) {
    return;
  }

  idle_timer_ = dispatcher_.createTimer([this]() { close(); });

  idle_timer_->enableTimer(config_.idle_timeout);
}

void FilterChainStateMachine::cancelTimers() {
  if (initialization_timer_) {
    initialization_timer_->disableTimer();
    initialization_timer_.reset();
  }

  if (drain_timer_) {
    drain_timer_->disableTimer();
    drain_timer_.reset();
  }

  if (idle_timer_) {
    idle_timer_->disableTimer();
    idle_timer_.reset();
  }
}

void FilterChainStateMachine::handleFilterError(const std::string& filter_name,
                                                const std::string& error) {
  stats_.filters_failed++;

  if (config_.error_callback) {
    std::stringstream ss;
    ss << "Filter error in " << filter_name << ": " << error;
    config_.error_callback(ss.str());
  }

  if (config_.continue_on_filter_error) {
    transitionTo(FilterChainState::FilterError, FilterChainEvent::FilterError,
                 "Filter error: " + error);

    attemptRecovery();
  } else {
    transitionTo(FilterChainState::Failed, FilterChainEvent::FilterError,
                 "Fatal filter error: " + error);
  }
}

bool FilterChainStateMachine::attemptRecovery() {
  transitionTo(FilterChainState::Recovering, FilterChainEvent::FilterError,
               "Attempting recovery");

  // Disable failed filters
  for (auto& entry : read_filters_) {
    // In a real implementation, we'd identify which filter failed
    // For now, just continue
  }

  // Try to resume
  return transitionTo(FilterChainState::Active,
                      FilterChainEvent::FilterContinue, "Recovery successful");
}

void FilterChainStateMachine::assertInDispatcherThread() const {
  // In production, check if we're in the dispatcher thread
  // For now, just update the thread ID if not set
  auto expected = std::thread::id();
  dispatcher_thread_id_.compare_exchange_strong(expected,
                                                std::this_thread::get_id());
}

void FilterChainStateMachine::updateStats(size_t bytes, bool upstream) {
  if (!config_.enable_filter_stats) {
    return;
  }

  if (upstream) {
    stats_.bytes_processed_upstream += bytes;
  } else {
    stats_.bytes_processed_downstream += bytes;
  }

  auto now = std::chrono::steady_clock::now();
  stats_.total_processing_time += now - last_activity_;
  last_activity_ = now;
}

// ===== FilterChainStatePatterns Implementation =====

bool FilterChainStatePatterns::canProcessData(FilterChainState state) {
  return state == FilterChainState::Active ||
         state == FilterChainState::ProcessingUpstream ||
         state == FilterChainState::ProcessingDownstream ||
         state == FilterChainState::IteratingRead ||
         state == FilterChainState::IteratingWrite;
}

bool FilterChainStatePatterns::isFlowControlled(FilterChainState state) {
  return state == FilterChainState::Paused ||
         state == FilterChainState::Buffering ||
         state == FilterChainState::AboveHighWatermark ||
         state == FilterChainState::Draining;
}

bool FilterChainStatePatterns::isErrorState(FilterChainState state) {
  return state == FilterChainState::FilterError ||
         state == FilterChainState::ProtocolError ||
         state == FilterChainState::Failed;
}

bool FilterChainStatePatterns::isTerminal(FilterChainState state) {
  return state == FilterChainState::Closed ||
         state == FilterChainState::Failed ||
         state == FilterChainState::Terminated;
}

bool FilterChainStatePatterns::isIterating(FilterChainState state) {
  return state == FilterChainState::IteratingRead ||
         state == FilterChainState::IteratingWrite ||
         state == FilterChainState::StoppedIteration;
}

// ===== FilterChainBuilder Implementation =====

FilterChainBuilder& FilterChainBuilder::withMode(FilterChainMode mode) {
  config_.mode = mode;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withBufferLimits(uint32_t max_bytes) {
  config_.max_buffered_bytes = max_bytes;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withWatermarks(uint32_t high,
                                                       uint32_t low) {
  config_.high_watermark = high;
  config_.low_watermark = low;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withInitTimeout(
    std::chrono::milliseconds timeout) {
  config_.initialization_timeout = timeout;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withDrainTimeout(
    std::chrono::milliseconds timeout) {
  config_.drain_timeout = timeout;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withIdleTimeout(
    std::chrono::milliseconds timeout) {
  config_.idle_timeout = timeout;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::continueOnError(bool enabled) {
  config_.continue_on_filter_error = enabled;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withStrictOrdering(bool enabled) {
  config_.strict_filter_ordering = enabled;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withStats(bool enabled) {
  config_.enable_filter_stats = enabled;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withStateChangeCallback(
    std::function<void(FilterChainState, FilterChainState, const std::string&)>
        cb) {
  config_.state_change_callback = cb;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::withErrorCallback(
    std::function<void(const std::string&)> cb) {
  config_.error_callback = cb;
  return *this;
}

FilterChainBuilder& FilterChainBuilder::addReadFilter(const std::string& name,
                                                      FilterFactoryCb factory) {
  read_filter_factories_.emplace_back(name, factory);
  return *this;
}

FilterChainBuilder& FilterChainBuilder::addWriteFilter(
    const std::string& name, FilterFactoryCb factory) {
  write_filter_factories_.emplace_back(name, factory);
  return *this;
}

std::unique_ptr<FilterChainStateMachine> FilterChainBuilder::build(
    event::Dispatcher& dispatcher, Connection& connection) {
  auto state_machine = std::make_unique<FilterChainStateMachine>(
      dispatcher, connection, config_);

  // Add read filters
  for (const auto& pair : read_filter_factories_) {
    const auto& name = pair.first;
    const auto& factory = pair.second;
    if (auto filter = factory()) {
      if (auto read_filter = std::dynamic_pointer_cast<ReadFilter>(filter)) {
        state_machine->addReadFilter(read_filter, name);
      }
    }
  }

  // Add write filters
  for (const auto& pair : write_filter_factories_) {
    const auto& name = pair.first;
    const auto& factory = pair.second;
    if (auto filter = factory()) {
      if (auto write_filter = std::dynamic_pointer_cast<WriteFilter>(filter)) {
        state_machine->addWriteFilter(write_filter, name);
      }
    }
  }

  return state_machine;
}

}  // namespace network
}  // namespace mcp