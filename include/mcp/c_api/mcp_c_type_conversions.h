/**
 * @file mcp_c_type_conversions.h
 * @brief Conversion functions between C and C++ MCP types
 *
 * This header provides conversion utilities to convert between the C API types
 * (mcp_c_types.h) and the C++ MCP types (types.h). These functions are used
 * internally by the C API implementation to bridge between the two type systems.
 *
 * Design principles:
 * - Zero-copy conversions where possible
 * - Deep copy when ownership transfer is needed
 * - Exception-safe conversions
 * - Automatic memory management via RAII in C++
 */

#ifndef MCP_C_TYPE_CONVERSIONS_H
#define MCP_C_TYPE_CONVERSIONS_H

#include "mcp/c_api/mcp_c_types.h"
#include "mcp/types.h"
#include <cstring>
#include <memory>
#include <type_traits>

namespace mcp {
namespace c_api {

/* ============================================================================
 * RAII Wrapper for C Allocations
 * ============================================================================
 */

/**
 * Custom deleter for C allocated memory
 * Ensures proper cleanup through RAII
 */
template<typename T>
struct c_deleter {
  void operator()(T* ptr) const {
    if (ptr) {
      // Free any owned string members
      if constexpr (std::is_same_v<T, mcp_string_t>) {
        if (ptr->data) {
          free(const_cast<char*>(ptr->data));
        }
      }
      free(ptr);
    }
  }
};

template<typename T>
using c_unique_ptr = std::unique_ptr<T, c_deleter<T>>;

/* ============================================================================
 * Ownership Annotations and Documentation
 * ============================================================================
 */

// Ownership macros for clear API documentation
#define MCP_BORROWED /* Returns borrowed reference - do not free */
#define MCP_OWNED [[nodiscard]] /* Returns owned memory - caller must free */

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
inline mcp_string_t to_c_string_copy(const std::string& str) {
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
inline mcp_result_t to_c_string_copy_safe(const std::string& str, mcp_string_t* out) {
  if (!out) return MCP_ERROR_INVALID_ARGUMENT;
  
  char* data = static_cast<char*>(malloc(str.length() + 1));
  if (!data) {
    out->data = nullptr;
    out->length = 0;
    return MCP_ERROR_OUT_OF_MEMORY;
  }
  
  std::memcpy(data, str.c_str(), str.length() + 1);
  out->data = data;
  out->length = str.length();
  return MCP_OK;
}

/**
 * Convert C++ string to owned C string pointer
 * @return Owned pointer - caller must free with mcp_string_free
 */
MCP_OWNED inline mcp_string_t* to_c_string_owned(const std::string& str) {
  auto result = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
  if (!result) return nullptr;
  
  if (to_c_string_copy_safe(str, result) != MCP_OK) {
    free(result);
    return nullptr;
  }
  
  return result;
}

/* ============================================================================
 * Request ID Conversions
 * ============================================================================
 */

/**
 * Convert C RequestId to C++ RequestId
 */
inline RequestId to_cpp_request_id(const mcp_request_id_t& id) {
  if (id.type == MCP_REQUEST_ID_STRING) {
    return RequestId(to_cpp_string(id.value.string_value));
  } else {
    return RequestId(static_cast<int>(id.value.number_value));
  }
}

/**
 * Convert C++ RequestId to C RequestId
 */
inline mcp_request_id_t to_c_request_id(const RequestId& id) {
  mcp_request_id_t result;
  if (mcp::holds_alternative<std::string>(id)) {
    result.type = MCP_REQUEST_ID_STRING;
    result.value.string_value = to_c_string(mcp::get<std::string>(id));
  } else {
    result.type = MCP_REQUEST_ID_NUMBER;
    result.value.number_value = mcp::get<int>(id);
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
  if (token.type == MCP_PROGRESS_TOKEN_STRING) {
    return ProgressToken(to_cpp_string(token.value.string_value));
  } else {
    return ProgressToken(static_cast<int>(token.value.number_value));
  }
}

/**
 * Convert C++ ProgressToken to C ProgressToken
 */
inline mcp_progress_token_t to_c_progress_token(const ProgressToken& token) {
  mcp_progress_token_t result;
  if (mcp::holds_alternative<std::string>(token)) {
    result.type = MCP_PROGRESS_TOKEN_STRING;
    result.value.string_value = to_c_string(mcp::get<std::string>(token));
  } else {
    result.type = MCP_PROGRESS_TOKEN_NUMBER;
    result.value.number_value = mcp::get<int>(token);
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
inline enums::LoggingLevel::Value to_cpp_logging_level(mcp_logging_level_t level) {
  return static_cast<enums::LoggingLevel::Value>(level);
}

/**
 * Convert C++ LoggingLevel to C LoggingLevel
 */
inline mcp_logging_level_t to_c_logging_level(enums::LoggingLevel::Value level) {
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
 * Convert C++ TextContent to C TextContent
 */
MCP_OWNED inline mcp_text_content_t* to_c_text_content(const TextContent& content) {
  auto result = c_unique_ptr<mcp_text_content_t>(
      static_cast<mcp_text_content_t*>(malloc(sizeof(mcp_text_content_t))));
  if (!result) return nullptr;
  
  // Use safe string copy
  mcp_string_t type_str, text_str;
  if (to_c_string_copy_safe("text", &type_str) != MCP_OK) {
    return nullptr;
  }
  if (to_c_string_copy_safe(content.text, &text_str) != MCP_OK) {
    // Clean up type_str
    free(const_cast<char*>(type_str.data));
    return nullptr;
  }
  
  result->type = type_str;
  result->text = text_str;
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
 * Convert C++ ImageContent to C ImageContent
 */
MCP_OWNED inline mcp_image_content_t* to_c_image_content(const ImageContent& content) {
  auto result = c_unique_ptr<mcp_image_content_t>(
      static_cast<mcp_image_content_t*>(malloc(sizeof(mcp_image_content_t))));
  if (!result) return nullptr;
  
  // Use safe string copy with cleanup on failure
  mcp_string_t type_str, data_str, mime_str;
  if (to_c_string_copy_safe("image", &type_str) != MCP_OK) {
    return nullptr;
  }
  if (to_c_string_copy_safe(content.data, &data_str) != MCP_OK) {
    free(const_cast<char*>(type_str.data));
    return nullptr;
  }
  if (to_c_string_copy_safe(content.mimeType, &mime_str) != MCP_OK) {
    free(const_cast<char*>(type_str.data));
    free(const_cast<char*>(data_str.data));
    return nullptr;
  }
  
  result->type = type_str;
  result->data = data_str;
  result->mime_type = mime_str;
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
 * Convert C++ AudioContent to C AudioContent
 */
MCP_OWNED inline mcp_audio_content_t* to_c_audio_content(const AudioContent& content) {
  auto result = c_unique_ptr<mcp_audio_content_t>(
      static_cast<mcp_audio_content_t*>(malloc(sizeof(mcp_audio_content_t))));
  if (!result) return nullptr;
  
  // Use safe string copy with cleanup on failure
  mcp_string_t type_str, data_str, mime_str;
  if (to_c_string_copy_safe("audio", &type_str) != MCP_OK) {
    return nullptr;
  }
  if (to_c_string_copy_safe(content.data, &data_str) != MCP_OK) {
    free(const_cast<char*>(type_str.data));
    return nullptr;
  }
  if (to_c_string_copy_safe(content.mimeType, &mime_str) != MCP_OK) {
    free(const_cast<char*>(type_str.data));
    free(const_cast<char*>(data_str.data));
    return nullptr;
  }
  
  result->type = type_str;
  result->data = data_str;
  result->mime_type = mime_str;
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
    auto* str = static_cast<mcp_string_t*>(mcp_optional_get_value(resource.description));
    if (str) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }
  
  if (resource.mime_type && mcp_optional_has_value(resource.mime_type)) {
    auto* str = static_cast<mcp_string_t*>(mcp_optional_get_value(resource.mime_type));
    if (str) {
      result.mimeType = mcp::make_optional(to_cpp_string(*str));
    }
  }
  
  return result;
}

/**
 * Convert C++ Resource to C Resource
 */
MCP_OWNED inline mcp_resource_t* to_c_resource(const Resource& resource) {
  auto result = c_unique_ptr<mcp_resource_t>(
      static_cast<mcp_resource_t*>(malloc(sizeof(mcp_resource_t))));
  if (!result) return nullptr;
  
  // Use safe string copy with cleanup on failure
  mcp_string_t uri_str, name_str;
  if (to_c_string_copy_safe(resource.uri, &uri_str) != MCP_OK) {
    return nullptr;
  }
  if (to_c_string_copy_safe(resource.name, &name_str) != MCP_OK) {
    free(const_cast<char*>(uri_str.data));
    return nullptr;
  }
  
  result->uri = uri_str;
  result->name = name_str;
  
  // Handle optional description
  if (resource.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc || to_c_string_copy_safe(*resource.description, desc) != MCP_OK) {
      free(const_cast<char*>(uri_str.data));
      free(const_cast<char*>(name_str.data));
      free(desc);
      return nullptr;
    }
    result->description = mcp_optional_create(desc);
  } else {
    result->description = nullptr;
  }
  
  // Handle optional mime type
  if (resource.mimeType) {
    auto mime = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!mime || to_c_string_copy_safe(*resource.mimeType, mime) != MCP_OK) {
      free(const_cast<char*>(uri_str.data));
      free(const_cast<char*>(name_str.data));
      if (result->description) {
        auto* desc = static_cast<mcp_string_t*>(mcp_optional_get_value(result->description));
        if (desc && desc->data) free(const_cast<char*>(desc->data));
        free(desc);
        mcp_optional_free(result->description);
      }
      free(mime);
      return nullptr;
    }
    result->mime_type = mcp_optional_create(mime);
  } else {
    result->mime_type = nullptr;
  }
  
  return result.release();
}

/**
 * Convert C ContentBlock to C++ ContentBlock with validation
 */
inline ContentBlock to_cpp_content_block(const mcp_content_block_t& block) {
  // Validate enum value is in range
  if (block.type < MCP_CONTENT_TEXT || block.type > MCP_CONTENT_EMBEDDED_RESOURCE) {
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
      if (block.content.audio) {
        return ContentBlock(to_cpp_audio_content(*block.content.audio));
      }
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
 * Convert C++ ContentBlock to C ContentBlock (unsafe - use to_c_content_block_safe)
 * @deprecated Use to_c_content_block_safe for proper error handling
 */
inline mcp_content_block_t* to_c_content_block(const ContentBlock& block) {
  auto result = c_unique_ptr<mcp_content_block_t>(
      static_cast<mcp_content_block_t*>(malloc(sizeof(mcp_content_block_t))));
  if (!result) return nullptr;
  
  if (mcp::holds_alternative<TextContent>(block)) {
    result->type = MCP_CONTENT_TEXT;
    result->content.text = to_c_text_content(mcp::get<TextContent>(block));
    if (!result->content.text) return nullptr;
  } else if (mcp::holds_alternative<ImageContent>(block)) {
    result->type = MCP_CONTENT_IMAGE;
    result->content.image = to_c_image_content(mcp::get<ImageContent>(block));
    if (!result->content.image) return nullptr;
  } else if (mcp::holds_alternative<ResourceContent>(block)) {
    result->type = MCP_CONTENT_RESOURCE;
    auto& rc = mcp::get<ResourceContent>(block);
    auto* resource = to_c_resource(rc.resource);
    if (!resource) return nullptr;
    result->content.resource = reinterpret_cast<mcp_embedded_resource_t*>(resource);
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
inline mcp_result_t to_c_content_block_safe(
    const ContentBlock& block,
    mcp_content_block_t** out
) {
  if (!out) return MCP_ERROR_INVALID_ARGUMENT;
  
  auto result = c_unique_ptr<mcp_content_block_t>(
      static_cast<mcp_content_block_t*>(malloc(sizeof(mcp_content_block_t))));
  if (!result) return MCP_ERROR_OUT_OF_MEMORY;
  
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
    result->content.resource = reinterpret_cast<mcp_embedded_resource_t*>(resource);
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
  
  if (tool.description && mcp_optional_has_value(&tool.description)) {
    auto* str = static_cast<mcp_string_t*>(mcp_optional_get_value(&tool.description));
    if (str) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }
  
  // TODO: Convert input_schema from mcp_json_value_t to ToolInputSchema
  
  return result;
}

/**
 * Convert C++ Tool to C Tool
 */
MCP_OWNED inline mcp_tool_t* to_c_tool(const Tool& tool) {
  auto result = c_unique_ptr<mcp_tool_t>(
      static_cast<mcp_tool_t*>(malloc(sizeof(mcp_tool_t))));
  if (!result) return nullptr;
  
  // Use safe string copy
  mcp_string_t name_str;
  if (to_c_string_copy_safe(tool.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  result->name = name_str;
  
  if (tool.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc || to_c_string_copy_safe(*tool.description, desc) != MCP_OK) {
      free(const_cast<char*>(name_str.data));
      free(desc);
      return nullptr;
    }
    result->description = *mcp_optional_create(desc);
  } else {
    result->description = *mcp_optional_empty();
  }
  
  // TODO: Convert inputSchema to mcp_json_value_t
  result->input_schema = nullptr;
  
  return result.release();
}

/**
 * Convert C Prompt to C++ Prompt
 */
inline Prompt to_cpp_prompt(const mcp_prompt_t& prompt) {
  Prompt result;
  result.name = to_cpp_string(prompt.name);
  
  if (prompt.description && mcp_optional_has_value(&prompt.description)) {
    auto* str = static_cast<mcp_string_t*>(mcp_optional_get_value(&prompt.description));
    if (str) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }
  
  // TODO: Convert arguments list
  
  return result;
}

/**
 * Convert C++ Prompt to C Prompt
 */
MCP_OWNED inline mcp_prompt_t* to_c_prompt(const Prompt& prompt) {
  auto result = c_unique_ptr<mcp_prompt_t>(
      static_cast<mcp_prompt_t*>(malloc(sizeof(mcp_prompt_t))));
  if (!result) return nullptr;
  
  // Use safe string copy
  mcp_string_t name_str;
  if (to_c_string_copy_safe(prompt.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  result->name = name_str;
  
  if (prompt.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc || to_c_string_copy_safe(*prompt.description, desc) != MCP_OK) {
      free(const_cast<char*>(name_str.data));
      free(desc);
      return nullptr;
    }
    result->description = *mcp_optional_create(desc);
  } else {
    result->description = *mcp_optional_empty();
  }
  
  // TODO: Convert arguments
  auto list = mcp_list_create(0);
  if (!list) {
    free(const_cast<char*>(name_str.data));
    if (prompt.description && result->description.has_value) {
      auto* desc = static_cast<mcp_string_t*>(result->description.value);
      if (desc && desc->data) free(const_cast<char*>(desc->data));
      free(desc);
    }
    return nullptr;
  }
  result->arguments = *list;
  
  return result.release();
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
 * Convert C++ Message to C Message with proper memory management
 */
MCP_OWNED inline mcp_message_t* to_c_message(const Message& msg) {
  auto result = c_unique_ptr<mcp_message_t>(
      static_cast<mcp_message_t*>(malloc(sizeof(mcp_message_t))));
  if (!result) return nullptr;
  
  result->role = to_c_role(msg.role);
  
  // Use safe conversion to avoid double free
  mcp_content_block_t* block = nullptr;
  if (to_c_content_block_safe(msg.content, &block) != MCP_OK || !block) {
    return nullptr;
  }
  
  // Copy content and free temporary
  result->content = *block;
  free(block);
  
  return result.release();
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
 * Convert C++ Error to C JSONRPCError
 */
MCP_OWNED inline mcp_jsonrpc_error_t* to_c_error(const Error& error) {
  auto result = c_unique_ptr<mcp_jsonrpc_error_t>(
      static_cast<mcp_jsonrpc_error_t*>(malloc(sizeof(mcp_jsonrpc_error_t))));
  if (!result) return nullptr;
  
  result->code = error.code;
  
  // Use safe string copy
  mcp_string_t message_str;
  if (to_c_string_copy_safe(error.message, &message_str) != MCP_OK) {
    return nullptr;
  }
  result->message = message_str;
  
  // TODO: Convert error data if present
  auto empty = mcp_optional_empty();
  if (!empty) {
    free(const_cast<char*>(message_str.data));
    return nullptr;
  }
  result->data = *empty;
  
  return result.release();
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
  if (!result) return nullptr;
  
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
  auto result = c_unique_ptr<mcp_server_capabilities_t>(
      static_cast<mcp_server_capabilities_t*>(
          malloc(sizeof(mcp_server_capabilities_t))));
  if (!result) return nullptr;
  
  // TODO: Convert all capability fields
  auto exp = mcp_optional_empty();
  auto log = mcp_optional_empty();
  auto prompts = mcp_optional_empty();
  auto res = mcp_optional_empty();
  auto tools = mcp_optional_empty();
  
  if (!exp || !log || !prompts || !res || !tools) {
    mcp_optional_free(exp);
    mcp_optional_free(log);
    mcp_optional_free(prompts);
    mcp_optional_free(res);
    mcp_optional_free(tools);
    return nullptr;
  }
  
  result->experimental = *exp;
  result->logging = *log;
  result->prompts = *prompts;
  result->resources = *res;
  result->tools = *tools;
  
  // Free the wrappers but not the content
  free(exp);
  free(log);
  free(prompts);
  free(res);
  free(tools);
  
  return result.release();
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
 * Convert C++ Implementation to C Implementation
 */
MCP_OWNED inline mcp_implementation_t* to_c_implementation(const Implementation& impl) {
  auto result = c_unique_ptr<mcp_implementation_t>(
      static_cast<mcp_implementation_t*>(malloc(sizeof(mcp_implementation_t))));
  if (!result) return nullptr;
  
  // Use safe string copy with cleanup on failure
  mcp_string_t name_str, version_str;
  if (to_c_string_copy_safe(impl.name, &name_str) != MCP_OK) {
    return nullptr;
  }
  if (to_c_string_copy_safe(impl.version, &version_str) != MCP_OK) {
    free(const_cast<char*>(name_str.data));
    return nullptr;
  }
  
  result->name = name_str;
  result->version = version_str;
  return result.release();
}

/* ============================================================================
 * Result Type Conversions
 * ============================================================================
 */

/**
 * Convert C CallToolResult to C++ CallToolResult
 */
inline CallToolResult to_cpp_call_tool_result(const mcp_call_tool_result_t& result) {
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
 * Convert C++ CallToolResult to C CallToolResult
 */
MCP_OWNED inline mcp_call_tool_result_t* to_c_call_tool_result(const CallToolResult& result) {
  auto c_result = c_unique_ptr<mcp_call_tool_result_t>(
      static_cast<mcp_call_tool_result_t*>(
          malloc(sizeof(mcp_call_tool_result_t))));
  if (!c_result) return nullptr;
  
  c_result->is_error = result.isError;
  
  // Convert content list
  auto list = mcp_list_create(result.content.size());
  if (!list) return nullptr;
  
  c_result->content = *list;
  free(list);  // Free wrapper but keep content
  
  for (size_t i = 0; i < result.content.size(); ++i) {
    const auto& block = result.content[i];
    
    // Convert ExtendedContentBlock to ContentBlock for C API
    mcp_content_block_t* c_block = nullptr;
    
    if (mcp::holds_alternative<TextContent>(block)) {
      ContentBlock cb(mcp::get<TextContent>(block));
      if (to_c_content_block_safe(cb, &c_block) != MCP_OK) {
        // Clean up previously allocated blocks
        for (size_t j = 0; j < i; ++j) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(
              mcp_list_get(&c_result->content, j)));
        }
        mcp_list_free(&c_result->content);
        return nullptr;
      }
    } else if (mcp::holds_alternative<ImageContent>(block)) {
      ContentBlock cb(mcp::get<ImageContent>(block));
      if (to_c_content_block_safe(cb, &c_block) != MCP_OK) {
        // Clean up previously allocated blocks
        for (size_t j = 0; j < i; ++j) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(
              mcp_list_get(&c_result->content, j)));
        }
        mcp_list_free(&c_result->content);
        return nullptr;
      }
    } else if (mcp::holds_alternative<AudioContent>(block)) {
      // Convert AudioContent to C
      c_block = static_cast<mcp_content_block_t*>(
          malloc(sizeof(mcp_content_block_t)));
      if (!c_block) {
        // Clean up previously allocated blocks
        for (size_t j = 0; j < i; ++j) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(
              mcp_list_get(&c_result->content, j)));
        }
        mcp_list_free(&c_result->content);
        return nullptr;
      }
      c_block->type = MCP_CONTENT_AUDIO;
      c_block->content.audio = to_c_audio_content(mcp::get<AudioContent>(block));
      if (!c_block->content.audio) {
        free(c_block);
        // Clean up previously allocated blocks
        for (size_t j = 0; j < i; ++j) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(
              mcp_list_get(&c_result->content, j)));
        }
        mcp_list_free(&c_result->content);
        return nullptr;
      }
    } else if (mcp::holds_alternative<ResourceLink>(block)) {
      // Convert ResourceLink to ResourceContent
      const auto& link = mcp::get<ResourceLink>(block);
      ContentBlock cb(ResourceContent(static_cast<const Resource&>(link)));
      if (to_c_content_block_safe(cb, &c_block) != MCP_OK) {
        // Clean up previously allocated blocks
        for (size_t j = 0; j < i; ++j) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(
              mcp_list_get(&c_result->content, j)));
        }
        mcp_list_free(&c_result->content);
        return nullptr;
      }
    }
    
    if (c_block) {
      if (mcp_list_append(&c_result->content, c_block) != MCP_OK) {
        mcp_content_block_free(c_block);
        // Clean up previously allocated blocks
        for (size_t j = 0; j < i; ++j) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(
              mcp_list_get(&c_result->content, j)));
        }
        mcp_list_free(&c_result->content);
        return nullptr;
      }
    }
  }
  
  return c_result.release();
}

}  // namespace c_api
}  // namespace mcp

#endif  // MCP_C_TYPE_CONVERSIONS_H