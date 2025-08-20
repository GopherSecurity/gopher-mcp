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
  std::cerr << "[DEBUG] FilterManagerImpl::initializeReadFilters() called, initialized_=" << initialized_ << std::endl;
  if (initialized_) {
    std::cerr << "[DEBUG] Already initialized, returning true" << std::endl;
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
  std::cerr << "[DEBUG] FilterManager initialized successfully, filter count: " << read_filters_.size() << std::endl;
  
  // Start the filter chain - transitions to Active state
  if (state_machine_) {
    state_machine_->start();
  }
  return true;
}

void FilterManagerImpl::onRead() {
  std::cerr << "[DEBUG] FilterManagerImpl::onRead() called, initialized_=" << initialized_ << std::endl;
  if (!initialized_) {
    std::cerr << "[DEBUG] FilterManager not initialized, returning" << std::endl;
    return;
  }

  // Transition to Processing state when reading starts
  // if (state_machine_ && state_machine_->getCurrentState() == FilterChainState::Idle) {
  //   state_machine_->transition(FilterChainState::Processing);
  // }

  Buffer& buffer = connection_.readBuffer();
  bool end_stream = connection_.readHalfClosed();
  
  std::cerr << "[DEBUG] FilterManager buffer length: " << buffer.length() 
            << " end_stream: " << end_stream 
            << " read_filters count: " << read_filters_.size() << std::endl;
  
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

void FilterManagerImpl::onWrite() {
  std::cerr << "[DEBUG] FilterManagerImpl::onWrite() called, initialized_=" << initialized_ << std::endl;
  if (!initialized_) {
    std::cerr << "[DEBUG] FilterManager not initialized for writing, returning" << std::endl;
    return;
  }

  Buffer& buffer = connection_.writeBuffer();
  std::cerr << "[DEBUG] Write buffer length before processing: " << buffer.length() << std::endl;
  std::cerr << "[DEBUG] Buffered write data length: " 
            << (buffered_write_data_ ? buffered_write_data_->length() : 0) << std::endl;
  
  current_write_filter_ = write_filters_.begin();
  onContinueWriting(buffer, false);
  
  std::cerr << "[DEBUG] Write buffer length after processing: " << buffer.length() << std::endl;
}

void FilterManagerImpl::onContinueWriting(Buffer& buffer, bool end_stream) {
  std::cerr << "[DEBUG] FilterManagerImpl::onContinueWriting called" << std::endl;
  if (current_write_filter_ == write_filters_.end()) {
    std::cerr << "[DEBUG] No write filters to process, returning" << std::endl;
    return;
  }

  if (buffered_write_data_) {
    std::cerr << "[DEBUG] Moving " << buffered_write_data_->length() 
              << " bytes from buffered_write_data_ to write buffer" << std::endl;
    // CRITICAL: move() moves FROM source TO destination
    // We need to move FROM buffered_write_data_ TO buffer
    buffered_write_data_->move(buffer);
    buffered_write_data_.reset();
    std::cerr << "[DEBUG] Write buffer now has " << buffer.length() << " bytes" << std::endl;
  }

  std::vector<WriteFilterSharedPtr>::iterator entry = current_write_filter_;
  current_write_filter_ = write_filters_.end();

  for (; entry != write_filters_.end(); entry++) {
    FilterStatus status = (*entry)->onWrite(buffer, end_stream || buffered_write_end_stream_);
    if (status == FilterStatus::StopIteration) {
      break;
    }
  }

  current_write_filter_ = entry;
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
  std::cerr << "[DEBUG] FilterManagerImpl::injectWriteDataToFilterChain called with " 
            << data.length() << " bytes" << std::endl;
  
  if (!buffered_write_data_) {
    buffered_write_data_ = std::make_unique<OwnedBuffer>();
  }
  
  std::cerr << "[DEBUG] About to copy " << data.length() << " bytes to buffered_write_data_" << std::endl;
  // Use add() instead of move() to avoid buffer move issues
  if (data.length() > 0) {
    auto linear_data = data.linearize(data.length());
    buffered_write_data_->add(linear_data, data.length());
  }
  std::cerr << "[DEBUG] After copy: buffered_write_data_ has " << buffered_write_data_->length() << " bytes" << std::endl;
  buffered_write_end_stream_ = end_stream;
  
  std::cerr << "[DEBUG] Buffered write data, now calling onWrite() to process" << std::endl;
  // Trigger write processing to move buffered data to write buffer
  onWrite();
  
  // After processing through filters, trigger actual write to socket
  // This is critical - the filter chain processes data but doesn't actually write it
  if (connection_.writeBuffer().length() > 0) {
    std::cerr << "[DEBUG] Triggering connection write with " 
              << connection_.writeBuffer().length() << " bytes" << std::endl;
    // Cast to ConnectionImpl to access write method
    auto& conn = dynamic_cast<ConnectionImpl&>(connection_);
    conn.write(connection_.writeBuffer(), end_stream);
  }
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