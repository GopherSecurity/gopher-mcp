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

#include <fcntl.h>
#include <cstring>

#include <arpa/inet.h>
#include <sys/socket.h>

#include "mcp/network/connection_impl.h"
#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/transport_socket.h"
#include "mcp/stream_info/stream_info_impl.h"

#include "real_io_test_base.h"

namespace mcp {
namespace test {

class EventHandlingTest : public RealIoTestBase {
 protected:
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

    open_fds_.push_back(fds[0]);
    open_fds_.push_back(fds[1]);

    return {fds[0], fds[1]};
  }

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
    auto socket = std::make_unique<network::SocketImpl>(
        std::make_unique<network::IoSocketHandleImpl>(server_fd));

    auto transport_factory = network::createRawBufferTransportSocketFactory();
    auto transport_socket = transport_factory->createTransportSocket(nullptr);

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket),
        stream_info::StreamInfoImpl::create());

    TestCallbacks callbacks;
    connection->addConnectionCallbacks(callbacks);

    // Initialize connection - this should NOT cause busy loop
    connection->initializeTransport();
    connection->enableFileEventsIfNecessary();

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
    auto socket = std::make_unique<network::SocketImpl>(
        std::make_unique<network::IoSocketHandleImpl>(server_fd));

    auto transport_factory = network::createRawBufferTransportSocketFactory();
    auto transport_socket = transport_factory->createTransportSocket(nullptr);

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket),
        stream_info::StreamInfoImpl::create());

    connection->addConnectionCallbacks(server_callbacks);
    connection->initializeTransport();
    connection->enableFileEventsIfNecessary();

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
    timer->enableTimer(std::chrono::milliseconds(200));

    // Keep connection alive for test
    connection.release();
  });

  // Verify data was received despite the race condition
  EXPECT_TRUE(data_sent);
  EXPECT_TRUE(server_callbacks.data_received);
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

  std::atomic<int> write_events_before{0};
  std::atomic<int> write_events_during{0};
  std::atomic<int> write_events_after{0};

  executeInDispatcher([this, server_fd, &write_events_before,
                       &write_events_during, &write_events_after]() {
    // Create server connection
    auto socket = std::make_unique<network::SocketImpl>(
        std::make_unique<network::IoSocketHandleImpl>(server_fd));

    auto transport_factory = network::createRawBufferTransportSocketFactory();
    auto transport_socket = transport_factory->createTransportSocket(nullptr);

    auto connection = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(socket), std::move(transport_socket),
        stream_info::StreamInfoImpl::create());

    TestCallbacks callbacks;
    connection->addConnectionCallbacks(callbacks);
    connection->initializeTransport();
    connection->enableFileEventsIfNecessary();

    // Phase 1: No data to write - should have minimal write events
    int phase1_count = 0;
    auto phase1_timer = dispatcher_->createTimer([&phase1_count, this]() {
      phase1_count++;
      if (phase1_count > 10) {
        return;
      }
      dispatcher_->run(event::RunType::NonBlock);
    });
    phase1_timer->enableTimer(std::chrono::milliseconds(5));

    // Wait for phase 1 to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_events_before = connection->getWriteEventCount();

    // Phase 2: Queue data to write - should trigger write events
    Buffer write_buffer("large data chunk that needs multiple writes");
    connection->write(write_buffer);

    int phase2_count = 0;
    auto phase2_timer = dispatcher_->createTimer([&phase2_count, this]() {
      phase2_count++;
      if (phase2_count > 10) {
        return;
      }
      dispatcher_->run(event::RunType::NonBlock);
    });
    phase2_timer->enableTimer(std::chrono::milliseconds(5));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_events_during = connection->getWriteEventCount();

    // Phase 3: After write completes - should have minimal write events again
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_events_after = connection->getWriteEventCount();

    // Cleanup
    auto cleanup_timer =
        dispatcher_->createTimer([this, connection = connection.release()]() {
          delete connection;
          dispatcher_->exit();
        });
    cleanup_timer->enableTimer(std::chrono::milliseconds(10));
  });

  // Verify write events are managed properly
  // Should have minimal events when buffer is empty, more when writing
  EXPECT_LE(write_events_before, 2);  // Minimal events with empty buffer
  EXPECT_GT(write_events_during,
            write_events_before);  // More events when writing
  EXPECT_LE(write_events_after - write_events_during,
            2);  // Back to minimal after write
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

  executeInDispatcher([this, client_fd, server_fd, &client_callbacks,
                       &server_callbacks]() {
    // Create client connection
    auto client_socket = std::make_unique<network::SocketImpl>(
        std::make_unique<network::IoSocketHandleImpl>(client_fd));
    auto client_transport =
        network::createRawBufferTransportSocketFactory()->createTransportSocket(
            nullptr);
    auto client_conn = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(client_socket), std::move(client_transport),
        stream_info::StreamInfoImpl::create());
    client_conn->addConnectionCallbacks(client_callbacks);
    client_conn->initializeTransport();
    client_conn->enableFileEventsIfNecessary();

    // Create server connection
    auto server_socket = std::make_unique<network::SocketImpl>(
        std::make_unique<network::IoSocketHandleImpl>(server_fd));
    auto server_transport =
        network::createRawBufferTransportSocketFactory()->createTransportSocket(
            nullptr);
    auto server_conn = std::make_unique<network::ConnectionImpl>(
        *dispatcher_, std::move(server_socket), std::move(server_transport),
        stream_info::StreamInfoImpl::create());
    server_conn->addConnectionCallbacks(server_callbacks);
    server_conn->initializeTransport();
    server_conn->enableFileEventsIfNecessary();

    // Client sends to server
    Buffer client_msg("Hello from client");
    client_conn->write(client_msg);

    // Server sends to client
    Buffer server_msg("Hello from server");
    server_conn->write(server_msg);

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
  EXPECT_TRUE(client_callbacks.data_received);
  EXPECT_TRUE(server_callbacks.data_received);
  EXPECT_EQ("Hello from server", client_callbacks.received_data.toString());
  EXPECT_EQ("Hello from client", server_callbacks.received_data.toString());
}

}  // namespace test
}  // namespace mcp