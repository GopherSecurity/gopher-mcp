#include "mcp/network/filter.h"
#include "mcp/network/filter_chain_state_machine.h"
#include "mcp/network/connection.h"
#include "mcp/network/connection_impl.h"
#include "mcp/event/event_loop.h"
#include "mcp/buffer.h"
#include <algorithm>
#include <list>
#include <iostream>

namespace mcp {
namespace network {

// FilterManagerImpl implementation

FilterManagerImpl::FilterManagerImpl(FilterManagerConnection& connection,
                                     event::Dispatcher& dispatcher)
    : connection_(connection),
      dispatcher_(&dispatcher),
      current_read_filter_(read_filters_.end()),
      current_write_filter_(write_filters_.end()) {
  // Store the dispatcher for later use
  // We'll create the state machine lazily when initializeReadFilters is called
  // to ensure we're in the dispatcher thread
}

FilterManagerImpl::~FilterManagerImpl() = default;

void FilterManagerImpl::addReadFilter(ReadFilterSharedPtr filter) {
  read_filters_.push_back(filter);
}

void FilterManagerImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  write_filters_.push_back(filter);
}

void FilterManagerImpl::addFilter(FilterSharedPtr filter) {
  addReadFilter(filter);
  addWriteFilter(filter);
}

void FilterManagerImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  auto it = std::find(read_filters_.begin(), read_filters_.end(), filter);
  if (it != read_filters_.end()) {
    read_filters_.erase(it);
  }
}

bool FilterManagerImpl::initializeReadFilters() {
  if (initialized_) {
    return true;
  }

  // Create state machine now if we have a dispatcher and haven't created it yet
  // This ensures we're in the dispatcher thread when creating the state machine
  if (!state_machine_ && dispatcher_) {
    FilterChainConfig config;
    config.state_change_callback = [this](FilterChainState from, FilterChainState to, 
                                          const std::string& reason) {
      onStateChanged(from, to);
    };
    
    // Cast FilterManagerConnection to Connection
    Connection* conn = dynamic_cast<Connection*>(&connection_);
    if (conn) {
      state_machine_ = std::make_unique<FilterChainStateMachine>(*dispatcher_, *conn, config);
      configureStateMachine();
      state_machine_->initialize();
    }
  }

  for (auto& filter : read_filters_) {
    filter->initializeReadFilterCallbacks(*this);
    // Add filter to state machine
    if (state_machine_) {
      state_machine_->addReadFilter(filter, "read_filter");
    }
  }
  
  for (auto it = write_filters_.rbegin(); it != write_filters_.rend(); ++it) {
    (*it)->initializeWriteFilterCallbacks(*this);
    // Add filter to state machine
    if (state_machine_) {
      state_machine_->addWriteFilter(*it, "write_filter");
    }
  }

  initialized_ = true;
  
  // Start the filter chain - transitions to Active state
  if (state_machine_) {
    state_machine_->start();
  }
  return true;
}

void FilterManagerImpl::onRead() {
  if (!initialized_) {
    return;
  }

  // Transition to Processing state when reading starts
  // if (state_machine_ && state_machine_->getCurrentState() == FilterChainState::Idle) {
  //   state_machine_->transition(FilterChainState::Processing);
  // }

  Buffer& buffer = connection_.readBuffer();
  bool end_stream = connection_.readHalfClosed();
  
  current_read_filter_ = read_filters_.begin();
  onContinueReading(buffer, end_stream);
  
  // Return to Idle state after processing
  // if (state_machine_ && state_machine_->getCurrentState() == FilterChainState::Processing) {
  //   state_machine_->transition(FilterChainState::Idle);
  // }
}

FilterStatus FilterManagerImpl::onContinueReading(Buffer& buffer, bool end_stream) {
  if (current_read_filter_ == read_filters_.end()) {
    return FilterStatus::StopIteration;
  }

  if (buffered_read_data_) {
    buffer.move(*buffered_read_data_);
    buffered_read_data_.reset();
  }

  std::vector<ReadFilterSharedPtr>::iterator entry = current_read_filter_;
  current_read_filter_ = read_filters_.end();

  FilterStatus status = FilterStatus::Continue;
  for (; entry != read_filters_.end(); entry++) {
    status = (*entry)->onData(buffer, end_stream || buffered_read_end_stream_);
    if (status == FilterStatus::StopIteration || buffer.length() == 0) {
      break;
    }
  }

  current_read_filter_ = entry;

  if (end_stream && current_read_filter_ == read_filters_.end()) {
    connection_.close(ConnectionCloseType::FlushWrite);
  }

  return status;
}

FilterStatus FilterManagerImpl::onWrite() {
  if (!initialized_) {
    return FilterStatus::Continue;
  }

  // Get the current write buffer from connection
  // This follows the production pattern where connection temporarily stores
  // the buffer being processed during write() call
  Buffer* current_buffer = connection_.currentWriteBuffer();
  bool end_stream = connection_.currentWriteEndStream();
  
  if (!current_buffer) {
    return FilterStatus::Continue;
  }
  
  // Process through write filters
  current_write_filter_ = write_filters_.begin();
  FilterStatus status = onContinueWriting(*current_buffer, end_stream);
  
  
  return status;
}

FilterStatus FilterManagerImpl::onContinueWriting(Buffer& buffer, bool end_stream) {
  if (current_write_filter_ == write_filters_.end()) {
    return FilterStatus::Continue;
  }

  // No buffered data needed - we're processing the current write buffer directly
  
  std::vector<WriteFilterSharedPtr>::iterator entry = current_write_filter_;
  current_write_filter_ = write_filters_.end();

  FilterStatus result = FilterStatus::Continue;
  for (; entry != write_filters_.end(); entry++) {
    FilterStatus status = (*entry)->onWrite(buffer, end_stream);
    if (status == FilterStatus::StopIteration) {
      result = FilterStatus::StopIteration;
      break;
    }
  }

  current_write_filter_ = entry;
  return result;
}

void FilterManagerImpl::onConnectionEvent(ConnectionEvent event) {
  if (!initialized_) {
    return;
  }

  if (event == ConnectionEvent::Connected || event == ConnectionEvent::ConnectedZeroRtt) {
    upstream_filters_initialized_ = false;
    callOnConnectionEvent(event);
  } else if (event == ConnectionEvent::RemoteClose || event == ConnectionEvent::LocalClose) {
    callOnConnectionEvent(event);
  }
}

void FilterManagerImpl::callOnConnectionEvent(ConnectionEvent event) {
  for (auto& filter : read_filters_) {
    if (event == ConnectionEvent::Connected || event == ConnectionEvent::ConnectedZeroRtt) {
      filter->onNewConnection();
    }
  }
}

Connection& FilterManagerImpl::connection() {
  // The connection_ member is a FilterManagerConnection& which is actually a Connection&
  // since Connection inherits from FilterManagerConnection
  return dynamic_cast<Connection&>(connection_);
}

void FilterManagerImpl::continueReading() {
  Buffer& buffer = connection_.readBuffer();
  bool end_stream = connection_.readHalfClosed();
  onContinueReading(buffer, end_stream);
}

bool FilterManagerImpl::shouldContinueFilterChain() {
  return current_read_filter_ != read_filters_.end();
}

void FilterManagerImpl::injectWriteDataToFilterChain(Buffer& data, bool end_stream) {
  // This method is deprecated in favor of the production pattern
  // where connection sets current_write_buffer_ and calls onWrite()
  // Keeping stub for interface compatibility
  std::cerr << "[DEBUG] injectWriteDataToFilterChain called (deprecated)" << std::endl;
}

bool FilterManagerImpl::aboveWriteBufferHighWatermark() const {
  return connection_.writeBuffer().length() > 1024 * 1024; // 1MB default
}

void FilterManagerImpl::onStateChanged(FilterChainState old_state, FilterChainState new_state) {
  // Handle state transitions
  // This method will be called by the state machine on state changes
  
  // Log state transitions for debugging
  // TODO: Add proper logging
  // CONN_LOG(debug, "Filter chain state transition: {} -> {}", 
  //               connection_, FilterChainStateMachine::getStateName(old_state),
  //               FilterChainStateMachine::getStateName(new_state));
  
  // Handle specific state transitions
  switch (new_state) {
    case FilterChainState::Active:
      // Filter chain is now active and processing
      break;
      
    case FilterChainState::Paused:
      // Filter chain is paused, possibly due to backpressure
      break;
      
    case FilterChainState::Failed:
      // Handle error state
      // connection_.close(ConnectionCloseType::NoFlush);
      break;
      
    case FilterChainState::Aborting:
      // Clean up resources after abort
      break;
      
    default:
      break;
  }
}

void FilterManagerImpl::configureStateMachine() {
  // Configure state machine behavior
  // This would be called during initialization
  
  // Set up entry/exit actions for states if needed
  // state_machine_->setEntryAction(FilterChainState::Active, [this]() {
  //   // Actions when entering Active state
  // });
  
  // state_machine_->setExitAction(FilterChainState::Processing, [this]() {
  //   // Actions when exiting Processing state
  // });
}

// FilterChainFactoryImpl implementation

bool FilterChainFactoryImpl::createFilterChain(FilterManager& filter_manager) const {
  return createNetworkFilterChain(filter_manager, filter_factories_);
}

bool FilterChainFactoryImpl::createNetworkFilterChain(
    FilterManager& filter_manager,
    const std::vector<FilterFactoryCb>& factories) const {
  for (const auto& factory : factories) {
    FilterSharedPtr filter = factory();
    if (!filter) {
      return false;
    }
    filter_manager.addFilter(filter);
  }
  return true;
}

bool FilterChainFactoryImpl::createListenerFilterChain(FilterManager& filter_manager) const {
  // Listener filters would be added here
  (void)filter_manager; // Suppress unused parameter warning
  return true;
}

} // namespace network
} // namespace mcp