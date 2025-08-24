/**
 * @file mcp_c_api_utils.cc
 * @brief Utility function implementations for MCP C types
 *
 * Provides memory management, validation, deep copy, and other
 * utility functions for working with MCP C types.
 */

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>

#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/c_api/mcp_c_types.h"

namespace mcp {
namespace c_api {

// Helper to allocate and copy string
static char* alloc_string(const char* str) {
  if (!str)
    return nullptr;
  size_t len = std::strlen(str);
  char* result = static_cast<char*>(std::malloc(len + 1));
  if (result) {
    std::strcpy(result, str);
  }
  return result;
}

// Memory pool implementation
struct mcp_memory_pool {
  std::vector<void*> allocations;
  size_t total_size;
  size_t peak_size;

  mcp_memory_pool() : total_size(0), peak_size(0) {}

  ~mcp_memory_pool() {
    for (void* ptr : allocations) {
      std::free(ptr);
    }
  }

  void* allocate(size_t size) {
    void* ptr = std::malloc(size);
    if (ptr) {
      allocations.push_back(ptr);
      total_size += size;
      if (total_size > peak_size) {
        peak_size = total_size;
      }
    }
    return ptr;
  }

  void deallocate(void* ptr, size_t size) {
    auto it = std::find(allocations.begin(), allocations.end(), ptr);
    if (it != allocations.end()) {
      allocations.erase(it);
      std::free(ptr);
      total_size -= size;
    }
  }
};

// Memory management
extern "C" void mcp_content_block_free(mcp_content_block_t* block) {
  if (!block)
    return;

  switch (block->type) {
    case MCP_CONTENT_TEXT:
      std::free(block->text.text);
      if (block->text.annotations.has_value &&
          block->text.annotations.value.audience) {
        std::free(block->text.annotations.value.audience);
      }
      break;

    case MCP_CONTENT_IMAGE:
      std::free(block->image.data);
      std::free(block->image.mime_type);
      break;

    case MCP_CONTENT_AUDIO:
      std::free(block->audio.data);
      std::free(block->audio.mime_type);
      break;

    case MCP_CONTENT_RESOURCE:
      std::free(block->resource.resource.uri);
      std::free(block->resource.resource.name);
      std::free(block->resource.resource.description);
      std::free(block->resource.resource.mime_type);
      break;

    case MCP_CONTENT_EMBEDDED:
      std::free(block->embedded.resource.uri);
      std::free(block->embedded.resource.name);
      std::free(block->embedded.resource.description);
      std::free(block->embedded.resource.mime_type);
      if (block->embedded.content) {
        for (size_t i = 0; i < block->embedded.content_count; ++i) {
          mcp_content_block_free(&block->embedded.content[i]);
        }
        std::free(block->embedded.content);
      }
      break;
  }

  std::free(block);
}

extern "C" void mcp_tool_free(mcp_tool_t* tool) {
  if (!tool)
    return;

  std::free(tool->name);
  std::free(tool->description);
  // Note: input_schema is a JSON handle that should be freed separately
  std::free(tool);
}

extern "C" void mcp_prompt_free(mcp_prompt_t* prompt) {
  if (!prompt)
    return;

  std::free(prompt->name);
  std::free(prompt->description);

  if (prompt->arguments) {
    for (size_t i = 0; i < prompt->argument_count; ++i) {
      std::free(prompt->arguments[i].name);
      std::free(prompt->arguments[i].description);
    }
    std::free(prompt->arguments);
  }

  std::free(prompt);
}

extern "C" void mcp_resource_free(mcp_resource_t* resource) {
  if (!resource)
    return;

  std::free(resource->uri);
  std::free(resource->name);
  std::free(resource->description);
  std::free(resource->mime_type);
  std::free(resource);
}

extern "C" void mcp_message_free(mcp_message_t* message) {
  if (!message)
    return;

  mcp_content_block_free(message->content);
  std::free(message);
}

extern "C" void mcp_error_free(mcp_error_t* error) {
  if (!error)
    return;

  std::free(error->message);
  // Note: data is a JSON handle that should be freed separately
  std::free(error);
}

// Deep copy functions
extern "C" mcp_content_block_t* mcp_content_block_copy(
    const mcp_content_block_t* block) {
  if (!block)
    return nullptr;

  auto* copy = static_cast<mcp_content_block_t*>(
      std::calloc(1, sizeof(mcp_content_block_t)));
  if (!copy)
    return nullptr;

  copy->type = block->type;

  switch (block->type) {
    case MCP_CONTENT_TEXT:
      copy->text.text = alloc_string(block->text.text);
      if (block->text.annotations.has_value) {
        copy->text.annotations.has_value = true;
        copy->text.annotations.value = block->text.annotations.value;
        if (block->text.annotations.value.audience_count > 0 &&
            block->text.annotations.value.audience) {
          size_t size =
              block->text.annotations.value.audience_count * sizeof(mcp_role_t);
          copy->text.annotations.value.audience =
              static_cast<mcp_role_t*>(std::malloc(size));
          if (copy->text.annotations.value.audience) {
            std::memcpy(copy->text.annotations.value.audience,
                        block->text.annotations.value.audience, size);
          }
        }
      }
      break;

    case MCP_CONTENT_IMAGE:
      copy->image.data = alloc_string(block->image.data);
      copy->image.mime_type = alloc_string(block->image.mime_type);
      break;

    case MCP_CONTENT_AUDIO:
      copy->audio.data = alloc_string(block->audio.data);
      copy->audio.mime_type = alloc_string(block->audio.mime_type);
      break;

    case MCP_CONTENT_RESOURCE:
      copy->resource.resource.uri = alloc_string(block->resource.resource.uri);
      copy->resource.resource.name =
          alloc_string(block->resource.resource.name);
      copy->resource.resource.description =
          alloc_string(block->resource.resource.description);
      copy->resource.resource.mime_type =
          alloc_string(block->resource.resource.mime_type);
      break;

    case MCP_CONTENT_EMBEDDED:
      copy->embedded.resource.uri = alloc_string(block->embedded.resource.uri);
      copy->embedded.resource.name =
          alloc_string(block->embedded.resource.name);
      copy->embedded.resource.description =
          alloc_string(block->embedded.resource.description);
      copy->embedded.resource.mime_type =
          alloc_string(block->embedded.resource.mime_type);

      if (block->embedded.content_count > 0 && block->embedded.content) {
        copy->embedded.content = static_cast<mcp_content_block_t*>(std::calloc(
            block->embedded.content_count, sizeof(mcp_content_block_t)));
        if (copy->embedded.content) {
          copy->embedded.content_count = block->embedded.content_count;
          for (size_t i = 0; i < block->embedded.content_count; ++i) {
            mcp_content_block_t* nested =
                mcp_content_block_copy(&block->embedded.content[i]);
            if (nested) {
              copy->embedded.content[i] = *nested;
              std::free(nested);
            }
          }
        }
      }
      break;
  }

  return copy;
}

extern "C" mcp_tool_t* mcp_tool_copy(const mcp_tool_t* tool) {
  if (!tool)
    return nullptr;

  auto* copy = static_cast<mcp_tool_t*>(std::calloc(1, sizeof(mcp_tool_t)));
  if (!copy)
    return nullptr;

  copy->name = alloc_string(tool->name);
  copy->description = alloc_string(tool->description);
  copy->input_schema = tool->input_schema;  // Shallow copy of JSON handle

  return copy;
}

extern "C" mcp_prompt_t* mcp_prompt_copy(const mcp_prompt_t* prompt) {
  if (!prompt)
    return nullptr;

  auto* copy = static_cast<mcp_prompt_t*>(std::calloc(1, sizeof(mcp_prompt_t)));
  if (!copy)
    return nullptr;

  copy->name = alloc_string(prompt->name);
  copy->description = alloc_string(prompt->description);

  if (prompt->argument_count > 0 && prompt->arguments) {
    copy->arguments = static_cast<mcp_prompt_argument_t*>(
        std::calloc(prompt->argument_count, sizeof(mcp_prompt_argument_t)));
    if (copy->arguments) {
      copy->argument_count = prompt->argument_count;
      for (size_t i = 0; i < prompt->argument_count; ++i) {
        copy->arguments[i].name = alloc_string(prompt->arguments[i].name);
        copy->arguments[i].description =
            alloc_string(prompt->arguments[i].description);
        copy->arguments[i].required = prompt->arguments[i].required;
      }
    }
  }

  return copy;
}

// Validation functions
extern "C" bool mcp_content_block_is_valid(const mcp_content_block_t* block) {
  if (!block)
    return false;

  switch (block->type) {
    case MCP_CONTENT_TEXT:
      return block->text.text != nullptr;

    case MCP_CONTENT_IMAGE:
      return block->image.data != nullptr && block->image.mime_type != nullptr;

    case MCP_CONTENT_AUDIO:
      return block->audio.data != nullptr && block->audio.mime_type != nullptr;

    case MCP_CONTENT_RESOURCE:
      return block->resource.resource.uri != nullptr &&
             block->resource.resource.name != nullptr;

    case MCP_CONTENT_EMBEDDED:
      return block->embedded.resource.uri != nullptr &&
             block->embedded.resource.name != nullptr;

    default:
      return false;
  }
}

extern "C" bool mcp_tool_is_valid(const mcp_tool_t* tool) {
  return tool && tool->name;
}

extern "C" bool mcp_prompt_is_valid(const mcp_prompt_t* prompt) {
  return prompt && prompt->name;
}

extern "C" bool mcp_resource_is_valid(const mcp_resource_t* resource) {
  return resource && resource->uri && resource->name;
}

extern "C" bool mcp_message_is_valid(const mcp_message_t* message) {
  return message && message->content &&
         mcp_content_block_is_valid(message->content);
}

// Type checking functions
extern "C" bool mcp_content_is_text(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_TEXT;
}

extern "C" bool mcp_content_is_image(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_IMAGE;
}

extern "C" bool mcp_content_is_audio(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_AUDIO;
}

extern "C" bool mcp_content_is_resource(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_RESOURCE;
}

extern "C" bool mcp_content_is_embedded(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_EMBEDDED;
}

// Request ID helpers
extern "C" mcp_request_id_t mcp_request_id_string(const char* id) {
  mcp_request_id_t result;
  result.type = MCP_REQUEST_ID_STRING;
  result.string_value = alloc_string(id);
  return result;
}

extern "C" mcp_request_id_t mcp_request_id_int(int id) {
  mcp_request_id_t result;
  result.type = MCP_REQUEST_ID_INT;
  result.int_value = id;
  return result;
}

extern "C" void mcp_request_id_free(mcp_request_id_t* id) {
  if (!id)
    return;

  if (id->type == MCP_REQUEST_ID_STRING) {
    std::free(id->string_value);
    id->string_value = nullptr;
  }
}

extern "C" bool mcp_request_id_equals(const mcp_request_id_t* a,
                                      const mcp_request_id_t* b) {
  if (!a || !b)
    return false;
  if (a->type != b->type)
    return false;

  if (a->type == MCP_REQUEST_ID_STRING) {
    if (!a->string_value || !b->string_value) {
      return a->string_value == b->string_value;
    }
    return std::strcmp(a->string_value, b->string_value) == 0;
  } else {
    return a->int_value == b->int_value;
  }
}

// Progress token helpers
extern "C" mcp_progress_token_t mcp_progress_token_string(const char* token) {
  mcp_progress_token_t result;
  result.type = MCP_PROGRESS_TOKEN_STRING;
  result.string_value = alloc_string(token);
  return result;
}

extern "C" mcp_progress_token_t mcp_progress_token_int(int token) {
  mcp_progress_token_t result;
  result.type = MCP_PROGRESS_TOKEN_INT;
  result.int_value = token;
  return result;
}

extern "C" void mcp_progress_token_free(mcp_progress_token_t* token) {
  if (!token)
    return;

  if (token->type == MCP_PROGRESS_TOKEN_STRING) {
    std::free(token->string_value);
    token->string_value = nullptr;
  }
}

// Array helpers
extern "C" mcp_content_block_array_t* mcp_content_block_array_create(
    size_t capacity) {
  auto* array = static_cast<mcp_content_block_array_t*>(
      std::calloc(1, sizeof(mcp_content_block_array_t)));
  if (!array)
    return nullptr;

  if (capacity > 0) {
    array->items = static_cast<mcp_content_block_t**>(
        std::calloc(capacity, sizeof(mcp_content_block_t*)));
    if (!array->items) {
      std::free(array);
      return nullptr;
    }
    array->capacity = capacity;
  }

  return array;
}

extern "C" void mcp_content_block_array_free(mcp_content_block_array_t* array) {
  if (!array)
    return;

  if (array->items) {
    for (size_t i = 0; i < array->count; ++i) {
      mcp_content_block_free(array->items[i]);
    }
    std::free(array->items);
  }
  std::free(array);
}

extern "C" bool mcp_content_block_array_append(mcp_content_block_array_t* array,
                                               mcp_content_block_t* block) {
  if (!array || !block)
    return false;

  if (array->count >= array->capacity) {
    size_t new_capacity = array->capacity ? array->capacity * 2 : 4;
    auto* new_items = static_cast<mcp_content_block_t**>(std::realloc(
        array->items, new_capacity * sizeof(mcp_content_block_t*)));
    if (!new_items)
      return false;

    array->items = new_items;
    array->capacity = new_capacity;
  }

  array->items[array->count++] = block;
  return true;
}

// Memory pool functions
extern "C" mcp_memory_pool_t mcp_memory_pool_create() {
  return reinterpret_cast<mcp_memory_pool_t>(new mcp_memory_pool());
}

extern "C" void mcp_memory_pool_destroy(mcp_memory_pool_t pool) {
  if (pool) {
    auto* p = reinterpret_cast<mcp_memory_pool*>(pool);
    delete p;
  }
}

extern "C" void* mcp_memory_pool_alloc(mcp_memory_pool_t pool, size_t size) {
  if (!pool)
    return nullptr;

  auto* p = reinterpret_cast<mcp_memory_pool*>(pool);
  return p->allocate(size);
}

extern "C" void mcp_memory_pool_free(mcp_memory_pool_t pool,
                                     void* ptr,
                                     size_t size) {
  if (!pool || !ptr)
    return;

  auto* p = reinterpret_cast<mcp_memory_pool*>(pool);
  p->deallocate(ptr, size);
}

extern "C" size_t mcp_memory_pool_get_size(mcp_memory_pool_t pool) {
  if (!pool)
    return 0;

  auto* p = reinterpret_cast<mcp_memory_pool*>(pool);
  return p->total_size;
}

extern "C" size_t mcp_memory_pool_get_peak_size(mcp_memory_pool_t pool) {
  if (!pool)
    return 0;

  auto* p = reinterpret_cast<mcp_memory_pool*>(pool);
  return p->peak_size;
}

// String utilities
extern "C" char* mcp_string_duplicate(const char* str) {
  return alloc_string(str);
}

extern "C" void mcp_string_free(char* str) { std::free(str); }

extern "C" bool mcp_string_equals(const char* a, const char* b) {
  if (!a || !b)
    return a == b;
  return std::strcmp(a, b) == 0;
}

extern "C" bool mcp_string_starts_with(const char* str, const char* prefix) {
  if (!str || !prefix)
    return false;
  return std::strncmp(str, prefix, std::strlen(prefix)) == 0;
}

extern "C" bool mcp_string_ends_with(const char* str, const char* suffix) {
  if (!str || !suffix)
    return false;

  size_t str_len = std::strlen(str);
  size_t suffix_len = std::strlen(suffix);

  if (suffix_len > str_len)
    return false;

  return std::strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Logging level utilities
extern "C" const char* mcp_logging_level_to_string(mcp_logging_level_t level) {
  switch (level) {
    case MCP_LOGGING_DEBUG:
      return "debug";
    case MCP_LOGGING_INFO:
      return "info";
    case MCP_LOGGING_NOTICE:
      return "notice";
    case MCP_LOGGING_WARNING:
      return "warning";
    case MCP_LOGGING_ERROR:
      return "error";
    case MCP_LOGGING_CRITICAL:
      return "critical";
    case MCP_LOGGING_ALERT:
      return "alert";
    case MCP_LOGGING_EMERGENCY:
      return "emergency";
    default:
      return "unknown";
  }
}

extern "C" mcp_logging_level_t mcp_logging_level_from_string(const char* str) {
  if (!str)
    return MCP_LOGGING_ERROR;

  if (std::strcmp(str, "debug") == 0)
    return MCP_LOGGING_DEBUG;
  if (std::strcmp(str, "info") == 0)
    return MCP_LOGGING_INFO;
  if (std::strcmp(str, "notice") == 0)
    return MCP_LOGGING_NOTICE;
  if (std::strcmp(str, "warning") == 0)
    return MCP_LOGGING_WARNING;
  if (std::strcmp(str, "error") == 0)
    return MCP_LOGGING_ERROR;
  if (std::strcmp(str, "critical") == 0)
    return MCP_LOGGING_CRITICAL;
  if (std::strcmp(str, "alert") == 0)
    return MCP_LOGGING_ALERT;
  if (std::strcmp(str, "emergency") == 0)
    return MCP_LOGGING_EMERGENCY;

  return MCP_LOGGING_ERROR;
}

// Schema array functions
extern "C" void mcp_string_schema_free(mcp_string_schema_t* schema) {
  if (!schema)
    return;

  std::free(schema->description);
  std::free(schema->pattern);
  std::free(schema);
}

extern "C" void mcp_number_schema_free(mcp_number_schema_t* schema) {
  if (!schema)
    return;

  std::free(schema->description);
  std::free(schema);
}

extern "C" void mcp_boolean_schema_free(mcp_boolean_schema_t* schema) {
  if (!schema)
    return;

  std::free(schema->description);
  std::free(schema);
}

extern "C" void mcp_enum_schema_free(mcp_enum_schema_t* schema) {
  if (!schema)
    return;

  std::free(schema->description);
  if (schema->values) {
    for (size_t i = 0; i < schema->value_count; ++i) {
      std::free(schema->values[i]);
    }
    std::free(schema->values);
  }
  std::free(schema);
}

// Request/Response utilities
extern "C" void mcp_request_free(mcp_request_t* request) {
  if (!request)
    return;

  std::free(request->jsonrpc);
  std::free(request->method);
  mcp_request_id_free(&request->id);
  // Note: params is a JSON handle that should be freed separately
  std::free(request);
}

extern "C" void mcp_response_free(mcp_response_t* response) {
  if (!response)
    return;

  std::free(response->jsonrpc);
  mcp_request_id_free(&response->id);
  mcp_error_free(response->error);
  // Note: result is a JSON handle that should be freed separately
  std::free(response);
}

extern "C" void mcp_notification_free(mcp_notification_t* notification) {
  if (!notification)
    return;

  std::free(notification->jsonrpc);
  std::free(notification->method);
  // Note: params is a JSON handle that should be freed separately
  std::free(notification);
}

// Implementation info utilities
extern "C" void mcp_implementation_free(mcp_implementation_t* impl) {
  if (!impl)
    return;

  std::free(impl->name);
  std::free(impl->version);
  std::free(impl);
}

// Root utilities
extern "C" void mcp_root_free(mcp_root_t* root) {
  if (!root)
    return;

  std::free(root->uri);
  std::free(root->name);
  std::free(root);
}

// Model preferences utilities
extern "C" void mcp_model_hint_free(mcp_model_hint_t* hint) {
  if (!hint)
    return;

  std::free(hint->name);
  std::free(hint);
}

extern "C" void mcp_model_preferences_free(mcp_model_preferences_t* prefs) {
  if (!prefs)
    return;

  if (prefs->hints) {
    for (size_t i = 0; i < prefs->hint_count; ++i) {
      mcp_model_hint_free(&prefs->hints[i]);
    }
    std::free(prefs->hints);
  }
  std::free(prefs);
}

}  // namespace c_api
}  // namespace mcp

// Extern C functions for content block utilities
extern "C" {

bool mcp_content_block_is_text(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_TEXT;
}

bool mcp_content_block_is_image(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_IMAGE;
}

bool mcp_content_block_is_audio(const mcp_content_block_t* block) {
  return block && block->type == MCP_CONTENT_AUDIO;
}

bool mcp_content_block_is_resource(const mcp_content_block_t* block) {
  return block && (block->type == MCP_CONTENT_RESOURCE ||
                   block->type == MCP_CONTENT_RESOURCE_LINK);
}

void mcp_string_free(mcp_string_t* str) {
  if (str && str->data) {
    free(const_cast<char*>(str->data));
    str->data = nullptr;
    str->length = 0;
  }
}

/* ============================================================================
 * FFI Safety Implementation
 * ============================================================================
 */

// Thread-local error storage
static thread_local char g_last_error[1024] = {0};

const char* mcp_get_last_error(void) {
  return g_last_error[0] ? g_last_error : nullptr;
}

void mcp_set_last_error(const char* error) {
  if (error) {
    std::strncpy(g_last_error, error, sizeof(g_last_error) - 1);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
  } else {
    g_last_error[0] = '\0';
  }
}

void mcp_clear_last_error(void) {
  g_last_error[0] = '\0';
}

// ABI version management
mcp_abi_version_t mcp_get_abi_version(void) {
  mcp_abi_version_t version = {
    MCP_C_API_VERSION_MAJOR,
    MCP_C_API_VERSION_MINOR,
    MCP_C_API_VERSION_PATCH,
    0 /* reserved */
  };
  return version;
}

mcp_bool_t mcp_check_abi_compatibility(uint32_t major, uint32_t minor) {
  // Major version must match exactly
  if (major != MCP_C_API_VERSION_MAJOR) {
    return MCP_FALSE;
  }
  
  // Minor version must be less than or equal to current
  if (minor > MCP_C_API_VERSION_MINOR) {
    return MCP_FALSE;
  }
  
  return MCP_TRUE;
}

// Safe memory functions
void* mcp_malloc_safe(size_t size) {
  if (size == 0) {
    mcp_set_last_error("Cannot allocate 0 bytes");
    return nullptr;
  }
  
  void* ptr = std::malloc(size);
  if (!ptr) {
    char error[256];
    std::snprintf(error, sizeof(error), "Failed to allocate %zu bytes", size);
    mcp_set_last_error(error);
  }
  return ptr;
}

void* mcp_calloc_safe(size_t count, size_t size) {
  if (count == 0 || size == 0) {
    mcp_set_last_error("Cannot allocate 0 bytes");
    return nullptr;
  }
  
  // Check for overflow
  if (count > SIZE_MAX / size) {
    mcp_set_last_error("Allocation size overflow");
    return nullptr;
  }
  
  void* ptr = std::calloc(count, size);
  if (!ptr) {
    char error[256];
    std::snprintf(error, sizeof(error), "Failed to allocate %zu x %zu bytes", count, size);
    mcp_set_last_error(error);
  }
  return ptr;
}

void* mcp_realloc_safe(void* ptr, size_t new_size) {
  if (new_size == 0) {
    std::free(ptr);
    return nullptr;
  }
  
  void* new_ptr = std::realloc(ptr, new_size);
  if (!new_ptr && new_size > 0) {
    char error[256];
    std::snprintf(error, sizeof(error), "Failed to reallocate to %zu bytes", new_size);
    mcp_set_last_error(error);
  }
  return new_ptr;
}

void mcp_free_safe(void* ptr) {
  std::free(ptr);
}

char* mcp_strdup_safe(const char* str) {
  if (!str) {
    return nullptr;
  }
  
  size_t len = std::strlen(str);
  char* copy = static_cast<char*>(mcp_malloc_safe(len + 1));
  if (copy) {
    std::memcpy(copy, str, len + 1);
  }
  return copy;
}

char* mcp_strndup_safe(const char* str, size_t max_len) {
  if (!str) {
    return nullptr;
  }
  
  size_t len = std::strnlen(str, max_len);
  char* copy = static_cast<char*>(mcp_malloc_safe(len + 1));
  if (copy) {
    std::memcpy(copy, str, len);
    copy[len] = '\0';
  }
  return copy;
}

// FFI initialization
static std::atomic<bool> g_ffi_initialized{false};

mcp_result_t mcp_ffi_initialize(void) {
  bool expected = false;
  if (!g_ffi_initialized.compare_exchange_strong(expected, true)) {
    // Already initialized
    return MCP_OK;
  }
  
  // Perform any one-time initialization here
  mcp_clear_last_error();
  
  return MCP_OK;
}

void mcp_ffi_cleanup(void) {
  bool expected = true;
  if (!g_ffi_initialized.compare_exchange_strong(expected, false)) {
    // Not initialized or already cleaned up
    return;
  }
  
  // Perform cleanup here
  mcp_clear_last_error();
}

mcp_bool_t mcp_ffi_is_initialized(void) {
  return g_ffi_initialized.load() ? MCP_TRUE : MCP_FALSE;
}

}  // extern "C"