/**
 * @file missing_conversions_part4.h
 * @brief MCP Request/Result type conversions with RAII safety (Part 4)
 */

#ifndef MISSING_CONVERSIONS_PART4_H
#define MISSING_CONVERSIONS_PART4_H

#include "missing_conversions_part3.h"

namespace mcp {
namespace c_api {

/**
 * Convert C++ CallToolRequest to C CallToolRequest with RAII safety
 */
MCP_OWNED inline mcp_call_tool_request_t* to_c_call_tool_request(
    const CallToolRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_call_tool_request_t*>(
      malloc(sizeof(mcp_call_tool_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Set tool name
  if (safe_string_dup(req.name, &result->name, txn) != MCP_OK)
    return nullptr;

  // Handle optional arguments (JsonValue)
  // For now, leave empty until JsonValue conversion is available
  auto opt = mcp_optional_empty();
  if (!opt)
    return nullptr;
  result->arguments = *opt;
  free(opt);

  txn.commit();
  return result;
}

/**
 * Convert C CallToolRequest to C++ CallToolRequest
 */
inline CallToolRequest to_cpp_call_tool_request(
    const mcp_call_tool_request_t& req) {
  CallToolRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);
  result.name = to_cpp_string(req.name);

  // Handle arguments when JsonValue conversion is available

  return result;
}

/**
 * Convert C++ GetPromptRequest to C GetPromptRequest with RAII safety
 */
MCP_OWNED inline mcp_get_prompt_request_t* to_c_get_prompt_request(
    const GetPromptRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_get_prompt_request_t*>(
      malloc(sizeof(mcp_get_prompt_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Set prompt name
  if (safe_string_dup(req.name, &result->name, txn) != MCP_OK)
    return nullptr;

  // Handle optional arguments
  auto opt = mcp_optional_empty();
  if (!opt)
    return nullptr;
  result->arguments = *opt;
  free(opt);

  txn.commit();
  return result;
}

/**
 * Convert C GetPromptRequest to C++ GetPromptRequest
 */
inline GetPromptRequest to_cpp_get_prompt_request(
    const mcp_get_prompt_request_t& req) {
  GetPromptRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);
  result.name = to_cpp_string(req.name);

  return result;
}

/**
 * Convert C++ ReadResourceRequest to C ReadResourceRequest with RAII safety
 */
MCP_OWNED inline mcp_read_resource_request_t* to_c_read_resource_request(
    const ReadResourceRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_read_resource_request_t*>(
      malloc(sizeof(mcp_read_resource_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Set URI
  if (safe_string_dup(req.uri, &result->uri, txn) != MCP_OK)
    return nullptr;

  txn.commit();
  return result;
}

/**
 * Convert C ReadResourceRequest to C++ ReadResourceRequest
 */
inline ReadResourceRequest to_cpp_read_resource_request(
    const mcp_read_resource_request_t& req) {
  ReadResourceRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);
  result.uri = to_cpp_string(req.uri);

  return result;
}

/**
 * Convert C++ ListResourcesRequest to C ListResourcesRequest with RAII safety
 */
MCP_OWNED inline mcp_list_resources_request_t* to_c_list_resources_request(
    const ListResourcesRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_list_resources_request_t*>(
      malloc(sizeof(mcp_list_resources_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Handle optional cursor
  if (req.cursor) {
    auto cursor = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!cursor)
      return nullptr;
    txn.track(cursor, [](void* p) { free(p); });

    if (safe_string_dup(*req.cursor, cursor, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(cursor);
    if (!opt)
      return nullptr;
    result->cursor = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->cursor = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ListResourcesRequest to C++ ListResourcesRequest
 */
inline ListResourcesRequest to_cpp_list_resources_request(
    const mcp_list_resources_request_t& req) {
  ListResourcesRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);

  if (req.cursor.has_value) {
    auto* str = static_cast<mcp_string_t*>(req.cursor.value);
    if (str && str->data) {
      result.cursor = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ ListPromptsRequest to C ListPromptsRequest with RAII safety
 */
MCP_OWNED inline mcp_list_prompts_request_t* to_c_list_prompts_request(
    const ListPromptsRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_list_prompts_request_t*>(
      malloc(sizeof(mcp_list_prompts_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Handle optional cursor
  if (req.cursor) {
    auto cursor = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!cursor)
      return nullptr;
    txn.track(cursor, [](void* p) { free(p); });

    if (safe_string_dup(*req.cursor, cursor, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(cursor);
    if (!opt)
      return nullptr;
    result->cursor = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->cursor = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ListPromptsRequest to C++ ListPromptsRequest
 */
inline ListPromptsRequest to_cpp_list_prompts_request(
    const mcp_list_prompts_request_t& req) {
  ListPromptsRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);

  if (req.cursor.has_value) {
    auto* str = static_cast<mcp_string_t*>(req.cursor.value);
    if (str && str->data) {
      result.cursor = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ ListToolsRequest to C ListToolsRequest with RAII safety
 */
MCP_OWNED inline mcp_list_tools_request_t* to_c_list_tools_request(
    const ListToolsRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_list_tools_request_t*>(
      malloc(sizeof(mcp_list_tools_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Handle optional cursor
  if (req.cursor) {
    auto cursor = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!cursor)
      return nullptr;
    txn.track(cursor, [](void* p) { free(p); });

    if (safe_string_dup(*req.cursor, cursor, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(cursor);
    if (!opt)
      return nullptr;
    result->cursor = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->cursor = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ListToolsRequest to C++ ListToolsRequest
 */
inline ListToolsRequest to_cpp_list_tools_request(
    const mcp_list_tools_request_t& req) {
  ListToolsRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);

  if (req.cursor.has_value) {
    auto* str = static_cast<mcp_string_t*>(req.cursor.value);
    if (str && str->data) {
      result.cursor = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ SetLevelRequest to C SetLevelRequest with RAII safety
 */
MCP_OWNED inline mcp_set_level_request_t* to_c_set_level_request(
    const SetLevelRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_set_level_request_t*>(
      malloc(sizeof(mcp_set_level_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Set logging level
  result->level = to_c_logging_level(req.level);

  txn.commit();
  return result;
}

/**
 * Convert C SetLevelRequest to C++ SetLevelRequest
 */
inline SetLevelRequest to_cpp_set_level_request(
    const mcp_set_level_request_t& req) {
  SetLevelRequest result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);
  result.level = to_cpp_logging_level(req.level);

  return result;
}

}  // namespace c_api
}  // namespace mcp

#endif  // MISSING_CONVERSIONS_PART4_H