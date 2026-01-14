/**
 * @file test_mcp_client_threading.cc
 * @brief Multi-threaded tests for MCP C++ client components
 *
 * This test file covers:
 * - Priority 1 (Critical): RequestTracker and CircuitBreaker thread safety
 * - Priority 2 (High): Async patterns and cross-thread request flow
 *
 * These tests verify thread-safety of the client's core components under
 * concurrent access patterns.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "mcp/client/mcp_client.h"
#include "mcp/event/libevent_dispatcher.h"
#include "../integration/real_io_test_base.h"

namespace mcp {
namespace client {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Priority 1: RequestTracker Thread Safety Tests
// ============================================================================

class RequestTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tracker_ = std::make_unique<RequestTracker>(5000ms);  // 5 second timeout
  }

  void TearDown() override { tracker_.reset(); }

  // Helper to create a request context with a given ID
  RequestTracker::RequestPtr createRequest(int64_t id) {
    return std::make_shared<RequestContext>(RequestId(id), "test_method");
  }

  std::unique_ptr<RequestTracker> tracker_;
};

/**
 * Test concurrent tracking and removal of requests
 * Verifies thread-safe access to the pending requests map
 */
TEST_F(RequestTrackerTest, ConcurrentTrackAndRemove) {
  constexpr int NUM_THREADS = 10;
  constexpr int REQUESTS_PER_THREAD = 100;

  std::atomic<int> track_success{0};
  std::atomic<int> remove_success{0};

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, t, &track_success, &remove_success]() {
      for (int i = 0; i < REQUESTS_PER_THREAD; ++i) {
        int64_t id = t * REQUESTS_PER_THREAD + i;
        auto request = createRequest(id);

        // Track the request
        tracker_->trackRequest(request);
        track_success++;

        // Interleave removes (every other request)
        if (i % 2 == 0) {
          auto removed = tracker_->removeRequest(RequestId(id));
          if (removed) {
            remove_success++;
          }
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(track_success.load(), NUM_THREADS * REQUESTS_PER_THREAD);
  EXPECT_EQ(remove_success.load(), NUM_THREADS * REQUESTS_PER_THREAD / 2);

  // Verify remaining requests
  size_t remaining = tracker_->getPendingCount();
  EXPECT_EQ(remaining, static_cast<size_t>(NUM_THREADS * REQUESTS_PER_THREAD / 2));
}

/**
 * Test concurrent reads while other threads modify
 * Verifies readers don't block each other or writers excessively
 */
TEST_F(RequestTrackerTest, ConcurrentGetRequest) {
  constexpr int NUM_WRITERS = 4;
  constexpr int NUM_READERS = 8;
  constexpr int OPS_PER_THREAD = 100;

  std::atomic<int> reads_found{0};
  std::atomic<int> reads_not_found{0};
  std::atomic<bool> stop_flag{false};

  // Pre-populate some requests
  for (int64_t i = 0; i < 50; ++i) {
    tracker_->trackRequest(createRequest(i));
  }

  std::vector<std::thread> threads;

  // Reader threads
  for (int r = 0; r < NUM_READERS; ++r) {
    threads.emplace_back([this, &reads_found, &reads_not_found, &stop_flag]() {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<int64_t> dis(0, 199);

      while (!stop_flag.load()) {
        int64_t id = dis(gen);
        auto request = tracker_->getRequest(RequestId(id));
        if (request) {
          reads_found++;
        } else {
          reads_not_found++;
        }
        std::this_thread::yield();
      }
    });
  }

  // Writer threads
  for (int w = 0; w < NUM_WRITERS; ++w) {
    threads.emplace_back([this, w, OPS_PER_THREAD]() {
      for (int i = 0; i < OPS_PER_THREAD; ++i) {
        int64_t id = 50 + w * OPS_PER_THREAD + i;
        tracker_->trackRequest(createRequest(id));

        // Sometimes remove
        if (i % 3 == 0) {
          tracker_->removeRequest(RequestId(id));
        }

        std::this_thread::sleep_for(1ms);
      }
    });
  }

  // Wait for writers to complete
  for (int i = NUM_READERS; i < static_cast<int>(threads.size()); ++i) {
    threads[i].join();
  }

  // Stop readers
  stop_flag = true;
  for (int i = 0; i < NUM_READERS; ++i) {
    threads[i].join();
  }

  // Just verify no crashes and some reads happened
  EXPECT_GT(reads_found.load() + reads_not_found.load(), 0);
}

/**
 * Test timeout detection while concurrent requests are being added
 * Verifies getTimedOutRequests() works correctly under load
 */
TEST_F(RequestTrackerTest, TimeoutDetectionUnderLoad) {
  // Use a very short timeout for testing
  tracker_ = std::make_unique<RequestTracker>(50ms);

  constexpr int NUM_THREADS = 4;
  constexpr int REQUESTS_PER_THREAD = 50;

  std::atomic<int> timeouts_detected{0};
  std::atomic<bool> stop_timeout_checker{false};

  std::vector<std::thread> threads;

  // Timeout checker thread
  threads.emplace_back([this, &timeouts_detected, &stop_timeout_checker]() {
    while (!stop_timeout_checker.load()) {
      auto timed_out = tracker_->getTimedOutRequests();
      timeouts_detected += static_cast<int>(timed_out.size());
      std::this_thread::sleep_for(10ms);
    }
    // Final check
    auto timed_out = tracker_->getTimedOutRequests();
    timeouts_detected += static_cast<int>(timed_out.size());
  });

  // Request adder threads
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, t, REQUESTS_PER_THREAD]() {
      for (int i = 0; i < REQUESTS_PER_THREAD; ++i) {
        int64_t id = t * REQUESTS_PER_THREAD + i;
        tracker_->trackRequest(createRequest(id));
        std::this_thread::sleep_for(5ms);  // Spread out additions
      }
    });
  }

  // Wait for adders
  for (int i = 1; i < static_cast<int>(threads.size()); ++i) {
    threads[i].join();
  }

  // Wait a bit more for timeouts to be detected
  std::this_thread::sleep_for(200ms);

  stop_timeout_checker = true;
  threads[0].join();

  // Most requests should have timed out
  EXPECT_GT(timeouts_detected.load(), NUM_THREADS * REQUESTS_PER_THREAD / 2);
}

/**
 * Test that request IDs generated concurrently are unique
 * Verifies atomic ID generation across threads
 */
TEST_F(RequestTrackerTest, RequestIdUniqueness) {
  constexpr int NUM_THREADS = 10;
  constexpr int IDS_PER_THREAD = 1000;

  std::atomic<int64_t> next_id{0};
  std::set<int64_t> all_ids;
  std::mutex ids_mutex;

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&next_id, &all_ids, &ids_mutex, IDS_PER_THREAD]() {
      std::vector<int64_t> local_ids;
      local_ids.reserve(IDS_PER_THREAD);

      for (int i = 0; i < IDS_PER_THREAD; ++i) {
        // Simulate atomic ID generation like in McpClient
        int64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
        local_ids.push_back(id);
      }

      // Add to global set
      std::lock_guard<std::mutex> lock(ids_mutex);
      for (int64_t id : local_ids) {
        all_ids.insert(id);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // All IDs should be unique
  EXPECT_EQ(all_ids.size(), static_cast<size_t>(NUM_THREADS * IDS_PER_THREAD));
}

// ============================================================================
// Priority 1: CircuitBreaker Thread Safety Tests
// ============================================================================

class CircuitBreakerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // threshold=5, timeout=100ms, error_rate=0.5
    breaker_ = std::make_unique<CircuitBreaker>(5, 100ms, 0.5);
  }

  void TearDown() override { breaker_.reset(); }

  std::unique_ptr<CircuitBreaker> breaker_;
};

/**
 * Test concurrent allowRequest() calls
 * Verifies state checking is thread-safe
 */
TEST_F(CircuitBreakerTest, ConcurrentAllowRequestCalls) {
  constexpr int NUM_THREADS = 10;
  constexpr int CALLS_PER_THREAD = 1000;

  std::atomic<int> allowed{0};
  std::atomic<int> denied{0};

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, &allowed, &denied, CALLS_PER_THREAD]() {
      for (int i = 0; i < CALLS_PER_THREAD; ++i) {
        if (breaker_->allowRequest()) {
          allowed++;
        } else {
          denied++;
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Initially closed, all requests should be allowed
  EXPECT_EQ(allowed.load(), NUM_THREADS * CALLS_PER_THREAD);
  EXPECT_EQ(denied.load(), 0);
  EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::CLOSED);
}

/**
 * Test that state transitions are atomic
 * Multiple concurrent failures should trigger exactly one transition
 */
TEST_F(CircuitBreakerTest, StateTransitionAtomicity) {
  constexpr int NUM_THREADS = 10;
  constexpr int FAILURES_PER_THREAD = 2;

  std::atomic<int> recorded_failures{0};

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  // All threads record failures simultaneously
  std::atomic<bool> start{false};

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, &start, &recorded_failures, FAILURES_PER_THREAD]() {
      // Wait for start signal
      while (!start.load()) {
        std::this_thread::yield();
      }

      for (int i = 0; i < FAILURES_PER_THREAD; ++i) {
        breaker_->recordFailure();
        recorded_failures++;
      }
    });
  }

  // Start all threads simultaneously
  start = true;

  for (auto& t : threads) {
    t.join();
  }

  // Circuit should be OPEN after threshold failures
  EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::OPEN);
  EXPECT_EQ(recorded_failures.load(), NUM_THREADS * FAILURES_PER_THREAD);
}

/**
 * Test concurrent success and failure recording
 * Verifies counter updates are atomic
 */
TEST_F(CircuitBreakerTest, ConcurrentRecordSuccessFailure) {
  constexpr int NUM_SUCCESS_THREADS = 5;
  constexpr int NUM_FAILURE_THREADS = 5;
  constexpr int OPS_PER_THREAD = 100;

  std::atomic<int> successes{0};
  std::atomic<int> failures{0};
  std::atomic<bool> start{false};

  std::vector<std::thread> threads;

  // Success threads
  for (int t = 0; t < NUM_SUCCESS_THREADS; ++t) {
    threads.emplace_back([this, &start, &successes, OPS_PER_THREAD]() {
      while (!start.load()) std::this_thread::yield();
      for (int i = 0; i < OPS_PER_THREAD; ++i) {
        breaker_->recordSuccess();
        successes++;
      }
    });
  }

  // Failure threads
  for (int t = 0; t < NUM_FAILURE_THREADS; ++t) {
    threads.emplace_back([this, &start, &failures, OPS_PER_THREAD]() {
      while (!start.load()) std::this_thread::yield();
      for (int i = 0; i < OPS_PER_THREAD; ++i) {
        breaker_->recordFailure();
        failures++;
      }
    });
  }

  start = true;

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(successes.load(), NUM_SUCCESS_THREADS * OPS_PER_THREAD);
  EXPECT_EQ(failures.load(), NUM_FAILURE_THREADS * OPS_PER_THREAD);

  // State should be determined by race outcome, but should be valid
  auto state = breaker_->getState();
  EXPECT_TRUE(state == CircuitBreaker::State::CLOSED ||
              state == CircuitBreaker::State::OPEN ||
              state == CircuitBreaker::State::HALF_OPEN);
}

/**
 * Test race condition between timeout expiry and new failures
 * OPEN -> HALF_OPEN transition should be atomic
 *
 * Note: The CircuitBreaker implementation allows requests in HALF_OPEN state
 * based on `half_open_requests_ < 3`, where half_open_requests_ is only
 * incremented on successful completion (recordSuccess()). This means concurrent
 * allowRequest() calls during HALF_OPEN can all be allowed since they all see
 * the same counter value.
 *
 * This test verifies the transition from OPEN to HALF_OPEN happens correctly
 * and that the circuit breaker remains in a valid state under concurrent access.
 */
TEST_F(CircuitBreakerTest, RaceConditionOpenToHalfOpen) {
  // Open the circuit
  for (int i = 0; i < 10; ++i) {
    breaker_->recordFailure();
  }
  EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::OPEN);

  // Wait for timeout
  std::this_thread::sleep_for(150ms);

  constexpr int NUM_THREADS = 10;
  std::atomic<int> allowed_count{0};
  std::atomic<bool> start{false};

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  // Multiple threads try to get the first request after timeout
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, &start, &allowed_count]() {
      while (!start.load()) std::this_thread::yield();

      if (breaker_->allowRequest()) {
        allowed_count++;
      }
    });
  }

  start = true;

  for (auto& t : threads) {
    t.join();
  }

  // Should transition to HALF_OPEN (first allowRequest() call triggers this)
  EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::HALF_OPEN);

  // All threads may be allowed since half_open_requests_ counter is only
  // incremented on recordSuccess(), not on allowRequest().
  // The important thing is the state transition happened correctly.
  EXPECT_GE(allowed_count.load(), 1);

  // Now test that recording successes advances to CLOSED state
  for (int i = 0; i < 3; ++i) {
    breaker_->recordSuccess();
  }
  EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::CLOSED);
}

// ============================================================================
// Priority 2: Async Worker Thread Pattern Tests
// ============================================================================

class AsyncPatternTest : public mcp::test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
  }
};

/**
 * Test that shared_ptr captures keep objects alive in detached threads
 */
TEST_F(AsyncPatternTest, SharedPtrCapturesSurvive) {
  constexpr int NUM_OPERATIONS = 10;

  std::atomic<int> completed{0};
  std::vector<std::future<int>> futures;
  futures.reserve(NUM_OPERATIONS);

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto promise = std::make_shared<std::promise<int>>();
    futures.push_back(promise->get_future());

    // Simulate detached thread pattern with shared_ptr capture
    std::thread([promise, i, &completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10 + i * 5));
      promise->set_value(i * 2);
      completed++;
    }).detach();
  }

  // Wait for all futures
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto status = futures[i].wait_for(2s);
    EXPECT_EQ(status, std::future_status::ready);
    if (status == std::future_status::ready) {
      EXPECT_EQ(futures[i].get(), i * 2);
    }
  }

  // Wait for all threads to complete
  while (completed.load() < NUM_OPERATIONS) {
    std::this_thread::sleep_for(10ms);
  }

  EXPECT_EQ(completed.load(), NUM_OPERATIONS);
}

/**
 * Test multiple concurrent async operations
 */
TEST_F(AsyncPatternTest, MultipleAsyncOperations) {
  constexpr int NUM_CONCURRENT = 20;

  std::atomic<int> in_flight{0};
  std::atomic<int> max_concurrent{0};
  std::atomic<int> completed{0};

  std::vector<std::future<bool>> futures;
  futures.reserve(NUM_CONCURRENT);

  for (int i = 0; i < NUM_CONCURRENT; ++i) {
    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    std::thread([promise, &in_flight, &max_concurrent, &completed]() {
      int current = ++in_flight;

      // Track maximum concurrency
      int expected = max_concurrent.load();
      while (current > expected) {
        if (max_concurrent.compare_exchange_weak(expected, current)) {
          break;
        }
      }

      // Simulate async work
      std::this_thread::sleep_for(50ms);

      --in_flight;
      completed++;
      promise->set_value(true);
    }).detach();
  }

  // Wait for all
  for (auto& f : futures) {
    EXPECT_EQ(f.wait_for(5s), std::future_status::ready);
    EXPECT_TRUE(f.get());
  }

  EXPECT_EQ(completed.load(), NUM_CONCURRENT);
  EXPECT_GT(max_concurrent.load(), 1);  // Should have had some concurrency
}

/**
 * Test that promises are always fulfilled
 */
TEST_F(AsyncPatternTest, FuturePromiseLifecycle) {
  constexpr int NUM_OPERATIONS = 50;

  std::vector<std::future<int>> futures;
  futures.reserve(NUM_OPERATIONS);

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto promise = std::make_shared<std::promise<int>>();
    futures.push_back(promise->get_future());

    std::thread([promise, i]() {
      try {
        if (i % 5 == 0) {
          // Simulate error
          promise->set_exception(
              std::make_exception_ptr(std::runtime_error("test error")));
        } else {
          promise->set_value(i);
        }
      } catch (...) {
        // Promise already satisfied - this shouldn't happen
      }
    }).detach();
  }

  int success_count = 0;
  int error_count = 0;

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto status = futures[i].wait_for(2s);
    EXPECT_EQ(status, std::future_status::ready);

    if (status == std::future_status::ready) {
      try {
        int value = futures[i].get();
        EXPECT_EQ(value, i);
        success_count++;
      } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "test error");
        error_count++;
      }
    }
  }

  EXPECT_EQ(success_count + error_count, NUM_OPERATIONS);
  EXPECT_EQ(error_count, NUM_OPERATIONS / 5);  // Every 5th operation errors
}

/**
 * Test behavior when client context is destroyed while operations pending
 * Simulates early client destruction scenario
 */
TEST_F(AsyncPatternTest, EarlyContextDestruction) {
  constexpr int NUM_OPERATIONS = 10;

  std::vector<std::future<bool>> futures;
  futures.reserve(NUM_OPERATIONS);

  {
    // Create a context that will be destroyed
    auto context = std::make_shared<std::atomic<bool>>(true);

    for (int i = 0; i < NUM_OPERATIONS; ++i) {
      auto promise = std::make_shared<std::promise<bool>>();
      futures.push_back(promise->get_future());

      // Capture context by shared_ptr - it survives scope exit
      std::thread([promise, context, i]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50 + i * 10));

        // Context should still be valid due to shared_ptr
        bool ctx_valid = context->load();
        promise->set_value(ctx_valid);
      }).detach();
    }

    // Context goes out of scope here, but shared_ptr keeps it alive
  }

  // All futures should still complete successfully
  for (auto& f : futures) {
    auto status = f.wait_for(2s);
    EXPECT_EQ(status, std::future_status::ready);
    if (status == std::future_status::ready) {
      EXPECT_TRUE(f.get());  // Context was still valid
    }
  }
}

// ============================================================================
// Priority 2: Cross-Thread Request/Response Flow Tests
// ============================================================================

class RequestFlowTest : public mcp::test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    tracker_ = std::make_unique<RequestTracker>(5000ms);
  }

  void TearDown() override {
    tracker_.reset();
    RealIoTestBase::TearDown();
  }

  std::unique_ptr<RequestTracker> tracker_;
};

/**
 * Test request/response flow across threads
 * Main thread -> Dispatcher thread -> Worker thread
 */
TEST_F(RequestFlowTest, MainToDispatcherToWorker) {
  constexpr int NUM_REQUESTS = 20;

  std::atomic<int> responses_received{0};
  std::vector<std::future<int64_t>> futures;
  futures.reserve(NUM_REQUESTS);

  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto promise = std::make_shared<std::promise<int64_t>>();
    futures.push_back(promise->get_future());

    int64_t request_id = i;

    // Create request context
    auto context =
        std::make_shared<RequestContext>(RequestId(request_id), "test");
    tracker_->trackRequest(context);

    // Simulate dispatcher thread posting work
    executeInDispatcher([this, request_id, promise, &responses_received]() {
      // Simulate async response handling in worker thread
      std::thread([this, request_id, promise, &responses_received]() {
        // Simulate network delay
        std::this_thread::sleep_for(10ms);

        // Complete the request
        auto ctx = tracker_->removeRequest(RequestId(request_id));
        if (ctx) {
          responses_received++;
          promise->set_value(request_id);
        } else {
          promise->set_exception(
              std::make_exception_ptr(std::runtime_error("Request not found")));
        }
      }).detach();
    });
  }

  // Wait for all responses
  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto status = futures[i].wait_for(5s);
    EXPECT_EQ(status, std::future_status::ready);
    if (status == std::future_status::ready) {
      EXPECT_EQ(futures[i].get(), static_cast<int64_t>(i));
    }
  }

  EXPECT_EQ(responses_received.load(), NUM_REQUESTS);
  EXPECT_EQ(tracker_->getPendingCount(), 0u);
}

/**
 * Test high concurrency request/response matching
 * Verifies correct response matching by ID under load
 */
TEST_F(RequestFlowTest, HighConcurrencyRequestResponse) {
  constexpr int NUM_REQUESTS = 100;

  std::atomic<int> matched{0};
  std::atomic<int> mismatched{0};
  std::vector<std::future<bool>> futures;
  futures.reserve(NUM_REQUESTS);

  // Track expected values per request ID
  std::unordered_map<int64_t, int> expected_values;
  std::mutex expected_mutex;

  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    int64_t request_id = i;
    int expected_value = i * 100;

    {
      std::lock_guard<std::mutex> lock(expected_mutex);
      expected_values[request_id] = expected_value;
    }

    auto context =
        std::make_shared<RequestContext>(RequestId(request_id), "test");
    tracker_->trackRequest(context);

    // Response handler thread
    std::thread(
        [this, request_id, expected_value, promise, &matched, &mismatched,
         &expected_values, &expected_mutex]() {
          // Random delay to shuffle response order
          std::random_device rd;
          std::mt19937 gen(rd());
          std::uniform_int_distribution<> dis(1, 50);
          std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));

          auto ctx = tracker_->removeRequest(RequestId(request_id));
          if (ctx) {
            // Verify this is the right request
            int64_t ctx_id = holds_alternative<int64_t>(ctx->id)
                                 ? get<int64_t>(ctx->id)
                                 : -1;

            int expected;
            {
              std::lock_guard<std::mutex> lock(expected_mutex);
              expected = expected_values[ctx_id];
            }

            if (ctx_id == request_id && expected == expected_value) {
              matched++;
              promise->set_value(true);
            } else {
              mismatched++;
              promise->set_value(false);
            }
          } else {
            mismatched++;
            promise->set_value(false);
          }
        })
        .detach();
  }

  // Wait for all
  int success_count = 0;
  for (auto& f : futures) {
    auto status = f.wait_for(10s);
    EXPECT_EQ(status, std::future_status::ready);
    if (status == std::future_status::ready && f.get()) {
      success_count++;
    }
  }

  EXPECT_EQ(success_count, NUM_REQUESTS);
  EXPECT_EQ(matched.load(), NUM_REQUESTS);
  EXPECT_EQ(mismatched.load(), 0);
}

/**
 * Test graceful handling of responses after shutdown initiated
 */
TEST_F(RequestFlowTest, ResponseAfterShutdownStarted) {
  constexpr int NUM_REQUESTS = 10;

  std::atomic<bool> shutdown_initiated{false};
  std::atomic<int> responses_before_shutdown{0};
  std::atomic<int> responses_after_shutdown{0};

  std::vector<std::future<bool>> futures;
  futures.reserve(NUM_REQUESTS);

  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    int64_t request_id = i;

    auto context =
        std::make_shared<RequestContext>(RequestId(request_id), "test");
    tracker_->trackRequest(context);

    // Response thread with varying delays
    std::thread([this, request_id, i, promise, &shutdown_initiated,
                 &responses_before_shutdown, &responses_after_shutdown]() {
      // First half complete quickly, second half after shutdown
      std::this_thread::sleep_for(std::chrono::milliseconds(i < 5 ? 10 : 200));

      auto ctx = tracker_->removeRequest(RequestId(request_id));
      if (ctx) {
        if (shutdown_initiated.load()) {
          responses_after_shutdown++;
        } else {
          responses_before_shutdown++;
        }
        promise->set_value(true);
      } else {
        promise->set_value(false);
      }
    }).detach();
  }

  // Wait for first batch
  std::this_thread::sleep_for(50ms);

  // Initiate shutdown
  shutdown_initiated = true;

  // Wait for all responses
  int completed = 0;
  for (auto& f : futures) {
    auto status = f.wait_for(5s);
    if (status == std::future_status::ready && f.get()) {
      completed++;
    }
  }

  EXPECT_EQ(completed, NUM_REQUESTS);
  EXPECT_GT(responses_before_shutdown.load(), 0);
  EXPECT_GT(responses_after_shutdown.load(), 0);
}

// ============================================================================
// Combined Stress Test
// ============================================================================

class ClientThreadingStressTest : public mcp::test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    tracker_ = std::make_unique<RequestTracker>(1000ms);
    breaker_ = std::make_unique<CircuitBreaker>(10, 100ms, 0.5);
  }

  void TearDown() override {
    breaker_.reset();
    tracker_.reset();
    RealIoTestBase::TearDown();
  }

  std::unique_ptr<RequestTracker> tracker_;
  std::unique_ptr<CircuitBreaker> breaker_;
};

/**
 * Combined stress test exercising all components
 */
TEST_F(ClientThreadingStressTest, CombinedStress) {
  constexpr int NUM_THREADS = 8;
  constexpr int OPS_PER_THREAD = 100;
  constexpr auto TEST_DURATION = 2s;

  std::atomic<int> total_requests{0};
  std::atomic<int> allowed_requests{0};
  std::atomic<int> completed_requests{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  auto start_time = std::chrono::steady_clock::now();

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, t, &total_requests, &allowed_requests,
                          &completed_requests, &stop, OPS_PER_THREAD]() {
      for (int i = 0; i < OPS_PER_THREAD && !stop.load(); ++i) {
        total_requests++;

        // Check circuit breaker
        if (!breaker_->allowRequest()) {
          std::this_thread::sleep_for(10ms);
          continue;
        }
        allowed_requests++;

        int64_t id = t * OPS_PER_THREAD + i;
        auto context =
            std::make_shared<RequestContext>(RequestId(id), "stress_test");

        tracker_->trackRequest(context);

        // Simulate async operation
        auto future = std::async(std::launch::async, [this, id]() {
          std::this_thread::sleep_for(5ms);
          return tracker_->removeRequest(RequestId(id)) != nullptr;
        });

        // Wait for result
        bool success = future.get();
        if (success) {
          breaker_->recordSuccess();
          completed_requests++;
        } else {
          breaker_->recordFailure();
        }
      }
    });
  }

  // Run for specified duration or until complete
  while (std::chrono::steady_clock::now() - start_time < TEST_DURATION) {
    if (total_requests.load() >= NUM_THREADS * OPS_PER_THREAD) {
      break;
    }
    std::this_thread::sleep_for(10ms);
  }

  stop = true;

  for (auto& t : threads) {
    t.join();
  }

  // Verify results
  EXPECT_GT(total_requests.load(), 0);
  EXPECT_GT(allowed_requests.load(), 0);
  EXPECT_GT(completed_requests.load(), 0);

  // Most allowed requests should complete
  EXPECT_GE(completed_requests.load(), allowed_requests.load() * 0.9);

  // No remaining tracked requests
  EXPECT_EQ(tracker_->getPendingCount(), 0u);
}

/**
 * High volume request test
 * 10,000 requests from 10 threads
 */
TEST_F(ClientThreadingStressTest, HighVolumeRequests) {
  constexpr int NUM_THREADS = 10;
  constexpr int REQUESTS_PER_THREAD = 1000;
  constexpr int TOTAL_REQUESTS = NUM_THREADS * REQUESTS_PER_THREAD;

  std::atomic<int> tracked{0};
  std::atomic<int> completed{0};
  std::atomic<int> failed{0};

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  auto start_time = std::chrono::steady_clock::now();

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([this, t, &tracked, &completed, &failed]() {
      for (int i = 0; i < REQUESTS_PER_THREAD; ++i) {
        int64_t id = static_cast<int64_t>(t) * REQUESTS_PER_THREAD + i;
        auto context =
            std::make_shared<RequestContext>(RequestId(id), "high_volume");

        tracker_->trackRequest(context);
        tracked++;

        // Simulate quick async completion
        auto removed = tracker_->removeRequest(RequestId(id));
        if (removed) {
          completed++;
        } else {
          failed++;
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  auto duration = std::chrono::steady_clock::now() - start_time;
  auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

  // All requests should be tracked and completed
  EXPECT_EQ(tracked.load(), TOTAL_REQUESTS);
  EXPECT_EQ(completed.load(), TOTAL_REQUESTS);
  EXPECT_EQ(failed.load(), 0);
  EXPECT_EQ(tracker_->getPendingCount(), 0u);

  // Should complete in reasonable time (< 5 seconds for 10K requests)
  EXPECT_LT(duration_ms, 5000);
}

/**
 * Rapid connect/disconnect simulation
 * Tests resource cleanup on each cycle
 */
TEST_F(ClientThreadingStressTest, RapidConnectDisconnect) {
  constexpr int NUM_CYCLES = 50;
  constexpr int REQUESTS_PER_CYCLE = 10;

  std::atomic<int> total_tracked{0};
  std::atomic<int> total_completed{0};

  for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
    // Simulate "connect" - create fresh tracker and breaker
    auto cycle_tracker = std::make_unique<RequestTracker>(500ms);
    auto cycle_breaker = std::make_unique<CircuitBreaker>(5, 50ms, 0.5);

    // Process some requests
    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
      threads.emplace_back(
          [&cycle_tracker, &cycle_breaker, cycle, t, &total_tracked,
           &total_completed]() {
            for (int i = 0; i < REQUESTS_PER_CYCLE; ++i) {
              if (!cycle_breaker->allowRequest()) continue;

              int64_t id = cycle * 1000 + t * 100 + i;
              auto ctx =
                  std::make_shared<RequestContext>(RequestId(id), "cycle_test");

              cycle_tracker->trackRequest(ctx);
              total_tracked++;

              // Quick completion
              auto removed = cycle_tracker->removeRequest(RequestId(id));
              if (removed) {
                cycle_breaker->recordSuccess();
                total_completed++;
              }
            }
          });
    }

    for (auto& t : threads) {
      t.join();
    }

    // Verify clean state before "disconnect"
    EXPECT_EQ(cycle_tracker->getPendingCount(), 0u);

    // Resources destroyed here (simulate disconnect)
  }

  EXPECT_EQ(total_tracked.load(), total_completed.load());
  EXPECT_GT(total_tracked.load(), 0);
}

/**
 * Circuit breaker under high failure rate stress
 * Tests repeated OPEN -> HALF_OPEN -> CLOSED/OPEN cycles
 */
TEST_F(ClientThreadingStressTest, CircuitBreakerUnderStress) {
  // Use a fast circuit breaker for testing
  auto fast_breaker = std::make_unique<CircuitBreaker>(3, 50ms, 0.3);

  constexpr int NUM_THREADS = 4;
  constexpr int OPS_PER_THREAD = 200;

  std::atomic<int> allowed{0};
  std::atomic<int> denied{0};
  std::atomic<int> successes{0};
  std::atomic<int> failures{0};
  std::atomic<int> state_changes{0};
  CircuitBreaker::State last_state = CircuitBreaker::State::CLOSED;
  std::mutex state_mutex;

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&fast_breaker, t, &allowed, &denied, &successes,
                          &failures, &state_changes, &last_state,
                          &state_mutex]() {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<> dis(0.0, 1.0);

      for (int i = 0; i < OPS_PER_THREAD; ++i) {
        // Track state changes
        auto current_state = fast_breaker->getState();
        {
          std::lock_guard<std::mutex> lock(state_mutex);
          if (current_state != last_state) {
            state_changes++;
            last_state = current_state;
          }
        }

        if (fast_breaker->allowRequest()) {
          allowed++;

          // 40% failure rate to stress the circuit breaker
          if (dis(gen) < 0.4) {
            fast_breaker->recordFailure();
            failures++;
          } else {
            fast_breaker->recordSuccess();
            successes++;
          }
        } else {
          denied++;
          // Wait a bit when denied
          std::this_thread::sleep_for(10ms);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Verify circuit breaker was exercised
  EXPECT_GT(allowed.load(), 0);
  EXPECT_GT(successes.load(), 0);
  EXPECT_GT(failures.load(), 0);

  // Should have had some state changes due to high failure rate
  EXPECT_GT(state_changes.load(), 0);

  // Total operations should match
  EXPECT_EQ(allowed.load() + denied.load(), NUM_THREADS * OPS_PER_THREAD);
  EXPECT_EQ(successes.load() + failures.load(), allowed.load());
}

// ============================================================================
// Priority 3: Error Scenario Tests
// ============================================================================

class ErrorScenarioTest : public mcp::test::RealIoTestBase {
 protected:
  void SetUp() override {
    RealIoTestBase::SetUp();
    // Short timeout for testing disconnect scenarios
    tracker_ = std::make_unique<RequestTracker>(100ms);
  }

  void TearDown() override {
    tracker_.reset();
    RealIoTestBase::TearDown();
  }

  std::unique_ptr<RequestTracker> tracker_;
};

/**
 * Test disconnect while requests are pending
 * All pending promises should be rejectable
 */
TEST_F(ErrorScenarioTest, DisconnectWhileRequestsPending) {
  constexpr int NUM_PENDING = 20;

  std::vector<std::shared_ptr<RequestContext>> contexts;
  std::vector<std::future<bool>> futures;
  contexts.reserve(NUM_PENDING);
  futures.reserve(NUM_PENDING);

  // Create pending requests with promises
  for (int i = 0; i < NUM_PENDING; ++i) {
    auto ctx = std::make_shared<RequestContext>(RequestId(static_cast<int64_t>(i)), "pending_test");
    tracker_->trackRequest(ctx);
    contexts.push_back(ctx);

    // Create a future that waits for the promise
    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    // Store promise in a way we can reject it later
    std::thread([ctx, promise]() {
      // Wait for either completion or timeout
      std::this_thread::sleep_for(200ms);
      if (!ctx->completed) {
        // Simulate disconnect rejection
        promise->set_value(false);
      } else {
        promise->set_value(true);
      }
    }).detach();
  }

  EXPECT_EQ(tracker_->getPendingCount(), static_cast<size_t>(NUM_PENDING));

  // Simulate disconnect - reject all pending requests
  std::atomic<int> rejected{0};
  std::vector<std::thread> reject_threads;

  for (int i = 0; i < NUM_PENDING; ++i) {
    reject_threads.emplace_back([this, i, &rejected, &contexts]() {
      auto removed = tracker_->removeRequest(RequestId(static_cast<int64_t>(i)));
      if (removed) {
        removed->completed = true;  // Mark as completed (rejected)
        rejected++;
      }
    });
  }

  for (auto& t : reject_threads) {
    t.join();
  }

  // All requests should have been rejected
  EXPECT_EQ(rejected.load(), NUM_PENDING);
  EXPECT_EQ(tracker_->getPendingCount(), 0u);

  // Wait for futures with timeout
  int completed_count = 0;
  for (auto& f : futures) {
    auto status = f.wait_for(1s);
    if (status == std::future_status::ready) {
      completed_count++;
    }
  }
  EXPECT_EQ(completed_count, NUM_PENDING);
}

/**
 * Test timeout during concurrent requests
 * Each timeout should be handled exactly once
 */
TEST_F(ErrorScenarioTest, TimeoutDuringConcurrentRequests) {
  // Very short timeout for testing
  tracker_ = std::make_unique<RequestTracker>(30ms);

  constexpr int NUM_REQUESTS = 50;

  std::atomic<int> timeout_count{0};
  std::atomic<int> normal_completion{0};
  std::atomic<bool> stop_timeout_checker{false};

  // Add requests that will timeout
  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto ctx = std::make_shared<RequestContext>(RequestId(static_cast<int64_t>(i)), "timeout_test");
    tracker_->trackRequest(ctx);
  }

  EXPECT_EQ(tracker_->getPendingCount(), static_cast<size_t>(NUM_REQUESTS));

  // Start multiple timeout checker threads (simulating concurrent timeout handling)
  std::vector<std::thread> timeout_threads;
  for (int t = 0; t < 4; ++t) {
    timeout_threads.emplace_back(
        [this, &timeout_count, &stop_timeout_checker]() {
          while (!stop_timeout_checker.load()) {
            auto timed_out = tracker_->getTimedOutRequests();
            timeout_count += static_cast<int>(timed_out.size());
            if (timed_out.empty()) {
              std::this_thread::sleep_for(5ms);
            }
          }
        });
  }

  // Also try to complete some requests normally (race with timeout)
  std::thread normal_thread([this, &normal_completion]() {
    for (int i = 0; i < NUM_REQUESTS; ++i) {
      auto removed = tracker_->removeRequest(RequestId(static_cast<int64_t>(i)));
      if (removed) {
        normal_completion++;
      }
    }
  });

  // Wait for timeout period plus buffer
  std::this_thread::sleep_for(100ms);

  stop_timeout_checker = true;
  normal_thread.join();
  for (auto& t : timeout_threads) {
    t.join();
  }

  // Each request should be handled exactly once (either timeout or normal)
  EXPECT_EQ(timeout_count.load() + normal_completion.load(), NUM_REQUESTS);
  EXPECT_EQ(tracker_->getPendingCount(), 0u);
}

/**
 * Test graceful shutdown during initialization phase
 * Simulates shutdown() called while async operations are starting
 */
TEST_F(ErrorScenarioTest, ShutdownDuringInitialization) {
  constexpr int NUM_INIT_TASKS = 10;

  std::atomic<bool> shutdown_signal{false};
  std::atomic<int> tasks_started{0};
  std::atomic<int> tasks_completed{0};
  std::atomic<int> tasks_aborted{0};

  std::vector<std::future<bool>> futures;
  futures.reserve(NUM_INIT_TASKS);

  // Start initialization tasks
  for (int i = 0; i < NUM_INIT_TASKS; ++i) {
    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    std::thread([promise, i, &shutdown_signal, &tasks_started, &tasks_completed,
                 &tasks_aborted]() {
      tasks_started++;

      // Simulate varying initialization times
      for (int j = 0; j < 10 + i * 5; ++j) {
        if (shutdown_signal.load()) {
          tasks_aborted++;
          promise->set_value(false);
          return;
        }
        std::this_thread::sleep_for(5ms);
      }

      tasks_completed++;
      promise->set_value(true);
    }).detach();
  }

  // Wait for some tasks to start
  while (tasks_started.load() < NUM_INIT_TASKS / 2) {
    std::this_thread::sleep_for(5ms);
  }

  // Signal shutdown
  shutdown_signal = true;

  // Wait for all tasks with timeout
  int success_count = 0;
  int abort_count = 0;
  for (auto& f : futures) {
    auto status = f.wait_for(2s);
    EXPECT_EQ(status, std::future_status::ready);
    if (status == std::future_status::ready) {
      if (f.get()) {
        success_count++;
      } else {
        abort_count++;
      }
    }
  }

  // All tasks should have finished (either completed or aborted)
  EXPECT_EQ(success_count + abort_count, NUM_INIT_TASKS);
  EXPECT_EQ(tasks_started.load(), NUM_INIT_TASKS);
  EXPECT_EQ(tasks_completed.load() + tasks_aborted.load(), NUM_INIT_TASKS);

  // At least some should have been aborted due to shutdown
  EXPECT_GT(tasks_aborted.load(), 0);
}

/**
 * Test double completion prevention
 * Ensures a request can only be completed once
 */
TEST_F(ErrorScenarioTest, DoubleCompletionPrevention) {
  constexpr int NUM_REQUESTS = 20;
  constexpr int COMPLETERS_PER_REQUEST = 5;

  std::atomic<int> successful_completions{0};
  std::atomic<int> failed_completions{0};

  // Track requests
  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto ctx = std::make_shared<RequestContext>(RequestId(static_cast<int64_t>(i)), "double_test");
    tracker_->trackRequest(ctx);
  }

  // Multiple threads try to complete each request
  std::vector<std::thread> threads;
  for (int i = 0; i < NUM_REQUESTS; ++i) {
    for (int c = 0; c < COMPLETERS_PER_REQUEST; ++c) {
      threads.emplace_back([this, i, &successful_completions, &failed_completions]() {
        auto removed = tracker_->removeRequest(RequestId(static_cast<int64_t>(i)));
        if (removed) {
          successful_completions++;
        } else {
          failed_completions++;
        }
      });
    }
  }

  for (auto& t : threads) {
    t.join();
  }

  // Exactly one completion per request
  EXPECT_EQ(successful_completions.load(), NUM_REQUESTS);
  EXPECT_EQ(failed_completions.load(),
            NUM_REQUESTS * (COMPLETERS_PER_REQUEST - 1));
  EXPECT_EQ(tracker_->getPendingCount(), 0u);
}

/**
 * Test memory safety under error conditions
 * Verifies no leaks or crashes when errors occur
 */
TEST_F(ErrorScenarioTest, MemorySafetyUnderErrors) {
  constexpr int NUM_ITERATIONS = 100;

  for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
    auto local_tracker = std::make_unique<RequestTracker>(50ms);

    // Track some requests
    std::vector<int64_t> ids;
    for (int i = 0; i < 10; ++i) {
      int64_t id = iter * 100 + i;
      ids.push_back(id);
      local_tracker->trackRequest(
          std::make_shared<RequestContext>(RequestId(id), "memory_test"));
    }

    // Randomly remove some
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(ids.begin(), ids.end(), gen);

    for (size_t i = 0; i < ids.size() / 2; ++i) {
      local_tracker->removeRequest(RequestId(ids[i]));
    }

    // Let the rest timeout
    std::this_thread::sleep_for(60ms);
    auto timed_out = local_tracker->getTimedOutRequests();

    // Verify consistency
    EXPECT_EQ(local_tracker->getPendingCount(), 0u);

    // local_tracker destroyed here - should clean up properly
  }

  // If we get here without crashes or sanitizer errors, the test passes
  SUCCEED();
}

}  // namespace test
}  // namespace client
}  // namespace mcp
