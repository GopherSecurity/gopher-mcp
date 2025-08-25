/**
 * @file mcp_types_improved.cc
 * @brief Implementation of improved FFI-safe MCP types without unions
 */

#include "mcp/c_api/mcp_c_types_improved.h"
#include "mcp/c_api/mcp_ffi_core.h"
#include "mcp/c_api/mcp_raii.h"
// Note: mcp/types.h is not available, using simplified JSON implementation
#include <unordered_map>
#include <vector>
#include <variant>
#include <string>
#include <memory>
#include <mutex>

/* ============================================================================
 * Internal Implementation Structures
 * ============================================================================ */

// Use RAII namespace
using namespace mcp::raii;

// Request ID implementation using std::variant instead of union
struct mcp_request_id_impl {
    mcp_handle_header_t header;
    mcp_request_id_type_t type;
    std::variant<std::monostate, std::string, int64_t> value;
    
    mcp_request_id_impl() {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_UNKNOWN;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED;
        header.version = 1;
        type = MCP_REQUEST_ID_TYPE_NONE;
    }
};

// List implementation with type safety
struct mcp_list_impl {
    mcp_handle_header_t header;
    mcp_type_id_t element_type;
    std::vector<void*> items;
    std::mutex mutex;  // Thread safety
    
    mcp_list_impl(mcp_type_id_t type) : element_type(type) {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_LIST;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED | MCP_HANDLE_FLAG_THREAD_SAFE;
        header.version = 1;
    }
};

// Map implementation with string keys
struct mcp_map_impl {
    mcp_handle_header_t header;
    mcp_type_id_t value_type;
    std::unordered_map<std::string, void*> items;
    std::mutex mutex;
    
    mcp_map_impl(mcp_type_id_t type) : value_type(type) {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_MAP;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED | MCP_HANDLE_FLAG_THREAD_SAFE;
        header.version = 1;
    }
};

// Simplified JSON implementation using std::variant
struct mcp_json_impl {
    mcp_handle_header_t header;
    using JsonValue = std::variant<
        std::nullptr_t,                                    // null
        bool,                                               // boolean
        double,                                             // number
        std::string,                                        // string
        std::vector<std::unique_ptr<mcp_json_impl>>,      // array
        std::unordered_map<std::string, std::unique_ptr<mcp_json_impl>> // object
    >;
    JsonValue value;
    
    mcp_json_impl() {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_JSON;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED;
        header.version = 1;
    }
};

// Request implementation
struct mcp_request_impl {
    mcp_handle_header_t header;
    std::string method;
    std::unique_ptr<mcp_request_id_impl> id;
    std::unique_ptr<mcp_json_impl> params;
    
    mcp_request_impl() {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_REQUEST;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED;
        header.version = 1;
    }
};

// Response implementation
struct mcp_response_impl {
    mcp_handle_header_t header;
    std::unique_ptr<mcp_request_id_impl> id;
    std::variant<std::unique_ptr<mcp_json_impl>, std::unique_ptr<struct mcp_error_impl>> result;
    
    mcp_response_impl() {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_RESPONSE;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED;
        header.version = 1;
    }
};

// Notification implementation
struct mcp_notification_impl {
    mcp_handle_header_t header;
    std::string method;
    std::unique_ptr<mcp_json_impl> params;
    
    mcp_notification_impl() {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_NOTIFICATION;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED;
        header.version = 1;
    }
};

// Error implementation
struct mcp_error_impl {
    mcp_handle_header_t header;
    int32_t code;
    std::string message;
    std::unique_ptr<mcp_json_impl> data;
    
    mcp_error_impl() {
        header.magic = MCP_HANDLE_MAGIC;
        header.type_id = MCP_TYPE_ERROR;
        header.flags = MCP_HANDLE_FLAG_VALID | MCP_HANDLE_FLAG_OWNED;
        header.version = 1;
    }
};

// Iterator implementations
struct mcp_list_iterator_impl {
    mcp_list_impl* list;
    size_t index;
};

struct mcp_map_iterator_impl {
    mcp_map_impl* map;
    std::unordered_map<std::string, void*>::iterator current;
    std::unordered_map<std::string, void*>::iterator end;
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

namespace {
    template<typename T>
    T* validate_handle(void* handle, mcp_type_id_t expected_type) {
        if (!handle) return nullptr;
        
        auto* h = static_cast<T*>(handle);
        if (h->header.magic != MCP_HANDLE_MAGIC) return nullptr;
        if (h->header.type_id != expected_type) return nullptr;
        if (!(h->header.flags & MCP_HANDLE_FLAG_VALID)) return nullptr;
        
        return h;
    }
}

extern "C" {

/* ============================================================================
 * Request ID Implementation
 * ============================================================================ */

MCP_API mcp_request_id_t mcp_request_id_create_string(const char* str) MCP_NOEXCEPT {
    if (!str) return nullptr;
    
    auto* impl = new mcp_request_id_impl();
    impl->type = MCP_REQUEST_ID_TYPE_STRING;
    impl->value = std::string(str);
    
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_request_id_t");
    return impl;
}

MCP_API mcp_request_id_t mcp_request_id_create_number(int64_t num) MCP_NOEXCEPT {
    auto* impl = new mcp_request_id_impl();
    impl->type = MCP_REQUEST_ID_TYPE_NUMBER;
    impl->value = num;
    
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_request_id_t");
    return impl;
}

MCP_API void mcp_request_id_free(mcp_request_id_t id) MCP_NOEXCEPT {
    if (id) {
        MCP_RAII_UNTRACK_RESOURCE(id);
        delete id;
    }
}

MCP_API mcp_request_id_type_t mcp_request_id_get_type(mcp_request_id_t id) MCP_NOEXCEPT {
    if (!id) return MCP_REQUEST_ID_TYPE_NONE;
    return id->type;
}

MCP_API const char* mcp_request_id_get_string(mcp_request_id_t id) MCP_NOEXCEPT {
    if (!id || id->type != MCP_REQUEST_ID_TYPE_STRING) return nullptr;
    
    try {
        return std::get<std::string>(id->value).c_str();
    } catch (...) {
        return nullptr;
    }
}

MCP_API int64_t mcp_request_id_get_number(mcp_request_id_t id) MCP_NOEXCEPT {
    if (!id || id->type != MCP_REQUEST_ID_TYPE_NUMBER) return 0;
    
    try {
        return std::get<int64_t>(id->value);
    } catch (...) {
        return 0;
    }
}

MCP_API mcp_bool_t mcp_request_id_is_valid(mcp_request_id_t id) MCP_NOEXCEPT {
    return (id && id->header.magic == MCP_HANDLE_MAGIC) ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_request_id_t mcp_request_id_clone(mcp_request_id_t id) MCP_NOEXCEPT {
    if (!id) return nullptr;
    
    if (id->type == MCP_REQUEST_ID_TYPE_STRING) {
        return mcp_request_id_create_string(mcp_request_id_get_string(id));
    } else if (id->type == MCP_REQUEST_ID_TYPE_NUMBER) {
        return mcp_request_id_create_number(mcp_request_id_get_number(id));
    }
    
    return nullptr;
}

/* ============================================================================
 * List Implementation
 * ============================================================================ */

MCP_API mcp_list_t mcp_list_create(mcp_type_id_t element_type) MCP_NOEXCEPT {
    auto* impl = new mcp_list_impl(element_type);
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_list_t");
    return impl;
}

MCP_API mcp_list_t mcp_list_create_with_capacity(mcp_type_id_t element_type, size_t capacity) MCP_NOEXCEPT {
    auto* impl = new mcp_list_impl(element_type);
    impl->items.reserve(capacity);
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_list_t");
    return impl;
}

MCP_API void mcp_list_free(mcp_list_t list) MCP_NOEXCEPT {
    if (list) {
        MCP_RAII_UNTRACK_RESOURCE(list);
        delete list;
    }
}

MCP_API mcp_result_t mcp_list_push(mcp_list_t list, void* item) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    if (!impl) return MCP_ERROR_INVALID_ARGUMENT;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->items.push_back(item);
    return MCP_OK;
}

MCP_API void* mcp_list_get(mcp_list_t list, size_t index) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    if (!impl) return nullptr;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (index >= impl->items.size()) return nullptr;
    return impl->items[index];
}

MCP_API mcp_result_t mcp_list_set(mcp_list_t list, size_t index, void* item) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    if (!impl) return MCP_ERROR_INVALID_ARGUMENT;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (index >= impl->items.size()) return MCP_ERROR_INVALID_ARGUMENT;
    impl->items[index] = item;
    return MCP_OK;
}

MCP_API size_t mcp_list_size(mcp_list_t list) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    if (!impl) return 0;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->items.size();
}

MCP_API size_t mcp_list_capacity(mcp_list_t list) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    if (!impl) return 0;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->items.capacity();
}

MCP_API mcp_result_t mcp_list_clear(mcp_list_t list) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    if (!impl) return MCP_ERROR_INVALID_ARGUMENT;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->items.clear();
    return MCP_OK;
}

MCP_API mcp_bool_t mcp_list_is_valid(mcp_list_t list) MCP_NOEXCEPT {
    return validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST) ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_type_id_t mcp_list_element_type(mcp_list_t list) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_list_impl>(list, MCP_TYPE_LIST);
    return impl ? impl->element_type : MCP_TYPE_UNKNOWN;
}

/* ============================================================================
 * Map Implementation
 * ============================================================================ */

MCP_API mcp_map_t mcp_map_create(mcp_type_id_t value_type) MCP_NOEXCEPT {
    auto* impl = new mcp_map_impl(value_type);
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_map_t");
    return impl;
}

MCP_API void mcp_map_free(mcp_map_t map) MCP_NOEXCEPT {
    if (map) {
        MCP_RAII_UNTRACK_RESOURCE(map);
        delete map;
    }
}

MCP_API mcp_result_t mcp_map_set(mcp_map_t map, const char* key, void* value) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    if (!impl || !key) return MCP_ERROR_INVALID_ARGUMENT;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->items[key] = value;
    return MCP_OK;
}

MCP_API void* mcp_map_get(mcp_map_t map, const char* key) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    if (!impl || !key) return nullptr;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto it = impl->items.find(key);
    return (it != impl->items.end()) ? it->second : nullptr;
}

MCP_API mcp_bool_t mcp_map_has(mcp_map_t map, const char* key) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    if (!impl || !key) return MCP_FALSE;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->items.find(key) != impl->items.end() ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_result_t mcp_map_remove(mcp_map_t map, const char* key) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    if (!impl || !key) return MCP_ERROR_INVALID_ARGUMENT;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->items.erase(key) > 0 ? MCP_OK : MCP_ERROR_NOT_FOUND;
}

MCP_API size_t mcp_map_size(mcp_map_t map) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    if (!impl) return 0;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->items.size();
}

MCP_API mcp_result_t mcp_map_clear(mcp_map_t map) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    if (!impl) return MCP_ERROR_INVALID_ARGUMENT;
    
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->items.clear();
    return MCP_OK;
}

MCP_API mcp_bool_t mcp_map_is_valid(mcp_map_t map) MCP_NOEXCEPT {
    return validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP) ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_type_id_t mcp_map_value_type(mcp_map_t map) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_map_impl>(map, MCP_TYPE_MAP);
    return impl ? impl->value_type : MCP_TYPE_UNKNOWN;
}

/* ============================================================================
 * JSON Implementation
 * ============================================================================ */

MCP_API mcp_json_t mcp_json_create_null(void) MCP_NOEXCEPT {
    auto* impl = new mcp_json_impl();
    impl->value = nullptr;
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_json_t");
    return impl;
}

MCP_API mcp_json_t mcp_json_create_bool(mcp_bool_t value) MCP_NOEXCEPT {
    auto* impl = new mcp_json_impl();
    impl->value = (value == MCP_TRUE);
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_json_t");
    return impl;
}

MCP_API mcp_json_t mcp_json_create_number(double value) MCP_NOEXCEPT {
    auto* impl = new mcp_json_impl();
    impl->value = value;
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_json_t");
    return impl;
}

MCP_API mcp_json_t mcp_json_create_string(const char* value) MCP_NOEXCEPT {
    if (!value) return nullptr;
    
    auto* impl = new mcp_json_impl();
    impl->value = std::string(value);
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_json_t");
    return impl;
}

MCP_API mcp_json_t mcp_json_create_array(void) MCP_NOEXCEPT {
    auto* impl = new mcp_json_impl();
    impl->value = std::vector<std::unique_ptr<mcp_json_impl>>();
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_json_t");
    return impl;
}

MCP_API mcp_json_t mcp_json_create_object(void) MCP_NOEXCEPT {
    auto* impl = new mcp_json_impl();
    impl->value = std::unordered_map<std::string, std::unique_ptr<mcp_json_impl>>();
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_json_t");
    return impl;
}

MCP_API void mcp_json_free(mcp_json_t json) MCP_NOEXCEPT {
    if (json) {
        MCP_RAII_UNTRACK_RESOURCE(json);
        delete json;
    }
}

MCP_API mcp_json_type_t mcp_json_get_type(mcp_json_t json) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_json_impl>(json, MCP_TYPE_JSON);
    if (!impl) return MCP_JSON_TYPE_NULL;
    
    return std::visit([](const auto& v) -> mcp_json_type_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return MCP_JSON_TYPE_NULL;
        else if constexpr (std::is_same_v<T, bool>) return MCP_JSON_TYPE_BOOLEAN;
        else if constexpr (std::is_same_v<T, double>) return MCP_JSON_TYPE_NUMBER;
        else if constexpr (std::is_same_v<T, std::string>) return MCP_JSON_TYPE_STRING;
        else if constexpr (std::is_same_v<T, std::vector<std::unique_ptr<mcp_json_impl>>>) return MCP_JSON_TYPE_ARRAY;
        else if constexpr (std::is_same_v<T, std::unordered_map<std::string, std::unique_ptr<mcp_json_impl>>>) return MCP_JSON_TYPE_OBJECT;
        else return MCP_JSON_TYPE_NULL;
    }, impl->value);
}

MCP_API mcp_bool_t mcp_json_get_bool(mcp_json_t json) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_json_impl>(json, MCP_TYPE_JSON);
    if (!impl) return MCP_FALSE;
    
    if (auto* b = std::get_if<bool>(&impl->value)) {
        return *b ? MCP_TRUE : MCP_FALSE;
    }
    return MCP_FALSE;
}

MCP_API double mcp_json_get_number(mcp_json_t json) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_json_impl>(json, MCP_TYPE_JSON);
    if (!impl) return 0.0;
    
    if (auto* n = std::get_if<double>(&impl->value)) {
        return *n;
    }
    return 0.0;
}

MCP_API const char* mcp_json_get_string(mcp_json_t json) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_json_impl>(json, MCP_TYPE_JSON);
    if (!impl) return nullptr;
    
    if (auto* s = std::get_if<std::string>(&impl->value)) {
        return s->c_str();
    }
    return nullptr;
}

/* ============================================================================
 * Request/Response Implementation
 * ============================================================================ */

MCP_API mcp_request_t mcp_request_create(const char* method) MCP_NOEXCEPT {
    if (!method) return nullptr;
    
    auto* impl = new mcp_request_impl();
    impl->method = method;
    MCP_RAII_TRACK_RESOURCE(impl, "mcp_request_t");
    return impl;
}

MCP_API void mcp_request_free(mcp_request_t request) MCP_NOEXCEPT {
    if (request) {
        MCP_RAII_UNTRACK_RESOURCE(request);
        delete request;
    }
}

MCP_API mcp_result_t mcp_request_set_id(mcp_request_t request, mcp_request_id_t id) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_request_impl>(request, MCP_TYPE_REQUEST);
    if (!impl || !id) return MCP_ERROR_INVALID_ARGUMENT;
    
    impl->id.reset(static_cast<mcp_request_id_impl*>(mcp_request_id_clone(id)));
    return MCP_OK;
}

MCP_API mcp_result_t mcp_request_set_params(mcp_request_t request, mcp_json_t params) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_request_impl>(request, MCP_TYPE_REQUEST);
    auto* json_impl = validate_handle<mcp_json_impl>(params, MCP_TYPE_JSON);
    if (!impl || !json_impl) return MCP_ERROR_INVALID_ARGUMENT;
    
    impl->params = std::make_unique<mcp_json_impl>(*json_impl);
    return MCP_OK;
}

MCP_API const char* mcp_request_get_method(mcp_request_t request) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_request_impl>(request, MCP_TYPE_REQUEST);
    return impl ? impl->method.c_str() : nullptr;
}

MCP_API mcp_request_id_t mcp_request_get_id(mcp_request_t request) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_request_impl>(request, MCP_TYPE_REQUEST);
    return impl ? impl->id.get() : nullptr;
}

MCP_API mcp_json_t mcp_request_get_params(mcp_request_t request) MCP_NOEXCEPT {
    auto* impl = validate_handle<mcp_request_impl>(request, MCP_TYPE_REQUEST);
    return impl ? impl->params.get() : nullptr;
}

MCP_API mcp_bool_t mcp_request_is_valid(mcp_request_t request) MCP_NOEXCEPT {
    return validate_handle<mcp_request_impl>(request, MCP_TYPE_REQUEST) ? MCP_TRUE : MCP_FALSE;
}

/* ============================================================================
 * Move Semantics Implementation
 * ============================================================================ */

MCP_API mcp_string_owned_t* mcp_string_move(mcp_string_owned_t* from, mcp_string_owned_t* to) MCP_NOEXCEPT {
    if (!from || !to) return nullptr;
    
    // Free target's existing data
    if (to->data) {
        free(to->data);
    }
    
    // Move from source to target
    to->data = from->data;
    to->length = from->length;
    to->capacity = from->capacity;
    
    // Clear source
    from->data = nullptr;
    from->length = 0;
    from->capacity = 0;
    
    return to;
}

/* ============================================================================
 * Validation Functions
 * ============================================================================ */

MCP_API mcp_bool_t mcp_validate_handle(mcp_handle_t handle, mcp_type_id_t expected_type) MCP_NOEXCEPT {
    if (!handle) return MCP_FALSE;
    
    auto* header = reinterpret_cast<mcp_handle_header_t*>(handle);
    if (header->magic != MCP_HANDLE_MAGIC) return MCP_FALSE;
    if (header->type_id != expected_type) return MCP_FALSE;
    if (!(header->flags & MCP_HANDLE_FLAG_VALID)) return MCP_FALSE;
    
    return MCP_TRUE;
}

MCP_API mcp_bool_t mcp_validate_string(const mcp_string_view_t* str) MCP_NOEXCEPT {
    if (!str) return MCP_FALSE;
    if (!str->data && str->length > 0) return MCP_FALSE;
    return MCP_TRUE;
}

MCP_API mcp_bool_t mcp_validate_memory_range(const void* ptr, size_t size) MCP_NOEXCEPT {
    if (!ptr && size > 0) return MCP_FALSE;
    // Additional platform-specific validation could go here
    return MCP_TRUE;
}

} // extern "C"