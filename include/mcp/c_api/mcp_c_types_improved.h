/**
 * @file mcp_c_types_improved.h
 * @brief Improved FFI-safe C API types with RAII integration
 *
 * This header provides improved C API types that follow FFI best practices
 * and integrate seamlessly with mcp_raii.h for safe resource management.
 * 
 * Improvements over original mcp_c_types.h:
 * - No unions in public API (uses tagged opaque types)
 * - Clear ownership semantics with _view and _owned suffixes
 * - Thread-safe reference counting
 * - Comprehensive validation functions
 * - Memory pool support for efficient batch operations
 * - Better error context with thread-local storage
 * - RAII-friendly move semantics
 */

#ifndef MCP_C_TYPES_IMPROVED_H
#define MCP_C_TYPES_IMPROVED_H

#include "mcp_ffi_core.h"  /* Core FFI types and utilities */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Type Identifiers for Validation
 * ============================================================================ */

typedef enum mcp_type_id {
    MCP_TYPE_UNKNOWN = 0,
    MCP_TYPE_STRING = 1,
    MCP_TYPE_LIST = 2,
    MCP_TYPE_MAP = 3,
    MCP_TYPE_JSON = 4,
    MCP_TYPE_REQUEST = 5,
    MCP_TYPE_RESPONSE = 6,
    MCP_TYPE_NOTIFICATION = 7,
    MCP_TYPE_ERROR = 8,
    MCP_TYPE_RESULT = 9,
    MCP_TYPE_CAPABILITY = 10,
    MCP_TYPE_IMPLEMENTATION = 11,
    MCP_TYPE_CLIENT_INFO = 12,
    MCP_TYPE_SERVER_INFO = 13,
    MCP_TYPE_TOOL = 14,
    MCP_TYPE_RESOURCE = 15,
    MCP_TYPE_PROMPT = 16,
    MCP_TYPE_CONNECTION = 17,
    MCP_TYPE_TRANSPORT = 18,
    MCP_TYPE_DISPATCHER = 19,
    MCP_TYPE_EVENT_LOOP = 20,
    MCP_TYPE_MAX
} mcp_type_id_t;

/* ============================================================================
 * Improved Request ID (No Union)
 * ============================================================================ */

/**
 * Request ID type enumeration
 */
typedef enum mcp_request_id_type {
    MCP_REQUEST_ID_TYPE_NONE = 0,
    MCP_REQUEST_ID_TYPE_STRING = 1,
    MCP_REQUEST_ID_TYPE_NUMBER = 2
} mcp_request_id_type_t;

/**
 * Opaque request ID handle
 * Internally stores either string or number, accessed via functions
 */
typedef struct mcp_request_id_impl* mcp_request_id_t;

/* Request ID creation and access */
MCP_API mcp_request_id_t mcp_request_id_create_string(const char* str) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_id_create_number(int64_t num) MCP_NOEXCEPT;
MCP_API void mcp_request_id_free(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_request_id_type_t mcp_request_id_get_type(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API const char* mcp_request_id_get_string(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API int64_t mcp_request_id_get_number(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_id_is_valid(mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_id_clone(mcp_request_id_t id) MCP_NOEXCEPT;

/* ============================================================================
 * Improved List Type (No void**)
 * ============================================================================ */

/**
 * Opaque list handle
 * Type-safe list operations with index-based access
 */
typedef struct mcp_list_impl* mcp_list_t;

/* List creation and management */
MCP_API mcp_list_t mcp_list_create(mcp_type_id_t element_type) MCP_NOEXCEPT;
MCP_API mcp_list_t mcp_list_create_with_capacity(mcp_type_id_t element_type, size_t capacity) MCP_NOEXCEPT;
MCP_API void mcp_list_free(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_push(mcp_list_t list, void* item) MCP_NOEXCEPT;
MCP_API void* mcp_list_get(mcp_list_t list, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_set(mcp_list_t list, size_t index, void* item) MCP_NOEXCEPT;
MCP_API size_t mcp_list_size(mcp_list_t list) MCP_NOEXCEPT;
MCP_API size_t mcp_list_capacity(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_clear(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_list_is_valid(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_type_id_t mcp_list_element_type(mcp_list_t list) MCP_NOEXCEPT;

/* List iteration */
typedef struct mcp_list_iterator_impl* mcp_list_iterator_t;
MCP_API mcp_list_iterator_t mcp_list_iterator_create(mcp_list_t list) MCP_NOEXCEPT;
MCP_API void mcp_list_iterator_free(mcp_list_iterator_t iter) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_list_iterator_has_next(mcp_list_iterator_t iter) MCP_NOEXCEPT;
MCP_API void* mcp_list_iterator_next(mcp_list_iterator_t iter) MCP_NOEXCEPT;
MCP_API void mcp_list_iterator_reset(mcp_list_iterator_t iter) MCP_NOEXCEPT;

/* ============================================================================
 * Improved Map Type
 * ============================================================================ */

/**
 * Opaque map handle
 * Key-value store with string keys
 */
typedef struct mcp_map_impl* mcp_map_t;

/* Map creation and management */
MCP_API mcp_map_t mcp_map_create(mcp_type_id_t value_type) MCP_NOEXCEPT;
MCP_API void mcp_map_free(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_set(mcp_map_t map, const char* key, void* value) MCP_NOEXCEPT;
MCP_API void* mcp_map_get(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_has(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_remove(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API size_t mcp_map_size(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_clear(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_is_valid(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_type_id_t mcp_map_value_type(mcp_map_t map) MCP_NOEXCEPT;

/* Map iteration */
typedef struct mcp_map_iterator_impl* mcp_map_iterator_t;
MCP_API mcp_map_iterator_t mcp_map_iterator_create(mcp_map_t map) MCP_NOEXCEPT;
MCP_API void mcp_map_iterator_free(mcp_map_iterator_t iter) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_iterator_has_next(mcp_map_iterator_t iter) MCP_NOEXCEPT;
MCP_API const char* mcp_map_iterator_next_key(mcp_map_iterator_t iter) MCP_NOEXCEPT;
MCP_API void* mcp_map_iterator_next_value(mcp_map_iterator_t iter) MCP_NOEXCEPT;
MCP_API void mcp_map_iterator_reset(mcp_map_iterator_t iter) MCP_NOEXCEPT;

/* ============================================================================
 * JSON Value (Opaque)
 * ============================================================================ */

/**
 * Opaque JSON value handle
 */
typedef struct mcp_json_impl* mcp_json_t;

typedef enum mcp_json_type {
    MCP_JSON_TYPE_NULL = 0,
    MCP_JSON_TYPE_BOOLEAN = 1,
    MCP_JSON_TYPE_NUMBER = 2,
    MCP_JSON_TYPE_STRING = 3,
    MCP_JSON_TYPE_ARRAY = 4,
    MCP_JSON_TYPE_OBJECT = 5
} mcp_json_type_t;

/* JSON creation */
MCP_API mcp_json_t mcp_json_create_null(void) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_create_bool(mcp_bool_t value) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_create_number(double value) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_create_string(const char* value) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_create_array(void) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_create_object(void) MCP_NOEXCEPT;
MCP_API void mcp_json_free(mcp_json_t json) MCP_NOEXCEPT;

/* JSON type checking */
MCP_API mcp_json_type_t mcp_json_get_type(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_is_null(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_is_bool(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_is_number(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_is_string(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_is_array(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_is_object(mcp_json_t json) MCP_NOEXCEPT;

/* JSON value access */
MCP_API mcp_bool_t mcp_json_get_bool(mcp_json_t json) MCP_NOEXCEPT;
MCP_API double mcp_json_get_number(mcp_json_t json) MCP_NOEXCEPT;
MCP_API const char* mcp_json_get_string(mcp_json_t json) MCP_NOEXCEPT;

/* JSON array operations */
MCP_API size_t mcp_json_array_size(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_array_get(mcp_json_t json, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_json_array_push(mcp_json_t json, mcp_json_t value) MCP_NOEXCEPT;

/* JSON object operations */
MCP_API size_t mcp_json_object_size(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_object_get(mcp_json_t json, const char* key) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_json_object_set(mcp_json_t json, const char* key, mcp_json_t value) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_object_has(mcp_json_t json, const char* key) MCP_NOEXCEPT;

/* JSON serialization */
MCP_API char* mcp_json_to_string(mcp_json_t json) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_from_string(const char* str) MCP_NOEXCEPT;

/* ============================================================================
 * MCP Protocol Types (Opaque Handles)
 * ============================================================================ */

/* Forward declarations of opaque protocol types */
typedef struct mcp_request_impl* mcp_request_t;
typedef struct mcp_response_impl* mcp_response_t;
typedef struct mcp_notification_impl* mcp_notification_t;
typedef struct mcp_error_impl* mcp_error_t;
typedef struct mcp_result_impl* mcp_result_obj_t;  /* Renamed to avoid conflict */
typedef struct mcp_capability_impl* mcp_capability_t;
typedef struct mcp_implementation_impl* mcp_implementation_t;
typedef struct mcp_client_info_impl* mcp_client_info_t;
typedef struct mcp_server_info_impl* mcp_server_info_t;
typedef struct mcp_tool_impl* mcp_tool_t;
typedef struct mcp_resource_impl* mcp_resource_t;
typedef struct mcp_prompt_impl* mcp_prompt_t;

/* ============================================================================
 * Request Type
 * ============================================================================ */

/* Request creation and management */
MCP_API mcp_request_t mcp_request_create(const char* method) MCP_NOEXCEPT;
MCP_API void mcp_request_free(mcp_request_t request) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_request_set_id(mcp_request_t request, mcp_request_id_t id) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_request_set_params(mcp_request_t request, mcp_json_t params) MCP_NOEXCEPT;
MCP_API const char* mcp_request_get_method(mcp_request_t request) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_request_get_id(mcp_request_t request) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_request_get_params(mcp_request_t request) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_request_is_valid(mcp_request_t request) MCP_NOEXCEPT;

/* ============================================================================
 * Response Type
 * ============================================================================ */

/* Response creation and management */
MCP_API mcp_response_t mcp_response_create_success(mcp_request_id_t id, mcp_json_t result) MCP_NOEXCEPT;
MCP_API mcp_response_t mcp_response_create_error(mcp_request_id_t id, mcp_error_t error) MCP_NOEXCEPT;
MCP_API void mcp_response_free(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_request_id_t mcp_response_get_id(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_response_is_success(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_response_is_error(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_response_get_result(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_error_t mcp_response_get_error(mcp_response_t response) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_response_is_valid(mcp_response_t response) MCP_NOEXCEPT;

/* ============================================================================
 * Notification Type
 * ============================================================================ */

/* Notification creation and management */
MCP_API mcp_notification_t mcp_notification_create(const char* method) MCP_NOEXCEPT;
MCP_API void mcp_notification_free(mcp_notification_t notification) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_notification_set_params(mcp_notification_t notification, mcp_json_t params) MCP_NOEXCEPT;
MCP_API const char* mcp_notification_get_method(mcp_notification_t notification) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_notification_get_params(mcp_notification_t notification) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_notification_is_valid(mcp_notification_t notification) MCP_NOEXCEPT;

/* ============================================================================
 * Error Type
 * ============================================================================ */

/* Error creation and management */
MCP_API mcp_error_t mcp_error_create(int32_t code, const char* message) MCP_NOEXCEPT;
MCP_API mcp_error_t mcp_error_create_with_data(int32_t code, const char* message, mcp_json_t data) MCP_NOEXCEPT;
MCP_API void mcp_error_free(mcp_error_t error) MCP_NOEXCEPT;
MCP_API int32_t mcp_error_get_code(mcp_error_t error) MCP_NOEXCEPT;
MCP_API const char* mcp_error_get_message(mcp_error_t error) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_error_get_data(mcp_error_t error) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_error_is_valid(mcp_error_t error) MCP_NOEXCEPT;

/* ============================================================================
 * RAII Integration Helpers
 * ============================================================================ */

/**
 * Move semantics support for efficient RAII
 * These functions transfer ownership without copying
 */
MCP_API mcp_string_owned_t* mcp_string_move(mcp_string_owned_t* from, mcp_string_owned_t* to) MCP_NOEXCEPT;
MCP_API mcp_list_t mcp_list_move(mcp_list_t* from, mcp_list_t* to) MCP_NOEXCEPT;
MCP_API mcp_map_t mcp_map_move(mcp_map_t* from, mcp_map_t* to) MCP_NOEXCEPT;
MCP_API mcp_json_t mcp_json_move(mcp_json_t* from, mcp_json_t* to) MCP_NOEXCEPT;

/**
 * Batch operations for efficient resource management
 */
typedef struct mcp_batch_operation {
    void* resource;
    void (*deleter)(void*);
} mcp_batch_operation_t;

MCP_API void mcp_batch_execute(const mcp_batch_operation_t* operations, size_t count) MCP_NOEXCEPT;

/**
 * Validation helpers for defensive programming
 */
MCP_API mcp_bool_t mcp_validate_handle(mcp_handle_t handle, mcp_type_id_t expected_type) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_validate_string(const mcp_string_view_t* str) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_validate_memory_range(const void* ptr, size_t size) MCP_NOEXCEPT;

/* ============================================================================
 * Thread Safety Helpers
 * ============================================================================ */

/**
 * Thread-safe singleton access pattern
 */
typedef struct mcp_singleton_impl* mcp_singleton_t;

MCP_API mcp_singleton_t mcp_singleton_get_or_create(
    const char* name,
    mcp_singleton_t (*creator)(void),
    void (*deleter)(mcp_singleton_t)
) MCP_NOEXCEPT;

/**
 * Scoped lock for thread-safe operations
 */
typedef struct mcp_scoped_lock_impl* mcp_scoped_lock_t;

MCP_API mcp_scoped_lock_t mcp_scoped_lock_acquire(const char* resource_name) MCP_NOEXCEPT;
MCP_API void mcp_scoped_lock_release(mcp_scoped_lock_t lock) MCP_NOEXCEPT;

/* ============================================================================
 * Memory Pool Integration
 * ============================================================================ */

/**
 * Pool-allocated string for efficient string operations
 */
MCP_API mcp_string_owned_t* mcp_string_create_from_pool(
    mcp_mempool_t pool, 
    const char* data, 
    size_t length
) MCP_NOEXCEPT;

/**
 * Pool-allocated list for efficient list operations
 */
MCP_API mcp_list_t mcp_list_create_from_pool(
    mcp_mempool_t pool,
    mcp_type_id_t element_type,
    size_t initial_capacity
) MCP_NOEXCEPT;

/**
 * Pool-allocated map for efficient map operations
 */
MCP_API mcp_map_t mcp_map_create_from_pool(
    mcp_mempool_t pool,
    mcp_type_id_t value_type,
    size_t initial_capacity
) MCP_NOEXCEPT;

/* ============================================================================
 * Lifecycle Callbacks for RAII
 * ============================================================================ */

/**
 * Resource lifecycle callbacks for custom RAII wrappers
 */
typedef struct mcp_lifecycle_callbacks {
    void* (*on_create)(void* user_data);
    void (*on_destroy)(void* resource, void* user_data);
    void (*on_error)(const char* message, void* user_data);
    void* user_data;
} mcp_lifecycle_callbacks_t;

MCP_API void mcp_set_lifecycle_callbacks(
    mcp_type_id_t type,
    const mcp_lifecycle_callbacks_t* callbacks
) MCP_NOEXCEPT;

/* ============================================================================
 * C++ RAII Bridge (When included from C++)
 * ============================================================================ */

#ifdef __cplusplus
} /* extern "C" */

/* When included from C++, provide RAII wrappers */
#include <memory>
#include <functional>

namespace mcp {
namespace c_api {

/* Custom deleters for unique_ptr */
struct StringDeleter {
    void operator()(mcp_string_owned_t* s) const noexcept {
        mcp_string_free(s);
    }
};

struct ListDeleter {
    void operator()(mcp_list_t l) const noexcept {
        mcp_list_free(l);
    }
};

struct MapDeleter {
    void operator()(mcp_map_t m) const noexcept {
        mcp_map_free(m);
    }
};

struct JsonDeleter {
    void operator()(mcp_json_t j) const noexcept {
        mcp_json_free(j);
    }
};

struct RequestDeleter {
    void operator()(mcp_request_t r) const noexcept {
        mcp_request_free(r);
    }
};

struct ResponseDeleter {
    void operator()(mcp_response_t r) const noexcept {
        mcp_response_free(r);
    }
};

/* Type aliases for RAII */
using UniqueString = std::unique_ptr<mcp_string_owned_t, StringDeleter>;
using UniqueList = std::unique_ptr<std::remove_pointer_t<mcp_list_t>, ListDeleter>;
using UniqueMap = std::unique_ptr<std::remove_pointer_t<mcp_map_t>, MapDeleter>;
using UniqueJson = std::unique_ptr<std::remove_pointer_t<mcp_json_t>, JsonDeleter>;
using UniqueRequest = std::unique_ptr<std::remove_pointer_t<mcp_request_t>, RequestDeleter>;
using UniqueResponse = std::unique_ptr<std::remove_pointer_t<mcp_response_t>, ResponseDeleter>;

/* Factory functions that return RAII wrappers */
inline UniqueString make_string(const char* data, size_t length) {
    return UniqueString(mcp_string_create(data, length));
}

inline UniqueList make_list(mcp_type_id_t element_type) {
    return UniqueList(mcp_list_create(element_type));
}

inline UniqueMap make_map(mcp_type_id_t value_type) {
    return UniqueMap(mcp_map_create(value_type));
}

inline UniqueJson make_json_string(const char* value) {
    return UniqueJson(mcp_json_create_string(value));
}

inline UniqueRequest make_request(const char* method) {
    return UniqueRequest(mcp_request_create(method));
}

} // namespace c_api
} // namespace mcp

#endif /* __cplusplus */

#endif /* MCP_C_TYPES_IMPROVED_H */