# Integration Guide for Improved FFI-Safe C API

## Overview
This guide explains how to integrate the improved FFI-safe types and RAII utilities into the existing MCP C API codebase.

## File Structure

### New Core Files (Keep Separate)
1. **`mcp_ffi_core.h`** - Core FFI utilities and primitives
2. **`mcp_c_types_improved.h`** - Improved type definitions without unions
3. **`mcp_raii_integration.h`** - Bridge between FFI types and RAII

### Existing Files to Modify
1. **`mcp_c_types.h`** - Gradually migrate to use new patterns
2. **`mcp_c_type_conversions.h`** - Update to handle new types
3. **`mcp_c_api.h`** - Main API header to include new headers

## Integration Strategy

### Phase 1: Non-Breaking Additions (Immediate)
Add new functionality without breaking existing code:

```cpp
// In mcp_c_types.h - Add at the top
#include "mcp_ffi_core.h"  // Import core FFI utilities

// Add improved types alongside existing ones
#ifdef MCP_USE_IMPROVED_FFI
  #include "mcp_c_types_improved.h"
#endif
```

### Phase 2: Parallel Implementation (Week 1-2)
Create new implementations that work alongside existing ones:

```cpp
// In src/c_api/mcp_c_api_core_improved.cc
#include "mcp/c_api/mcp_ffi_core.h"
#include "mcp/c_api/mcp_c_types_improved.h"
#include "mcp/c_api/mcp_raii_integration.h"

// Implement new FFI-safe functions
extern "C" {

mcp_result_t mcp_ffi_initialize(const mcp_allocator_t* allocator) {
    // Implementation
}

mcp_request_id_t mcp_request_id_create_string(const char* str) {
    // Opaque handle implementation
}

// ... other implementations
}
```

### Phase 3: Migration Helpers (Week 2-3)
Add conversion functions between old and new types:

```cpp
// In mcp_c_type_conversions.h
namespace mcp {
namespace c_api {

// Convert old union-based request_id to new opaque handle
mcp_request_id_t convert_to_improved(const mcp_request_id_old_t& old) {
    switch(old.type) {
        case MCP_REQUEST_ID_STRING:
            return mcp_request_id_create_string(old.value.string_value.data);
        case MCP_REQUEST_ID_NUMBER:
            return mcp_request_id_create_number(old.value.number_value);
    }
    return nullptr;
}

// Convert new opaque handle to old union-based
mcp_request_id_old_t convert_to_legacy(mcp_request_id_t improved) {
    mcp_request_id_old_t old;
    if (mcp_request_id_get_type(improved) == MCP_REQUEST_ID_TYPE_STRING) {
        old.type = MCP_REQUEST_ID_STRING;
        old.value.string_value = {mcp_request_id_get_string(improved), ...};
    } else {
        old.type = MCP_REQUEST_ID_NUMBER;
        old.value.number_value = mcp_request_id_get_number(improved);
    }
    return old;
}

} // namespace c_api
} // namespace mcp
```

### Phase 4: Gradual Migration (Week 3-4)
Update existing code to use new types:

1. Start with internal implementation files
2. Update tests to use new APIs
3. Deprecate old APIs but keep for compatibility
4. Update documentation

## Implementation Files Structure

### 1. Create `src/c_api/mcp_ffi_core.cc`
```cpp
#include "mcp/c_api/mcp_ffi_core.h"
#include "mcp/c_api/mcp_raii.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace {
    std::atomic<bool> g_initialized{false};
    std::mutex g_init_mutex;
    mcp_allocator_t g_allocator;
    thread_local mcp_error_info_t* g_last_error = nullptr;
}

extern "C" {

MCP_API mcp_result_t mcp_ffi_initialize(const mcp_allocator_t* allocator) MCP_NOEXCEPT {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized.load()) {
        return MCP_ERROR_ALREADY_INITIALIZED;
    }
    
    if (allocator) {
        g_allocator = *allocator;
    } else {
        // Use default allocator
        g_allocator = {
            [](size_t size, void*) { return malloc(size); },
            [](void* ptr, size_t size, void*) { return realloc(ptr, size); },
            [](void* ptr, void*) { free(ptr); },
            nullptr
        };
    }
    
    g_initialized.store(true);
    return MCP_OK;
}

MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT {
    return g_last_error;
}

// ... implement other core functions
}
```

### 2. Create `src/c_api/mcp_types_improved.cc`
```cpp
#include "mcp/c_api/mcp_c_types_improved.h"
#include "mcp/c_api/mcp_raii_integration.h"
#include "mcp/types.h"
#include <unordered_map>
#include <variant>

using namespace mcp::raii;

// Implementation of opaque request ID
struct mcp_request_id_impl {
    mcp_request_id_type_t type;
    std::variant<std::string, int64_t> value;
};

extern "C" {

mcp_request_id_t mcp_request_id_create_string(const char* str) {
    if (!str) return nullptr;
    
    auto* impl = new mcp_request_id_impl;
    impl->type = MCP_REQUEST_ID_TYPE_STRING;
    impl->value = std::string(str);
    
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_request_id_t");
    return impl;
}

mcp_request_id_t mcp_request_id_create_number(int64_t num) {
    auto* impl = new mcp_request_id_impl;
    impl->type = MCP_REQUEST_ID_TYPE_NUMBER;
    impl->value = num;
    
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_request_id_t");
    return impl;
}

void mcp_request_id_free(mcp_request_id_t id) {
    if (id) {
        MCP_RAII_UNTRACK_RESOURCE(id);
        delete id;
    }
}

const char* mcp_request_id_get_string(mcp_request_id_t id) {
    if (!id || id->type != MCP_REQUEST_ID_TYPE_STRING) {
        return nullptr;
    }
    return std::get<std::string>(id->value).c_str();
}

// ... implement other type functions
}
```

### 3. Update `src/c_api/CMakeLists.txt`
```cmake
# Add new source files
set(C_API_SOURCES
    # Existing files
    mcp_c_api_core.cc
    mcp_c_api_utils.cc
    mcp_c_api_builders.cc
    mcp_c_api_connection.cc
    mcp_c_api_server.cc
    mcp_c_api_client.cc
    mcp_c_api_json.cc
    mcp_raii.cc
    
    # New improved FFI files
    mcp_ffi_core.cc
    mcp_types_improved.cc
    mcp_raii_integration.cc
)

# Create library
add_library(gopher_mcp_c ${C_API_SOURCES})

# Set compile definitions for improved FFI
target_compile_definitions(gopher_mcp_c 
    PRIVATE MCP_BUILD_LIBRARY
    PUBLIC MCP_USE_IMPROVED_FFI  # Enable improved types
)
```

## Migration Path for Existing Code

### Step 1: Update Headers
```cpp
// mcp_c_types.h - Add compatibility layer
#ifndef MCP_C_TYPES_H
#define MCP_C_TYPES_H

#include "mcp_ffi_core.h"  // Import core FFI types

// Keep existing types for backward compatibility
typedef struct mcp_request_id_old {
    enum { MCP_REQUEST_ID_STRING, MCP_REQUEST_ID_NUMBER } type;
    union {
        mcp_string_t string_value;
        int64_t number_value;
    } value;
} mcp_request_id_old_t;

// Use improved types if enabled
#ifdef MCP_USE_IMPROVED_FFI
    // Forward to improved types
    typedef mcp_request_id_t mcp_request_id_new_t;
    
    // Compatibility macro
    #define MCP_REQUEST_ID_USE_IMPROVED 1
#else
    // Use old types
    typedef mcp_request_id_old_t mcp_request_id_t;
#endif

#endif
```

### Step 2: Update Implementation Files
```cpp
// In existing implementation files, add conditional compilation
#ifdef MCP_REQUEST_ID_USE_IMPROVED
    // Use new opaque handle API
    auto id = mcp_request_id_create_string("test");
    const char* str = mcp_request_id_get_string(id);
    mcp_request_id_free(id);
#else
    // Use old union-based API
    mcp_request_id_t id;
    id.type = MCP_REQUEST_ID_STRING;
    id.value.string_value = {"test", 4};
#endif
```

### Step 3: Update Tests
```cpp
// In test files
#include "mcp/c_api/mcp_c_types_improved.h"
#include "mcp/c_api/mcp_raii_integration.h"

TEST(CApiTest, ImprovedFFI) {
    // Use new improved API
    auto guard = mcp::raii::make_string_guard("test", 4);
    ASSERT_NE(guard.get(), nullptr);
}
```

## Testing Strategy

### 1. Parallel Testing
Run both old and new implementations in tests:
```cpp
TEST(CompatibilityTest, OldVsNew) {
    // Test old API
    mcp_request_id_old_t old_id;
    old_id.type = MCP_REQUEST_ID_STRING;
    old_id.value.string_value = {"test", 4};
    
    // Convert to new
    auto new_id = convert_to_improved(old_id);
    EXPECT_STREQ(mcp_request_id_get_string(new_id), "test");
    
    // Convert back
    auto converted_back = convert_to_legacy(new_id);
    EXPECT_EQ(converted_back.type, MCP_REQUEST_ID_STRING);
    
    mcp_request_id_free(new_id);
}
```

### 2. Performance Testing
Compare performance of old vs new:
```cpp
BENCHMARK(OldAPI) {
    // Benchmark old union-based API
}

BENCHMARK(NewAPI) {
    // Benchmark new opaque handle API
}
```

## Deprecation Timeline

### Week 1-2: Addition
- Add new headers and implementations
- No breaking changes

### Week 3-4: Migration
- Update internal code to use new APIs
- Add deprecation warnings to old APIs

### Week 5-6: Testing
- Extensive testing of both APIs
- Performance validation

### Week 7-8: Documentation
- Update all documentation
- Provide migration guides for users

### Month 3: Deprecation
- Mark old APIs as deprecated
- Plan removal in next major version

## Benefits After Integration

1. **Better FFI Compatibility**: Works with Python ctypes, Go CGO, Rust FFI
2. **Clearer Ownership**: No ambiguity about memory ownership
3. **Thread Safety**: Built-in reference counting and synchronization
4. **RAII Integration**: Automatic resource management in C++
5. **Better Error Handling**: Thread-local error context
6. **Performance**: Memory pools for batch operations
7. **Type Safety**: No unions, validated handles
8. **Debugging**: Production debugging with resource tracking

## Example Usage After Integration

```cpp
// C++ with RAII
{
    mcp::raii::MCPAllocationTransaction txn;
    
    auto request = mcp_request_create("method");
    txn.track_request(request);
    
    auto id = mcp_request_id_create_string("req-123");
    mcp_request_set_id(request, id);
    mcp_request_id_free(id);
    
    // Automatic cleanup if exception
    process_request(request);
    
    txn.commit(); // Success - manual cleanup needed
    mcp_request_free(request);
}

// C with manual management
mcp_request_t request = mcp_request_create("method");
if (!request) {
    const mcp_error_info_t* error = mcp_get_last_error();
    printf("Error: %s\n", error->message);
    return -1;
}

mcp_request_id_t id = mcp_request_id_create_string("req-123");
mcp_request_set_id(request, id);
mcp_request_id_free(id);

// Use request...

mcp_request_free(request);
```

## Questions to Address

1. **Backward Compatibility**: How long to maintain old API?
2. **Performance Impact**: Benchmark opaque handles vs unions
3. **Language Bindings**: Test with Python, Go, Rust
4. **Documentation**: Update all examples and guides
5. **Migration Tools**: Provide automated migration scripts?