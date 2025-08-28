/**
 * @file wrapper.c
 * @brief Example C wrapper for creating a dynamic library from a static library
 * 
 * This file demonstrates how to create a dynamic library that links to your
 * static MCP Filter library, making it compatible with FFI libraries like koffi.
 * 
 * Usage:
 *   gcc -shared -fPIC -o libgopher-mcp.dylib wrapper.c -L. -lgopher-mcp
 */

#include <stdint.h>

// Forward declarations for the static library functions
// These should match the actual function signatures in your static library
extern int mcp_init_impl(void* allocator);
extern void mcp_shutdown_impl(void);
extern int mcp_is_initialized_impl(void);
extern const char* mcp_get_version_impl(void);

extern int mcp_memory_pool_create_impl(uint64_t size);
extern void mcp_memory_pool_destroy_impl(int pool);
extern void* mcp_memory_pool_alloc_impl(int pool, uint64_t size);

extern int mcp_dispatcher_create_impl(void);
extern void mcp_dispatcher_destroy_impl(int dispatcher);

extern int mcp_filter_create_impl(int dispatcher, void* config);
extern int mcp_filter_create_builtin_impl(int dispatcher, uint32_t type, int config);
extern void mcp_filter_release_impl(int filter);

extern int mcp_filter_buffer_create_impl(void* data, uint64_t size, uint32_t flags);
extern void mcp_filter_buffer_release_impl(int buffer);
extern int mcp_filter_buffer_get_data_impl(int buffer, void** data, uint64_t* size);
extern int mcp_filter_buffer_set_data_impl(int buffer, void* data, uint64_t size);

extern int mcp_json_create_object_impl(void);
extern void mcp_json_destroy_impl(int json);
extern const char* mcp_json_stringify_impl(int json);

extern int mcp_filter_chain_create_impl(const char* name);
extern void mcp_filter_chain_destroy_impl(int chain);
extern int mcp_filter_chain_add_filter_impl(void* builder, int filter, uint32_t position, int reference);

extern int mcp_buffer_pool_create_impl(void* config);
extern void mcp_buffer_pool_destroy_impl(int pool);
extern int mcp_buffer_pool_alloc_impl(int pool);

extern int mcp_filter_guard_create_impl(int dispatcher);
extern void mcp_filter_guard_destroy_impl(void* guard);
extern int mcp_filter_guard_add_filter_impl(void* guard, int filter);

extern int mcp_filter_post_data_impl(int filter, void* data, uint64_t length, void* callback, void* userData);

extern const char* mcp_get_error_string_impl(int result);

// Export the functions with proper visibility
// This makes them available to FFI libraries

__attribute__((visibility("default")))
int mcp_init(void* allocator) {
    return mcp_init_impl(allocator);
}

__attribute__((visibility("default")))
void mcp_shutdown(void) {
    mcp_shutdown_impl();
}

__attribute__((visibility("default")))
int mcp_is_initialized(void) {
    return mcp_is_initialized_impl();
}

__attribute__((visibility("default")))
const char* mcp_get_version(void) {
    return mcp_get_version_impl();
}

__attribute__((visibility("default")))
int mcp_memory_pool_create(uint64_t size) {
    return mcp_memory_pool_create_impl(size);
}

__attribute__((visibility("default")))
void mcp_memory_pool_destroy(int pool) {
    mcp_memory_pool_destroy_impl(pool);
}

__attribute__((visibility("default")))
void* mcp_memory_pool_alloc(int pool, uint64_t size) {
    return mcp_memory_pool_alloc_impl(pool, size);
}

__attribute__((visibility("default")))
int mcp_dispatcher_create(void) {
    return mcp_dispatcher_create_impl();
}

__attribute__((visibility("default")))
void mcp_dispatcher_destroy(int dispatcher) {
    mcp_dispatcher_destroy_impl(dispatcher);
}

__attribute__((visibility("default")))
int mcp_filter_create(int dispatcher, void* config) {
    return mcp_filter_create_impl(dispatcher, config);
}

__attribute__((visibility("default")))
int mcp_filter_create_builtin(int dispatcher, uint32_t type, int config) {
    return mcp_filter_create_builtin_impl(dispatcher, type, config);
}

__attribute__((visibility("default")))
void mcp_filter_release(int filter) {
    mcp_filter_release_impl(filter);
}

__attribute__((visibility("default")))
int mcp_filter_buffer_create(void* data, uint64_t size, uint32_t flags) {
    return mcp_filter_buffer_create_impl(data, size, flags);
}

__attribute__((visibility("default")))
void mcp_filter_buffer_release(int buffer) {
    mcp_filter_buffer_release_impl(buffer);
}

__attribute__((visibility("default")))
int mcp_filter_buffer_get_data(int buffer, void** data, uint64_t* size) {
    return mcp_filter_buffer_get_data_impl(buffer, data, size);
}

__attribute__((visibility("default")))
int mcp_filter_buffer_set_data(int buffer, void* data, uint64_t size) {
    return mcp_filter_buffer_set_data_impl(buffer, data, size);
}

__attribute__((visibility("default")))
int mcp_json_create_object(void) {
    return mcp_json_create_object_impl();
}

__attribute__((visibility("default")))
void mcp_json_destroy(int json) {
    mcp_json_destroy_impl(json);
}

__attribute__((visibility("default")))
const char* mcp_json_stringify(int json) {
    return mcp_json_stringify_impl(json);
}

__attribute__((visibility("default")))
int mcp_filter_chain_create(const char* name) {
    return mcp_filter_chain_create_impl(name);
}

__attribute__((visibility("default")))
void mcp_filter_chain_destroy(int chain) {
    mcp_filter_chain_destroy_impl(chain);
}

__attribute__((visibility("default")))
int mcp_filter_chain_add_filter(void* builder, int filter, uint32_t position, int reference) {
    return mcp_filter_chain_add_filter_impl(builder, filter, position, reference);
}

__attribute__((visibility("default")))
int mcp_buffer_pool_create(void* config) {
    return mcp_buffer_pool_create_impl(config);
}

__attribute__((visibility("default")))
void mcp_buffer_pool_destroy(int pool) {
    mcp_buffer_pool_destroy_impl(pool);
}

__attribute__((visibility("default")))
int mcp_buffer_pool_alloc(int pool) {
    return mcp_buffer_pool_alloc_impl(pool);
}

__attribute__((visibility("default")))
int mcp_filter_guard_create(int dispatcher) {
    return mcp_filter_guard_create_impl(dispatcher);
}

__attribute__((visibility("default")))
void mcp_filter_guard_destroy(void* guard) {
    mcp_filter_guard_destroy_impl(guard);
}

__attribute__((visibility("default")))
int mcp_filter_guard_add_filter(void* guard, int filter) {
    return mcp_filter_guard_add_filter_impl(guard, filter);
}

__attribute__((visibility("default")))
int mcp_filter_post_data(int filter, void* data, uint64_t length, void* callback, void* userData) {
    return mcp_filter_post_data_impl(filter, data, length, callback, userData);
}

__attribute__((visibility("default")))
const char* mcp_get_error_string(int result) {
    return mcp_get_error_string_impl(result);
}
