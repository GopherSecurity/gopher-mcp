#ifndef MCP_NETWORK_FILTER_H
#define MCP_NETWORK_FILTER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"
#include "mcp/network/transport_socket.h"

namespace mcp {

// Forward declare event namespace types
namespace event {
class Dispatcher;
}

namespace network {

// Forward declarations
class Connection;
class FilterChainStateMachine;
enum class FilterChainState;
class ReadFilter;
class WriteFilter;
class Filter;
class FilterManager;
class ReadFilterCallbacks;
class WriteFilterCallbacks;
class FilterCallbacks;

using ReadFilterSharedPtr = std::shared_ptr<ReadFilter>;
using WriteFilterSharedPtr = std::shared_ptr<WriteFilter>;
using FilterSharedPtr = std::shared_ptr<Filter>;
using FilterFactoryCb = std::function<FilterSharedPtr()>;

/**
 * Filter status returned by filter operations
 */
enum class FilterStatus {
  Continue,      // Continue filter chain iteration
  StopIteration  // Stop iterating through filters
};

/**
 * Filter close source
 */
enum class FilterCloseSource {
  Local,  // Closed by local side
  Remote  // Closed by remote side
};

/**
 * Read filter callbacks interface
 *
 * Provided to read filters for interacting with the connection
 */
class ReadFilterCallbacks {
 public:
  virtual ~ReadFilterCallbacks() = default;

  /**
   * Get the connection associated with the filter
   */
  virtual Connection& connection() = 0;

  /**
   * Continue reading from the connection
   * Used after a filter has stopped iteration
   */
  virtual void continueReading() = 0;

  /**
   * Get the upstream host description (for client connections)
   */
  virtual const std::string& upstreamHost() const = 0;

  /**
   * Set the upstream host (for client connections)
   */
  virtual void setUpstreamHost(const std::string& host) = 0;

  /**
   * Check if we should continue filter iteration on new data
   */
  virtual bool shouldContinueFilterChain() = 0;
};

/**
 * Write filter callbacks interface
 */
class WriteFilterCallbacks {
 public:
  virtual ~WriteFilterCallbacks() = default;

  /**
   * Get the connection associated with the filter
   */
  virtual Connection& connection() = 0;

  /**
   * Inject data to be written before the current write
   */
  virtual void injectWriteDataToFilterChain(Buffer& data, bool end_stream) = 0;

  /**
   * Check if the write buffer is above the high watermark
   */
  virtual bool aboveWriteBufferHighWatermark() const = 0;
};

/**
 * Combined filter callbacks for filters that read and write
 */
class FilterCallbacks : public ReadFilterCallbacks,
                        public WriteFilterCallbacks {
 public:
  virtual ~FilterCallbacks() = default;
};

/**
 * Read filter interface
 *
 * Processes data read from the connection
 */
class ReadFilter {
 public:
  virtual ~ReadFilter() = default;

  /**
   * Called when new data is read from the connection
   *
   * @param data Buffer containing the read data
   * @param end_stream Whether this is the last data (half-close)
   * @return FilterStatus indicating whether to continue filter chain
   */
  virtual FilterStatus onData(Buffer& data, bool end_stream) = 0;

  /**
   * Called when a new connection is established
   * @return FilterStatus indicating whether to continue filter chain
   */
  virtual FilterStatus onNewConnection() = 0;

  /**
   * Initialize the filter with read callbacks
   */
  virtual void initializeReadFilterCallbacks(
      ReadFilterCallbacks& callbacks) = 0;
};

/**
 * Write filter interface
 *
 * Processes data before writing to the connection
 */
class WriteFilter {
 public:
  virtual ~WriteFilter() = default;

  /**
   * Called when data is about to be written to the connection
   *
   * @param data Buffer containing the data to write
   * @param end_stream Whether this is the last data (half-close)
   * @return FilterStatus indicating whether to continue filter chain
   */
  virtual FilterStatus onWrite(Buffer& data, bool end_stream) = 0;

  /**
   * Initialize the filter with write callbacks
   */
  virtual void initializeWriteFilterCallbacks(
      WriteFilterCallbacks& callbacks) = 0;
};

/**
 * Combined read/write filter interface
 */
class Filter : public ReadFilter, public WriteFilter {
 public:
  virtual ~Filter() = default;
};

/**
 * Network filter chain callbacks
 *
 * Used by filters to interact with the filter chain
 */
class FilterChainCallbacks {
 public:
  virtual ~FilterChainCallbacks() = default;

  /**
   * Add a filter to the read filter chain
   */
  virtual void addReadFilter(ReadFilterSharedPtr filter) = 0;

  /**
   * Add a filter to the write filter chain
   */
  virtual void addWriteFilter(WriteFilterSharedPtr filter) = 0;

  /**
   * Add a bidirectional filter
   */
  virtual void addFilter(FilterSharedPtr filter) = 0;
};

/**
 * Filter manager connection interface
 *
 * Connection methods exposed to the filter manager
 */
class FilterManagerConnection {
 public:
  virtual ~FilterManagerConnection() = default;

  /**
   * Get the read buffer
   */
  virtual Buffer& readBuffer() = 0;

  /**
   * Get the write buffer
   */
  virtual Buffer& writeBuffer() = 0;

  /**
   * Close the connection
   */
  virtual void close(ConnectionCloseType type) = 0;

  /**
   * Check if the connection is half-closed for reads
   */
  virtual bool readHalfClosed() const = 0;

  /**
   * Check if the connection is closed
   */
  virtual bool isClosed() const = 0;

  /**
   * Enable/disable reading
   * Note: For the full readDisable with return status, use
   * Connection::readDisable
   */
  virtual void readDisable(bool disable) = 0;

  /**
   * Check if reading is disabled
   */
  virtual bool readDisabled() const = 0;

  /**
   * Start secure transport
   */
  virtual bool startSecureTransport() = 0;

  /**
   * Get SSL connection info
   */
  virtual SslConnectionInfoConstSharedPtr ssl() const = 0;
};

/**
 * Filter manager interface
 *
 * Manages the filter chain for a connection
 */
class FilterManager {
 public:
  virtual ~FilterManager() = default;

  /**
   * Add a read filter
   */
  virtual void addReadFilter(ReadFilterSharedPtr filter) = 0;

  /**
   * Add a write filter
   */
  virtual void addWriteFilter(WriteFilterSharedPtr filter) = 0;

  /**
   * Add a bidirectional filter
   */
  virtual void addFilter(FilterSharedPtr filter) = 0;

  /**
   * Remove a read filter
   */
  virtual void removeReadFilter(ReadFilterSharedPtr filter) = 0;

  /**
   * Initialize read filters
   * @return true if all filters initialized successfully
   */
  virtual bool initializeReadFilters() = 0;

  /**
   * Called when new data is available
   */
  virtual void onRead() = 0;

  /**
   * Called when the socket is ready for writing
   */
  virtual void onWrite() = 0;

  /**
   * Called on connection event
   */
  virtual void onConnectionEvent(ConnectionEvent event) = 0;
};

/**
 * Filter manager implementation
 *
 * Manages filter chains and dispatches data through them
 */
class FilterManagerImpl : public FilterManager,
                          public ReadFilterCallbacks,
                          public WriteFilterCallbacks {
 public:
  FilterManagerImpl(FilterManagerConnection& connection, event::Dispatcher& dispatcher);
  ~FilterManagerImpl() override;

  // FilterManager interface
  void addReadFilter(ReadFilterSharedPtr filter) override;
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void removeReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;
  void onRead() override;
  void onWrite() override;
  void onConnectionEvent(ConnectionEvent event) override;

  // ReadFilterCallbacks interface
  Connection& connection() override;
  void continueReading() override;
  const std::string& upstreamHost() const override { return upstream_host_; }
  void setUpstreamHost(const std::string& host) override {
    upstream_host_ = host;
  }
  bool shouldContinueFilterChain() override;

  // WriteFilterCallbacks interface
  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) override;
  bool aboveWriteBufferHighWatermark() const override;

 private:
  // Helper methods
  FilterStatus onContinueReading(Buffer& buffer, bool end_stream);
  void onContinueWriting(Buffer& buffer, bool end_stream);
  void callOnConnectionEvent(ConnectionEvent event);
  
  // State machine integration
  void onStateChanged(FilterChainState old_state, FilterChainState new_state);
  void configureStateMachine();

  // Member variables
  FilterManagerConnection& connection_;
  event::Dispatcher* dispatcher_{nullptr};
  std::vector<ReadFilterSharedPtr> read_filters_;
  std::vector<WriteFilterSharedPtr> write_filters_;

  // Current filter being processed
  std::vector<ReadFilterSharedPtr>::iterator current_read_filter_;
  std::vector<WriteFilterSharedPtr>::iterator current_write_filter_;

  // State machine for managing filter chain lifecycle
  std::unique_ptr<FilterChainStateMachine> state_machine_;
  
  // State
  bool initialized_{false};
  bool upstream_filters_initialized_{false};
  std::string upstream_host_;

  // Buffered data for filter processing
  std::unique_ptr<Buffer> buffered_read_data_;
  bool buffered_read_end_stream_{false};
  std::unique_ptr<Buffer> buffered_write_data_;
  bool buffered_write_end_stream_{false};
};

/**
 * Base class for network filters
 *
 * Provides common functionality for filter implementations
 */
class NetworkFilterBase : public Filter, protected FilterCallbacks {
 public:
  // ReadFilter interface
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // WriteFilter interface
  void initializeWriteFilterCallbacks(
      WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  // FilterCallbacks interface (delegation)
  Connection& connection() override { return read_callbacks_->connection(); }

  void continueReading() override { read_callbacks_->continueReading(); }

  const std::string& upstreamHost() const override {
    return read_callbacks_->upstreamHost();
  }

  void setUpstreamHost(const std::string& host) override {
    read_callbacks_->setUpstreamHost(host);
  }

  bool shouldContinueFilterChain() override {
    return read_callbacks_->shouldContinueFilterChain();
  }

  void injectWriteDataToFilterChain(Buffer& data, bool end_stream) override {
    write_callbacks_->injectWriteDataToFilterChain(data, end_stream);
  }

  bool aboveWriteBufferHighWatermark() const override {
    return write_callbacks_->aboveWriteBufferHighWatermark();
  }

 protected:
  ReadFilterCallbacks* read_callbacks_{nullptr};
  WriteFilterCallbacks* write_callbacks_{nullptr};
};

/**
 * Factory for creating filter chains
 */
class FilterChainFactory {
 public:
  virtual ~FilterChainFactory() = default;

  /**
   * Create filter chain for a connection
   */
  virtual bool createFilterChain(FilterManager& filter_manager) const = 0;

  /**
   * Create network filter chain
   */
  virtual bool createNetworkFilterChain(
      FilterManager& filter_manager,
      const std::vector<FilterFactoryCb>& factories) const = 0;

  /**
   * Create listener filter chain
   */
  virtual bool createListenerFilterChain(
      FilterManager& filter_manager) const = 0;
};

/**
 * Default filter chain factory implementation
 */
class FilterChainFactoryImpl : public FilterChainFactory {
 public:
  FilterChainFactoryImpl() = default;

  bool createFilterChain(FilterManager& filter_manager) const override;
  bool createNetworkFilterChain(
      FilterManager& filter_manager,
      const std::vector<FilterFactoryCb>& factories) const override;
  bool createListenerFilterChain(FilterManager& filter_manager) const override;

  // Add filter factory
  void addFilterFactory(FilterFactoryCb factory) {
    filter_factories_.push_back(factory);
  }

 private:
  std::vector<FilterFactoryCb> filter_factories_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_FILTER_H