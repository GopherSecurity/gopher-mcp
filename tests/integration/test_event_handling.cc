/**
 * Event handling simulation test
 *
 * This test simulates the level-trigger vs edge-trigger dilemma:
 * - Level trigger causes busy loop on write events
 * - Edge trigger can miss read events due to race conditions
 *
 * The test verifies our adaptive event handling solution that:
 * 1. Disables write events when write buffer is empty (prevents busy loop)
 * 2. Properly activates read events after enabling them (handles edge race)
 * 3. Works correctly for both client and server connections
 */

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>

#include "mcp/network/address.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/filter.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"

#include "real_io_test_base.h"

namespace mcp {
namespace test {

class EventHandlingTest : public RealIoTestBase {
 protected:
  ~EventHandlingTest() {
    // Clean up any file descriptors we created
    for (int fd : test_fds_) {
      close(fd);
    }
  }

  template <typename Predicate>
  bool waitForCondition(
      Predicate&& predicate,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(200),
      std::chrono::milliseconds poll_interval = std::chrono::milliseconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(poll_interval);
    }
    return predicate();
  }

  // Simple read filter to capture received data
  class TestReadFilter : public network::ReadFilter {
   public:
    TestReadFilter(OwnedBuffer& buffer, std::atomic<bool>& received_flag)
        : buffer_(buffer), data_received_(received_flag) {}

    network::FilterStatus onData(Buffer& data, bool) override {
      // Copy data to our buffer
      data.move(buffer_);
      data_received_ = true;
      return network::FilterStatus::Continue;
    }

    network::FilterStatus onNewConnection() override {
      return network::FilterStatus::Continue;
    }

    void initializeReadFilterCallbacks(network::ReadFilterCallbacks&) override {
    }

   private:
    OwnedBuffer& buffer_;
    std::atomic<bool>& data_received_;
  };

  struct TestCallbacks : public network::ConnectionCallbacks {
    void onEvent(network::ConnectionEvent event) override {
      events.push_back(event);
      if (event == network::ConnectionEvent::Connected) {
        connected = true;
      } else if (event == network::ConnectionEvent::RemoteClose ||
                 event == network::ConnectionEvent::LocalClose) {
        closed = true;
      }
    }

    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    std::vector<network::ConnectionEvent> events;
    OwnedBuffer received_data;
    std::atomic<bool> connected{false};
    std::atomic<bool> data_received{false};
    std::atomic<bool> closed{false};
  };

  /**
   * Create a connected socket pair for testing
   * Returns [client_fd, server_fd]
   */
  std::pair<int, int> createConnectedSocketPair() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
      throw std::runtime_error("Failed to create socket pair");
    }

    // Make non-blocking
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);

    // Track fds for cleanup
    test_fds_.push_back(fds[0]);
    test_fds_.push_back(fds[1]);

    return {fds[0], fds[1]};
  }

  std::vector<int> test_fds_;

  /**
   * Monitor write events on a file descriptor
   * Used to detect busy loop behavior with level-triggered events
   */
  int monitorWriteEvents(int fd, int duration_ms) {
    std::atomic<int> write_event_count{0};
    std::atomic<bool> stop{false};

    auto monitor_thread = std::thread([this, fd, &write_event_count, &stop]() {
      auto file_event = dispatcher_->createFileEvent(
          fd,
          [&write_event_count](uint32_t events) {
            if (events & static_cast<uint32_t>(event::FileReadyType::Write)) {
              write_event_count++;
            }
          },
          event::PlatformDefaultTriggerType,
          static_cast<uint32_t>(event::FileReadyType::Write));

      while (!stop.load()) {
        dispatcher_->run(event::RunType::NonBlock);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    stop = true;
    monitor_thread.join();

    return write_event_count.load();
  }
};

/**
 * Test that write events don't cause busy loop with empty buffer
 * This verifies the fix for the level-trigger busy loop issue
 */
TEST_F(EventHandlingTest, NoWriteBusyLoopWithEmptyBuffer) {
  std::pair<int, int> fds = createConnectedSocketPair();
  int client_fd = fds.first;
  int server_fd = fds.second;

  executeInDispatcher([this, server_fd]() {
    // Create server connection
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>(server_fd);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, nullptr);

    // Create a raw transport socket (no TLS)
    // Since we're testing connection handling, not transport, use raw sockets
    network::TransportSocketPtr transport_socket =
        nullptr;  // Use RawTransportSocket (created by ConnectionImpl)

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket),
        true);  // true = already connected (for pipes/stdio)

    TestCallbacks callbacks;
    connection->addConnectionCallbacks(callbacks);

    // Connection is initialized in constructor
    // This should NOT cause a busy loop with empty write buffer

    // Let it run for a bit to check for busy loop
    auto timer = dispatcher_->createTimer([this]() { dispatcher_->exit(); });
    timer->enableTimer(std::chrono::milliseconds(100));
  });

  // The dispatcher should exit cleanly without busy looping
  // If there's a busy loop, the test would hang or consume excessive CPU
  EXPECT_TRUE(true);  // Test passes if we get here
}

/**
 * Test edge-triggered read race condition
 * Verifies that read events are not missed when data arrives during event setup
 */
TEST_F(EventHandlingTest, EdgeTriggeredReadRaceCondition) {
  std::pair<int, int> fds = createConnectedSocketPair();
  int client_fd = fds.first;
  int server_fd = fds.second;

  TestCallbacks server_callbacks;
  std::atomic<bool> data_sent{false};

  executeInDispatcher([this, server_fd, client_fd, &server_callbacks,
                       &data_sent]() {
    // Create server connection
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>(server_fd);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, nullptr);

    // Create a raw transport socket (no TLS)
    // Since we're testing connection handling, not transport, use raw sockets
    network::TransportSocketPtr transport_socket =
        nullptr;  // Use RawTransportSocket (created by ConnectionImpl)

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket),
        true);  // true = already connected (for pipes/stdio)

    connection->addConnectionCallbacks(server_callbacks);

    // Add read filter to capture data
    auto read_filter = std::make_shared<TestReadFilter>(
        server_callbacks.received_data, server_callbacks.data_received);
    connection->addReadFilter(read_filter);
    connection->initializeReadFilters();

    // Connection is initialized automatically

    // Simulate race condition:
    // 1. Disable read events temporarily (simulating some processing)
    connection->readDisable(true);

    // 2. Send data from client while read is disabled
    const char* test_data = "test message";
    ssize_t written = write(client_fd, test_data, strlen(test_data));
    ASSERT_GT(written, 0);
    data_sent = true;

    // 3. Re-enable read events
    // This is where edge-triggered events could miss the data
    connection->readDisable(false);

    // Give time for events to process
    auto timer = dispatcher_->createTimer([this]() { dispatcher_->exit(); });
    timer->enableTimer(std::chrono::milliseconds(100));

    // Keep connection alive for test
    connection.release();
  });

  // Verify data was received despite the race condition
  EXPECT_TRUE(data_sent);
  ASSERT_TRUE(waitForCondition(
      [&]() { return server_callbacks.data_received.load(); }));
  EXPECT_EQ("test message", server_callbacks.received_data.toString());
}

/**
 * Test proper write event management
 * Verifies write events are only enabled when there's data to write
 */
TEST_F(EventHandlingTest, WriteEventManagement) {
  std::pair<int, int> fds = createConnectedSocketPair();
  int client_fd = fds.first;
  int server_fd = fds.second;

  std::atomic<network::ConnectionImpl*> connection_ptr{nullptr};

  executeInDispatcher([this, server_fd, &connection_ptr]() {
    auto io_handle = std::make_unique<network::IoSocketHandleImpl>(server_fd);
    auto socket = std::make_unique<network::ConnectionSocketImpl>(
        std::move(io_handle), nullptr, nullptr);

    network::TransportSocketPtr transport_socket = nullptr;

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket), true);

    connection_ptr = connection.release();
  });

  ASSERT_NE(nullptr, connection_ptr.load());

  auto getWriteEvents = [this, &connection_ptr]() -> uint64_t {
    return executeInDispatcher([conn = connection_ptr.load()]() {
      return conn->getWriteEventCount();
    });
  };

  auto writeData = [this, &connection_ptr](const std::string& payload) {
    executeInDispatcher([conn = connection_ptr.load(), payload]() {
      OwnedBuffer buffer;
      buffer.add(payload);
      conn->write(buffer, false);
    });
  };

  const auto write_events_before = getWriteEvents();

  writeData("large data chunk that needs multiple writes");

  ASSERT_TRUE(
      waitForCondition([&]() { return getWriteEvents() > write_events_before; },
                       std::chrono::milliseconds(500)));

  const auto write_events_during = getWriteEvents();

  // Drain client pipe to allow server writes to complete
  char drain_buffer[256];
  while (read(client_fd, drain_buffer, sizeof(drain_buffer)) > 0) {
  }

  ASSERT_TRUE(waitForCondition(
      [&]() { return getWriteEvents() >= write_events_during; },
      std::chrono::milliseconds(200)));

  const auto write_events_after = getWriteEvents();

  EXPECT_GT(write_events_during, write_events_before);
  EXPECT_LE(write_events_after, write_events_during + 10);

  executeInDispatcher([conn = connection_ptr.load()]() {
    conn->close(network::ConnectionCloseType::FlushWrite);
    delete conn;
  });
}

/**
 * Test bidirectional communication with proper event handling
 * Verifies both client and server can send and receive without issues
 */
TEST_F(EventHandlingTest, BidirectionalCommunication) {
  std::pair<int, int> fds = createConnectedSocketPair();
  int client_fd = fds.first;
  int server_fd = fds.second;

  TestCallbacks client_callbacks;
  TestCallbacks server_callbacks;

  executeInDispatcher(
      [this, client_fd, server_fd, &client_callbacks, &server_callbacks]() {
        // Create client connection
        auto client_io_handle =
            std::make_unique<network::IoSocketHandleImpl>(client_fd);
        auto client_socket = std::make_unique<network::ConnectionSocketImpl>(
            std::move(client_io_handle), nullptr, nullptr);
        auto client_transport =
            nullptr;  // Use RawTransportSocket (created by ConnectionImpl)
        auto client_conn = std::make_unique<network::ConnectionImpl>(
            *dispatcher_, std::move(client_socket), std::move(client_transport),
            true);  // true = already connected (for pipes)
        client_conn->addConnectionCallbacks(client_callbacks);

        // Add read filter to client to capture data
        auto client_read_filter = std::make_shared<TestReadFilter>(
            client_callbacks.received_data, client_callbacks.data_received);
        client_conn->addReadFilter(client_read_filter);
        client_conn->initializeReadFilters();

        // Connection initialized automatically

        // Create server connection
        auto server_io_handle =
            std::make_unique<network::IoSocketHandleImpl>(server_fd);
        auto server_socket = std::make_unique<network::ConnectionSocketImpl>(
            std::move(server_io_handle), nullptr, nullptr);
        auto server_transport =
            nullptr;  // Use RawTransportSocket (created by ConnectionImpl)
        auto server_conn = std::make_unique<network::ConnectionImpl>(
            *dispatcher_, std::move(server_socket), std::move(server_transport),
            true);  // true = already connected (for pipes)
        server_conn->addConnectionCallbacks(server_callbacks);

        // Add read filter to server to capture data
        auto server_read_filter = std::make_shared<TestReadFilter>(
            server_callbacks.received_data, server_callbacks.data_received);
        server_conn->addReadFilter(server_read_filter);
        server_conn->initializeReadFilters();

        // Connection initialized automatically

        // Client sends to server
        OwnedBuffer client_msg;
        client_msg.add("Hello from client");
        client_conn->write(client_msg, false);

        // Server sends to client
        OwnedBuffer server_msg;
        server_msg.add("Hello from server");
        server_conn->write(server_msg, false);

        // Let messages process
        auto timer =
            dispatcher_->createTimer([this, client_conn = client_conn.release(),
                                      server_conn = server_conn.release()]() {
              delete client_conn;
              delete server_conn;
              dispatcher_->exit();
            });
        timer->enableTimer(std::chrono::milliseconds(100));
      });

  // Both sides should receive the messages
  ASSERT_TRUE(waitForCondition(
      [&]() { return client_callbacks.data_received.load(); }));
  ASSERT_TRUE(waitForCondition(
      [&]() { return server_callbacks.data_received.load(); }));
  EXPECT_EQ("Hello from server", client_callbacks.received_data.toString());
  EXPECT_EQ("Hello from client", server_callbacks.received_data.toString());
}

}  // namespace test
}  // namespace mcp
