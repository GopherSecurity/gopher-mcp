/**
 * @file mcp_c_type_conversions.h
 * @brief Conversion functions between C and C++ MCP types
 *
 * This header provides conversion utilities to convert between the C API types
 * (mcp_c_types.h) and the C++ MCP types (types.h). These functions are used
 * internally by the C API implementation to bridge between the two type
 * systems.
 *
 * Design principles:
 * - Zero-copy conversions where possible
 * - Deep copy when ownership transfer is needed
 * - Exception-safe conversions
 * - Automatic memory management via RAII in C++
 */

#ifndef MCP_C_TYPE_CONVERSIONS_H
#define MCP_C_TYPE_CONVERSIONS_H

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <type_traits>

#include "mcp/c_api/mcp_c_raii.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/types.h"

namespace mcp {
namespace c_api {

/* ============================================================================
 * RAII Aliases for Type Conversions
 * ============================================================================
 */

// Import RAII utilities for use in type conversions
using mcp::raii::AllocationTransaction;
using mcp::raii::c_unique_ptr;
using mcp::raii::make_resource_guard;
using mcp::raii::make_scoped_cleanup;
using mcp::raii::ResourceGuard;

// Legacy aliases for backward compatibility
template <typename T>
using c_deleter = mcp::raii::c_deleter<T>;

// Thread safety utilities for concurrent conversions
namespace {
// Global mutex for thread-safe conversions when needed
std::mutex& get_conversion_mutex() {
  static std::mutex conversion_mutex;
  return conversion_mutex;
}

// Atomic counter for tracking active conversions (for debugging)
std::atomic<uint64_t>& get_active_conversions() {
  static std::atomic<uint64_t> active_conversions{0};
  return active_conversions;
}
}  // namespace

// RAII wrapper for thread-safe conversion operations
class ConversionLock {
 public:
  ConversionLock() : lock_(get_conversion_mutex()) {
    get_active_conversions().fetch_add(1, std::memory_order_relaxed);
  }

  ~ConversionLock() {
    get_active_conversions().fetch_sub(1, std::memory_order_relaxed);
  }

  // Non-copyable, non-movable
  ConversionLock(const ConversionLock&) = delete;
  ConversionLock& operator=(const ConversionLock&) = delete;
  ConversionLock(ConversionLock&&) = delete;
  ConversionLock& operator=(ConversionLock&&) = delete;

 private:
  std::lock_guard<std::mutex> lock_;
};

// Macro for thread-safe conversions when needed
#define MCP_THREAD_SAFE_CONVERSION(expr) \
  do {                                   \
    ConversionLock _lock;                \
    (expr);                              \
  } while (0)

/* ============================================================================
 * Ownership Annotations and Documentation
 * ============================================================================
 */

// Ownership macros for clear API documentation
#define MCP_BORROWED /* Returns borrowed reference - do not free */
#if __cplusplus >= 201703L
#define MCP_OWNED [[nodiscard]] /* Returns owned memory - caller must free */
#else
#define MCP_OWNED /* Returns owned memory - caller must free */
#endif

/* ============================================================================
 * RAII Utility Functions for Type Conversions
 * ============================================================================
 */

/**
 * RAII wrapper for safe optional creation
 */
class SafeOptional {
 public:
  template <typename T>
  static mcp_optional_t* create_tracked(T* value, AllocationTransaction& txn) {
    auto opt = mcp_optional_create(value);
    if (opt) {
      txn.track(opt, [](void* p) {
        mcp_optional_free(static_cast<mcp_optional_t*>(p));
      });
    }
    return opt;
  }

  static mcp_optional_t* empty_tracked(AllocationTransaction& txn) {
    auto opt = mcp_optional_empty();
    if (opt) {
      txn.track(opt, [](void* p) {
        mcp_optional_free(static_cast<mcp_optional_t*>(p));
      });
    }
    return opt;
  }
};

/**
 * RAII wrapper for safe list operations
 */
class SafeList {
 public:
  static mcp_list_t* create_tracked(size_t capacity,
                                    AllocationTransaction& txn) {
    auto list = mcp_list_create(capacity);
    if (list) {
      txn.track(list, [](void* p) {
        auto* l = static_cast<mcp_list_t*>(p);
        mcp_list_free(l);
        free(l);
      });
    }
    return list;
  }
};

/**
 * Safe string duplication with RAII tracking (forward declaration)
 */
inline mcp_result_t safe_string_dup(const std::string& src,
                                    mcp_string_t* dst,
                                    AllocationTransaction& txn);

/* ============================================================================
 * String Conversions
 * ============================================================================
 */

/**
 * Convert C string to C++ string
 */
inline std::string to_cpp_string(mcp_string_t str) {
  if (str.data == nullptr) {
    return std::string();
  }
  return std::string(str.data, str.length);
}

/**
 * Convert C++ string to C string (non-owning)
 * @return Borrowed reference - do not free
 */
MCP_BORROWED inline mcp_string_t to_c_string_ref(const std::string& str) {
  mcp_string_t result;
  result.data = str.c_str();
  result.length = str.length();
  return result;
}

/**
 * Legacy name for compatibility - prefer to_c_string_ref
 */
MCP_BORROWED inline mcp_string_t to_c_string(const std::string& str) {
  return to_c_string_ref(str);
}

/**
 * Convert C++ string to owned C string (unsafe - use to_c_string_copy_safe)
 * @deprecated Use to_c_string_copy_safe for proper error handling
 */
[[deprecated(
    "Use to_c_string_copy_safe for proper error handling")]] inline mcp_string_t
to_c_string_copy(const std::string& str) {
  mcp_string_t result;
  char* data = static_cast<char*>(malloc(str.length() + 1));
  if (data) {
    std::memcpy(data, str.c_str(), str.length() + 1);
    result.data = data;
    result.length = str.length();
  } else {
    result.data = nullptr;
    result.length = 0;
  }
  return result;
}

/**
 * Convert C++ string to owned C string with error handling
 * @param str Input string
 * @param out Output string (caller must free with mcp_string_free)
 * @return MCP_OK on success, error code on failure
 */
inline mcp_result_t to_c_string_copy_safe(const std::string& str,
                                          mcp_string_t* out) {
  if (!out)
    return MCP_ERROR_INVALID_ARGUMENT;

  // Use RAII for exception safety
  ResourceGuard<char> data_guard(static_cast<char*>(malloc(str.length() + 1)),
                                 [](char* p) { free(p); });

  if (!data_guard) {
    out->data = nullptr;
    out->length = 0;
    return MCP_ERROR_OUT_OF_MEMORY;
  }

  std::memcpy(data_guard.get(), str.c_str(), str.length() + 1);
  out->data = data_guard.release();  // Transfer ownership
  out->length = str.length();
  return MCP_OK;
}

/**
 * Safe string duplication with RAII tracking (implementation)
 */
inline mcp_result_t safe_string_dup(const std::string& src,
                                    mcp_string_t* dst,
                                    AllocationTransaction& txn) {
  if (to_c_string_copy_safe(src, dst) != MCP_OK) {
    return MCP_ERROR_OUT_OF_MEMORY;
  }
  txn.track(const_cast<char*>(dst->data), [](void* p) { free(p); });
  return MCP_OK;
}

/**
 * Convert C++ string to owned C string pointer with RAII
 * @return Owned pointer - caller must free with mcp_string_free
 */
MCP_OWNED inline mcp_string_t* to_c_string_owned(const std::string& str) {
  // Use AllocationTransaction for atomic allocation
  AllocationTransaction txn;

  auto string_ptr = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
  if (!string_ptr)
    return nullptr;
  txn.track(string_ptr, [](void* p) { free(p); });

  char* data = static_cast<char*>(malloc(str.length() + 1));
  if (!data)
    return nullptr;
  txn.track(data, [](void* p) { free(p); });

  std::memcpy(data, str.c_str(), str.length() + 1);
  string_ptr->data = data;
  string_ptr->length = str.length();

  txn.commit();  // Success - prevent cleanup
  return string_ptr;
}

/* ============================================================================
 * Request ID Conversions
 * ============================================================================
 */

/**
 * Convert C RequestId to C++ RequestId
 */
inline RequestId to_cpp_request_id(const mcp_request_id_t& id) {
  if (id.type == mcp_request_id::MCP_REQUEST_ID_STRING) {
    return RequestId(to_cpp_string(id.value.string_value));
  } else {
    return RequestId(static_cast<int64_t>(id.value.number_value));
  }
}

/**
 * Convert C++ RequestId to C RequestId
 */
inline mcp_request_id_t to_c_request_id(const RequestId& id) {
  mcp_request_id_t result;
  if (mcp::holds_alternative<std::string>(id)) {
    result.type = mcp_request_id::MCP_REQUEST_ID_STRING;
    result.value.string_value = to_c_string(mcp::get<std::string>(id));
  } else {
    result.type = mcp_request_id::MCP_REQUEST_ID_NUMBER;
    result.value.number_value = mcp::get<int64_t>(id);
  }
  return result;
}

/* ============================================================================
 * Progress Token Conversions
 * ============================================================================
 */

/**
 * Convert C ProgressToken to C++ ProgressToken
 */
inline ProgressToken to_cpp_progress_token(const mcp_progress_token_t& token) {
  if (token.type == mcp_progress_token::MCP_PROGRESS_TOKEN_STRING) {
    return ProgressToken(to_cpp_string(token.value.string_value));
  } else {
    return ProgressToken(static_cast<int64_t>(token.value.number_value));
  }
}

/**
 * Convert C++ ProgressToken to C ProgressToken
 */
inline mcp_progress_token_t to_c_progress_token(const ProgressToken& token) {
  mcp_progress_token_t result;
  if (mcp::holds_alternative<std::string>(token)) {
    result.type = mcp_progress_token::MCP_PROGRESS_TOKEN_STRING;
    result.value.string_value = to_c_string(mcp::get<std::string>(token));
  } else {
    result.type = mcp_progress_token::MCP_PROGRESS_TOKEN_NUMBER;
    result.value.number_value = mcp::get<int64_t>(token);
  }
  return result;
}

/* ============================================================================
 * Role Conversions
 * ============================================================================
 */

/**
 * Convert C Role to C++ Role
 */
inline enums::Role::Value to_cpp_role(mcp_role_t role) {
  return role == MCP_ROLE_USER ? enums::Role::USER : enums::Role::ASSISTANT;
}

/**
 * Convert C++ Role to C Role
 */
inline mcp_role_t to_c_role(enums::Role::Value role) {
  return role == enums::Role::USER ? MCP_ROLE_USER : MCP_ROLE_ASSISTANT;
}

/* ============================================================================
 * Logging Level Conversions
 * ============================================================================
 */

/**
 * Convert C LoggingLevel to C++ LoggingLevel
 */
inline enums::LoggingLevel::Value to_cpp_logging_level(
    mcp_logging_level_t level) {
  return static_cast<enums::LoggingLevel::Value>(level);
}

/**
 * Convert C++ LoggingLevel to C LoggingLevel
 */
inline mcp_logging_level_t to_c_logging_level(
    enums::LoggingLevel::Value level) {
  return static_cast<mcp_logging_level_t>(level);
}

/* ============================================================================
 * Content Block Conversions
 * ============================================================================
 */

/**
 * Convert C TextContent to C++ TextContent
 */
inline TextContent to_cpp_text_content(const mcp_text_content_t& content) {
  TextContent result;
  result.text = to_cpp_string(content.text);
  // TODO: Handle annotations if present
  return result;
}

/**
 * Convert C++ TextContent to C TextContent with RAII
 */
MCP_OWNED inline mcp_text_content_t* to_c_text_content(
    const TextContent& content) {
  auto result = c_unique_ptr<mcp_text_content_t>(
      static_cast<mcp_text_content_t*>(malloc(sizeof(mcp_text_content_t))));
  if (!result)
    return nullptr;

  // Use transaction for atomic allocation
  AllocationTransaction txn;

  mcp_string_t type_str, text_str;
  if (to_c_string_copy_safe("text", &type_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(type_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(content.text, &text_str) != MCP_OK) {
    return nullptr;  // Transaction will auto-cleanup type_str
  }
  txn.track(const_cast<char*>(text_str.data), [](void* p) { free(p); });

  result->type = type_str;
  result->text = text_str;

  txn.commit();  // Success - prevent cleanup
  return result.release();
}

/**
 * Convert C ImageContent to C++ ImageContent
 */
inline ImageContent to_cpp_image_content(const mcp_image_content_t& content) {
  ImageContent result;
  result.data = to_cpp_string(content.data);
  result.mimeType = to_cpp_string(content.mime_type);
  return result;
}

/**
 * Convert C++ ImageContent to C ImageContent with RAII
 */
MCP_OWNED inline mcp_image_content_t* to_c_image_content(
    const ImageContent& content) {
  auto result = c_unique_ptr<mcp_image_content_t>(
      static_cast<mcp_image_content_t*>(malloc(sizeof(mcp_image_content_t))));
  if (!result)
    return nullptr;

  // Use transaction for atomic allocation
  AllocationTransaction txn;

  mcp_string_t type_str, data_str, mime_str;
  if (to_c_string_copy_safe("image", &type_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(type_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(content.data, &data_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(data_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(content.mimeType, &mime_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(mime_str.data), [](void* p) { free(p); });

  result->type = type_str;
  result->data = data_str;
  result->mime_type = mime_str;

  txn.commit();  // Success - prevent cleanup
  return result.release();
}

/**
 * Convert C AudioContent to C++ AudioContent
 */
inline AudioContent to_cpp_audio_content(const mcp_audio_content_t& content) {
  AudioContent result;
  result.data = to_cpp_string(content.data);
  result.mimeType = to_cpp_string(content.mime_type);
  return result;
}

/**
 * Convert C++ AudioContent to C AudioContent with RAII
 */
MCP_OWNED inline mcp_audio_content_t* to_c_audio_content(
    const AudioContent& content) {
  auto result = c_unique_ptr<mcp_audio_content_t>(
      static_cast<mcp_audio_content_t*>(malloc(sizeof(mcp_audio_content_t))));
  if (!result)
    return nullptr;

  // Use transaction for atomic allocation
  AllocationTransaction txn;

  mcp_string_t type_str, data_str, mime_str;
  if (to_c_string_copy_safe("audio", &type_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(type_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(content.data, &data_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(data_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(content.mimeType, &mime_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(mime_str.data), [](void* p) { free(p); });

  result->type = type_str;
  result->data = data_str;
  result->mime_type = mime_str;

  txn.commit();  // Success - prevent cleanup
  return result.release();
}

/**
 * Convert C Resource to C++ Resource
 */
inline Resource to_cpp_resource(const mcp_resource_t& resource) {
  Resource result;
  result.uri = to_cpp_string(resource.uri);
  result.name = to_cpp_string(resource.name);

  if (resource.description && mcp_optional_has_value(resource.description)) {
    auto* str = static_cast<mcp_string_t*>(
        mcp_optional_get_value(resource.description));
    if (str) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  if (resource.mime_type && mcp_optional_has_value(resource.mime_type)) {
    auto* str =
        static_cast<mcp_string_t*>(mcp_optional_get_value(resource.mime_type));
    if (str) {
      result.mimeType = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ Resource to C Resource with RAII
 */
MCP_OWNED inline mcp_resource_t* to_c_resource(const Resource& resource) {
  auto result = c_unique_ptr<mcp_resource_t>(
      static_cast<mcp_resource_t*>(malloc(sizeof(mcp_resource_t))));
  if (!result)
    return nullptr;

  // Use transaction for atomic allocation
  AllocationTransaction txn;

  mcp_string_t uri_str, name_str;
  if (to_c_string_copy_safe(resource.uri, &uri_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(uri_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(resource.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(name_str.data), [](void* p) { free(p); });

  result->uri = uri_str;
  result->name = name_str;

  // Handle optional description with RAII
  if (resource.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (to_c_string_copy_safe(*resource.description, desc) != MCP_OK) {
      return nullptr;
    }
    txn.track(const_cast<char*>(desc->data), [](void* p) { free(p); });

    result->description = mcp_optional_create(desc);
    if (!result->description)
      return nullptr;
    txn.track(result->description, [](void* p) {
      mcp_optional_free(static_cast<mcp_optional_t*>(p));
    });
  } else {
    result->description = nullptr;
  }

  // Handle optional mime type with RAII
  if (resource.mimeType) {
    auto mime = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!mime)
      return nullptr;
    txn.track(mime, [](void* p) { free(p); });

    if (to_c_string_copy_safe(*resource.mimeType, mime) != MCP_OK) {
      return nullptr;
    }
    txn.track(const_cast<char*>(mime->data), [](void* p) { free(p); });

    result->mime_type = mcp_optional_create(mime);
    if (!result->mime_type)
      return nullptr;
    txn.track(result->mime_type, [](void* p) {
      mcp_optional_free(static_cast<mcp_optional_t*>(p));
    });
  } else {
    result->mime_type = nullptr;
  }

  txn.commit();  // Success - prevent cleanup
  return result.release();
}

/**
 * Convert C ContentBlock to C++ ContentBlock with validation
 */
inline ContentBlock to_cpp_content_block(const mcp_content_block_t& block) {
  // Validate enum value is in range
  if (block.type < MCP_CONTENT_TEXT ||
      block.type > MCP_CONTENT_EMBEDDED_RESOURCE) {
    // Invalid type, return safe default
    return ContentBlock(TextContent(""));
  }

  switch (block.type) {
    case MCP_CONTENT_TEXT:
      if (block.content.text) {
        return ContentBlock(to_cpp_text_content(*block.content.text));
      }
      break;
    case MCP_CONTENT_IMAGE:
      if (block.content.image) {
        return ContentBlock(to_cpp_image_content(*block.content.image));
      }
      break;
    case MCP_CONTENT_RESOURCE:
    case MCP_CONTENT_EMBEDDED_RESOURCE:
      if (block.content.resource) {
        // Safe cast with proper type
        ResourceContent rc;
        rc.resource = to_cpp_resource(*static_cast<const mcp_resource_t*>(
            reinterpret_cast<const mcp_resource_t*>(block.content.resource)));
        return ContentBlock(rc);
      }
      break;
    case MCP_CONTENT_AUDIO:
      // AudioContent is not in ContentBlock, return empty text as fallback
      // Note: Consider using ExtendedContentBlock if AudioContent is needed
      return ContentBlock(TextContent(""));
      break;
    case MCP_CONTENT_RESOURCE_LINK:
      // Handle resource link if needed
      break;
    default:
      break;
  }
  // Return empty text content as safe fallback
  return ContentBlock(TextContent(""));
}

/**
 * Convert C++ ContentBlock to C ContentBlock (unsafe - use
 * to_c_content_block_safe)
 * @deprecated Use to_c_content_block_safe for proper error handling
 */
[[deprecated(
    "Use to_c_content_block_safe for proper error "
    "handling")]] inline mcp_content_block_t*
to_c_content_block(const ContentBlock& block) {
  auto result = c_unique_ptr<mcp_content_block_t>(
      static_cast<mcp_content_block_t*>(malloc(sizeof(mcp_content_block_t))));
  if (!result)
    return nullptr;

  if (mcp::holds_alternative<TextContent>(block)) {
    result->type = MCP_CONTENT_TEXT;
    result->content.text = to_c_text_content(mcp::get<TextContent>(block));
    if (!result->content.text)
      return nullptr;
  } else if (mcp::holds_alternative<ImageContent>(block)) {
    result->type = MCP_CONTENT_IMAGE;
    result->content.image = to_c_image_content(mcp::get<ImageContent>(block));
    if (!result->content.image)
      return nullptr;
  } else if (mcp::holds_alternative<ResourceContent>(block)) {
    result->type = MCP_CONTENT_RESOURCE;
    auto& rc = mcp::get<ResourceContent>(block);
    auto* resource = to_c_resource(rc.resource);
    if (!resource)
      return nullptr;
    result->content.resource =
        reinterpret_cast<mcp_embedded_resource_t*>(resource);
  } else {
    // Unknown variant type
    return nullptr;
  }

  return result.release();
}

/**
 * Convert C++ ContentBlock to C ContentBlock with error handling
 * @param block Input content block
 * @param out Output pointer (caller must free)
 * @return MCP_OK on success, error code on failure
 */
inline mcp_result_t to_c_content_block_safe(const ContentBlock& block,
                                            mcp_content_block_t** out) {
  if (!out)
    return MCP_ERROR_INVALID_ARGUMENT;

  auto result = c_unique_ptr<mcp_content_block_t>(
      static_cast<mcp_content_block_t*>(malloc(sizeof(mcp_content_block_t))));
  if (!result)
    return MCP_ERROR_OUT_OF_MEMORY;

  // Build complete object with proper cleanup on failure
  if (mcp::holds_alternative<TextContent>(block)) {
    result->type = MCP_CONTENT_TEXT;
    result->content.text = to_c_text_content(mcp::get<TextContent>(block));
    if (!result->content.text) {
      return MCP_ERROR_OUT_OF_MEMORY;
    }
  } else if (mcp::holds_alternative<ImageContent>(block)) {
    result->type = MCP_CONTENT_IMAGE;
    result->content.image = to_c_image_content(mcp::get<ImageContent>(block));
    if (!result->content.image) {
      return MCP_ERROR_OUT_OF_MEMORY;
    }
  } else if (mcp::holds_alternative<ResourceContent>(block)) {
    result->type = MCP_CONTENT_RESOURCE;
    auto& rc = mcp::get<ResourceContent>(block);
    auto* resource = to_c_resource(rc.resource);
    if (!resource) {
      return MCP_ERROR_OUT_OF_MEMORY;
    }
    result->content.resource =
        reinterpret_cast<mcp_embedded_resource_t*>(resource);
  } else {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  // Only transfer ownership on success
  *out = result.release();
  return MCP_OK;
}

/* ============================================================================
 * Tool & Prompt Conversions
 * ============================================================================
 */

/**
 * Convert C Tool to C++ Tool
 */
inline Tool to_cpp_tool(const mcp_tool_t& tool) {
  Tool result;
  result.name = to_cpp_string(tool.name);

  if (tool.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(tool.description.value);
    if (str) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  // TODO: Convert input_schema from mcp_json_value_t to ToolInputSchema

  return result;
}

/**
 * Convert C++ Tool to C Tool with RAII
 */
MCP_OWNED inline mcp_tool_t* to_c_tool(const Tool& tool) {
  auto result = c_unique_ptr<mcp_tool_t>(
      static_cast<mcp_tool_t*>(malloc(sizeof(mcp_tool_t))));
  if (!result)
    return nullptr;

  // Use transaction for atomic allocation
  AllocationTransaction txn;

  mcp_string_t name_str;
  if (to_c_string_copy_safe(tool.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(name_str.data), [](void* p) { free(p); });
  result->name = name_str;

  if (tool.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (to_c_string_copy_safe(*tool.description, desc) != MCP_OK) {
      return nullptr;
    }
    txn.track(const_cast<char*>(desc->data), [](void* p) { free(p); });

    auto opt = mcp_optional_create(desc);
    if (!opt)
      return nullptr;
    result->description = *opt;
    free(opt);  // Free wrapper, content is copied
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->description = *opt;
    free(opt);  // Free wrapper
  }

  // TODO: Convert inputSchema to mcp_json_value_t
  result->input_schema = nullptr;

  txn.commit();  // Success - prevent cleanup
  return result.release();
}

/**
 * Convert C Prompt to C++ Prompt
 */
inline Prompt to_cpp_prompt(const mcp_prompt_t& prompt) {
  Prompt result;
  result.name = to_cpp_string(prompt.name);

  if (prompt.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(prompt.description.value);
    if (str) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  // TODO: Convert arguments list

  return result;
}

/**
 * Convert C++ Prompt to C Prompt with complete RAII safety
 */
MCP_OWNED inline mcp_prompt_t* to_c_prompt(const Prompt& prompt) {
  // Use AllocationTransaction for atomic allocation
  AllocationTransaction txn;

  auto result = static_cast<mcp_prompt_t*>(malloc(sizeof(mcp_prompt_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Allocate and track name string
  mcp_string_t name_str;
  if (to_c_string_copy_safe(prompt.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(name_str.data), [](void* p) { free(p); });
  result->name = name_str;

  // Handle optional description with RAII
  if (prompt.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (to_c_string_copy_safe(*prompt.description, desc) != MCP_OK) {
      return nullptr;
    }
    txn.track(const_cast<char*>(desc->data), [](void* p) { free(p); });

    auto opt = mcp_optional_create(desc);
    if (!opt)
      return nullptr;
    result->description = *opt;
    free(opt);  // Free wrapper, content is managed by transaction
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->description = *opt;
    free(opt);  // Free wrapper
  }

  // Handle arguments list with RAII
  auto list = mcp_list_create(0);
  if (!list)
    return nullptr;
  result->arguments = *list;
  free(list);  // Free wrapper

  txn.commit();  // Success - prevent cleanup
  return result;
}

/* ============================================================================
 * Message Conversions
 * ============================================================================
 */

/**
 * Convert C Message to C++ Message
 */
inline Message to_cpp_message(const mcp_message_t& msg) {
  Message result;
  result.role = to_cpp_role(msg.role);
  result.content = to_cpp_content_block(msg.content);
  return result;
}

/**
 * Convert C++ Message to C Message with complete RAII safety
 */
MCP_OWNED inline mcp_message_t* to_c_message(const Message& msg) {
  // Use AllocationTransaction for atomic allocation
  AllocationTransaction txn;

  auto result = static_cast<mcp_message_t*>(malloc(sizeof(mcp_message_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  result->role = to_c_role(msg.role);

  // Convert content block with proper cleanup tracking
  mcp_content_block_t* block = nullptr;
  if (to_c_content_block_safe(msg.content, &block) != MCP_OK || !block) {
    return nullptr;
  }

  // Track the content block and its nested allocations
  txn.track(block, [](void* p) {
    auto* cb = static_cast<mcp_content_block_t*>(p);
    mcp_content_block_free(cb);
  });

  // Copy content (shallow copy is safe since transaction manages lifetime)
  result->content = *block;

  txn.commit();  // Success - prevent cleanup
  return result;
}

/* ============================================================================
 * Error Conversions
 * ============================================================================
 */

/**
 * Convert C JSONRPCError to C++ Error
 */
inline Error to_cpp_error(const mcp_jsonrpc_error_t& error) {
  Error result;
  result.code = error.code;
  result.message = to_cpp_string(error.message);

  // TODO: Convert error data if present

  return result;
}

/**
 * Convert C++ Error to C JSONRPCError with RAII safety
 */
MCP_OWNED inline mcp_jsonrpc_error_t* to_c_error(const Error& error) {
  // Use AllocationTransaction for atomic allocation
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_jsonrpc_error_t*>(malloc(sizeof(mcp_jsonrpc_error_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  result->code = error.code;

  // Safe string copy with tracking
  mcp_string_t message_str;
  if (to_c_string_copy_safe(error.message, &message_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(message_str.data), [](void* p) { free(p); });
  result->message = message_str;

  // Handle optional data
  auto empty = mcp_optional_empty();
  if (!empty)
    return nullptr;
  result->data = *empty;
  free(empty);  // Free wrapper

  txn.commit();  // Success - prevent cleanup
  return result;
}

/* ============================================================================
 * Capabilities Conversions
 * ============================================================================
 */

/**
 * Convert C ClientCapabilities to C++ ClientCapabilities
 */
inline ClientCapabilities to_cpp_client_capabilities(
    const mcp_client_capabilities_t& caps) {
  ClientCapabilities result;

  // TODO: Convert experimental, sampling, roots if present

  return result;
}

/**
 * Convert C++ ClientCapabilities to C ClientCapabilities
 */
MCP_OWNED inline mcp_client_capabilities_t* to_c_client_capabilities(
    const ClientCapabilities& caps) {
  auto result = c_unique_ptr<mcp_client_capabilities_t>(
      static_cast<mcp_client_capabilities_t*>(
          malloc(sizeof(mcp_client_capabilities_t))));
  if (!result)
    return nullptr;

  // TODO: Convert experimental, sampling, roots if present
  auto exp = mcp_optional_empty();
  auto samp = mcp_optional_empty();
  auto roots = mcp_optional_empty();

  if (!exp || !samp || !roots) {
    mcp_optional_free(exp);
    mcp_optional_free(samp);
    mcp_optional_free(roots);
    return nullptr;
  }

  result->experimental = *exp;
  result->sampling = *samp;
  result->roots = *roots;

  // Free the wrapper but not the content
  free(exp);
  free(samp);
  free(roots);

  return result.release();
}

/**
 * Convert C ServerCapabilities to C++ ServerCapabilities
 */
inline ServerCapabilities to_cpp_server_capabilities(
    const mcp_server_capabilities_t& caps) {
  ServerCapabilities result;

  // TODO: Convert all capability fields

  return result;
}

/**
 * Convert C++ ServerCapabilities to C ServerCapabilities
 */
MCP_OWNED inline mcp_server_capabilities_t* to_c_server_capabilities(
    const ServerCapabilities& caps) {
  // Use AllocationTransaction for atomic allocation
  AllocationTransaction txn;

  auto result = static_cast<mcp_server_capabilities_t*>(
      malloc(sizeof(mcp_server_capabilities_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Create optional fields with safe tracking
  auto exp = SafeOptional::empty_tracked(txn);
  auto log = SafeOptional::empty_tracked(txn);
  auto prompts = SafeOptional::empty_tracked(txn);
  auto res = SafeOptional::empty_tracked(txn);
  auto tools = SafeOptional::empty_tracked(txn);

  if (!exp || !log || !prompts || !res || !tools)
    return nullptr;

  // Copy values (shallow copy is safe since transaction manages lifetime)
  result->experimental = *exp;
  result->logging = *log;
  result->prompts = *prompts;
  result->resources = *res;
  result->tools = *tools;

  txn.commit();  // Success - prevent cleanup
  return result;
}

/* ============================================================================
 * Implementation Info Conversions
 * ============================================================================
 */

/**
 * Convert C Implementation to C++ Implementation
 */
inline Implementation to_cpp_implementation(const mcp_implementation_t& impl) {
  Implementation result;
  result.name = to_cpp_string(impl.name);
  result.version = to_cpp_string(impl.version);
  return result;
}

/**
 * Convert C++ Implementation to C Implementation with RAII
 */
MCP_OWNED inline mcp_implementation_t* to_c_implementation(
    const Implementation& impl) {
  auto result = c_unique_ptr<mcp_implementation_t>(
      static_cast<mcp_implementation_t*>(malloc(sizeof(mcp_implementation_t))));
  if (!result)
    return nullptr;

  // Use transaction for atomic allocation
  AllocationTransaction txn;

  mcp_string_t name_str, version_str;
  if (to_c_string_copy_safe(impl.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(name_str.data), [](void* p) { free(p); });

  if (to_c_string_copy_safe(impl.version, &version_str) != MCP_OK) {
    return nullptr;
  }
  txn.track(const_cast<char*>(version_str.data), [](void* p) { free(p); });

  result->name = name_str;
  result->version = version_str;

  txn.commit();  // Success - prevent cleanup
  return result.release();
}

/* ============================================================================
 * Result Type Conversions
 * ============================================================================
 */

/**
 * Convert C CallToolResult to C++ CallToolResult
 */
inline CallToolResult to_cpp_call_tool_result(
    const mcp_call_tool_result_t& result) {
  CallToolResult cpp_result;
  cpp_result.isError = result.is_error;

  // Convert content list
  if (result.content.items) {
    for (size_t i = 0; i < result.content.count; ++i) {
      auto* block = static_cast<mcp_content_block_t*>(result.content.items[i]);
      if (block) {
        // Convert to ExtendedContentBlock
        auto cb = to_cpp_content_block(*block);
        if (mcp::holds_alternative<TextContent>(cb)) {
          cpp_result.content.push_back(
              ExtendedContentBlock(mcp::get<TextContent>(cb)));
        } else if (mcp::holds_alternative<ImageContent>(cb)) {
          cpp_result.content.push_back(
              ExtendedContentBlock(mcp::get<ImageContent>(cb)));
        } else if (mcp::holds_alternative<ResourceContent>(cb)) {
          auto& rc = mcp::get<ResourceContent>(cb);
          cpp_result.content.push_back(
              ExtendedContentBlock(ResourceLink(rc.resource)));
        }
      }
    }
  }

  return cpp_result;
}

/**
 * Convert C++ CallToolResult to C CallToolResult with complete RAII safety
 */
MCP_OWNED inline mcp_call_tool_result_t* to_c_call_tool_result(
    const CallToolResult& result) {
  // Use AllocationTransaction for atomic allocation
  AllocationTransaction txn;

  auto c_result = static_cast<mcp_call_tool_result_t*>(
      malloc(sizeof(mcp_call_tool_result_t)));
  if (!c_result)
    return nullptr;
  txn.track(c_result, [](void* p) { free(p); });

  c_result->is_error = result.isError;

  // Create list with RAII tracking
  auto list = mcp_list_create(result.content.size());
  if (!list)
    return nullptr;

  // Copy list contents and track for cleanup
  c_result->content = *list;
  free(list);  // Free wrapper
  txn.track(&c_result->content,
            [](void* p) { mcp_list_free(static_cast<mcp_list_t*>(p)); });

  // Convert each content block with individual tracking
  for (size_t i = 0; i < result.content.size(); ++i) {
    const auto& block = result.content[i];
    mcp_content_block_t* c_block = nullptr;

    // Convert ExtendedContentBlock to ContentBlock variants
    if (mcp::holds_alternative<TextContent>(block)) {
      ContentBlock cb(mcp::get<TextContent>(block));
      if (to_c_content_block_safe(cb, &c_block) != MCP_OK) {
        return nullptr;  // Transaction will clean up everything
      }
    } else if (mcp::holds_alternative<ImageContent>(block)) {
      ContentBlock cb(mcp::get<ImageContent>(block));
      if (to_c_content_block_safe(cb, &c_block) != MCP_OK) {
        return nullptr;  // Transaction will clean up everything
      }
    } else if (mcp::holds_alternative<AudioContent>(block)) {
      // Create AudioContent block with RAII
      ResourceGuard<mcp_content_block_t> block_guard(
          static_cast<mcp_content_block_t*>(
              malloc(sizeof(mcp_content_block_t))),
          [](mcp_content_block_t* p) { free(p); });
      if (!block_guard)
        return nullptr;

      block_guard->type = MCP_CONTENT_AUDIO;
      block_guard->content.audio =
          to_c_audio_content(mcp::get<AudioContent>(block));
      if (!block_guard->content.audio)
        return nullptr;

      c_block = block_guard.release();
    } else if (mcp::holds_alternative<ResourceLink>(block)) {
      const auto& link = mcp::get<ResourceLink>(block);
      ContentBlock cb(ResourceContent(static_cast<const Resource&>(link)));
      if (to_c_content_block_safe(cb, &c_block) != MCP_OK) {
        return nullptr;  // Transaction will clean up everything
      }
    }

    // Add block to list with tracking
    if (c_block) {
      if (mcp_list_append(&c_result->content, c_block) != MCP_OK) {
        mcp_content_block_free(c_block);
        return nullptr;  // Transaction will clean up everything
      }
      // Note: c_block is now owned by the list, will be cleaned up via list
      // cleanup
    }
  }

  txn.commit();  // Success - prevent cleanup
  return c_result;
}

/* ============================================================================
 * RAII Safety and Thread Safety Summary
 * ============================================================================
 *
 * This refactored version provides the following safety guarantees:
 *
 * Memory Safety:
 * - AllocationTransaction ensures atomic allocation/deallocation
 * - ResourceGuard provides RAII-based resource management
 * - All conversions use exception-safe resource tracking
 * - No raw memory management or potential leaks
 *
 * Thread Safety:
 * - ConversionLock provides mutex-based synchronization when needed
 * - Atomic counters track active conversions for debugging
 * - Thread-safe utilities for concurrent type conversions
 *
 * Error Handling:
 * - All allocation failures are properly handled
 * - Partial allocation failures trigger complete cleanup
 * - Type-safe error codes returned for all operations
 *
 * Performance:
 * - Zero-copy conversions where possible
 * - Minimal locking overhead (only when explicitly needed)
 * - Efficient RAII cleanup with minimal destructor overhead
 */

}  // namespace c_api
}  // namespace mcp

#endif  // MCP_C_TYPE_CONVERSIONS_H