/**
 * @file filter_example.cc
 * @brief Example usage of MCP Filter C API
 *
 * This example demonstrates how to:
 * 1. Create and configure filters
 * 2. Build filter chains
 * 3. Process data through filters
 * 4. Use zero-copy buffer operations
 * 5. Handle callbacks in dispatcher thread
 */

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_filter_api.h"

// ============================================================================
// Custom Filter Callbacks
// ============================================================================

// Statistics tracking
struct FilterStats {
  uint64_t bytes_processed;
  uint64_t packets_processed;
  uint64_t errors;
};

FilterStats g_stats = {0, 0, 0};

// Logging filter callback
mcp_filter_status_t logging_filter_on_data(mcp_buffer_handle_t buffer,
                                           mcp_bool_t end_stream,
                                           void* user_data) {
  size_t length = mcp_filter_buffer_length(buffer);
  std::cout << "[Logging Filter] Processing " << length << " bytes";
  if (end_stream) {
    std::cout << " (end of stream)";
  }
  std::cout << std::endl;

  g_stats.bytes_processed += length;
  g_stats.packets_processed++;

  return MCP_FILTER_CONTINUE;
}

// Rate limiting filter callback
mcp_filter_status_t rate_limit_filter_on_data(mcp_buffer_handle_t buffer,
                                              mcp_bool_t end_stream,
                                              void* user_data) {
  static auto last_time = std::chrono::steady_clock::now();
  static uint64_t bytes_this_second = 0;
  const uint64_t max_bytes_per_second = 1024 * 1024;  // 1MB/s

  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - last_time);

  if (elapsed.count() >= 1) {
    bytes_this_second = 0;
    last_time = now;
  }

  size_t length = mcp_filter_buffer_length(buffer);

  if (bytes_this_second + length > max_bytes_per_second) {
    std::cout << "[Rate Limit] Throttling - exceeded 1MB/s" << std::endl;
    return MCP_FILTER_STOP_ITERATION;
  }

  bytes_this_second += length;
  std::cout << "[Rate Limit] Passed - " << bytes_this_second
            << " bytes this second" << std::endl;

  return MCP_FILTER_CONTINUE;
}

// Compression filter callback (mock)
mcp_filter_status_t compression_filter_on_write(mcp_buffer_handle_t buffer,
                                                mcp_bool_t end_stream,
                                                void* user_data) {
  size_t original_length = mcp_filter_buffer_length(buffer);
  size_t compressed_length = original_length * 0.7;  // Mock 30% compression

  std::cout << "[Compression] Compressed " << original_length << " bytes to "
            << compressed_length << " bytes" << std::endl;

  return MCP_FILTER_CONTINUE;
}

// Error handler
void filter_error_handler(mcp_filter_t filter,
                          mcp_filter_error_t error,
                          const char* message,
                          void* user_data) {
  std::cerr << "[Error] Filter " << filter << " error " << error << ": "
            << message << std::endl;
  g_stats.errors++;
}

// ============================================================================
// Example 1: Basic Filter Chain
// ============================================================================

void example_basic_filter_chain() {
  std::cout << "\n=== Example 1: Basic Filter Chain ===" << std::endl;

  // Initialize MCP
  mcp_result_t result = mcp_init(nullptr);
  if (result != MCP_OK) {
    std::cerr << "Failed to initialize MCP" << std::endl;
    return;
  }

  // Create dispatcher
  mcp_dispatcher_t dispatcher = mcp_dispatcher_create();
  if (!dispatcher) {
    std::cerr << "Failed to create dispatcher" << std::endl;
    return;
  }

  // Create filter chain builder
  mcp_filter_chain_builder_t builder =
      mcp_filter_chain_builder_create(dispatcher);

  // Create logging filter
  mcp_filter_config_t logging_config = {};
  logging_config.name = "logging_filter";
  logging_config.type = MCP_FILTER_ACCESS_LOG;
  logging_config.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;

  mcp_filter_t logging_filter = mcp_filter_create(dispatcher, &logging_config);

  mcp_filter_callbacks_t logging_callbacks = {};
  logging_callbacks.on_data = logging_filter_on_data;
  logging_callbacks.on_error = filter_error_handler;
  mcp_filter_set_callbacks(logging_filter, &logging_callbacks);

  // Create rate limit filter
  mcp_filter_config_t rate_limit_config = {};
  rate_limit_config.name = "rate_limit_filter";
  rate_limit_config.type = MCP_FILTER_RATE_LIMIT;
  rate_limit_config.layer = MCP_PROTOCOL_LAYER_4_TRANSPORT;

  mcp_filter_t rate_limit_filter =
      mcp_filter_create(dispatcher, &rate_limit_config);

  mcp_filter_callbacks_t rate_limit_callbacks = {};
  rate_limit_callbacks.on_data = rate_limit_filter_on_data;
  rate_limit_callbacks.on_error = filter_error_handler;
  mcp_filter_set_callbacks(rate_limit_filter, &rate_limit_callbacks);

  // Add filters to chain
  mcp_filter_chain_add_filter(builder, logging_filter,
                              MCP_FILTER_POSITION_FIRST, 0);
  mcp_filter_chain_add_filter(builder, rate_limit_filter,
                              MCP_FILTER_POSITION_LAST, 0);

  // Build chain
  mcp_filter_chain_t chain = mcp_filter_chain_build(builder);
  mcp_filter_chain_builder_destroy(builder);

  std::cout << "Filter chain created with logging and rate limiting"
            << std::endl;

  // Cleanup
  mcp_filter_chain_release(chain);
  mcp_filter_release(logging_filter);
  mcp_filter_release(rate_limit_filter);
  mcp_dispatcher_destroy(dispatcher);
}

// ============================================================================
// Example 2: Zero-Copy Buffer Operations
// ============================================================================

void example_zero_copy_buffers() {
  std::cout << "\n=== Example 2: Zero-Copy Buffer Operations ===" << std::endl;

  // Create a buffer pool
  size_t buffer_size = 4096;
  size_t max_buffers = 10;
  mcp_buffer_pool_t pool = mcp_buffer_pool_create(buffer_size, max_buffers);

  // Acquire buffer from pool
  mcp_buffer_handle_t buffer = mcp_buffer_pool_acquire(pool);
  if (!buffer) {
    std::cerr << "Failed to acquire buffer from pool" << std::endl;
    return;
  }

  std::cout << "Acquired buffer from pool" << std::endl;

  // Reserve space for writing (zero-copy)
  mcp_buffer_slice_t slice;
  mcp_result_t result = mcp_filter_reserve_buffer(buffer, 1024, &slice);
  if (result == MCP_OK) {
    std::cout << "Reserved " << slice.length << " bytes for writing"
              << std::endl;

    // Write data directly to reserved memory
    const char* message = "Hello from zero-copy buffer!";
    std::memcpy(const_cast<uint8_t*>(slice.data), message, strlen(message));

    // Commit the write
    mcp_filter_commit_buffer(buffer, strlen(message));
    std::cout << "Committed " << strlen(message) << " bytes" << std::endl;
  }

  // Get buffer slices for reading (scatter-gather I/O)
  mcp_buffer_slice_t read_slices[10];
  size_t slice_count = 10;
  result = mcp_filter_get_buffer_slices(buffer, read_slices, &slice_count);
  if (result == MCP_OK) {
    std::cout << "Got " << slice_count << " buffer slices:" << std::endl;
    for (size_t i = 0; i < slice_count; ++i) {
      std::cout << "  Slice " << i << ": " << read_slices[i].length
                << " bytes at " << (void*)read_slices[i].data << std::endl;
    }
  }

  // Release buffer back to pool
  mcp_buffer_pool_release(pool, buffer);
  std::cout << "Released buffer back to pool" << std::endl;

  // Cleanup
  mcp_buffer_pool_destroy(pool);
}

// ============================================================================
// Example 3: Protocol Layer Processing
// ============================================================================

void example_protocol_layers() {
  std::cout << "\n=== Example 3: Protocol Layer Processing ===" << std::endl;

  mcp_dispatcher_t dispatcher = mcp_dispatcher_create();

  // Create HTTP filter for L7
  mcp_filter_t http_filter = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_HTTP_CODEC, mcp_json_create_null());

  // Set L7 protocol metadata
  mcp_protocol_metadata_t l7_metadata = {};
  l7_metadata.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;
  l7_metadata.data.l7.protocol = MCP_APP_PROTOCOL_HTTP;
  l7_metadata.data.l7.method = "GET";
  l7_metadata.data.l7.path = "/api/data";
  l7_metadata.data.l7.status_code = 200;

  mcp_filter_set_protocol_metadata(http_filter, &l7_metadata);
  std::cout << "Configured HTTP filter with L7 metadata" << std::endl;

  // Create TLS filter for L5/L6
  mcp_filter_t tls_filter = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_TLS_TERMINATION, mcp_json_create_null());

  mcp_protocol_metadata_t l5_metadata = {};
  l5_metadata.layer = MCP_PROTOCOL_LAYER_5_SESSION;
  l5_metadata.data.l5.is_tls = MCP_TRUE;
  l5_metadata.data.l5.alpn = "h2";
  l5_metadata.data.l5.sni = "example.com";

  mcp_filter_set_protocol_metadata(tls_filter, &l5_metadata);
  std::cout << "Configured TLS filter with L5 metadata" << std::endl;

  // Create TCP filter for L4
  mcp_filter_t tcp_filter = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_TCP_PROXY, mcp_json_create_null());

  mcp_protocol_metadata_t l4_metadata = {};
  l4_metadata.layer = MCP_PROTOCOL_LAYER_4_TRANSPORT;
  l4_metadata.data.l4.protocol = MCP_TRANSPORT_PROTOCOL_TCP;
  l4_metadata.data.l4.src_port = 54321;
  l4_metadata.data.l4.dst_port = 443;

  mcp_filter_set_protocol_metadata(tcp_filter, &l4_metadata);
  std::cout << "Configured TCP filter with L4 metadata" << std::endl;

  // Build layered filter chain
  mcp_filter_chain_builder_t builder =
      mcp_filter_chain_builder_create(dispatcher);
  mcp_filter_chain_add_filter(builder, tcp_filter, MCP_FILTER_POSITION_FIRST,
                              0);
  mcp_filter_chain_add_filter(builder, tls_filter, MCP_FILTER_POSITION_LAST, 0);
  mcp_filter_chain_add_filter(builder, http_filter, MCP_FILTER_POSITION_LAST,
                              0);

  mcp_filter_chain_t chain = mcp_filter_chain_build(builder);
  std::cout << "Built protocol layer filter chain: TCP -> TLS -> HTTP"
            << std::endl;

  // Cleanup
  mcp_filter_chain_builder_destroy(builder);
  mcp_filter_chain_release(chain);
  mcp_filter_release(http_filter);
  mcp_filter_release(tls_filter);
  mcp_filter_release(tcp_filter);
  mcp_dispatcher_destroy(dispatcher);
}

// ============================================================================
// Example 4: Client/Server Integration
// ============================================================================

void example_client_server_integration() {
  std::cout << "\n=== Example 4: Client/Server Integration ===" << std::endl;

  mcp_dispatcher_t dispatcher = mcp_dispatcher_create();

  // Create client filter context
  mcp_filter_client_context_t client_context = {};

  // Build request filter chain
  mcp_filter_chain_builder_t req_builder =
      mcp_filter_chain_builder_create(dispatcher);
  mcp_filter_t compression = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_HTTP_COMPRESSION, mcp_json_create_null());
  mcp_filter_t auth = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_AUTHENTICATION, mcp_json_create_null());

  mcp_filter_chain_add_filter(req_builder, auth, MCP_FILTER_POSITION_FIRST, 0);
  mcp_filter_chain_add_filter(req_builder, compression,
                              MCP_FILTER_POSITION_LAST, 0);
  client_context.request_filters = mcp_filter_chain_build(req_builder);

  // Build response filter chain
  mcp_filter_chain_builder_t resp_builder =
      mcp_filter_chain_builder_create(dispatcher);
  mcp_filter_t metrics = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_METRICS, mcp_json_create_null());
  mcp_filter_t logging = mcp_filter_create_builtin(
      dispatcher, MCP_FILTER_ACCESS_LOG, mcp_json_create_null());

  mcp_filter_chain_add_filter(resp_builder, metrics, MCP_FILTER_POSITION_FIRST,
                              0);
  mcp_filter_chain_add_filter(resp_builder, logging, MCP_FILTER_POSITION_LAST,
                              0);
  client_context.response_filters = mcp_filter_chain_build(resp_builder);

  std::cout << "Created client filter context with:" << std::endl;
  std::cout << "  Request filters: Authentication -> Compression" << std::endl;
  std::cout << "  Response filters: Metrics -> Logging" << std::endl;

  // Simulate sending a request
  const char* request_data = "{\"action\": \"get_data\", \"id\": 12345}";
  auto completion_cb = [](mcp_result_t result, void* user_data) {
    std::cout << "Request completed with result: " << result << std::endl;
  };

  mcp_request_id_t request_id = mcp_client_send_filtered(
      &client_context, reinterpret_cast<const uint8_t*>(request_data),
      strlen(request_data), completion_cb, nullptr);

  std::cout << "Sent filtered request with ID: " << request_id << std::endl;

  // Cleanup
  mcp_filter_chain_builder_destroy(req_builder);
  mcp_filter_chain_builder_destroy(resp_builder);
  mcp_filter_chain_release(client_context.request_filters);
  mcp_filter_chain_release(client_context.response_filters);
  mcp_filter_release(compression);
  mcp_filter_release(auth);
  mcp_filter_release(metrics);
  mcp_filter_release(logging);
  mcp_dispatcher_destroy(dispatcher);
}

// ============================================================================
// Example 5: Resource Guard (RAII)
// ============================================================================

void example_resource_guard() {
  std::cout << "\n=== Example 5: Resource Guard (RAII) ===" << std::endl;

  mcp_dispatcher_t dispatcher = mcp_dispatcher_create();

  // Create resource guard for automatic cleanup
  mcp_filter_resource_guard_t* guard = mcp_filter_guard_create(dispatcher);

  // Create filters and add to guard
  for (int i = 0; i < 5; ++i) {
    mcp_filter_config_t config = {};
    config.name = "test_filter";
    config.type = MCP_FILTER_CUSTOM;
    config.layer = MCP_PROTOCOL_LAYER_7_APPLICATION;

    mcp_filter_t filter = mcp_filter_create(dispatcher, &config);
    mcp_filter_guard_add_filter(guard, filter);

    std::cout << "Created and tracked filter " << filter << std::endl;
  }

  std::cout << "All filters tracked by resource guard" << std::endl;

  // Resource guard will automatically clean up all filters
  mcp_filter_guard_release(guard);
  std::cout << "Resource guard released - all filters cleaned up" << std::endl;

  mcp_dispatcher_destroy(dispatcher);
}

// ============================================================================
// Main Function
// ============================================================================

int main(int argc, char* argv[]) {
  std::cout << "MCP Filter API Examples" << std::endl;
  std::cout << "=======================" << std::endl;

  // Run examples
  example_basic_filter_chain();
  example_zero_copy_buffers();
  example_protocol_layers();
  example_client_server_integration();
  example_resource_guard();

  // Print statistics
  std::cout << "\n=== Final Statistics ===" << std::endl;
  std::cout << "Bytes processed: " << g_stats.bytes_processed << std::endl;
  std::cout << "Packets processed: " << g_stats.packets_processed << std::endl;
  std::cout << "Errors: " << g_stats.errors << std::endl;

  // Shutdown
  mcp_shutdown();

  return 0;
}