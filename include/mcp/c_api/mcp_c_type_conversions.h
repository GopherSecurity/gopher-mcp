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

namespace mcp {
namespace c_api {

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
 */
inline mcp_string_t to_c_string(const std::string& str) {
  mcp_string_t result;
  result.data = str.c_str();
  result.length = str.length();
  return result;
}

/**
 * Convert C++ string to owned C string (caller must free)
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
inline mcp_text_content_t* to_c_text_content(const TextContent& content) {
  auto* result = static_cast<mcp_text_content_t*>(malloc(sizeof(mcp_text_content_t)));
  if (result) {
    result->type = to_c_string_copy("text");
    result->text = to_c_string_copy(content.text);
  }
  return result;
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
inline mcp_image_content_t* to_c_image_content(const ImageContent& content) {
  auto* result = static_cast<mcp_image_content_t*>(malloc(sizeof(mcp_image_content_t)));
  if (result) {
    result->type = to_c_string_copy("image");
    result->data = to_c_string_copy(content.data);
    result->mime_type = to_c_string_copy(content.mimeType);
  }
  return result;
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
inline mcp_audio_content_t* to_c_audio_content(const AudioContent& content) {
  auto* result = static_cast<mcp_audio_content_t*>(malloc(sizeof(mcp_audio_content_t)));
  if (result) {
    result->type = to_c_string_copy("audio");
    result->data = to_c_string_copy(content.data);
    result->mime_type = to_c_string_copy(content.mimeType);
  }
  return result;
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
inline mcp_resource_t* to_c_resource(const Resource& resource) {
  auto* result = static_cast<mcp_resource_t*>(malloc(sizeof(mcp_resource_t)));
  if (result) {
    result->uri = to_c_string_copy(resource.uri);
    result->name = to_c_string_copy(resource.name);
    
    if (resource.description) {
      auto* desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
      *desc = to_c_string_copy(*resource.description);
      result->description = mcp_optional_create(desc);
    } else {
      result->description = nullptr;
    }
    
    if (resource.mimeType) {
      auto* mime = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
      *mime = to_c_string_copy(*resource.mimeType);
      result->mime_type = mcp_optional_create(mime);
    } else {
      result->mime_type = nullptr;
    }
  }
  return result;
}

/**
 * Convert C ContentBlock to C++ ContentBlock
 */
inline ContentBlock to_cpp_content_block(const mcp_content_block_t& block) {
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
      if (block.content.resource) {
        ResourceContent rc;
        rc.resource = to_cpp_resource(
            *reinterpret_cast<mcp_resource_t*>(block.content.resource));
        return ContentBlock(rc);
      }
      break;
    default:
      break;
  }
  // Return empty text content as fallback
  return ContentBlock(TextContent(""));
}

/**
 * Convert C++ ContentBlock to C ContentBlock
 */
inline mcp_content_block_t* to_c_content_block(const ContentBlock& block) {
  auto* result = static_cast<mcp_content_block_t*>(malloc(sizeof(mcp_content_block_t)));
  if (!result) return nullptr;
  
  if (mcp::holds_alternative<TextContent>(block)) {
    result->type = MCP_CONTENT_TEXT;
    result->content.text = to_c_text_content(mcp::get<TextContent>(block));
  } else if (mcp::holds_alternative<ImageContent>(block)) {
    result->type = MCP_CONTENT_IMAGE;
    result->content.image = to_c_image_content(mcp::get<ImageContent>(block));
  } else if (mcp::holds_alternative<ResourceContent>(block)) {
    result->type = MCP_CONTENT_RESOURCE;
    auto& rc = mcp::get<ResourceContent>(block);
    result->content.resource = reinterpret_cast<mcp_embedded_resource_t*>(
        to_c_resource(rc.resource));
  }
  
  return result;
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
inline mcp_tool_t* to_c_tool(const Tool& tool) {
  auto* result = static_cast<mcp_tool_t*>(malloc(sizeof(mcp_tool_t)));
  if (result) {
    result->name = to_c_string_copy(tool.name);
    
    if (tool.description) {
      auto* desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
      *desc = to_c_string_copy(*tool.description);
      result->description = *mcp_optional_create(desc);
    } else {
      result->description = *mcp_optional_empty();
    }
    
    // TODO: Convert inputSchema to mcp_json_value_t
    result->input_schema = nullptr;
  }
  return result;
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
inline mcp_prompt_t* to_c_prompt(const Prompt& prompt) {
  auto* result = static_cast<mcp_prompt_t*>(malloc(sizeof(mcp_prompt_t)));
  if (result) {
    result->name = to_c_string_copy(prompt.name);
    
    if (prompt.description) {
      auto* desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
      *desc = to_c_string_copy(*prompt.description);
      result->description = *mcp_optional_create(desc);
    } else {
      result->description = *mcp_optional_empty();
    }
    
    // TODO: Convert arguments
    result->arguments = *mcp_list_create(0);
  }
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
 * Convert C++ Message to C Message
 */
inline mcp_message_t* to_c_message(const Message& msg) {
  auto* result = static_cast<mcp_message_t*>(malloc(sizeof(mcp_message_t)));
  if (result) {
    result->role = to_c_role(msg.role);
    auto* block = to_c_content_block(msg.content);
    if (block) {
      result->content = *block;
      free(block);
    }
  }
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
 * Convert C++ Error to C JSONRPCError
 */
inline mcp_jsonrpc_error_t* to_c_error(const Error& error) {
  auto* result = static_cast<mcp_jsonrpc_error_t*>(malloc(sizeof(mcp_jsonrpc_error_t)));
  if (result) {
    result->code = error.code;
    result->message = to_c_string_copy(error.message);
    
    // TODO: Convert error data if present
    result->data = *mcp_optional_empty();
  }
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
inline mcp_client_capabilities_t* to_c_client_capabilities(
    const ClientCapabilities& caps) {
  auto* result = static_cast<mcp_client_capabilities_t*>(
      malloc(sizeof(mcp_client_capabilities_t)));
  if (result) {
    // TODO: Convert experimental, sampling, roots if present
    result->experimental = *mcp_optional_empty();
    result->sampling = *mcp_optional_empty();
    result->roots = *mcp_optional_empty();
  }
  return result;
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
inline mcp_server_capabilities_t* to_c_server_capabilities(
    const ServerCapabilities& caps) {
  auto* result = static_cast<mcp_server_capabilities_t*>(
      malloc(sizeof(mcp_server_capabilities_t)));
  if (result) {
    // TODO: Convert all capability fields
    result->experimental = *mcp_optional_empty();
    result->logging = *mcp_optional_empty();
    result->prompts = *mcp_optional_empty();
    result->resources = *mcp_optional_empty();
    result->tools = *mcp_optional_empty();
  }
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
 * Convert C++ Implementation to C Implementation
 */
inline mcp_implementation_t* to_c_implementation(const Implementation& impl) {
  auto* result = static_cast<mcp_implementation_t*>(
      malloc(sizeof(mcp_implementation_t)));
  if (result) {
    result->name = to_c_string_copy(impl.name);
    result->version = to_c_string_copy(impl.version);
  }
  return result;
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
inline mcp_call_tool_result_t* to_c_call_tool_result(const CallToolResult& result) {
  auto* c_result = static_cast<mcp_call_tool_result_t*>(
      malloc(sizeof(mcp_call_tool_result_t)));
  if (c_result) {
    c_result->is_error = result.isError;
    
    // Convert content list
    c_result->content = *mcp_list_create(result.content.size());
    for (size_t i = 0; i < result.content.size(); ++i) {
      const auto& block = result.content[i];
      
      // Convert ExtendedContentBlock to ContentBlock for C API
      mcp_content_block_t* c_block = nullptr;
      
      if (mcp::holds_alternative<TextContent>(block)) {
        ContentBlock cb(mcp::get<TextContent>(block));
        c_block = to_c_content_block(cb);
      } else if (mcp::holds_alternative<ImageContent>(block)) {
        ContentBlock cb(mcp::get<ImageContent>(block));
        c_block = to_c_content_block(cb);
      } else if (mcp::holds_alternative<AudioContent>(block)) {
        // Convert AudioContent to C
        auto* audio_block = static_cast<mcp_content_block_t*>(
            malloc(sizeof(mcp_content_block_t)));
        audio_block->type = MCP_CONTENT_AUDIO;
        audio_block->content.audio = to_c_audio_content(mcp::get<AudioContent>(block));
        c_block = audio_block;
      } else if (mcp::holds_alternative<ResourceLink>(block)) {
        // Convert ResourceLink to ResourceContent
        const auto& link = mcp::get<ResourceLink>(block);
        ContentBlock cb(ResourceContent(static_cast<const Resource&>(link)));
        c_block = to_c_content_block(cb);
      }
      
      if (c_block) {
        mcp_list_append(&c_result->content, c_block);
      }
    }
  }
  return c_result;
}

}  // namespace c_api
}  // namespace mcp

#endif  // MCP_C_TYPE_CONVERSIONS_H