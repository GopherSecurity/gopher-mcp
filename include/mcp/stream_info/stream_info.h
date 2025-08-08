#ifndef MCP_STREAM_INFO_STREAM_INFO_H
#define MCP_STREAM_INFO_STREAM_INFO_H

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/network/address.h"

namespace mcp {
namespace stream_info {

/**
 * Filter state interface
 *
 * Allows filters to store and share state within a request/connection context
 */
class FilterState {
 public:
  virtual ~FilterState() = default;

  /**
   * Object lifetime in the filter state
   */
  enum class StateType {
    ReadOnly,    // Object is read-only after initial set
    Mutable,     // Object can be modified
    Connection,  // Object lives for connection lifetime
    Request      // Object lives for request lifetime (HTTP)
  };

  /**
   * Base class for objects stored in filter state
   */
  class Object {
   public:
    virtual ~Object() = default;
  };

  using ObjectSharedPtr = std::shared_ptr<Object>;

  /**
   * Set data in the filter state
   */
  virtual void setData(const std::string& name,
                       ObjectSharedPtr object,
                       StateType state_type) = 0;

  /**
   * Get data from the filter state
   */
  virtual const Object* getData(const std::string& name) const = 0;

  /**
   * Check if data exists
   */
  virtual bool hasData(const std::string& name) const = 0;

  /**
   * Get data with type checking
   */
  template <typename T>
  const T* getDataTyped(const std::string& name) const {
    auto* obj = getData(name);
    return dynamic_cast<const T*>(obj);
  }
};

/**
 * Dynamic metadata interface
 *
 * Allows filters to set metadata that can be used for logging, stats, etc.
 */
class DynamicMetadata {
 public:
  virtual ~DynamicMetadata() = default;

  using MetadataMap = std::map<std::string, std::string>;

  /**
   * Set metadata value
   */
  virtual void setMetadata(const std::string& name,
                           const std::string& value) = 0;

  /**
   * Get metadata value
   */
  virtual const std::string* getMetadata(const std::string& name) const = 0;

  /**
   * Get all metadata
   */
  virtual const MetadataMap& getAllMetadata() const = 0;
};

/**
 * Response flags for tracking response properties
 */
struct ResponseFlags {
  // Connection failures
  bool failed_local_health_check{false};
  bool no_healthy_upstream{false};
  bool upstream_connection_failure{false};
  bool upstream_connection_termination{false};
  bool local_reset{false};
  bool upstream_remote_reset{false};
  bool upstream_connection_pool_overflow{false};
  bool no_route_found{false};

  // Timeout failures
  bool timeout{false};
  bool upstream_request_timeout{false};
  bool stream_idle_timeout{false};

  // Protocol errors
  bool downstream_protocol_error{false};
  bool upstream_protocol_error{false};
  bool upstream_max_stream_duration_reached{false};

  // Response properties
  bool response_from_cache{false};
  bool no_filter_config_found{false};
  bool unauthorized_request{false};
  bool rate_limited{false};
  bool downstream_connection_termination{false};
  bool upstream_retry_limit_exceeded{false};
  bool downstream_remote_reset{false};
};

/**
 * Stream info interface
 *
 * Provides information about a stream (connection or request)
 */
class StreamInfo {
 public:
  virtual ~StreamInfo() = default;

  /**
   * Get the start time of the stream
   */
  virtual std::chrono::steady_clock::time_point startTime() const = 0;

  /**
   * Get the monotonic start time
   */
  virtual std::chrono::nanoseconds startTimeMonotonic() const = 0;

  /**
   * Get the end time of the stream (if ended)
   */
  virtual optional<std::chrono::steady_clock::time_point> endTime() const = 0;

  /**
   * Set the end time
   */
  virtual void setEndTime() = 0;

  /**
   * Get the duration of the stream
   */
  virtual optional<std::chrono::nanoseconds> duration() const = 0;

  /**
   * Get/set the protocol
   */
  virtual optional<std::string> protocol() const = 0;
  virtual void setProtocol(const std::string& protocol) = 0;

  /**
   * Get response code (HTTP)
   */
  virtual optional<uint32_t> responseCode() const = 0;
  virtual void setResponseCode(uint32_t code) = 0;

  /**
   * Get response code details
   */
  virtual optional<std::string> responseCodeDetails() const = 0;
  virtual void setResponseCodeDetails(const std::string& details) = 0;

  /**
   * Get/set bytes sent
   */
  virtual uint64_t bytesSent() const = 0;
  virtual void setBytesSent(uint64_t bytes) = 0;

  /**
   * Get/set bytes received
   */
  virtual uint64_t bytesReceived() const = 0;
  virtual void setBytesReceived(uint64_t bytes) = 0;

  /**
   * Get response flags
   */
  virtual const ResponseFlags& responseFlags() const = 0;
  virtual ResponseFlags& responseFlags() = 0;

  /**
   * Get/set upstream info
   */
  virtual const network::Address::InstanceConstSharedPtr& upstreamAddress()
      const = 0;
  virtual void setUpstreamAddress(
      const network::Address::InstanceConstSharedPtr& address) = 0;

  /**
   * Get/set upstream cluster
   */
  virtual const std::string& upstreamCluster() const = 0;
  virtual void setUpstreamCluster(const std::string& cluster) = 0;

  /**
   * Get filter state
   */
  virtual FilterState& filterState() = 0;
  virtual const FilterState& filterState() const = 0;

  /**
   * Get dynamic metadata
   */
  virtual DynamicMetadata& dynamicMetadata() = 0;
  virtual const DynamicMetadata& dynamicMetadata() const = 0;

  /**
   * Set the connection ID
   */
  virtual void setConnectionID(uint64_t id) = 0;
  virtual optional<uint64_t> connectionID() const = 0;
};

using StreamInfoSharedPtr = std::shared_ptr<StreamInfo>;

}  // namespace stream_info
}  // namespace mcp

#endif  // MCP_STREAM_INFO_STREAM_INFO_H