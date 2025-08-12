#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <errno.h>
#include <fcntl.h>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/address.h"
#include "mcp/network/connection.h"
#include "mcp/network/socket_interface.h"

namespace mcp {
namespace test {

/**
 * Base class for integration tests that require real IO operations.
 * This class provides utilities for running dispatchers in background threads,
 * executing operations within dispatcher thread context, and creating real
 * network connections for testing.
 *
 * Key features:
 * - Automatic dispatcher thread management
 * - Thread-safe execution within dispatcher context
 * - Real socket pair creation for testing
 * - Timeout management for async operations
 */
class RealIoTestBase : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher factory and dispatcher
    factory_ = event::createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("integration_test");

    // Start dispatcher in background thread to handle thread context
    // requirements
    dispatcher_running_ = true;
    dispatcher_thread_ = std::thread([this]() {
      // Set thread name for debugging
      pthread_setname_np("test_dispatcher");

      // Signal that dispatcher is ready
      {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        dispatcher_ready_ = true;
        ready_cv_.notify_all();
      }

      // Run dispatcher
      dispatcher_->run(event::RunType::RunUntilExit);

      // Mark as stopped
      dispatcher_running_ = false;
    });

    // Wait for dispatcher to be ready
    waitForDispatcherReady();
  }

  void TearDown() override {
    // Clean shutdown
    if (dispatcher_ && dispatcher_running_) {
      dispatcher_->exit();
    }

    if (dispatcher_thread_.joinable()) {
      dispatcher_thread_.join();
    }

    // Clean up any open file descriptors
    for (int fd : open_fds_) {
      close(fd);
    }
    open_fds_.clear();

    dispatcher_.reset();
    factory_.reset();
  }

  /**
   * Execute a function within the dispatcher thread context.
   * This ensures that operations requiring dispatcher thread context
   * (like creating file events, timers, connections) work correctly.
   *
   * @param func Function to execute in dispatcher thread
   * @return Result of the function (supports void and non-void returns)
   */
  template <typename F>
  auto executeInDispatcher(F&& func) -> decltype(func()) {
    using ReturnType = decltype(func());

    // Check if dispatcher is running
    if (!dispatcher_running_) {
      throw std::runtime_error("Dispatcher not running");
    }

    return executeInDispatcherImpl(std::forward<F>(func),
                                    std::is_void<ReturnType>{});
  }

private:
  // Helper for void return type
  template <typename F>
  void executeInDispatcherImpl(F&& func, std::true_type) {
    std::promise<void> promise;
    auto future = promise.get_future();

    dispatcher_->post([&promise, func = std::forward<F>(func)]() mutable {
      try {
        func();
        promise.set_value();
      } catch (...) {
        promise.set_exception(std::current_exception());
      }
    });

    // Wait with timeout
    if (future.wait_for(operation_timeout_) != std::future_status::ready) {
      throw std::runtime_error("Operation timed out");
    }
    future.get();  // Re-throw any exception
  }

  // Helper for non-void return type
  template <typename F>
  auto executeInDispatcherImpl(F&& func, std::false_type)
      -> decltype(func()) {
    using ReturnType = decltype(func());
    std::promise<ReturnType> promise;
    auto future = promise.get_future();

    dispatcher_->post([&promise, func = std::forward<F>(func)]() mutable {
      try {
        promise.set_value(func());
      } catch (...) {
        promise.set_exception(std::current_exception());
      }
    });

    // Wait with timeout
    if (future.wait_for(operation_timeout_) != std::future_status::ready) {
      throw std::runtime_error("Operation timed out");
    }
    return future.get();
  }

public:

  /**
   * Create a pair of connected sockets for testing.
   * Uses real TCP sockets bound to localhost.
   * NOTE: This must be called from within the dispatcher thread context.
   *
   * @return Pair of connected IoHandles (client, server)
   */
  std::pair<network::IoHandlePtr, network::IoHandlePtr> createSocketPair() {
    // This function should be called from within dispatcher thread context
    // Do not wrap in executeInDispatcher as it causes nested execution
    auto& socket_interface = network::socketInterface();

    // Create server socket
    auto server_fd_result = socket_interface.socket(
        network::SocketType::Stream, network::Address::Type::Ip,
        network::Address::IpVersion::v4);

    if (!server_fd_result.ok()) {
      throw std::runtime_error("Failed to create server socket");
    }

    auto server_handle =
        socket_interface.ioHandleForFd(*server_fd_result, false);

    // Set socket to non-blocking
    server_handle->setBlocking(false);

    // Bind to localhost with ephemeral port
    auto bind_addr = network::Address::parseInternetAddress("127.0.0.1", 0);
    auto bind_result = server_handle->bind(bind_addr);
    if (!bind_result.ok()) {
      throw std::runtime_error("Failed to bind server socket");
    }

    // Listen
    auto listen_result = server_handle->listen(1);
    if (!listen_result.ok()) {
      throw std::runtime_error("Failed to listen on server socket");
    }

    // Get actual port
    auto local_addr_result = server_handle->localAddress();
    if (!local_addr_result.ok()) {
      throw std::runtime_error("Failed to get local address");
    }
    auto local_addr = *local_addr_result;

    // Create client socket
    auto client_fd_result = socket_interface.socket(
        network::SocketType::Stream, network::Address::Type::Ip,
        network::Address::IpVersion::v4);

    if (!client_fd_result.ok()) {
      throw std::runtime_error("Failed to create client socket");
    }

    auto client_handle =
        socket_interface.ioHandleForFd(*client_fd_result, false);
    client_handle->setBlocking(false);

    // Connect client to server
    auto connect_result = client_handle->connect(local_addr);
    if (!connect_result.ok()) {
      // For non-blocking socket, EINPROGRESS is expected
      if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        throw std::runtime_error("Failed to connect client socket");
      }
    }

    // Accept connection on server side
    auto accepted_result = server_handle->accept();
    if (!accepted_result.ok()) {
      // For non-blocking socket, might need to wait
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      accepted_result = server_handle->accept();
      if (!accepted_result.ok()) {
        throw std::runtime_error("Failed to accept connection");
      }
    }

    // Note: We don't track FDs directly as IoHandle manages them

    // Close listening socket as we don't need it anymore
    server_handle->close();

    return std::make_pair(std::move(client_handle),
                          std::move(*accepted_result));
  }

  /**
   * Create a pipe for testing file events.
   *
   * @return Pair of file descriptors (read_fd, write_fd)
   */
  std::pair<int, int> createPipe() {
    int fds[2];
    if (pipe(fds) != 0) {
      throw std::runtime_error("Failed to create pipe");
    }

    // Make non-blocking
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    // Track for cleanup
    trackFd(fds[0]);
    trackFd(fds[1]);

    return {fds[0], fds[1]};
  }

  /**
   * Wait for an async operation with timeout.
   *
   * @param condition Function that returns true when condition is met
   * @param timeout Maximum time to wait
   * @return true if condition was met, false if timed out
   */
  bool waitFor(
      const std::function<bool()>& condition,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    auto start = std::chrono::steady_clock::now();

    while (!condition()) {
      if (std::chrono::steady_clock::now() - start > timeout) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return true;
  }

  /**
   * Post a callback to the dispatcher and wait for completion.
   *
   * @param callback Function to execute
   */
  void postAndWait(std::function<void()> callback) {
    executeInDispatcher(std::move(callback));
  }

  // Protected members accessible to derived test classes
  event::DispatcherFactoryPtr factory_;
  event::DispatcherPtr dispatcher_;
  std::chrono::seconds operation_timeout_{5};

 private:
  void waitForDispatcherReady() {
    std::unique_lock<std::mutex> lock(ready_mutex_);
    ready_cv_.wait_for(lock, std::chrono::seconds(5),
                       [this]() { return dispatcher_ready_.load(); });

    if (!dispatcher_ready_) {
      throw std::runtime_error("Dispatcher failed to start");
    }
  }

  void trackFd(int fd) {
    if (fd >= 0) {
      open_fds_.push_back(fd);
    }
  }

  // Thread management
  std::thread dispatcher_thread_;
  std::atomic<bool> dispatcher_running_{false};
  std::atomic<bool> dispatcher_ready_{false};
  std::mutex ready_mutex_;
  std::condition_variable ready_cv_;

  // Resource tracking
  std::vector<int> open_fds_;
};

/**
 * Base class for tests that need real network listeners.
 */
class RealListenerTestBase : public RealIoTestBase {
 protected:
  /**
   * Create a real TCP listener on an ephemeral port.
   *
   * @return The port number the listener is bound to
   */
  uint16_t createRealListener() {
    return executeInDispatcher([this]() {
      auto& socket_interface = network::socketInterface();

      // Create listen socket
      auto listen_fd_result = socket_interface.socket(
          network::SocketType::Stream, network::Address::Type::Ip,
          network::Address::IpVersion::v4);

      if (!listen_fd_result.ok()) {
        throw std::runtime_error("Failed to create listen socket");
      }

      listen_handle_ = socket_interface.ioHandleForFd(*listen_fd_result, false);

      // Set socket to non-blocking
      listen_handle_->setBlocking(false);

      // Bind to ephemeral port
      auto bind_addr = network::Address::parseInternetAddress("127.0.0.1", 0);
      auto bind_result = listen_handle_->bind(bind_addr);
      if (!bind_result.ok()) {
        throw std::runtime_error("Failed to bind listen socket");
      }

      // Start listening
      auto listen_result = listen_handle_->listen(128);
      if (!listen_result.ok()) {
        throw std::runtime_error("Failed to listen");
      }

      // Get actual port
      auto local_addr_result = listen_handle_->localAddress();
      if (!local_addr_result.ok()) {
        throw std::runtime_error("Failed to get local address");
      }
      auto local_addr = *local_addr_result;

      // Get port from IP address
      auto ip_addr = dynamic_cast<const network::Address::Ip*>(local_addr.get());
      if (!ip_addr) {
        throw std::runtime_error("Failed to get IP address");
      }
      return ip_addr->port();
    });
  }

  /**
   * Accept a connection on the listener.
   *
   * @return Accepted connection handle
   */
  network::IoHandlePtr acceptConnection() {
    return executeInDispatcher([this]() {
      auto accepted = listen_handle_->accept();
      if (!accepted.ok()) {
        throw std::runtime_error("Failed to accept connection");
      }
      return std::move(*accepted);
    });
  }

  network::IoHandlePtr listen_handle_;
};

}  // namespace test
}  // namespace mcp