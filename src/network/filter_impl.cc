#include "mcp/network/filter.h"
#include "mcp/network/connection.h"
#include "mcp/buffer.h"
#include <algorithm>
#include <list>

namespace mcp {
namespace network {

// FilterManagerImpl implementation

FilterManagerImpl::FilterManagerImpl(FilterManagerConnection& connection)
    : connection_(connection),
      current_read_filter_(read_filters_.end()),
      current_write_filter_(write_filters_.end()) {}

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

  for (auto& filter : read_filters_) {
    filter->initializeReadFilterCallbacks(*this);
  }
  
  for (auto it = write_filters_.rbegin(); it != write_filters_.rend(); ++it) {
    (*it)->initializeWriteFilterCallbacks(*this);
  }

  initialized_ = true;
  return true;
}

void FilterManagerImpl::onRead() {
  if (!initialized_) {
    return;
  }

  Buffer& buffer = connection_.readBuffer();
  bool end_stream = connection_.readHalfClosed();
  
  current_read_filter_ = read_filters_.begin();
  onContinueReading(buffer, end_stream);
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
  if (!initialized_) {
    return;
  }

  Buffer& buffer = connection_.writeBuffer();
  current_write_filter_ = write_filters_.begin();
  onContinueWriting(buffer, false);
}

void FilterManagerImpl::onContinueWriting(Buffer& buffer, bool end_stream) {
  if (current_write_filter_ == write_filters_.end()) {
    return;
  }

  if (buffered_write_data_) {
    buffer.move(*buffered_write_data_);
    buffered_write_data_.reset();
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
  if (!buffered_write_data_) {
    buffered_write_data_ = std::make_unique<OwnedBuffer>();
  }
  buffered_write_data_->move(data);
  buffered_write_end_stream_ = end_stream;
}

bool FilterManagerImpl::aboveWriteBufferHighWatermark() const {
  return connection_.writeBuffer().length() > 1024 * 1024; // 1MB default
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