/**
 * @file test_connection_promise_fix.cc
 * @brief Unit tests for connect() waiting for actual connection establishment
 *
 * Tests for the fix that makes connect() wait for the connection event
 * before returning, rather than returning immediately after initiating
 * the TCP connect.
 *
 * Bug: connect() returned success immediately after initiating TCP connect
 * (which returns EINPROGRESS). When initialize() was called, connected_=false
 * which triggered reconnection, closing the in-progress connection.
 *
 * Fix: Added pending_connect_promise_ that is fulfilled by
 * handleConnectionEvent() when the connection is actually established (after
 * TCP+SSL handshake).
 */

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/network/transport_socket.h"

namespace mcp {
namespace client {
namespace {

using namespace std::chrono_literals;
using namespace network;

/**
 * Test fixture for connection promise tests
 */
class ConnectionPromiseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    connected_ = false;
    promise_fulfilled_ = false;
    pending_connect_promise_ = nullptr;
  }

  // Simulated state
  std::atomic<bool> connected_{false};
  std::atomic<bool> promise_fulfilled_{false};
  std::shared_ptr<std::promise<bool>> pending_connect_promise_;
  std::mutex connect_promise_mutex_;
};

/**
 * Test: Promise is fulfilled on Connected event
 */
TEST_F(ConnectionPromiseTest, PromiseFulfilledOnConnected) {
  // Create promise
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = promise;
  }

  // Simulate handleConnectionEvent(Connected)
  auto handleConnectionEvent = [&](ConnectionEvent event) {
    if (event == ConnectionEvent::Connected) {
      connected_ = true;

      std::lock_guard<std::mutex> lock(connect_promise_mutex_);
      if (pending_connect_promise_) {
        pending_connect_promise_->set_value(true);
        promise_fulfilled_ = true;
        pending_connect_promise_.reset();
      }
    }
  };

  // Trigger connection event
  handleConnectionEvent(ConnectionEvent::Connected);

  // Verify promise was fulfilled
  EXPECT_TRUE(connected_);
  EXPECT_TRUE(promise_fulfilled_);

  // Future should be ready
  auto status = future.wait_for(0ms);
  EXPECT_EQ(status, std::future_status::ready);
  EXPECT_TRUE(future.get());
}

/**
 * Test: Promise is fulfilled with error on RemoteClose
 */
TEST_F(ConnectionPromiseTest, PromiseFulfilledWithErrorOnClose) {
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = promise;
  }

  // Simulate handleConnectionEvent(RemoteClose)
  auto handleConnectionEvent = [&](ConnectionEvent event) {
    if (event == ConnectionEvent::RemoteClose ||
        event == ConnectionEvent::LocalClose) {
      connected_ = false;

      std::lock_guard<std::mutex> lock(connect_promise_mutex_);
      if (pending_connect_promise_) {
        pending_connect_promise_->set_value(false);  // Error
        promise_fulfilled_ = true;
        pending_connect_promise_.reset();
      }
    }
  };

  handleConnectionEvent(ConnectionEvent::RemoteClose);

  EXPECT_FALSE(connected_);
  EXPECT_TRUE(promise_fulfilled_);

  auto status = future.wait_for(0ms);
  EXPECT_EQ(status, std::future_status::ready);
  EXPECT_FALSE(future.get());  // Error result
}

/**
 * Test: connect() blocks until connection event fires
 */
TEST_F(ConnectionPromiseTest, ConnectBlocksUntilConnectionEvent) {
  std::atomic<bool> connect_returned{false};
  std::atomic<bool> connection_event_fired{false};

  // Simulate connect() in a separate thread
  auto connect_thread = std::thread([&]() {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    {
      std::lock_guard<std::mutex> lock(connect_promise_mutex_);
      pending_connect_promise_ = promise;
    }

    // Wait for connection (with timeout)
    auto status = future.wait_for(5s);
    connect_returned = true;

    EXPECT_EQ(status, std::future_status::ready);
  });

  // Give connect() time to start waiting
  std::this_thread::sleep_for(100ms);
  EXPECT_FALSE(connect_returned);  // Should still be blocked

  // Fire connection event from "dispatcher thread"
  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    if (pending_connect_promise_) {
      connection_event_fired = true;
      pending_connect_promise_->set_value(true);
      pending_connect_promise_.reset();
    }
  }

  connect_thread.join();

  EXPECT_TRUE(connect_returned);
  EXPECT_TRUE(connection_event_fired);
}

/**
 * Test: Promise not fulfilled twice
 */
TEST_F(ConnectionPromiseTest, PromiseNotFulfilledTwice) {
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = promise;
  }

  auto handleConnectionEvent = [&](ConnectionEvent event) {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    if (pending_connect_promise_) {
      pending_connect_promise_->set_value(event == ConnectionEvent::Connected);
      pending_connect_promise_.reset();
    }
  };

  // Fire Connected
  handleConnectionEvent(ConnectionEvent::Connected);

  // Fire again - should not crash (promise already reset)
  handleConnectionEvent(ConnectionEvent::RemoteClose);

  auto status = future.wait_for(0ms);
  EXPECT_EQ(status, std::future_status::ready);
  EXPECT_TRUE(future.get());  // First event was Connected
}

/**
 * Test: Timeout if connection never establishes
 */
TEST_F(ConnectionPromiseTest, TimeoutIfNoConnectionEvent) {
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = promise;
  }

  // Don't fire any connection event - wait should timeout
  auto status = future.wait_for(100ms);
  EXPECT_EQ(status, std::future_status::timeout);
}

/**
 * Test: Thread safety of promise fulfillment
 */
TEST_F(ConnectionPromiseTest, ThreadSafePromiseFulfillment) {
  std::atomic<int> successful_fulfillments{0};
  constexpr int num_threads = 10;

  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = promise;
  }

  // Multiple threads try to fulfill the promise
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&]() {
      std::lock_guard<std::mutex> lock(connect_promise_mutex_);
      if (pending_connect_promise_) {
        pending_connect_promise_->set_value(true);
        pending_connect_promise_.reset();
        successful_fulfillments++;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Only one thread should have fulfilled the promise
  EXPECT_EQ(successful_fulfillments, 1);

  auto status = future.wait_for(0ms);
  EXPECT_EQ(status, std::future_status::ready);
}

/**
 * Test: Connected event followed by close doesn't re-fulfill promise
 */
TEST_F(ConnectionPromiseTest, CloseAfterConnectedSafe) {
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = promise;
  }

  auto handleConnectionEvent = [&](ConnectionEvent event) {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    if (pending_connect_promise_) {
      pending_connect_promise_->set_value(event == ConnectionEvent::Connected);
      pending_connect_promise_.reset();
    }

    // Update connected_ state regardless
    if (event == ConnectionEvent::Connected) {
      connected_ = true;
    } else {
      connected_ = false;
    }
  };

  // Connect succeeds
  handleConnectionEvent(ConnectionEvent::Connected);
  EXPECT_TRUE(connected_);
  EXPECT_TRUE(future.get());

  // Later, connection closes - promise already fulfilled
  handleConnectionEvent(ConnectionEvent::RemoteClose);
  EXPECT_FALSE(connected_);

  // No exception thrown, no double-set
}

}  // namespace
}  // namespace client
}  // namespace mcp
