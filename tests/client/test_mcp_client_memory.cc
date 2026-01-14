/**
 * @file test_mcp_client_memory.cc
 * @brief Dynamic memory management tests for MCP C++ client components
 *
 * This test file verifies memory safety of the client:
 * - RequestTracker: weak_ptr leak verification
 * - CircuitBreaker: constant memory footprint
 * - Client lifecycle: constructor/destructor cleanup
 * - Detached threads: completion safety
 *
 * Run with AddressSanitizer for comprehensive leak detection:
 *   cmake -DENABLE_ASAN=ON ..
 *   make test_mcp_client_memory
 *   ./tests/client/test_mcp_client_memory
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "mcp/client/mcp_client.h"

namespace mcp {
namespace client {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Suite 1: RequestTracker Memory Tests
// ============================================================================

class RequestTrackerMemoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Short timeout for memory tests
    tracker_ = std::make_unique<RequestTracker>(50ms);
  }

  void TearDown() override { tracker_.reset(); }

  // Helper to create a request context with a given ID
  RequestTracker::RequestPtr createRequest(int64_t id) {
    return std::make_shared<RequestContext>(RequestId(id), "test_method");
  }

  std::unique_ptr<RequestTracker> tracker_;
};

/**
 * Test 1.1: Verify timeout cleanup releases all RequestContext objects
 * Uses weak_ptr to detect leaked objects
 */
TEST_F(RequestTrackerMemoryTest, TimeoutCleansUpCompletely) {
  constexpr int NUM_REQUESTS = 1000;

  // Track weak_ptrs to verify cleanup
  std::vector<std::weak_ptr<RequestContext>> weak_refs;
  weak_refs.reserve(NUM_REQUESTS);

  // Create and track requests
  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto request = createRequest(i);
    weak_refs.push_back(request);  // Store weak reference
    tracker_->trackRequest(request);
  }

  // Verify all requests are tracked
  EXPECT_EQ(tracker_->getPendingCount(), NUM_REQUESTS);

  // Wait for timeout
  std::this_thread::sleep_for(100ms);

  // Get timed out requests (removes from tracker)
  auto timed_out = tracker_->getTimedOutRequests();
  EXPECT_EQ(timed_out.size(), NUM_REQUESTS);

  // Clear the returned vector - this should be the last reference
  timed_out.clear();

  // Verify tracker is empty
  EXPECT_EQ(tracker_->getPendingCount(), 0);

  // Verify all weak_ptrs are expired (no memory leaks)
  int leaked_count = 0;
  for (const auto& weak : weak_refs) {
    if (!weak.expired()) {
      leaked_count++;
    }
  }
  EXPECT_EQ(leaked_count, 0) << "Detected " << leaked_count
                             << " leaked RequestContext objects";
}

/**
 * Test 1.2: Concurrent completion doesn't leak memory
 * Multiple threads try to complete the same request
 */
TEST_F(RequestTrackerMemoryTest, ConcurrentCompletionNoLeak) {
  constexpr int NUM_REQUESTS = 100;
  constexpr int COMPLETERS_PER_REQUEST = 5;

  std::vector<std::weak_ptr<RequestContext>> weak_refs;
  weak_refs.reserve(NUM_REQUESTS);

  // Create and track requests
  for (int i = 0; i < NUM_REQUESTS; ++i) {
    auto request = createRequest(i);
    weak_refs.push_back(request);
    tracker_->trackRequest(request);
  }

  // Multiple threads try to complete each request
  std::atomic<int> successful_completions{0};
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_REQUESTS; ++i) {
    for (int c = 0; c < COMPLETERS_PER_REQUEST; ++c) {
      threads.emplace_back([this, i, &successful_completions]() {
        auto removed = tracker_->removeRequest(RequestId(static_cast<int64_t>(i)));
        if (removed) {
          successful_completions++;
        }
      });
    }
  }

  for (auto& t : threads) {
    t.join();
  }

  // Exactly one completion per request
  EXPECT_EQ(successful_completions.load(), NUM_REQUESTS);

  // Tracker should be empty
  EXPECT_EQ(tracker_->getPendingCount(), 0);

  // All weak_ptrs should be expired (no leaks)
  int leaked_count = 0;
  for (const auto& weak : weak_refs) {
    if (!weak.expired()) {
      leaked_count++;
    }
  }
  EXPECT_EQ(leaked_count, 0) << "Detected " << leaked_count
                             << " leaked RequestContext objects";
}

/**
 * Test 1.3: Tracker destruction cleans up all pending requests
 */
TEST_F(RequestTrackerMemoryTest, TrackerDestructionCleansUp) {
  constexpr int NUM_REQUESTS = 100;

  std::vector<std::weak_ptr<RequestContext>> weak_refs;

  // Create tracker in a scope to test destruction cleanup
  {
    auto local_tracker = std::make_unique<RequestTracker>(60000ms);

    for (int i = 0; i < NUM_REQUESTS; ++i) {
      auto request = createRequest(i);
      weak_refs.push_back(request);
      local_tracker->trackRequest(request);
    }

    EXPECT_EQ(local_tracker->getPendingCount(), NUM_REQUESTS);

    // Tracker destroyed here - should release all shared_ptrs
  }

  // All weak_ptrs should be expired after tracker destruction
  int leaked_count = 0;
  for (const auto& weak : weak_refs) {
    if (!weak.expired()) {
      leaked_count++;
    }
  }
  EXPECT_EQ(leaked_count, 0) << "Tracker destruction leaked " << leaked_count
                             << " RequestContext objects";
}

/**
 * Test 1.4: Large scale memory stability
 * Track and complete many requests in batches to verify no memory growth
 */
TEST_F(RequestTrackerMemoryTest, LargeScaleMemoryStability) {
  constexpr int BATCHES = 100;
  constexpr int REQUESTS_PER_BATCH = 1000;

  // Use longer timeout for this test
  tracker_ = std::make_unique<RequestTracker>(60000ms);

  for (int batch = 0; batch < BATCHES; ++batch) {
    std::vector<std::weak_ptr<RequestContext>> weak_refs;

    // Add batch of requests
    for (int i = 0; i < REQUESTS_PER_BATCH; ++i) {
      int64_t id = batch * REQUESTS_PER_BATCH + i;
      auto request = createRequest(id);
      weak_refs.push_back(request);
      tracker_->trackRequest(request);
    }

    EXPECT_EQ(tracker_->getPendingCount(),
              static_cast<size_t>(REQUESTS_PER_BATCH));

    // Complete all requests
    for (int i = 0; i < REQUESTS_PER_BATCH; ++i) {
      int64_t id = batch * REQUESTS_PER_BATCH + i;
      tracker_->removeRequest(RequestId(id));
    }

    // Verify tracker is empty
    EXPECT_EQ(tracker_->getPendingCount(), 0);

    // Verify all weak_ptrs expired
    for (const auto& weak : weak_refs) {
      EXPECT_TRUE(weak.expired()) << "Leak detected in batch " << batch;
    }
  }
}

// ============================================================================
// Suite 2: CircuitBreaker Memory Tests
// ============================================================================

class CircuitBreakerMemoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    breaker_ = std::make_unique<CircuitBreaker>(5, 100ms, 0.5);
  }

  void TearDown() override { breaker_.reset(); }

  std::unique_ptr<CircuitBreaker> breaker_;
};

/**
 * Test 2.1: CircuitBreaker has constant memory footprint
 * Record many operations and verify no memory growth
 */
TEST_F(CircuitBreakerMemoryTest, ConstantMemoryFootprint) {
  constexpr int NUM_OPERATIONS = 1000000;  // 1 million operations

  // Record many success/failure operations
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    breaker_->allowRequest();

    if (i % 10 == 0) {
      breaker_->recordFailure();
    } else {
      breaker_->recordSuccess();
    }
  }

  // CircuitBreaker only uses POD types - no dynamic allocation
  // If ASAN is enabled, it would detect any leaks
  // This test verifies no crash and state is valid
  auto state = breaker_->getState();
  EXPECT_TRUE(state == CircuitBreaker::State::CLOSED ||
              state == CircuitBreaker::State::OPEN ||
              state == CircuitBreaker::State::HALF_OPEN);
}

/**
 * Test 2.2: State transition cycles don't accumulate memory
 */
TEST_F(CircuitBreakerMemoryTest, StateTransitionNoLeak) {
  constexpr int CYCLES = 1000;

  for (int cycle = 0; cycle < CYCLES; ++cycle) {
    // Force circuit to OPEN by recording failures
    breaker_ = std::make_unique<CircuitBreaker>(3, 10ms, 0.5);

    for (int i = 0; i < 5; ++i) {
      breaker_->allowRequest();
      breaker_->recordFailure();
    }
    EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::OPEN);

    // Wait for timeout to transition to HALF_OPEN
    std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(breaker_->allowRequest());
    EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::HALF_OPEN);

    // Record successes to close circuit
    for (int i = 0; i < 3; ++i) {
      breaker_->allowRequest();
      breaker_->recordSuccess();
    }
    EXPECT_EQ(breaker_->getState(), CircuitBreaker::State::CLOSED);
  }
}

// ============================================================================
// Suite 3: Client Component Lifecycle Tests
// ============================================================================

class ClientComponentLifecycleTest : public ::testing::Test {
 protected:
  McpClientConfig createConfig() {
    McpClientConfig config;
    config.request_timeout = std::chrono::milliseconds(100);
    config.circuit_breaker_threshold = 5;
    config.circuit_breaker_timeout = std::chrono::milliseconds(100);
    return config;
  }
};

/**
 * Test 3.1: RequestTracker and CircuitBreaker creation/destruction cycles
 */
TEST_F(ClientComponentLifecycleTest, ComponentCreationDestructionCycles) {
  constexpr int CYCLES = 100;

  for (int i = 0; i < CYCLES; ++i) {
    // Create components
    auto tracker = std::make_unique<RequestTracker>(1000ms);
    auto breaker = std::make_unique<CircuitBreaker>(5, 100ms, 0.5);

    // Use them briefly
    auto request = std::make_shared<RequestContext>(RequestId(static_cast<int64_t>(i)), "test");
    tracker->trackRequest(request);
    breaker->allowRequest();
    breaker->recordSuccess();

    // Destroy - should clean up all resources
    tracker.reset();
    breaker.reset();
  }

  // If ASAN is enabled, any leak would be detected
  SUCCEED();
}

/**
 * Test 3.2: Rapid component recreation under stress
 */
TEST_F(ClientComponentLifecycleTest, RapidRecreationStress) {
  constexpr int ITERATIONS = 50;
  constexpr int REQUESTS_PER_ITERATION = 100;

  for (int iter = 0; iter < ITERATIONS; ++iter) {
    auto tracker = std::make_unique<RequestTracker>(60000ms);
    auto breaker = std::make_unique<CircuitBreaker>(5, 100ms, 0.5);

    std::vector<std::weak_ptr<RequestContext>> weak_refs;

    // Create and track requests
    for (int i = 0; i < REQUESTS_PER_ITERATION; ++i) {
      auto request =
          std::make_shared<RequestContext>(RequestId(static_cast<int64_t>(i)), "stress_test");
      weak_refs.push_back(request);
      tracker->trackRequest(request);

      // Simulate circuit breaker activity
      if (breaker->allowRequest()) {
        if (i % 3 == 0) {
          breaker->recordFailure();
        } else {
          breaker->recordSuccess();
        }
      }
    }

    // Remove half the requests
    for (int i = 0; i < REQUESTS_PER_ITERATION / 2; ++i) {
      tracker->removeRequest(RequestId(static_cast<int64_t>(i)));
    }

    // Destroy components
    tracker.reset();
    breaker.reset();

    // All weak_ptrs should be expired
    for (const auto& weak : weak_refs) {
      EXPECT_TRUE(weak.expired())
          << "Leak detected in iteration " << iter;
    }
  }
}

// ============================================================================
// Suite 4: Async Pattern Memory Safety Tests
// ============================================================================

class AsyncPatternMemoryTest : public ::testing::Test {};

/**
 * Test 4.1: shared_ptr captures in detached threads survive scope exit
 */
TEST_F(AsyncPatternMemoryTest, SharedPtrCapturesSurvive) {
  constexpr int NUM_OPERATIONS = 20;

  std::vector<std::future<int>> futures;
  std::atomic<int> completed{0};

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto promise = std::make_shared<std::promise<int>>();
    futures.push_back(promise->get_future());

    // Simulate the detached thread pattern used in McpClient
    std::thread([promise, i, &completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10 + (i % 5)));
      promise->set_value(i);
      completed++;
    }).detach();
  }

  // Wait for all futures
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto status = futures[i].wait_for(5s);
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(futures[i].get(), i);
  }

  EXPECT_EQ(completed.load(), NUM_OPERATIONS);
}

/**
 * Test 4.2: Promise/future pairs don't leak on exception
 */
TEST_F(AsyncPatternMemoryTest, PromiseFutureExceptionSafety) {
  constexpr int NUM_OPERATIONS = 50;

  std::atomic<int> exceptions_caught{0};

  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    std::thread([promise, i]() {
      if (i % 2 == 0) {
        promise->set_value(i);
      } else {
        promise->set_exception(
            std::make_exception_ptr(std::runtime_error("test error")));
      }
    }).detach();

    try {
      auto status = future.wait_for(1s);
      ASSERT_EQ(status, std::future_status::ready);
      future.get();
    } catch (const std::runtime_error&) {
      exceptions_caught++;
    }
  }

  EXPECT_EQ(exceptions_caught.load(), NUM_OPERATIONS / 2);
}

/**
 * Test 4.3: Multiple async operations with shared context
 */
TEST_F(AsyncPatternMemoryTest, SharedContextMultipleOperations) {
  // Simulate a request context shared across multiple async operations
  struct SharedContext {
    std::atomic<int> operation_count{0};
    std::mutex mutex;
    std::vector<int> results;
  };

  auto context = std::make_shared<SharedContext>();
  std::weak_ptr<SharedContext> weak_context = context;

  constexpr int NUM_THREADS = 10;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([context, i]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(5 * (i % 3)));

      std::lock_guard<std::mutex> lock(context->mutex);
      context->results.push_back(i);
      context->operation_count++;
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(context->operation_count.load(), NUM_THREADS);
  EXPECT_EQ(context->results.size(), NUM_THREADS);

  // Release our reference
  context.reset();

  // Verify context is properly cleaned up
  EXPECT_TRUE(weak_context.expired());
}

/**
 * Test 4.4: Request context with timeout timer cleanup
 */
TEST_F(AsyncPatternMemoryTest, RequestContextTimerCleanup) {
  constexpr int NUM_CONTEXTS = 100;

  std::vector<std::weak_ptr<RequestContext>> weak_refs;

  {
    std::vector<std::shared_ptr<RequestContext>> contexts;

    for (int i = 0; i < NUM_CONTEXTS; ++i) {
      auto ctx = std::make_shared<RequestContext>(
          RequestId(static_cast<int64_t>(i)), "timer_test");
      // Note: timeout_timer would be set by actual client
      // Here we just verify the context cleanup
      weak_refs.push_back(ctx);
      contexts.push_back(ctx);
    }

    EXPECT_EQ(contexts.size(), NUM_CONTEXTS);
    // contexts destroyed here
  }

  // All weak_ptrs should be expired
  for (const auto& weak : weak_refs) {
    EXPECT_TRUE(weak.expired());
  }
}

// ============================================================================
// Suite 5: Stress Tests for Memory Safety
// ============================================================================

class MemoryStressTest : public ::testing::Test {};

/**
 * Test 5.1: Combined stress test with all components
 */
TEST_F(MemoryStressTest, CombinedComponentStress) {
  constexpr int NUM_ITERATIONS = 10;
  constexpr int REQUESTS_PER_ITERATION = 500;
  constexpr int NUM_THREADS = 8;

  for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
    auto tracker = std::make_unique<RequestTracker>(60000ms);
    auto breaker = std::make_unique<CircuitBreaker>(5, 100ms, 0.5);

    std::atomic<int> tracked{0};
    std::atomic<int> removed{0};
    std::vector<std::thread> threads;

    // Concurrent tracking and removal
    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back(
          [&tracker, &breaker, &tracked, &removed, t]() {
            constexpr int OPS_PER_THREAD = 500 / 8;  // REQUESTS_PER_ITERATION / NUM_THREADS
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
              int64_t id = t * 10000 + i;

              // Circuit breaker check
              if (breaker->allowRequest()) {
                auto request =
                    std::make_shared<RequestContext>(RequestId(id), "stress");
                tracker->trackRequest(request);
                tracked++;

                // Randomly decide to complete or let timeout
                if (i % 3 != 0) {
                  auto result = tracker->removeRequest(RequestId(id));
                  if (result) {
                    removed++;
                    breaker->recordSuccess();
                  }
                } else {
                  breaker->recordFailure();
                }
              }
            }
          });
    }

    for (auto& t : threads) {
      t.join();
    }

    // Cleanup remaining requests
    while (tracker->getPendingCount() > 0) {
      auto timed_out = tracker->getTimedOutRequests();
      // Process timed out requests...
      if (timed_out.empty()) {
        // Force remove remaining
        break;
      }
    }

    tracker.reset();
    breaker.reset();
  }

  SUCCEED();
}

/**
 * Test 5.2: Memory safety with error injection
 */
TEST_F(MemoryStressTest, ErrorInjectionMemorySafety) {
  constexpr int ITERATIONS = 100;

  for (int iter = 0; iter < ITERATIONS; ++iter) {
    std::vector<std::weak_ptr<RequestContext>> weak_refs;

    auto tracker = std::make_unique<RequestTracker>(100ms);

    // Create requests
    for (int i = 0; i < 50; ++i) {
      auto request = std::make_shared<RequestContext>(
          RequestId(static_cast<int64_t>(i)), "error_test");
      weak_refs.push_back(request);
      tracker->trackRequest(request);
    }

    // Simulate various error scenarios
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 3);

    for (int i = 0; i < 50; ++i) {
      switch (dis(gen)) {
        case 0:
          // Normal removal
          tracker->removeRequest(RequestId(static_cast<int64_t>(i)));
          break;
        case 1:
          // Double removal attempt
          tracker->removeRequest(RequestId(static_cast<int64_t>(i)));
          tracker->removeRequest(RequestId(static_cast<int64_t>(i)));
          break;
        case 2:
          // Get without remove
          tracker->getRequest(RequestId(static_cast<int64_t>(i)));
          break;
        case 3:
          // Let it timeout
          break;
      }
    }

    // Wait for any remaining to timeout
    std::this_thread::sleep_for(150ms);
    auto timed_out = tracker->getTimedOutRequests();
    timed_out.clear();

    // Destroy tracker
    tracker.reset();

    // Verify all contexts are cleaned up
    for (const auto& weak : weak_refs) {
      EXPECT_TRUE(weak.expired())
          << "Memory leak in error injection iteration " << iter;
    }
  }
}

}  // namespace test
}  // namespace client
}  // namespace mcp

// Main entry point
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
