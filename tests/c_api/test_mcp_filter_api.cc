/**
 * @file test_mcp_filter_api.cc
 * @brief Comprehensive unit tests for MCP Filter C API
 * 
 * This test file provides extensive coverage for the filter API including:
 * - Filter lifecycle management
 * - Filter chain operations
 * - Buffer management and zero-copy operations
 * - Protocol metadata handling
 * - Callback mechanisms
 * - Thread safety
 * - Error handling
 * - Memory management
 * - Statistics and monitoring
 */

#include <gtest/gtest.h>
#include "mcp/c_api/mcp_filter_api.h"
#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_collections.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <random>

namespace {

// ============================================================================
// RAII Wrappers for Resource Management
// ============================================================================

/**
 * RAII wrapper for mcp_filter_t
 * Ensures automatic cleanup of filter resources
 */
class FilterGuard {
public:
    explicit FilterGuard(mcp_filter_t filter = 0) : filter_(filter) {}
    
    FilterGuard(mcp_dispatcher_t dispatcher, const std::string& name,
                mcp_protocol_layer_t layer = MCP_PROTOCOL_LAYER_7_APPLICATION,
                mcp_memory_pool_t memory_pool = nullptr) {
        mcp_filter_config_t config = {};
        config.name = name.c_str();
        config.type = MCP_FILTER_CUSTOM;
        config.layer = layer;
        config.memory_pool = memory_pool;
        filter_ = mcp_filter_create(dispatcher, &config);
    }
    
    ~FilterGuard() {
        if (filter_) {
            mcp_filter_release(filter_);
        }
    }
    
    // Move constructor
    FilterGuard(FilterGuard&& other) noexcept : filter_(other.filter_) {
        other.filter_ = 0;
    }
    
    // Move assignment
    FilterGuard& operator=(FilterGuard&& other) noexcept {
        if (this != &other) {
            if (filter_) {
                mcp_filter_release(filter_);
            }
            filter_ = other.filter_;
            other.filter_ = 0;
        }
        return *this;
    }
    
    // Delete copy operations
    FilterGuard(const FilterGuard&) = delete;
    FilterGuard& operator=(const FilterGuard&) = delete;
    
    mcp_filter_t get() const { return filter_; }
    mcp_filter_t release() {
        mcp_filter_t temp = filter_;
        filter_ = 0;
        return temp;
    }
    
    operator mcp_filter_t() const { return filter_; }
    
private:
    mcp_filter_t filter_;
};

/**
 * RAII wrapper for mcp_filter_chain_t
 */
class FilterChainGuard {
public:
    explicit FilterChainGuard(mcp_filter_chain_t chain = 0) : chain_(chain) {}
    
    ~FilterChainGuard() {
        if (chain_) {
            mcp_filter_chain_release(chain_);
        }
    }
    
    // Move constructor
    FilterChainGuard(FilterChainGuard&& other) noexcept : chain_(other.chain_) {
        other.chain_ = 0;
    }
    
    // Move assignment
    FilterChainGuard& operator=(FilterChainGuard&& other) noexcept {
        if (this != &other) {
            if (chain_) {
                mcp_filter_chain_release(chain_);
            }
            chain_ = other.chain_;
            other.chain_ = 0;
        }
        return *this;
    }
    
    // Delete copy operations
    FilterChainGuard(const FilterChainGuard&) = delete;
    FilterChainGuard& operator=(const FilterChainGuard&) = delete;
    
    mcp_filter_chain_t get() const { return chain_; }
    mcp_filter_chain_t release() {
        mcp_filter_chain_t temp = chain_;
        chain_ = 0;
        return temp;
    }
    
    operator mcp_filter_chain_t() const { return chain_; }
    
private:
    mcp_filter_chain_t chain_;
};

/**
 * RAII wrapper for mcp_buffer_handle_t
 */
class BufferGuard {
public:
    explicit BufferGuard(mcp_buffer_handle_t buffer = 0) : buffer_(buffer) {}
    
    BufferGuard(const uint8_t* data, size_t size, uint32_t flags)
        : buffer_(mcp_filter_buffer_create(data, size, flags)) {}
    
    ~BufferGuard() {
        if (buffer_) {
            mcp_filter_buffer_release(buffer_);
        }
    }
    
    // Move constructor
    BufferGuard(BufferGuard&& other) noexcept : buffer_(other.buffer_) {
        other.buffer_ = 0;
    }
    
    // Move assignment
    BufferGuard& operator=(BufferGuard&& other) noexcept {
        if (this != &other) {
            if (buffer_) {
                mcp_filter_buffer_release(buffer_);
            }
            buffer_ = other.buffer_;
            other.buffer_ = 0;
        }
        return *this;
    }
    
    // Delete copy operations
    BufferGuard(const BufferGuard&) = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;
    
    mcp_buffer_handle_t get() const { return buffer_; }
    mcp_buffer_handle_t release() {
        mcp_buffer_handle_t temp = buffer_;
        buffer_ = 0;
        return temp;
    }
    
    operator mcp_buffer_handle_t() const { return buffer_; }
    
private:
    mcp_buffer_handle_t buffer_;
};

/**
 * RAII wrapper for mcp_filter_chain_builder_t
 */
class FilterChainBuilderGuard {
public:
    explicit FilterChainBuilderGuard(mcp_dispatcher_t dispatcher)
        : builder_(mcp_filter_chain_builder_create(dispatcher)) {}
    
    ~FilterChainBuilderGuard() {
        if (builder_) {
            mcp_filter_chain_builder_destroy(builder_);
        }
    }
    
    // Move constructor
    FilterChainBuilderGuard(FilterChainBuilderGuard&& other) noexcept : builder_(other.builder_) {
        other.builder_ = nullptr;
    }
    
    // Move assignment
    FilterChainBuilderGuard& operator=(FilterChainBuilderGuard&& other) noexcept {
        if (this != &other) {
            if (builder_) {
                mcp_filter_chain_builder_destroy(builder_);
            }
            builder_ = other.builder_;
            other.builder_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    FilterChainBuilderGuard(const FilterChainBuilderGuard&) = delete;
    FilterChainBuilderGuard& operator=(const FilterChainBuilderGuard&) = delete;
    
    mcp_filter_chain_builder_t get() const { return builder_; }
    operator mcp_filter_chain_builder_t() const { return builder_; }
    
private:
    mcp_filter_chain_builder_t builder_;
};

/**
 * RAII wrapper for mcp_buffer_pool_t
 */
class BufferPoolGuard {
public:
    BufferPoolGuard(size_t buffer_size, size_t max_buffers)
        : pool_(mcp_buffer_pool_create(buffer_size, max_buffers)) {}
    
    ~BufferPoolGuard() {
        if (pool_) {
            mcp_buffer_pool_destroy(pool_);
        }
    }
    
    // Move constructor
    BufferPoolGuard(BufferPoolGuard&& other) noexcept : pool_(other.pool_) {
        other.pool_ = nullptr;
    }
    
    // Move assignment
    BufferPoolGuard& operator=(BufferPoolGuard&& other) noexcept {
        if (this != &other) {
            if (pool_) {
                mcp_buffer_pool_destroy(pool_);
            }
            pool_ = other.pool_;
            other.pool_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    BufferPoolGuard(const BufferPoolGuard&) = delete;
    BufferPoolGuard& operator=(const BufferPoolGuard&) = delete;
    
    mcp_buffer_pool_t get() const { return pool_; }
    operator mcp_buffer_pool_t() const { return pool_; }
    
    // Helper to acquire buffer (returns guarded buffer)
    BufferGuard acquire() {
        return BufferGuard(mcp_buffer_pool_acquire(pool_));
    }
    
    // Release buffer back to pool
    void release(mcp_buffer_handle_t buffer) {
        mcp_buffer_pool_release(pool_, buffer);
    }
    
private:
    mcp_buffer_pool_t pool_;
};

/**
 * RAII wrapper for mcp_filter_manager_t
 */
class FilterManagerGuard {
public:
    FilterManagerGuard(mcp_connection_t connection, mcp_dispatcher_t dispatcher)
        : manager_(mcp_filter_manager_create(connection, dispatcher)) {}
    
    ~FilterManagerGuard() {
        if (manager_) {
            mcp_filter_manager_release(manager_);
        }
    }
    
    // Move constructor
    FilterManagerGuard(FilterManagerGuard&& other) noexcept : manager_(other.manager_) {
        other.manager_ = 0;
    }
    
    // Move assignment
    FilterManagerGuard& operator=(FilterManagerGuard&& other) noexcept {
        if (this != &other) {
            if (manager_) {
                mcp_filter_manager_release(manager_);
            }
            manager_ = other.manager_;
            other.manager_ = 0;
        }
        return *this;
    }
    
    // Delete copy operations
    FilterManagerGuard(const FilterManagerGuard&) = delete;
    FilterManagerGuard& operator=(const FilterManagerGuard&) = delete;
    
    mcp_filter_manager_t get() const { return manager_; }
    operator mcp_filter_manager_t() const { return manager_; }
    
private:
    mcp_filter_manager_t manager_;
};

/**
 * RAII wrapper for mcp_dispatcher_t
 */
class DispatcherGuard {
public:
    DispatcherGuard() : dispatcher_(mcp_dispatcher_create()) {}
    
    ~DispatcherGuard() {
        if (dispatcher_) {
            mcp_dispatcher_destroy(dispatcher_);
        }
    }
    
    // Move constructor
    DispatcherGuard(DispatcherGuard&& other) noexcept : dispatcher_(other.dispatcher_) {
        other.dispatcher_ = nullptr;
    }
    
    // Move assignment
    DispatcherGuard& operator=(DispatcherGuard&& other) noexcept {
        if (this != &other) {
            if (dispatcher_) {
                mcp_dispatcher_destroy(dispatcher_);
            }
            dispatcher_ = other.dispatcher_;
            other.dispatcher_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    DispatcherGuard(const DispatcherGuard&) = delete;
    DispatcherGuard& operator=(const DispatcherGuard&) = delete;
    
    mcp_dispatcher_t get() const { return dispatcher_; }
    operator mcp_dispatcher_t() const { return dispatcher_; }
    
private:
    mcp_dispatcher_t dispatcher_;
};

/**
 * RAII wrapper for mcp_memory_pool_t
 */
class MemoryPoolGuard {
public:
    explicit MemoryPoolGuard(size_t size) : pool_(mcp_memory_pool_create(size)) {}
    
    ~MemoryPoolGuard() {
        if (pool_) {
            mcp_memory_pool_destroy(pool_);
        }
    }
    
    // Move constructor
    MemoryPoolGuard(MemoryPoolGuard&& other) noexcept : pool_(other.pool_) {
        other.pool_ = nullptr;
    }
    
    // Move assignment
    MemoryPoolGuard& operator=(MemoryPoolGuard&& other) noexcept {
        if (this != &other) {
            if (pool_) {
                mcp_memory_pool_destroy(pool_);
            }
            pool_ = other.pool_;
            other.pool_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    MemoryPoolGuard(const MemoryPoolGuard&) = delete;
    MemoryPoolGuard& operator=(const MemoryPoolGuard&) = delete;
    
    mcp_memory_pool_t get() const { return pool_; }
    operator mcp_memory_pool_t() const { return pool_; }
    
private:
    mcp_memory_pool_t pool_;
};

/**
 * RAII wrapper for mcp_json_value_t
 */
class JsonValueGuard {
public:
    JsonValueGuard() : value_(mcp_json_create_object()) {}
    explicit JsonValueGuard(mcp_json_value_t value) : value_(value) {}
    
    ~JsonValueGuard() {
        if (value_) {
            mcp_json_release(value_);
        }
    }
    
    // Move constructor
    JsonValueGuard(JsonValueGuard&& other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }
    
    // Move assignment
    JsonValueGuard& operator=(JsonValueGuard&& other) noexcept {
        if (this != &other) {
            if (value_) {
                mcp_json_release(value_);
            }
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    JsonValueGuard(const JsonValueGuard&) = delete;
    JsonValueGuard& operator=(const JsonValueGuard&) = delete;
    
    mcp_json_value_t get() const { return value_; }
    operator mcp_json_value_t() const { return value_; }
    
private:
    mcp_json_value_t value_;
};

// ============================================================================
// Test Utilities
// ============================================================================

// Test data generator
class TestDataGenerator {
public:
    static std::vector<uint8_t> generateRandomData(size_t size) {
        std::vector<uint8_t> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(dis(gen));
        }
        return data;
    }
    
    static std::string generateRandomString(size_t length) {
        const char* charset = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::string result;
        result.reserve(length);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, strlen(charset) - 1);
        
        for (size_t i = 0; i < length; ++i) {
            result += charset[dis(gen)];
        }
        return result;
    }
};

// Callback tracker for testing
class CallbackTracker {
public:
    void recordCallback(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(name);
        cv_.notify_all();
    }
    
    bool waitForCallback(const std::string& name, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, &name]() {
            return std::find(callbacks_.begin(), callbacks_.end(), name) != callbacks_.end();
        });
    }
    
    size_t getCallbackCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_.size();
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.clear();
    }
    
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::string> callbacks_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class McpFilterApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize MCP
        ASSERT_EQ(mcp_init(nullptr), MCP_OK);
        
        // Create dispatcher using RAII
        dispatcher_ = std::make_unique<DispatcherGuard>();
        ASSERT_TRUE(dispatcher_->get() != 0) << "Failed to create dispatcher";
        
        // Create memory pool for testing using RAII
        memory_pool_ = std::make_unique<MemoryPoolGuard>(1024 * 1024); // 1MB pool
        ASSERT_TRUE(memory_pool_->get() != 0) << "Failed to create memory pool";
        
        // Reset callback tracker
        callback_tracker_.reset();
    }
    
    void TearDown() override {
        // RAII handles all cleanup automatically
        // Clear smart pointers in reverse order of creation
        memory_pool_.reset();
        dispatcher_.reset();
        
        // Shutdown MCP
        mcp_shutdown();
    }
    
    // Helper to create a filter using RAII
    FilterGuard createTestFilter(const std::string& name, 
                                 mcp_protocol_layer_t layer = MCP_PROTOCOL_LAYER_7_APPLICATION) {
        return FilterGuard(dispatcher_->get(), name, layer, memory_pool_->get());
    }
    
    // Helper to create builtin filter using RAII
    FilterGuard createBuiltinFilter(mcp_builtin_filter_type_t type) {
        JsonValueGuard config;
        mcp_filter_t filter = mcp_filter_create_builtin(dispatcher_->get(), type, config);
        return FilterGuard(filter);
    }
    
protected:
    std::unique_ptr<DispatcherGuard> dispatcher_;
    std::unique_ptr<MemoryPoolGuard> memory_pool_;
    CallbackTracker callback_tracker_;
};

// ============================================================================
// Filter Lifecycle Tests
// ============================================================================

TEST_F(McpFilterApiTest, CreateAndDestroyFilter) {
    // Test basic filter creation and destruction using RAII
    FilterGuard filter = createTestFilter("lifecycle_test");
    ASSERT_TRUE(filter.get() != 0) << "Failed to create filter";
    
    // Verify filter exists
    mcp_filter_stats_t stats = {};
    EXPECT_EQ(mcp_filter_get_stats(filter, &stats), MCP_OK);
    
    // Filter automatically cleaned up by RAII
}

TEST_F(McpFilterApiTest, FilterReferenceCountingTest) {
    FilterGuard filter = createTestFilter("refcount_test");
    ASSERT_TRUE(filter.get() != 0);
    
    // Retain multiple times
    mcp_filter_retain(filter);
    mcp_filter_retain(filter);
    mcp_filter_retain(filter);
    
    // Release same number of times
    mcp_filter_release(filter);
    mcp_filter_release(filter);
    mcp_filter_release(filter);
    
    // Filter should still be valid (original reference)
    mcp_filter_stats_t stats = {};
    EXPECT_EQ(mcp_filter_get_stats(filter, &stats), MCP_OK);
}

TEST_F(McpFilterApiTest, CreateAllBuiltinFilterTypes) {
    struct BuiltinFilterTest {
        mcp_builtin_filter_type_t type;
        const char* description;
    };
    
    BuiltinFilterTest filter_types[] = {
        // Network filters
        {MCP_FILTER_TCP_PROXY, "TCP Proxy"},
        {MCP_FILTER_UDP_PROXY, "UDP Proxy"},
        
        // HTTP filters
        {MCP_FILTER_HTTP_CODEC, "HTTP Codec"},
        {MCP_FILTER_HTTP_ROUTER, "HTTP Router"},
        {MCP_FILTER_HTTP_COMPRESSION, "HTTP Compression"},
        
        // Security filters
        {MCP_FILTER_TLS_TERMINATION, "TLS Termination"},
        {MCP_FILTER_AUTHENTICATION, "Authentication"},
        {MCP_FILTER_AUTHORIZATION, "Authorization"},
        
        // Observability
        {MCP_FILTER_ACCESS_LOG, "Access Log"},
        {MCP_FILTER_METRICS, "Metrics"},
        {MCP_FILTER_TRACING, "Tracing"},
        
        // Traffic management
        {MCP_FILTER_RATE_LIMIT, "Rate Limit"},
        {MCP_FILTER_CIRCUIT_BREAKER, "Circuit Breaker"},
        {MCP_FILTER_RETRY, "Retry"},
        {MCP_FILTER_LOAD_BALANCER, "Load Balancer"},
    };
    
    // Store filters in vector to ensure proper RAII cleanup
    std::vector<FilterGuard> filters;
    for (const auto& test : filter_types) {
        FilterGuard filter = createBuiltinFilter(test.type);
        ASSERT_TRUE(filter.get() != 0) << "Failed to create " << test.description << " filter";
        filters.push_back(std::move(filter));
    }
}

// ============================================================================
// Callback Tests
// ============================================================================

// Callback functions for testing
static mcp_filter_status_t test_data_callback(mcp_buffer_handle_t buffer,
                                             mcp_bool_t end_stream,
                                             void* user_data) {
    auto* tracker = static_cast<CallbackTracker*>(user_data);
    tracker->recordCallback("on_data");
    
    // Verify buffer is valid
    EXPECT_NE(buffer, 0u);
    size_t length = mcp_filter_buffer_length(buffer);
    EXPECT_GT(length, 0u);
    
    return MCP_FILTER_CONTINUE;
}

static mcp_filter_status_t test_write_callback(mcp_buffer_handle_t buffer,
                                              mcp_bool_t end_stream,
                                              void* user_data) {
    auto* tracker = static_cast<CallbackTracker*>(user_data);
    tracker->recordCallback("on_write");
    return MCP_FILTER_CONTINUE;
}

static mcp_filter_status_t test_event_callback(mcp_connection_state_t state,
                                              void* user_data) {
    auto* tracker = static_cast<CallbackTracker*>(user_data);
    tracker->recordCallback("on_event");
    return MCP_FILTER_CONTINUE;
}

static void test_error_callback(mcp_filter_t filter,
                               mcp_filter_error_t error,
                               const char* message,
                               void* user_data) {
    auto* tracker = static_cast<CallbackTracker*>(user_data);
    tracker->recordCallback("on_error");
}

TEST_F(McpFilterApiTest, SetAndTriggerCallbacks) {
    FilterGuard filter = createTestFilter("callback_test");
    ASSERT_TRUE(filter.get() != 0);
    
    // Set up callbacks
    mcp_filter_callbacks_t callbacks = {};
    callbacks.on_data = test_data_callback;
    callbacks.on_write = test_write_callback;
    callbacks.on_new_connection = test_event_callback;
    callbacks.on_error = test_error_callback;
    callbacks.user_data = &callback_tracker_;
    
    ASSERT_EQ(mcp_filter_set_callbacks(filter, &callbacks), MCP_OK);
    
    // Create test data using RAII
    std::vector<uint8_t> test_data = TestDataGenerator::generateRandomData(1024);
    BufferGuard buffer(test_data.data(), test_data.size(), MCP_BUFFER_FLAG_READONLY);
    ASSERT_NE(buffer.get(), 0u);
    
    // Trigger callbacks would normally happen through filter chain processing
    // Here we verify the setup was successful
    
    // Buffer automatically released by RAII
}

// ============================================================================
// Protocol Metadata Tests
// ============================================================================

TEST_F(McpFilterApiTest, SetAndGetProtocolMetadata) {
    FilterGuard filter = createTestFilter("metadata_test");
    ASSERT_TRUE(filter.get() != 0);
    
    // Test L3 metadata
    {
        mcp_protocol_metadata_t metadata = {};
        metadata.layer = MCP_PROTOCOL_LAYER_3_NETWORK;
        metadata.data.l3.src_ip = 0x0100007F; // 127.0.0.1
        metadata.data.l3.dst_ip = 0x08080808; // 8.8.8.8
        metadata.data.l3.protocol = 6; // TCP
        metadata.data.l3.ttl = 64;
        
        ASSERT_EQ(mcp_filter_set_protocol_metadata(filter, &metadata), MCP_OK);
        
        mcp_protocol_metadata_t retrieved = {};
        ASSERT_EQ(mcp_filter_get_protocol_metadata(filter, &retrieved), MCP_OK);
        EXPECT_EQ(retrieved.layer, MCP_PROTOCOL_LAYER_3_NETWORK);
        EXPECT_EQ(retrieved.data.l3.src_ip, metadata.data.l3.src_ip);
        EXPECT_EQ(retrieved.data.l3.dst_ip, metadata.data.l3.dst_ip);
    }
    
    // Test L4 metadata
    {
        mcp_protocol_metadata_t metadata = {};
        metadata.layer = MCP_PROTOCOL_LAYER_4_TRANSPORT;
        metadata.data.l4.src_port = 12345;
        metadata.data.l4.dst_port = 443;
        metadata.data.l4.protocol = MCP_TRANSPORT_PROTOCOL_TCP;
        metadata.data.l4.sequence_num = 1000;
        
        ASSERT_EQ(mcp_filter_set_protocol_metadata(filter, &metadata), MCP_OK);
        
        mcp_protocol_metadata_t retrieved = {};
        ASSERT_EQ(mcp_filter_get_protocol_metadata(filter, &retrieved), MCP_OK);
        EXPECT_EQ(retrieved.layer, MCP_PROTOCOL_LAYER_4_TRANSPORT);
        EXPECT_EQ(retrieved.data.l4.src_port, metadata.data.l4.src_port);
        EXPECT_EQ(retrieved.data.l4.dst_port, metadata.data.l4.dst_port);
    }
    
    // Test L7 metadata
    {
        mcp_protocol_metadata_t metadata = {};
        metadata.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;
        metadata.data.l7.protocol = MCP_APP_PROTOCOL_HTTP;
        metadata.data.l7.method = "GET";
        metadata.data.l7.path = "/api/v1/test";
        metadata.data.l7.status_code = 200;
        
        ASSERT_EQ(mcp_filter_set_protocol_metadata(filter, &metadata), MCP_OK);
        
        mcp_protocol_metadata_t retrieved = {};
        ASSERT_EQ(mcp_filter_get_protocol_metadata(filter, &retrieved), MCP_OK);
        EXPECT_EQ(retrieved.layer, MCP_PROTOCOL_LAYER_7_APPLICATION);
        EXPECT_EQ(retrieved.data.l7.protocol, metadata.data.l7.protocol);
        EXPECT_EQ(retrieved.data.l7.status_code, metadata.data.l7.status_code);
    }
}

// ============================================================================
// Filter Chain Tests
// ============================================================================

TEST_F(McpFilterApiTest, CreateAndBuildFilterChain) {
    // Create chain builder using RAII
    FilterChainBuilderGuard builder(dispatcher_->get());
    ASSERT_TRUE(builder.get() != 0);
    
    // Create filters for chain using RAII
    FilterGuard filter1 = createTestFilter("filter1");
    FilterGuard filter2 = createTestFilter("filter2");
    FilterGuard filter3 = createTestFilter("filter3");
    
    ASSERT_TRUE(filter1.get() != 0);
    ASSERT_TRUE(filter2.get() != 0);
    ASSERT_TRUE(filter3.get() != 0);
    
    // Add filters to chain in different positions
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filter1, MCP_FILTER_POSITION_FIRST, 0), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filter2, MCP_FILTER_POSITION_LAST, 0), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filter3, MCP_FILTER_POSITION_AFTER, filter1), MCP_OK);
    
    // Build the chain using RAII
    FilterChainGuard chain(mcp_filter_chain_build(builder));
    ASSERT_TRUE(chain.get() != 0u);
    
    // Chain should still be valid
    mcp_filter_chain_retain(chain);
    mcp_filter_chain_release(chain);
    
    // All resources automatically cleaned up by RAII
}

TEST_F(McpFilterApiTest, FilterChainPositioning) {
    FilterChainBuilderGuard builder(dispatcher_->get());
    ASSERT_TRUE(builder.get() != 0);
    
    // Create multiple filters using RAII
    std::vector<FilterGuard> filters;
    for (int i = 0; i < 5; ++i) {
        std::string name = "chain_filter_" + std::to_string(i);
        filters.push_back(createTestFilter(name));
        ASSERT_TRUE(filters.back().get() != nullptr);
    }
    
    // Test different positioning strategies
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[0], MCP_FILTER_POSITION_FIRST, 0), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[1], MCP_FILTER_POSITION_LAST, 0), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[2], MCP_FILTER_POSITION_BEFORE, filters[1]), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[3], MCP_FILTER_POSITION_AFTER, filters[0]), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, filters[4], MCP_FILTER_POSITION_LAST, 0), MCP_OK);
    
    // Build and verify chain using RAII
    FilterChainGuard chain(mcp_filter_chain_build(builder));
    ASSERT_TRUE(chain.get() != 0u);
    
    // All resources automatically cleaned up by RAII
}

// ============================================================================
// Buffer Management Tests
// ============================================================================

TEST_F(McpFilterApiTest, CreateAndReleaseBuffer) {
    std::vector<uint8_t> data = TestDataGenerator::generateRandomData(1024);
    
    BufferGuard buffer(data.data(), data.size(), MCP_BUFFER_FLAG_READONLY);
    ASSERT_TRUE(buffer.get() != 0u);
    
    // Verify buffer properties
    size_t length = mcp_filter_buffer_length(buffer);
    EXPECT_EQ(length, data.size());
    
    // Buffer automatically released by RAII
}

TEST_F(McpFilterApiTest, BufferSliceOperations) {
    std::vector<uint8_t> data = TestDataGenerator::generateRandomData(4096);
    
    BufferGuard buffer(data.data(), data.size(), MCP_BUFFER_FLAG_READONLY);
    ASSERT_TRUE(buffer.get() != 0u);
    
    // Get buffer slices
    mcp_buffer_slice_t slices[10];
    size_t slice_count = 10;
    
    ASSERT_EQ(mcp_filter_get_buffer_slices(buffer, slices, &slice_count), MCP_OK);
    EXPECT_GT(slice_count, 0);
    
    // Verify slices
    size_t total_length = 0;
    for (size_t i = 0; i < slice_count; ++i) {
        EXPECT_TRUE(slices[i].data != 0);
        EXPECT_GT(slices[i].length, 0);
        total_length += slices[i].length;
    }
    EXPECT_EQ(total_length, data.size());
    
    // Buffer automatically released by RAII
}

TEST_F(McpFilterApiTest, BufferReserveAndCommit) {
    // Create writable buffer using RAII
    BufferGuard buffer(nullptr, 0, MCP_BUFFER_FLAG_OWNED);
    ASSERT_TRUE(buffer.get() != 0u);
    
    // Reserve space
    size_t reserve_size = 1024;
    mcp_buffer_slice_t slice;
    ASSERT_EQ(mcp_filter_reserve_buffer(buffer, reserve_size, &slice), MCP_OK);
    
    // Verify reserved space
    EXPECT_TRUE(slice.data != 0);
    EXPECT_GE(slice.length, reserve_size);
    
    // Write data to reserved space
    const char* test_data = "Hello, Buffer!";
    size_t write_size = strlen(test_data);
    memcpy(const_cast<uint8_t*>(slice.data), test_data, write_size);
    
    // Commit written data
    ASSERT_EQ(mcp_filter_commit_buffer(buffer, write_size), MCP_OK);
    
    // Verify buffer length
    EXPECT_EQ(mcp_filter_buffer_length(buffer), write_size);
    
    // Buffer automatically released by RAII
}

TEST_F(McpFilterApiTest, ZeroCopyBufferOperations) {
    // Create source data
    std::vector<uint8_t> data = TestDataGenerator::generateRandomData(8192);
    
    // Create zero-copy buffer using RAII
    BufferGuard buffer(data.data(), data.size(), MCP_BUFFER_FLAG_ZERO_COPY | MCP_BUFFER_FLAG_EXTERNAL);
    ASSERT_TRUE(buffer.get() != 0u);
    
    // Get slices for zero-copy access
    mcp_buffer_slice_t slices[1];
    size_t slice_count = 1;
    
    ASSERT_EQ(mcp_filter_get_buffer_slices(buffer, slices, &slice_count), MCP_OK);
    EXPECT_EQ(slice_count, 1);
    
    // Verify zero-copy - slice should point to original data
    EXPECT_EQ(slices[0].data, data.data());
    EXPECT_EQ(slices[0].length, data.size());
    EXPECT_TRUE(slices[0].flags & MCP_BUFFER_FLAG_ZERO_COPY);
    
    // Buffer automatically released by RAII
}

// ============================================================================
// Buffer Pool Tests
// ============================================================================

TEST_F(McpFilterApiTest, BufferPoolOperations) {
    // Create buffer pool using RAII
    size_t buffer_size = 4096;
    size_t max_buffers = 10;
    BufferPoolGuard pool(buffer_size, max_buffers);
    ASSERT_TRUE(pool.get() != 0);
    
    // Acquire buffers using RAII
    std::vector<BufferGuard> buffers;
    for (size_t i = 0; i < max_buffers; ++i) {
        BufferGuard buffer = pool.acquire();
        ASSERT_TRUE(buffer.get() != 0u) << "Failed to acquire buffer " << i;
        
        // Verify buffer size
        EXPECT_GE(mcp_filter_buffer_length(buffer), 0);
        
        buffers.push_back(std::move(buffer));
    }
    
    // Pool should be exhausted
    BufferGuard exhausted = pool.acquire();
    EXPECT_EQ(exhausted.get(), 0u) << "Pool should be exhausted";
    
    // Release one buffer back to pool
    pool.release(buffers[0].release());
    
    // Should be able to acquire again
    BufferGuard recycled = pool.acquire();
    EXPECT_TRUE(recycled.get() != 0) << "Should acquire recycled buffer";
    
    // Release remaining buffers properly
    for (size_t i = 1; i < buffers.size(); ++i) {
        if (buffers[i].get()) {
            pool.release(buffers[i].release());
        }
    }
    if (recycled.get()) {
        pool.release(recycled.release());
    }
    
    // Pool automatically destroyed by RAII
}

// ============================================================================
// Filter Manager Tests
// ============================================================================

TEST_F(McpFilterApiTest, FilterManagerOperations) {
    // Create a mock connection (would need actual connection in real test)
    mcp_connection_t connection = 0; // Placeholder
    
    // Create filter manager using RAII
    FilterManagerGuard manager(connection, dispatcher_->get());
    ASSERT_TRUE(manager.get() != 0);
    
    // Create and add filters using RAII
    FilterGuard filter1 = createTestFilter("manager_filter1");
    FilterGuard filter2 = createTestFilter("manager_filter2");
    
    ASSERT_EQ(mcp_filter_manager_add_filter(manager, filter1), MCP_OK);
    ASSERT_EQ(mcp_filter_manager_add_filter(manager, filter2), MCP_OK);
    
    // Create and add filter chain using RAII
    FilterChainBuilderGuard builder(dispatcher_->get());
    FilterGuard chain_filter = createTestFilter("chain_filter");
    mcp_filter_chain_add_filter(builder, chain_filter, MCP_FILTER_POSITION_LAST, 0);
    FilterChainGuard chain(mcp_filter_chain_build(builder));
    
    ASSERT_EQ(mcp_filter_manager_add_chain(manager, chain), MCP_OK);
    
    // Initialize manager
    ASSERT_EQ(mcp_filter_manager_initialize(manager), MCP_OK);
    
    // All resources automatically cleaned up by RAII
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(McpFilterApiTest, ThreadSafeFilterOperations) {
    FilterGuard filter = createTestFilter("thread_safe_test");
    ASSERT_TRUE(filter.get() != 0);
    
    std::atomic<int> counter{0};
    std::vector<std::thread> threads;
    
    // Launch multiple threads performing filter operations
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; ++j) {
                // Retain/release filter
                mcp_filter_retain(filter);
                
                // Get/set metadata
                mcp_protocol_metadata_t metadata = {};
                metadata.layer = MCP_PROTOCOL_LAYER_4_TRANSPORT;
                metadata.data.l4.src_port = j;
                mcp_filter_set_protocol_metadata(filter, &metadata);
                mcp_filter_get_protocol_metadata(filter, &metadata);
                
                // Get stats
                mcp_filter_stats_t stats = {};
                mcp_filter_get_stats(filter, &stats);
                
                mcp_filter_release(filter);
                counter++;
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter.load(), 1000);
}

TEST_F(McpFilterApiTest, ConcurrentPostData) {
    FilterGuard filter = createTestFilter("concurrent_post_test");
    ASSERT_TRUE(filter.get() != 0);
    
    std::atomic<int> completed{0};
    
    auto completion_cb = [](mcp_result_t result, void* user_data) {
        auto* counter = static_cast<std::atomic<int>*>(user_data);
        counter->fetch_add(1);
    };
    
    std::vector<std::thread> threads;
    const int thread_count = 5;
    const int posts_per_thread = 20;
    
    // Launch threads posting data
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < posts_per_thread; ++j) {
                std::string data = "Thread " + std::to_string(i) + " Post " + std::to_string(j);
                mcp_filter_post_data(
                    filter,
                    reinterpret_cast<const uint8_t*>(data.c_str()),
                    data.length(),
                    completion_cb,
                    &completed
                );
                
                // Small delay to simulate real work
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Wait for completions (with timeout)
    auto start = std::chrono::steady_clock::now();
    while (completed.load() < thread_count * posts_per_thread) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // All posts should complete
    EXPECT_EQ(completed.load(), thread_count * posts_per_thread);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(McpFilterApiTest, FilterStatistics) {
    FilterGuard filter = createTestFilter("stats_test");
    ASSERT_TRUE(filter.get() != 0);
    
    // Get initial stats
    mcp_filter_stats_t stats = {};
    ASSERT_EQ(mcp_filter_get_stats(filter, &stats), MCP_OK);
    
    // Initial stats should be zero
    EXPECT_EQ(stats.bytes_processed, 0);
    EXPECT_EQ(stats.packets_processed, 0);
    EXPECT_EQ(stats.errors, 0);
    
    // Simulate processing (would be done through actual filter operations)
    // Here we just verify the stats API works
    
    // Reset stats
    ASSERT_EQ(mcp_filter_reset_stats(filter), MCP_OK);
    
    // Stats should be zero after reset
    ASSERT_EQ(mcp_filter_get_stats(filter, &stats), MCP_OK);
    EXPECT_EQ(stats.bytes_processed, 0);
    EXPECT_EQ(stats.packets_processed, 0);
    EXPECT_EQ(stats.errors, 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(McpFilterApiTest, InvalidParameterHandling) {
    // Test null dispatcher
    mcp_filter_config_t config = {};
    config.name = "test";
    config.type = MCP_FILTER_CUSTOM;
    mcp_filter_t filter = mcp_filter_create(nullptr, &config);
    EXPECT_EQ(filter, 0u);
    
    // Test null config
    filter = mcp_filter_create(dispatcher_->get(), nullptr);
    EXPECT_EQ(filter, 0u);
    
    // Test invalid filter handle
    mcp_filter_stats_t stats = {};
    EXPECT_NE(mcp_filter_get_stats(0, &stats), MCP_OK);
    
    // Test null callbacks
    FilterGuard test_filter = createTestFilter("error_test");
    ASSERT_TRUE(test_filter.get() != 0);
    EXPECT_NE(mcp_filter_set_callbacks(test_filter, nullptr), MCP_OK);
    
    // Test invalid buffer operations
    EXPECT_EQ(mcp_filter_buffer_length(0), 0u);
    mcp_filter_buffer_release(0); // Should not crash
    
    // Test invalid chain operations
    EXPECT_EQ(mcp_filter_chain_build(nullptr), 0u);
    mcp_filter_chain_release(0); // Should not crash
}

TEST_F(McpFilterApiTest, ErrorCallbackTrigger) {
    FilterGuard filter = createTestFilter("error_callback_test");
    ASSERT_TRUE(filter.get() != 0);
    
    // Set up error callback
    mcp_filter_callbacks_t callbacks = {};
    callbacks.on_error = test_error_callback;
    callbacks.user_data = &callback_tracker_;
    
    ASSERT_EQ(mcp_filter_set_callbacks(filter, &callbacks), MCP_OK);
    
    // Error callbacks would be triggered by filter operations
    // This tests the setup mechanism
}

// ============================================================================
// Resource Guard Tests (RAII)
// ============================================================================

TEST_F(McpFilterApiTest, ResourceGuardManagement) {
    // Create resource guard
    mcp_filter_resource_guard_t* guard = mcp_filter_guard_create(dispatcher_->get());
    ASSERT_TRUE(guard != nullptr);
    
    // Create filters and add to guard using RAII
    std::vector<FilterGuard> filters;
    for (int i = 0; i < 5; ++i) {
        std::string name = "guarded_filter_" + std::to_string(i);
        filters.push_back(createTestFilter(name));
        ASSERT_TRUE(filters.back().get() != nullptr);
        
        // Release to guard ownership
        ASSERT_EQ(mcp_filter_guard_add_filter(guard, filters.back().release()), MCP_OK);
    }
    
    // Release guard - should clean up all tracked filters
    mcp_filter_guard_release(guard);
    
    // Guard handles cleanup of released filters
}

TEST_F(McpFilterApiTest, ResourceGuardMultipleGuards) {
    // Create multiple guards
    mcp_filter_resource_guard_t* guard1 = mcp_filter_guard_create(dispatcher_->get());
    mcp_filter_resource_guard_t* guard2 = mcp_filter_guard_create(dispatcher_->get());
    ASSERT_TRUE(guard1 != 0);
    ASSERT_TRUE(guard2 != 0);
    
    // Add filters to different guards using RAII
    FilterGuard filter1 = createTestFilter("guard1_filter");
    FilterGuard filter2 = createTestFilter("guard2_filter");
    
    ASSERT_EQ(mcp_filter_guard_add_filter(guard1, filter1.release()), MCP_OK);
    ASSERT_EQ(mcp_filter_guard_add_filter(guard2, filter2.release()), MCP_OK);
    
    // Release guards independently
    mcp_filter_guard_release(guard1);
    mcp_filter_guard_release(guard2);
    
    // Guards handle cleanup of released filters
}

// ============================================================================
// Layer 6 Presentation Layer Tests
// ============================================================================

TEST_F(McpFilterApiTest, Layer6PresentationMetadata) {
    FilterGuard filter = createTestFilter("l6_test", MCP_PROTOCOL_LAYER_6_PRESENTATION);
    ASSERT_TRUE(filter.get() != 0);
    
    // Test L6 presentation layer metadata
    mcp_protocol_metadata_t metadata = {};
    metadata.layer = MCP_PROTOCOL_LAYER_6_PRESENTATION;
    // L6 typically handles encoding/compression/encryption
    // Since the union doesn't have l6 specific fields, we test the layer setting
    
    ASSERT_EQ(mcp_filter_set_protocol_metadata(filter, &metadata), MCP_OK);
    
    mcp_protocol_metadata_t retrieved = {};
    ASSERT_EQ(mcp_filter_get_protocol_metadata(filter, &retrieved), MCP_OK);
    EXPECT_EQ(retrieved.layer, MCP_PROTOCOL_LAYER_6_PRESENTATION);
}

// ============================================================================
// Buffer Flag Combination Tests
// ============================================================================

TEST_F(McpFilterApiTest, BufferFlagCombinations) {
    // Test all valid buffer flag combinations
    struct FlagTest {
        uint32_t flags;
        const char* description;
    };
    
    FlagTest flag_tests[] = {
        {MCP_BUFFER_FLAG_READONLY, "Readonly only"},
        {MCP_BUFFER_FLAG_OWNED, "Owned only"},
        {MCP_BUFFER_FLAG_EXTERNAL, "External only"},
        {MCP_BUFFER_FLAG_ZERO_COPY, "Zero copy only"},
        {MCP_BUFFER_FLAG_READONLY | MCP_BUFFER_FLAG_ZERO_COPY, "Readonly + Zero copy"},
        {MCP_BUFFER_FLAG_OWNED | MCP_BUFFER_FLAG_EXTERNAL, "Owned + External"},
        {MCP_BUFFER_FLAG_ZERO_COPY | MCP_BUFFER_FLAG_EXTERNAL, "Zero copy + External"},
        {MCP_BUFFER_FLAG_READONLY | MCP_BUFFER_FLAG_ZERO_COPY | MCP_BUFFER_FLAG_EXTERNAL, 
         "Readonly + Zero copy + External"}
    };
    
    std::vector<uint8_t> data = TestDataGenerator::generateRandomData(256);
    
    for (const auto& test : flag_tests) {
        BufferGuard buffer(data.data(), data.size(), test.flags);
        ASSERT_TRUE(buffer.get() != 0u) << "Failed to create buffer with flags: " << test.description;
        
        // Verify buffer is valid
        size_t length = mcp_filter_buffer_length(buffer);
        EXPECT_EQ(length, data.size());
        
        // Check if we can get slices
        mcp_buffer_slice_t slice;
        size_t slice_count = 1;
        ASSERT_EQ(mcp_filter_get_buffer_slices(buffer, &slice, &slice_count), MCP_OK);
        
        // Verify flags are preserved in slice
        if (test.flags & MCP_BUFFER_FLAG_ZERO_COPY) {
            EXPECT_TRUE(slice.flags & MCP_BUFFER_FLAG_ZERO_COPY);
        }
        
        // Buffer automatically released by RAII
    }
}

// ============================================================================
// Connection Event Type Tests
// ============================================================================

TEST_F(McpFilterApiTest, ConnectionEventTypes) {
    FilterGuard filter = createTestFilter("event_test");
    ASSERT_TRUE(filter.get() != 0);
    
    // Test different connection states
    std::vector<mcp_connection_state_t> states = {
        MCP_CONNECTION_STATE_IDLE,
        MCP_CONNECTION_STATE_CONNECTING,
        MCP_CONNECTION_STATE_CONNECTED,
        MCP_CONNECTION_STATE_CLOSING,
        MCP_CONNECTION_STATE_DISCONNECTED,
        MCP_CONNECTION_STATE_ERROR
    };
    
    int event_count = 0;
    auto event_callback = [](mcp_connection_state_t state, void* user_data) -> mcp_filter_status_t {
        int* count = static_cast<int*>(user_data);
        (*count)++;
        
        // Verify state is valid
        EXPECT_GE(state, MCP_CONNECTION_STATE_IDLE);
        EXPECT_LE(state, MCP_CONNECTION_STATE_ERROR);
        
        return MCP_FILTER_CONTINUE;
    };
    
    mcp_filter_callbacks_t callbacks = {};
    callbacks.on_new_connection = event_callback;
    callbacks.user_data = &event_count;
    
    ASSERT_EQ(mcp_filter_set_callbacks(filter, &callbacks), MCP_OK);
    
    // Events would be triggered by actual connection activity
}

// ============================================================================
// Error Scenario Tests
// ============================================================================

TEST_F(McpFilterApiTest, SpecificErrorScenarios) {
    FilterGuard filter = createTestFilter("error_scenario_test");
    ASSERT_TRUE(filter.get() != 0);
    
    struct ErrorTest {
        mcp_filter_error_t error;
        const char* description;
    };
    
    ErrorTest error_tests[] = {
        {MCP_FILTER_ERROR_INVALID_CONFIG, "Invalid configuration"},
        {MCP_FILTER_ERROR_INITIALIZATION_FAILED, "Initialization failed"},
        {MCP_FILTER_ERROR_BUFFER_OVERFLOW, "Buffer overflow"},
        {MCP_FILTER_ERROR_PROTOCOL_VIOLATION, "Protocol violation"},
        {MCP_FILTER_ERROR_UPSTREAM_TIMEOUT, "Upstream timeout"},
        {MCP_FILTER_ERROR_CIRCUIT_OPEN, "Circuit breaker open"},
        {MCP_FILTER_ERROR_RESOURCE_EXHAUSTED, "Resource exhausted"},
        {MCP_FILTER_ERROR_INVALID_STATE, "Invalid state"}
    };
    
    int error_count = 0;
    auto error_callback = [](mcp_filter_t filter, mcp_filter_error_t error, 
                            const char* message, void* user_data) {
        int* count = static_cast<int*>(user_data);
        (*count)++;
        
        // Verify error is in valid range
        EXPECT_TRUE(error == MCP_FILTER_ERROR_NONE || 
                   (error >= MCP_FILTER_ERROR_INVALID_STATE && 
                    error <= MCP_FILTER_ERROR_INVALID_CONFIG));
    };
    
    mcp_filter_callbacks_t callbacks = {};
    callbacks.on_error = error_callback;
    callbacks.user_data = &error_count;
    
    ASSERT_EQ(mcp_filter_set_callbacks(filter, &callbacks), MCP_OK);
    
    // Errors would be triggered by actual filter operations
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(McpFilterApiTest, CompleteFilterPipeline) {
    // Create a complete filter pipeline using RAII
    
    // 1. Create filters for different layers
    FilterGuard l4_filter = createTestFilter("l4_tcp", MCP_PROTOCOL_LAYER_4_TRANSPORT);
    FilterGuard l5_filter = createTestFilter("l5_tls", MCP_PROTOCOL_LAYER_5_SESSION);
    FilterGuard l7_filter = createTestFilter("l7_http", MCP_PROTOCOL_LAYER_7_APPLICATION);
    
    ASSERT_TRUE(l4_filter.get() != 0);
    ASSERT_TRUE(l5_filter.get() != 0);
    ASSERT_TRUE(l7_filter.get() != 0);
    
    // 2. Set protocol metadata for each
    mcp_protocol_metadata_t l4_meta = {};
    l4_meta.layer = MCP_PROTOCOL_LAYER_4_TRANSPORT;
    l4_meta.data.l4.protocol = MCP_TRANSPORT_PROTOCOL_TCP;
    l4_meta.data.l4.src_port = 54321;
    l4_meta.data.l4.dst_port = 443;
    ASSERT_EQ(mcp_filter_set_protocol_metadata(l4_filter, &l4_meta), MCP_OK);
    
    mcp_protocol_metadata_t l5_meta = {};
    l5_meta.layer = MCP_PROTOCOL_LAYER_5_SESSION;
    l5_meta.data.l5.is_tls = MCP_TRUE;
    l5_meta.data.l5.sni = "example.com";
    ASSERT_EQ(mcp_filter_set_protocol_metadata(l5_filter, &l5_meta), MCP_OK);
    
    mcp_protocol_metadata_t l7_meta = {};
    l7_meta.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;
    l7_meta.data.l7.protocol = MCP_APP_PROTOCOL_HTTPS;
    l7_meta.data.l7.method = "GET";
    l7_meta.data.l7.path = "/";
    ASSERT_EQ(mcp_filter_set_protocol_metadata(l7_filter, &l7_meta), MCP_OK);
    
    // 3. Build filter chain using RAII
    FilterChainBuilderGuard builder(dispatcher_->get());
    ASSERT_TRUE(builder.get() != 0);
    
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, l4_filter, MCP_FILTER_POSITION_FIRST, 0), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, l5_filter, MCP_FILTER_POSITION_LAST, 0), MCP_OK);
    ASSERT_EQ(mcp_filter_chain_add_filter(builder, l7_filter, MCP_FILTER_POSITION_LAST, 0), MCP_OK);
    
    FilterChainGuard chain(mcp_filter_chain_build(builder));
    ASSERT_TRUE(chain.get() != 0u);
    
    // 4. Create buffer pool for processing using RAII
    BufferPoolGuard pool(8192, 5);
    ASSERT_TRUE(pool.get() != 0);
    
    // 5. Simulate data processing using RAII
    BufferGuard buffer = pool.acquire();
    ASSERT_TRUE(buffer.get() != 0u);
    
    // Write test data
    const char* http_request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    size_t request_len = strlen(http_request);
    
    mcp_buffer_slice_t slice;
    ASSERT_EQ(mcp_filter_reserve_buffer(buffer, request_len, &slice), MCP_OK);
    memcpy(const_cast<uint8_t*>(slice.data), http_request, request_len);
    ASSERT_EQ(mcp_filter_commit_buffer(buffer, request_len), MCP_OK);
    
    // Return buffer to pool
    pool.release(buffer.release());
    
    // All resources automatically cleaned up by RAII
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(McpFilterApiTest, FilterCreationPerformance) {
    auto start = std::chrono::high_resolution_clock::now();
    
    const int filter_count = 1000;
    std::vector<mcp_filter_t> filters;
    
    for (int i = 0; i < filter_count; ++i) {
        std::string name = "perf_filter_" + std::to_string(i);
        FilterGuard filter = createTestFilter(name);
        ASSERT_TRUE(filter.get() != 0);
        filters.push_back(filter.release());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Performance expectation: should create 1000 filters in under 1 second
    EXPECT_LT(duration.count(), 1000) << "Filter creation too slow: " << duration.count() << "ms";
    
    // Clean up
    for (auto filter : filters) {
        mcp_filter_release(filter);
    }
}

TEST_F(McpFilterApiTest, BufferOperationPerformance) {
    const int iterations = 10000;
    const size_t data_size = 4096;
    
    std::vector<uint8_t> test_data = TestDataGenerator::generateRandomData(data_size);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        mcp_buffer_handle_t buffer = mcp_filter_buffer_create(
            test_data.data(), test_data.size(), MCP_BUFFER_FLAG_READONLY);
        
        size_t length = mcp_filter_buffer_length(buffer);
        ASSERT_EQ(length, data_size);
        
        mcp_filter_buffer_release(buffer);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Performance expectation: 10000 buffer operations in under 1 second
    EXPECT_LT(duration.count(), 1000) << "Buffer operations too slow: " << duration.count() << "ms";
    
    double ops_per_sec = (iterations * 1000.0) / duration.count();
    std::cout << "Buffer operations per second: " << ops_per_sec << std::endl;
}

// ============================================================================
// Memory Leak Tests
// ============================================================================

TEST_F(McpFilterApiTest, NoMemoryLeakOnRepeatedOperations) {
    // This test performs repeated operations to check for memory leaks
    // In production, this would be run with valgrind or similar tools
    
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Create and destroy filters
        FilterGuard filter = createTestFilter("leak_test");
        ASSERT_TRUE(filter.get() != 0);
        
        // Set callbacks
        mcp_filter_callbacks_t callbacks = {};
        callbacks.user_data = this;
        mcp_filter_set_callbacks(filter, &callbacks);
        
        // Create and release buffers
        std::vector<uint8_t> data(1024);
        mcp_buffer_handle_t buffer = mcp_filter_buffer_create(
            data.data(), data.size(), MCP_BUFFER_FLAG_OWNED);
        mcp_filter_buffer_release(buffer);
        
        // Create and destroy chain
        mcp_filter_chain_builder_t builder = mcp_filter_chain_builder_create(dispatcher_->get());
        mcp_filter_chain_add_filter(builder, filter, MCP_FILTER_POSITION_LAST, 0);
        mcp_filter_chain_t chain = mcp_filter_chain_build(builder);
        mcp_filter_chain_builder_destroy(builder);
        mcp_filter_chain_release(chain);
        
        // Filter is automatically released by RAII
    }
}

} // namespace

// ============================================================================
// Test Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Set up test environment
    std::cout << "Running MCP Filter API Comprehensive Tests" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "==========================================" << std::endl;
    std::cout << "Test run complete. Result: " << (result == 0 ? "PASS" : "FAIL") << std::endl;
    
    return result;
}