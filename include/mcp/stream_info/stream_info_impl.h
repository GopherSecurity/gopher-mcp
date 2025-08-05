#ifndef MCP_STREAM_INFO_STREAM_INFO_IMPL_H
#define MCP_STREAM_INFO_STREAM_INFO_IMPL_H

#include <map>
#include <memory>

#include "mcp/stream_info/stream_info.h"

namespace mcp {
namespace stream_info {

/**
 * Filter state implementation
 */
class FilterStateImpl : public FilterState {
 public:
  FilterStateImpl() = default;

  // FilterState interface
  void setData(const std::string& name,
               ObjectSharedPtr object,
               StateType state_type) override;
  const Object* getData(const std::string& name) const override;
  bool hasData(const std::string& name) const override;

 private:
  struct StoredObject {
    ObjectSharedPtr object;
    StateType state_type;
  };

  std::map<std::string, StoredObject> data_;
};

/**
 * Dynamic metadata implementation
 */
class DynamicMetadataImpl : public DynamicMetadata {
 public:
  DynamicMetadataImpl() = default;

  // DynamicMetadata interface
  void setMetadata(const std::string& name, const std::string& value) override;
  const std::string* getMetadata(const std::string& name) const override;
  const MetadataMap& getAllMetadata() const override { return metadata_; }

 private:
  MetadataMap metadata_;
};

/**
 * Stream info implementation
 */
class StreamInfoImpl : public StreamInfo {
 public:
  using SharedPtr = std::shared_ptr<StreamInfoImpl>;

  StreamInfoImpl();
  explicit StreamInfoImpl(std::chrono::steady_clock::time_point start_time);

  // StreamInfo interface
  std::chrono::steady_clock::time_point startTime() const override {
    return start_time_;
  }
  std::chrono::nanoseconds startTimeMonotonic() const override {
    return start_time_monotonic_;
  }
  optional<std::chrono::steady_clock::time_point> endTime() const override {
    return end_time_;
  }
  void setEndTime() override;
  optional<std::chrono::nanoseconds> duration() const override;

  optional<std::string> protocol() const override { return protocol_; }
  void setProtocol(const std::string& protocol) override {
    protocol_ = protocol;
  }

  optional<uint32_t> responseCode() const override { return response_code_; }
  void setResponseCode(uint32_t code) override { response_code_ = code; }

  optional<std::string> responseCodeDetails() const override {
    return response_code_details_;
  }
  void setResponseCodeDetails(const std::string& details) override {
    response_code_details_ = details;
  }

  uint64_t bytesSent() const override { return bytes_sent_; }
  void setBytesSent(uint64_t bytes) override { bytes_sent_ = bytes; }

  uint64_t bytesReceived() const override { return bytes_received_; }
  void setBytesReceived(uint64_t bytes) override { bytes_received_ = bytes; }

  const ResponseFlags& responseFlags() const override {
    return response_flags_;
  }
  ResponseFlags& responseFlags() override { return response_flags_; }

  const network::Address::InstanceConstSharedPtr& upstreamAddress()
      const override {
    return upstream_address_;
  }
  void setUpstreamAddress(
      const network::Address::InstanceConstSharedPtr& address) override {
    upstream_address_ = address;
  }

  const std::string& upstreamCluster() const override {
    return upstream_cluster_;
  }
  void setUpstreamCluster(const std::string& cluster) override {
    upstream_cluster_ = cluster;
  }

  FilterState& filterState() override { return filter_state_; }
  const FilterState& filterState() const override { return filter_state_; }

  DynamicMetadata& dynamicMetadata() override { return dynamic_metadata_; }
  const DynamicMetadata& dynamicMetadata() const override {
    return dynamic_metadata_;
  }

  void setConnectionID(uint64_t id) override { connection_id_ = id; }
  optional<uint64_t> connectionID() const override { return connection_id_; }

  // Create a shared instance
  static SharedPtr create() { return std::make_shared<StreamInfoImpl>(); }

  static SharedPtr create(std::chrono::steady_clock::time_point start_time) {
    return std::make_shared<StreamInfoImpl>(start_time);
  }

 private:
  // Timing
  const std::chrono::steady_clock::time_point start_time_;
  const std::chrono::nanoseconds start_time_monotonic_;
  optional<std::chrono::steady_clock::time_point> end_time_;

  // Protocol info
  optional<std::string> protocol_;
  optional<uint32_t> response_code_;
  optional<std::string> response_code_details_;

  // Stats
  uint64_t bytes_sent_{0};
  uint64_t bytes_received_{0};

  // Response properties
  ResponseFlags response_flags_;

  // Upstream info
  network::Address::InstanceConstSharedPtr upstream_address_;
  std::string upstream_cluster_;

  // Filter state and metadata
  FilterStateImpl filter_state_;
  DynamicMetadataImpl dynamic_metadata_;

  // Connection info
  optional<uint64_t> connection_id_;
};

}  // namespace stream_info
}  // namespace mcp

#endif  // MCP_STREAM_INFO_STREAM_INFO_IMPL_H