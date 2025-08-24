#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace stream_info {

// FilterStateImpl implementation

void FilterStateImpl::setData(const std::string& name,
                              ObjectSharedPtr object,
                              StateType state_type) {
  data_[name] = StoredObject{object, state_type};
}

const FilterState::Object* FilterStateImpl::getData(
    const std::string& name) const {
  auto it = data_.find(name);
  if (it == data_.end()) {
    return nullptr;
  }
  return it->second.object.get();
}

bool FilterStateImpl::hasData(const std::string& name) const {
  return data_.find(name) != data_.end();
}

// DynamicMetadataImpl implementation

void DynamicMetadataImpl::setMetadata(const std::string& name,
                                      const std::string& value) {
  metadata_[name] = value;
}

const std::string* DynamicMetadataImpl::getMetadata(
    const std::string& name) const {
  auto it = metadata_.find(name);
  if (it == metadata_.end()) {
    return nullptr;
  }
  return &it->second;
}

// StreamInfoImpl implementation

StreamInfoImpl::StreamInfoImpl()
    : start_time_(std::chrono::steady_clock::now()),
      start_time_monotonic_(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              start_time_.time_since_epoch())) {}

StreamInfoImpl::StreamInfoImpl(std::chrono::steady_clock::time_point start_time)
    : start_time_(start_time),
      start_time_monotonic_(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              start_time_.time_since_epoch())) {}

void StreamInfoImpl::setEndTime() {
  end_time_ = std::chrono::steady_clock::now();
}

optional<std::chrono::nanoseconds> StreamInfoImpl::duration() const {
  if (!end_time_.has_value()) {
    return nullopt;
  }

  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time_.value() - start_time_);
}

}  // namespace stream_info
}  // namespace mcp