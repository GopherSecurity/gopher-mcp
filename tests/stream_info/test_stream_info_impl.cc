#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/stream_info/stream_info_impl.h"

namespace mcp {
namespace stream_info {
namespace {

class StreamInfoImplTest : public ::testing::Test {
 protected:
  void SetUp() override { stream_info_ = StreamInfoImpl::create(); }

  StreamInfoImpl::SharedPtr stream_info_;
};

TEST_F(StreamInfoImplTest, TimingOperations) {
  // Test start time
  auto start = stream_info_->startTime();
  auto start_mono = stream_info_->startTimeMonotonic();

  EXPECT_GT(start.time_since_epoch().count(), 0);
  EXPECT_GT(start_mono.count(), 0);

  // End time should not be set initially
  EXPECT_FALSE(stream_info_->endTime().has_value());
  EXPECT_FALSE(stream_info_->duration().has_value());

  // Sleep briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Set end time
  stream_info_->setEndTime();

  // Now end time and duration should be available
  EXPECT_TRUE(stream_info_->endTime().has_value());
  EXPECT_TRUE(stream_info_->duration().has_value());

  // Duration should be positive
  EXPECT_GT(stream_info_->duration()->count(), 0);
}

TEST_F(StreamInfoImplTest, ProtocolInformation) {
  // Initially no protocol
  EXPECT_FALSE(stream_info_->protocol().has_value());

  // Set protocol
  stream_info_->setProtocol("HTTP/1.1");
  EXPECT_TRUE(stream_info_->protocol().has_value());
  EXPECT_EQ("HTTP/1.1", stream_info_->protocol().value());

  // Update protocol
  stream_info_->setProtocol("HTTP/2");
  EXPECT_EQ("HTTP/2", stream_info_->protocol().value());
}

TEST_F(StreamInfoImplTest, ResponseCodeHandling) {
  // Initially no response code
  EXPECT_FALSE(stream_info_->responseCode().has_value());
  EXPECT_FALSE(stream_info_->responseCodeDetails().has_value());

  // Set response code
  stream_info_->setResponseCode(200);
  EXPECT_TRUE(stream_info_->responseCode().has_value());
  EXPECT_EQ(200, stream_info_->responseCode().value());

  // Set response code details
  stream_info_->setResponseCodeDetails("OK");
  EXPECT_TRUE(stream_info_->responseCodeDetails().has_value());
  EXPECT_EQ("OK", stream_info_->responseCodeDetails().value());

  // Update values
  stream_info_->setResponseCode(404);
  stream_info_->setResponseCodeDetails("Not Found");
  EXPECT_EQ(404, stream_info_->responseCode().value());
  EXPECT_EQ("Not Found", stream_info_->responseCodeDetails().value());
}

TEST_F(StreamInfoImplTest, ByteCounters) {
  // Initially zero
  EXPECT_EQ(0, stream_info_->bytesSent());
  EXPECT_EQ(0, stream_info_->bytesReceived());

  // Set bytes sent
  stream_info_->setBytesSent(1024);
  EXPECT_EQ(1024, stream_info_->bytesSent());

  // Set bytes received
  stream_info_->setBytesReceived(2048);
  EXPECT_EQ(2048, stream_info_->bytesReceived());

  // Update values
  stream_info_->setBytesSent(3072);
  stream_info_->setBytesReceived(4096);
  EXPECT_EQ(3072, stream_info_->bytesSent());
  EXPECT_EQ(4096, stream_info_->bytesReceived());
}

TEST_F(StreamInfoImplTest, ResponseFlags) {
  auto& flags = stream_info_->responseFlags();

  // Initially all false
  EXPECT_FALSE(flags.failed_local_health_check);
  EXPECT_FALSE(flags.no_healthy_upstream);
  EXPECT_FALSE(flags.upstream_request_timeout);
  EXPECT_FALSE(flags.local_reset);
  EXPECT_FALSE(flags.upstream_remote_reset);
  EXPECT_FALSE(flags.upstream_connection_failure);
  EXPECT_FALSE(flags.upstream_connection_termination);
  EXPECT_FALSE(flags.upstream_connection_pool_overflow);
  EXPECT_FALSE(flags.no_route_found);
  EXPECT_FALSE(flags.timeout);
  EXPECT_FALSE(flags.upstream_request_timeout);
  EXPECT_FALSE(flags.stream_idle_timeout);

  // Set some flags
  flags.failed_local_health_check = true;
  flags.upstream_request_timeout = true;
  flags.rate_limited = true;

  // Verify changes
  EXPECT_TRUE(stream_info_->responseFlags().failed_local_health_check);
  EXPECT_TRUE(stream_info_->responseFlags().upstream_request_timeout);
  EXPECT_TRUE(stream_info_->responseFlags().rate_limited);
}

TEST_F(StreamInfoImplTest, ConnectionId) {
  // Initially no connection ID
  EXPECT_FALSE(stream_info_->connectionID().has_value());

  // Set connection ID
  stream_info_->setConnectionID(12345);
  EXPECT_TRUE(stream_info_->connectionID().has_value());
  EXPECT_EQ(12345, stream_info_->connectionID().value());

  // Update connection ID
  stream_info_->setConnectionID(67890);
  EXPECT_EQ(67890, stream_info_->connectionID().value());
}

TEST_F(StreamInfoImplTest, UpstreamCluster) {
  // Initially empty
  EXPECT_TRUE(stream_info_->upstreamCluster().empty());

  // Set cluster
  stream_info_->setUpstreamCluster("backend-cluster");
  EXPECT_EQ("backend-cluster", stream_info_->upstreamCluster());

  // Update cluster
  stream_info_->setUpstreamCluster("another-cluster");
  EXPECT_EQ("another-cluster", stream_info_->upstreamCluster());
}

// FilterStateImpl tests

class FilterStateImplTest : public ::testing::Test {
 protected:
  FilterStateImpl filter_state_;
};

class TestFilterStateObject : public FilterState::Object {
 public:
  explicit TestFilterStateObject(int value) : value_(value) {}
  int getValue() const { return value_; }

 private:
  int value_;
};

TEST_F(FilterStateImplTest, SetAndGetData) {
  // Initially no data
  EXPECT_FALSE(filter_state_.hasData("test"));
  EXPECT_EQ(nullptr, filter_state_.getData("test"));

  // Set data
  auto obj = std::make_shared<TestFilterStateObject>(42);
  filter_state_.setData("test", obj, FilterState::StateType::ReadOnly);

  // Verify data exists
  EXPECT_TRUE(filter_state_.hasData("test"));

  // Get data
  const auto* retrieved = filter_state_.getData("test");
  EXPECT_NE(nullptr, retrieved);

  // Cast and verify value
  const auto* test_obj = dynamic_cast<const TestFilterStateObject*>(retrieved);
  EXPECT_NE(nullptr, test_obj);
  EXPECT_EQ(42, test_obj->getValue());
}

TEST_F(FilterStateImplTest, MultipleObjects) {
  // Set multiple objects
  auto obj1 = std::make_shared<TestFilterStateObject>(10);
  auto obj2 = std::make_shared<TestFilterStateObject>(20);
  auto obj3 = std::make_shared<TestFilterStateObject>(30);

  filter_state_.setData("obj1", obj1, FilterState::StateType::ReadOnly);
  filter_state_.setData("obj2", obj2, FilterState::StateType::Mutable);
  filter_state_.setData("obj3", obj3, FilterState::StateType::ReadOnly);

  // Verify all exist
  EXPECT_TRUE(filter_state_.hasData("obj1"));
  EXPECT_TRUE(filter_state_.hasData("obj2"));
  EXPECT_TRUE(filter_state_.hasData("obj3"));

  // Verify values
  auto* retrieved1 =
      dynamic_cast<const TestFilterStateObject*>(filter_state_.getData("obj1"));
  auto* retrieved2 =
      dynamic_cast<const TestFilterStateObject*>(filter_state_.getData("obj2"));
  auto* retrieved3 =
      dynamic_cast<const TestFilterStateObject*>(filter_state_.getData("obj3"));

  EXPECT_EQ(10, retrieved1->getValue());
  EXPECT_EQ(20, retrieved2->getValue());
  EXPECT_EQ(30, retrieved3->getValue());
}

TEST_F(FilterStateImplTest, OverwriteData) {
  // Set initial data
  auto obj1 = std::make_shared<TestFilterStateObject>(100);
  filter_state_.setData("key", obj1, FilterState::StateType::ReadOnly);

  // Verify initial value
  auto* retrieved1 =
      dynamic_cast<const TestFilterStateObject*>(filter_state_.getData("key"));
  EXPECT_EQ(100, retrieved1->getValue());

  // Overwrite with new data
  auto obj2 = std::make_shared<TestFilterStateObject>(200);
  filter_state_.setData("key", obj2, FilterState::StateType::Mutable);

  // Verify new value
  auto* retrieved2 =
      dynamic_cast<const TestFilterStateObject*>(filter_state_.getData("key"));
  EXPECT_EQ(200, retrieved2->getValue());
}

// DynamicMetadataImpl tests

class DynamicMetadataImplTest : public ::testing::Test {
 protected:
  DynamicMetadataImpl metadata_;
};

TEST_F(DynamicMetadataImplTest, SetAndGetMetadata) {
  // Initially no metadata
  EXPECT_EQ(nullptr, metadata_.getMetadata("key"));

  // Set metadata
  metadata_.setMetadata("key", "value");

  // Get metadata
  const std::string* value = metadata_.getMetadata("key");
  EXPECT_NE(nullptr, value);
  EXPECT_EQ("value", *value);
}

TEST_F(DynamicMetadataImplTest, MultipleMetadata) {
  // Set multiple metadata entries
  metadata_.setMetadata("key1", "value1");
  metadata_.setMetadata("key2", "value2");
  metadata_.setMetadata("key3", "value3");

  // Verify all entries
  EXPECT_EQ("value1", *metadata_.getMetadata("key1"));
  EXPECT_EQ("value2", *metadata_.getMetadata("key2"));
  EXPECT_EQ("value3", *metadata_.getMetadata("key3"));

  // Non-existent key
  EXPECT_EQ(nullptr, metadata_.getMetadata("nonexistent"));
}

TEST_F(DynamicMetadataImplTest, OverwriteMetadata) {
  // Set initial value
  metadata_.setMetadata("key", "initial");
  EXPECT_EQ("initial", *metadata_.getMetadata("key"));

  // Overwrite
  metadata_.setMetadata("key", "updated");
  EXPECT_EQ("updated", *metadata_.getMetadata("key"));
}

TEST_F(DynamicMetadataImplTest, GetAllMetadata) {
  // Set multiple entries
  metadata_.setMetadata("a", "1");
  metadata_.setMetadata("b", "2");
  metadata_.setMetadata("c", "3");

  // Get all metadata
  const auto& all = metadata_.getAllMetadata();
  EXPECT_EQ(3, all.size());

  // Verify contents
  EXPECT_EQ("1", all.at("a"));
  EXPECT_EQ("2", all.at("b"));
  EXPECT_EQ("3", all.at("c"));
}

// Integration test with StreamInfoImpl

TEST_F(StreamInfoImplTest, FilterStateIntegration) {
  // Get filter state
  auto& filter_state = stream_info_->filterState();

  // Add some state
  auto obj = std::make_shared<TestFilterStateObject>(999);
  filter_state.setData("request_id", obj, FilterState::StateType::ReadOnly);

  // Verify through stream info
  EXPECT_TRUE(stream_info_->filterState().hasData("request_id"));
  auto* retrieved = dynamic_cast<const TestFilterStateObject*>(
      stream_info_->filterState().getData("request_id"));
  EXPECT_EQ(999, retrieved->getValue());
}

TEST_F(StreamInfoImplTest, DynamicMetadataIntegration) {
  // Get dynamic metadata
  auto& metadata = stream_info_->dynamicMetadata();

  // Add metadata
  metadata.setMetadata("trace_id", "abc123");
  metadata.setMetadata("request_path", "/api/v1/users");

  // Verify through stream info
  EXPECT_EQ("abc123", *stream_info_->dynamicMetadata().getMetadata("trace_id"));
  EXPECT_EQ("/api/v1/users",
            *stream_info_->dynamicMetadata().getMetadata("request_path"));
}

}  // namespace
}  // namespace stream_info
}  // namespace mcp