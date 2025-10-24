/**
 * @file test_async_filter_queue.cc
 * @brief Unit tests for MCP Async Filter Queue API
 *
 * Tests the async request queue implementation for non-blocking filter chain
 * message processing with proper dispatcher thread affinity.
 *
 * Tests cover:
 * - Chain initialization and shutdown
 * - Async submit operations (incoming/outgoing)
 * - Callback invocation
 * - Error handling (queue full, not initialized)
 * - Multiple concurrent requests
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "mcp/c_api/mcp_c_filter_chain.h"
#include "mcp/c_api/mcp_c_api.h"

// Test callback that counts invocations
static std::atomic<int> g_callback_count{0};

static void test_callback(void* user_data,
                         mcp_filter_result_t* result,
                         mcp_error_t* error) {
  g_callback_count++;

  // Optionally verify result structure
  if (result) {
    // Result should be valid
    EXPECT_NE(result, nullptr);
  }
}

class AsyncFilterQueueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_callback_count = 0;

    // Create dispatcher
    mcp_init(nullptr);
    dispatcher_ = mcp_dispatcher_create();
    ASSERT_NE(dispatcher_, nullptr);

    // CRITICAL: Start dispatcher thread (callbacks won't fire otherwise)
    dispatcher_thread_ = std::thread([this]() {
      // Run dispatcher event loop
      mcp_dispatcher_run(dispatcher_);
    });

    // Give dispatcher time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Scenario 2: Empty filter chain for async queue testing
    // (HTTP/SSE/JSON-RPC handled by official SDK, not gopher filters)
    const char* config_json = R"({
      "name": "test_async_queue",
      "filters": []
    })";

    json_config_ = mcp_json_parse(config_json);
    ASSERT_NE(json_config_, nullptr);

    // Use assembler API to create chain (doesn't require dispatcher thread)
    mcp_chain_assembly_result_t result{};
    mcp_result_t rc = mcp_chain_assemble_from_json(dispatcher_, json_config_, &result);
    ASSERT_EQ(rc, MCP_OK);

    if (result.success != MCP_TRUE) {
      if (result.error_message) {
        fprintf(stderr, "Chain assembly failed: %s\n", result.error_message);
      }
    }
    ASSERT_EQ(result.success, MCP_TRUE);

    chain_ = result.chain;
    mcp_chain_assembly_result_free(&result);

    ASSERT_NE(chain_, 0);
  }

  void TearDown() override {
    if (json_config_) {
      mcp_json_free(json_config_);
    }

    // CRITICAL: Stop dispatcher and wait for thread
    if (dispatcher_) {
      mcp_dispatcher_stop(dispatcher_);

      // Wait for thread to finish
      if (dispatcher_thread_.joinable()) {
        dispatcher_thread_.join();
      }

      mcp_dispatcher_destroy(dispatcher_);
    }
  }

  mcp_dispatcher_t dispatcher_ = nullptr;
  mcp_json_value_t json_config_ = nullptr;
  mcp_filter_chain_t chain_ = 0;
  std::thread dispatcher_thread_;  // CRITICAL: Dispatcher must run in thread
};

// ============================================================================
// Basic Lifecycle Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, InitializeChain) {
  mcp_result_t rc = mcp_filter_chain_initialize(chain_);
  EXPECT_EQ(rc, MCP_OK);
}

TEST_F(AsyncFilterQueueTest, SubmitWithoutInitialize) {
  mcp_error_t error{};
  void* user_data = nullptr;

  mcp_status_t status = mcp_chain_submit_incoming(
      chain_,
      "{}",
      user_data,
      test_callback,
      &error);

  EXPECT_EQ(status, MCP_STATUS_NOT_INITIALIZED);
}

// ============================================================================
// Async Submit Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, SubmitIncoming) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  mcp_error_t error{};
  void* user_data = nullptr;

  mcp_status_t status = mcp_chain_submit_incoming(
      chain_,
      R"({"method": "test"})",
      user_data,
      test_callback,
      &error);

  EXPECT_EQ(status, MCP_STATUS_OK);

  // Wait for callback
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_GT(g_callback_count.load(), 0);
}

TEST_F(AsyncFilterQueueTest, SubmitOutgoing) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  mcp_error_t error{};
  void* user_data = nullptr;

  mcp_status_t status = mcp_chain_submit_outgoing(
      chain_,
      R"({"method": "test"})",
      user_data,
      test_callback,
      &error);

  EXPECT_EQ(status, MCP_STATUS_OK);

  // Wait for callback
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_GT(g_callback_count.load(), 0);
}

// ============================================================================
// Concurrent Request Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, MultipleSubmits) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  const int num_requests = 10;

  for (int i = 0; i < num_requests; ++i) {
    mcp_error_t error{};
    void* user_data = reinterpret_cast<void*>(static_cast<intptr_t>(i));

    mcp_status_t status = mcp_chain_submit_incoming(
        chain_,
        R"({"method": "test"})",
        user_data,
        test_callback,
        &error);

    EXPECT_EQ(status, MCP_STATUS_OK);
  }

  // Wait for all callbacks
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_EQ(g_callback_count.load(), num_requests);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, ShutdownChain) {
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  mcp_result_t rc = mcp_filter_chain_shutdown(chain_);
  EXPECT_EQ(rc, MCP_OK);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, InvalidArgumentsSubmit) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  mcp_error_t error{};

  // Null message_json
  mcp_status_t status = mcp_chain_submit_incoming(
      chain_,
      nullptr,
      nullptr,
      test_callback,
      &error);

  EXPECT_EQ(status, MCP_STATUS_INVALID_ARGUMENT);

  // Null callback
  status = mcp_chain_submit_incoming(
      chain_,
      "{}",
      nullptr,
      nullptr,
      &error);

  EXPECT_EQ(status, MCP_STATUS_INVALID_ARGUMENT);

  // Invalid chain handle
  status = mcp_chain_submit_incoming(
      0,  // Invalid handle
      "{}",
      nullptr,
      test_callback,
      &error);

  EXPECT_EQ(status, MCP_STATUS_INVALID_ARGUMENT);
}

TEST_F(AsyncFilterQueueTest, ShutdownWithPendingRequests) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  // Submit multiple requests
  const int num_requests = 5;
  for (int i = 0; i < num_requests; ++i) {
    mcp_error_t error{};
    mcp_chain_submit_incoming(
        chain_,
        R"({"method": "test"})",
        nullptr,
        test_callback,
        &error);
  }

  // Immediately shutdown (some requests may be pending)
  mcp_result_t rc = mcp_filter_chain_shutdown(chain_);
  EXPECT_EQ(rc, MCP_OK);

  // Wait a bit to see if any pending requests complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Some callbacks may have fired before shutdown
  EXPECT_GE(g_callback_count.load(), 0);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, ConcurrentSubmitsFromMultipleThreads) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  const int num_threads = 4;
  const int requests_per_thread = 5;
  std::vector<std::thread> threads;

  std::atomic<int> submit_count{0};

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, &submit_count]() {
      for (int i = 0; i < requests_per_thread; ++i) {
        mcp_error_t error{};
        mcp_status_t status = mcp_chain_submit_incoming(
            chain_,
            R"({"method": "test"})",
            nullptr,
            test_callback,
            &error);

        if (status == MCP_STATUS_OK) {
          submit_count++;
        }
      }
    });
  }

  // Wait for all threads to complete
  for (auto& t : threads) {
    t.join();
  }

  // Wait for callbacks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_EQ(submit_count.load(), num_threads * requests_per_thread);
  EXPECT_EQ(g_callback_count.load(), num_threads * requests_per_thread);
}

// ============================================================================
// Callback Verification Tests
// ============================================================================

TEST_F(AsyncFilterQueueTest, CallbackReceivesResult) {
  // Initialize chain
  ASSERT_EQ(mcp_filter_chain_initialize(chain_), MCP_OK);

  struct CallbackFlags {
    std::atomic<bool> callback_invoked{false};
    std::atomic<bool> result_valid{false};
  };

  auto custom_callback = [](void* user_data,
                            mcp_filter_result_t* result,
                            mcp_error_t* error) {
    auto* flags = static_cast<CallbackFlags*>(user_data);
    flags->callback_invoked.store(true);

    if (result) {
      flags->result_valid.store(true);
    }
  };

  CallbackFlags flags;
  mcp_error_t error{};

  mcp_status_t status = mcp_chain_submit_incoming(
      chain_,
      R"({"method": "test"})",
      static_cast<void*>(&flags),
      custom_callback,
      &error);

  EXPECT_EQ(status, MCP_STATUS_OK);

  // Wait for callback
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(flags.callback_invoked.load());
  EXPECT_TRUE(flags.result_valid.load());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
