/**
 * Unit tests for MCP JSON-RPC Protocol Filter
 *
 * Tests the JSON-RPC message parsing, encoding, and protocol handling
 * using real I/O for more realistic testing
 */

#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/json/json_serialization.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/socket_impl.h"

#include "../integration/real_io_test_base.h"

namespace mcp {
namespace filter {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

/**
 * Mock callbacks for JSON-RPC filter
 */
class MockJsonRpcCallbacks : public JsonRpcProtocolFilter::MessageHandler {
 public:
  MOCK_METHOD(void, onRequest, (const jsonrpc::Request&), (override));
  MOCK_METHOD(void, onNotification, (const jsonrpc::Notification&), (override));
  MOCK_METHOD(void, onResponse, (const jsonrpc::Response&), (override));
  MOCK_METHOD(void, onProtocolError, (const Error&), (override));
};

/**
 * Test fixture for JsonRpcProtocolFilter using real I/O
 */
class JsonRpcProtocolFilterTest : public test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();

    callbacks_ = std::make_unique<NiceMock<MockJsonRpcCallbacks>>();

    // Create filter in dispatcher thread
    executeInDispatcher([this]() {
      filter_ = std::make_unique<JsonRpcProtocolFilter>(
          *callbacks_, *dispatcher_,
          false);  // client mode by default
    });
  }

  void TearDown() override {
    executeInDispatcher([this]() { filter_.reset(); });
    callbacks_.reset();

    RealIoTestBase::TearDown();
  }

  // Helper to process data through filter
  network::FilterStatus processData(const std::string& data,
                                    bool end_stream = false) {
    return executeInDispatcher([this, data, end_stream]() {
      OwnedBuffer buffer;
      buffer.add(data);
      return filter_->onData(buffer, end_stream);
    });
  }

  // Helper to test encoder
  void testEncoder(
      std::function<void(JsonRpcProtocolFilter::Encoder&)> test_func) {
    executeInDispatcher([this, test_func]() { test_func(filter_->encoder()); });
  }

  std::unique_ptr<MockJsonRpcCallbacks> callbacks_;
  std::unique_ptr<JsonRpcProtocolFilter> filter_;
};

/**
 * Test parsing JSON-RPC request
 */
TEST_F(JsonRpcProtocolFilterTest, ParseRequest) {
  // Expect onRequest to be called
  EXPECT_CALL(*callbacks_, onRequest(_))
      .WillOnce([](const jsonrpc::Request& req) {
        EXPECT_EQ("test.method", req.method);
        EXPECT_TRUE(holds_alternative<int64_t>(req.id));
        EXPECT_EQ(1, get<int64_t>(req.id));
      });

  // Create JSON-RPC request
  std::string json_str =
      R"({"jsonrpc":"2.0","id":1,"method":"test.method","params":{"key":"value"}})";
  json_str += "\n";  // Add delimiter for non-framed mode

  auto status = processData(json_str);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test parsing JSON-RPC notification
 */
TEST_F(JsonRpcProtocolFilterTest, ParseNotification) {
  // Create filter in server mode
  executeInDispatcher([this]() {
    filter_ = std::make_unique<JsonRpcProtocolFilter>(*callbacks_, *dispatcher_,
                                                      true);  // server mode
  });

  // Expect onNotification to be called
  EXPECT_CALL(*callbacks_, onNotification(_))
      .WillOnce([](const jsonrpc::Notification& notif) {
        EXPECT_EQ("notify.method", notif.method);
        EXPECT_TRUE(notif.params.has_value());
      });

  std::string json_str =
      R"({"jsonrpc":"2.0","method":"notify.method","params":{"data":"test"}})";
  json_str += "\n";

  auto status = processData(json_str);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test parsing JSON-RPC response
 */
TEST_F(JsonRpcProtocolFilterTest, ParseResponse) {
  // Expect onResponse to be called
  EXPECT_CALL(*callbacks_, onResponse(_))
      .WillOnce([](const jsonrpc::Response& resp) {
        EXPECT_TRUE(holds_alternative<int64_t>(resp.id));
        EXPECT_EQ(42, get<int64_t>(resp.id));
        EXPECT_TRUE(resp.result.has_value());
      });

  std::string json_str =
      R"({"jsonrpc":"2.0","id":42,"result":{"success":true}})";
  json_str += "\n";

  auto status = processData(json_str);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test parsing error response
 */
TEST_F(JsonRpcProtocolFilterTest, ParseErrorResponse) {
  // Expect onResponse to be called with error
  EXPECT_CALL(*callbacks_, onResponse(_))
      .WillOnce([](const jsonrpc::Response& resp) {
        EXPECT_TRUE(resp.error.has_value());
        EXPECT_EQ(-32600, resp.error->code);
        EXPECT_EQ("Invalid Request", resp.error->message);
      });

  std::string json_str =
      R"({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"Invalid Request"}})";
  json_str += "\n";

  auto status = processData(json_str);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test handling invalid JSON
 */
TEST_F(JsonRpcProtocolFilterTest, InvalidJson) {
  // Expect protocol error to be called
  EXPECT_CALL(*callbacks_, onProtocolError(_)).WillOnce([](const Error& error) {
    EXPECT_EQ(jsonrpc::PARSE_ERROR, error.code);
  });

  std::string json_str = "{ invalid json }";
  json_str += "\n";

  auto status = processData(json_str);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test message framing mode
 */
TEST_F(JsonRpcProtocolFilterTest, MessageFraming) {
  executeInDispatcher([this]() {
    filter_->setUseFraming(true);  // Enable framing
  });

  // Expect request to be parsed
  EXPECT_CALL(*callbacks_, onRequest(_)).Times(1);

  // Create framed message (4-byte length prefix in big-endian)
  std::string json_str = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
  uint32_t length = htonl(json_str.length());  // Convert to big-endian

  std::string framed_data;
  framed_data.append(reinterpret_cast<char*>(&length), 4);
  framed_data.append(json_str);

  auto status = processData(framed_data);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test partial message handling
 */
TEST_F(JsonRpcProtocolFilterTest, PartialMessage) {
  // Expect one complete request
  EXPECT_CALL(*callbacks_, onRequest(_)).Times(1);

  std::string json_str =
      R"({"jsonrpc":"2.0","id":1,"method":"test","params":{}})";

  // Send first half
  auto status1 = processData(json_str.substr(0, json_str.length() / 2));
  EXPECT_EQ(network::FilterStatus::Continue, status1);

  // Send second half with delimiter
  std::string second_half = json_str.substr(json_str.length() / 2);
  second_half += "\n";
  auto status2 = processData(second_half);
  EXPECT_EQ(network::FilterStatus::Continue, status2);
}

/**
 * Test multiple messages in one buffer
 */
TEST_F(JsonRpcProtocolFilterTest, MultipleMessages) {
  // Create filter in server mode
  executeInDispatcher([this]() {
    filter_ = std::make_unique<JsonRpcProtocolFilter>(*callbacks_, *dispatcher_,
                                                      true);  // server mode
  });

  // Expect two requests
  InSequence seq;
  EXPECT_CALL(*callbacks_, onRequest(_))
      .WillOnce([](const jsonrpc::Request& req) {
        EXPECT_EQ("method1", req.method);
      });
  EXPECT_CALL(*callbacks_, onRequest(_))
      .WillOnce([](const jsonrpc::Request& req) {
        EXPECT_EQ("method2", req.method);
      });

  std::string json1 = R"({"jsonrpc":"2.0","id":1,"method":"method1"})";
  std::string json2 = R"({"jsonrpc":"2.0","id":2,"method":"method2"})";

  std::string combined = json1 + "\n" + json2 + "\n";
  auto status = processData(combined);
  EXPECT_EQ(network::FilterStatus::Continue, status);
}

/**
 * Test onNewConnection resets state
 */
TEST_F(JsonRpcProtocolFilterTest, NewConnectionResetsState) {
  // Send partial message
  processData(R"({"jsonrpc":"2.0","id":1,)");

  // New connection should reset state
  auto status =
      executeInDispatcher([this]() { return filter_->onNewConnection(); });
  EXPECT_EQ(network::FilterStatus::Continue, status);

  // Now a complete different message should work
  EXPECT_CALL(*callbacks_, onRequest(_)).Times(1);

  processData(R"({"jsonrpc":"2.0","id":2,"method":"test"})"
              "\n");
}

/**
 * Test write filter adds framing
 */
TEST_F(JsonRpcProtocolFilterTest, WriteFilterAddsFraming) {
  executeInDispatcher([this]() {
    filter_->setUseFraming(true);  // Enable framing
  });

  executeInDispatcher([this]() {
    OwnedBuffer buffer;
    std::string data = "test message";
    buffer.add(data);

    // Process write - should add framing
    auto status = filter_->onWrite(buffer, false);
    EXPECT_EQ(network::FilterStatus::Continue, status);

    // Buffer should now have length prefix
    EXPECT_GT(buffer.length(), data.length());

    // Check that first 4 bytes are the length
    uint32_t length;
    buffer.copyOut(0, 4, &length);
    length = ntohl(length);  // Convert from big-endian
    EXPECT_EQ(data.length(), length);
  });
}

/**
 * Integration test with real connection
 * NOTE: Disabled due to issues with real I/O test infrastructure
 */
#if 0
class JsonRpcProtocolFilterIntegrationTest : public test::RealIoTestBase,
                                         public network::ConnectionCallbacks {
protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    
    // Create connections in dispatcher thread
    executeInDispatcher([this]() {
      // Create socket pair for testing
      auto socket_pair = createSocketPair();
      client_handle_ = std::move(socket_pair.first);
      server_handle_ = std::move(socket_pair.second);
      
      setupConnections();
    });
  }
  
  void TearDown() override {
    executeInDispatcher([this]() {
      if (client_connection_) {
        client_connection_->close(network::ConnectionCloseType::NoFlush);
      }
      if (server_connection_) {
        server_connection_->close(network::ConnectionCloseType::NoFlush);
      }
    });
    
    RealIoTestBase::TearDown();
  }
  
  void setupConnections() {
    // Create client connection
    auto client_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(client_handle_),
        network::Address::pipeAddress("client"),
        network::Address::pipeAddress("server"));
    
    client_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_,
        std::move(client_socket),
        network::TransportSocketPtr(nullptr),
        true); // connected
    
    // Create server connection
    auto server_socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(server_handle_),
        network::Address::pipeAddress("server"),
        network::Address::pipeAddress("client"));
    
    server_connection_ = std::make_unique<network::ConnectionImpl>(
        *dispatcher_,
        std::move(server_socket),
        network::TransportSocketPtr(nullptr),
        true); // connected
    
    // Add callbacks
    client_connection_->addConnectionCallbacks(*this);
    server_connection_->addConnectionCallbacks(*this);
    
    // Create and add filters
    client_callbacks_ = std::make_unique<NiceMock<MockJsonRpcCallbacks>>();
    server_callbacks_ = std::make_unique<NiceMock<MockJsonRpcCallbacks>>();
    
    auto client_filter = std::make_shared<JsonRpcProtocolFilter>(
        *client_callbacks_, *dispatcher_, false);
    auto server_filter = std::make_shared<JsonRpcProtocolFilter>(
        *server_callbacks_, *dispatcher_, true);
    
    client_connection_->filterManager().addReadFilter(client_filter);
    client_connection_->filterManager().addWriteFilter(client_filter);
    server_connection_->filterManager().addReadFilter(server_filter);
    server_connection_->filterManager().addWriteFilter(server_filter);
    
    // Initialize filters
    client_connection_->filterManager().initializeReadFilters();
    server_connection_->filterManager().initializeReadFilters();
  }
  
  // ConnectionCallbacks
  void onEvent(network::ConnectionEvent event) override {
    last_event_ = event;
  }
  
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  
  std::unique_ptr<network::ConnectionImpl> client_connection_;
  std::unique_ptr<network::ConnectionImpl> server_connection_;
  std::unique_ptr<MockJsonRpcCallbacks> client_callbacks_;
  std::unique_ptr<MockJsonRpcCallbacks> server_callbacks_;
  network::ConnectionEvent last_event_;
  network::IoHandlePtr client_handle_;
  network::IoHandlePtr server_handle_;
};

/**
 * Test end-to-end message flow
 */
TEST_F(JsonRpcProtocolFilterIntegrationTest, EndToEndMessageFlow) {
  // Server should receive request
  EXPECT_CALL(*server_callbacks_, onRequest(_))
      .WillOnce([](const jsonrpc::Request& req) {
        EXPECT_EQ("test.method", req.method);
        EXPECT_EQ(123, get<int64_t>(req.id));
      });
  
  // Send request from client
  executeInDispatcher([this]() {
    std::string request = R"({"jsonrpc":"2.0","id":123,"method":"test.method"})" "\n";
    OwnedBuffer buffer;
    buffer.add(request);
    client_connection_->write(buffer, false);
  });
  
  // Allow time for message to be processed
  waitFor([this]() {
    // Mock objects don't have call_count, just wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
  }, std::chrono::seconds(2));
}
#endif  // Disabled integration test

}  // namespace
}  // namespace filter
}  // namespace mcp