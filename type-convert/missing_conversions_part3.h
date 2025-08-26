/**
 * @file missing_conversions_part3.h
 * @brief JSON-RPC and Protocol type conversions with RAII safety (Part 3)
 */

#ifndef MISSING_CONVERSIONS_PART3_H
#define MISSING_CONVERSIONS_PART3_H

#include "missing_conversions_part2.h"

namespace mcp {
namespace c_api {

/**
 * Convert C++ jsonrpc::Request to C jsonrpc Request with RAII safety
 */
MCP_OWNED inline mcp_jsonrpc_request_t* to_c_jsonrpc_request(
    const jsonrpc::Request& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_jsonrpc_request_t*>(
      malloc(sizeof(mcp_jsonrpc_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set jsonrpc version
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;

  // Set method
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  // Convert id (RequestId variant)
  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Handle optional params (JsonValue)
  // For now, leave empty until JsonValue conversion is available
  auto opt = mcp_optional_empty();
  if (!opt)
    return nullptr;
  result->params = *opt;
  free(opt);

  txn.commit();
  return result;
}

/**
 * Convert C jsonrpc Request to C++ jsonrpc::Request
 */
inline jsonrpc::Request to_cpp_jsonrpc_request(
    const mcp_jsonrpc_request_t& req) {
  jsonrpc::Request result;

  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);

  // Handle params when JsonValue conversion is available
  // if (req.params.has_value) { ... }

  return result;
}

/**
 * Convert C++ jsonrpc::Response to C jsonrpc Response with RAII safety
 */
MCP_OWNED inline mcp_jsonrpc_response_t* to_c_jsonrpc_response(
    const jsonrpc::Response& resp) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_jsonrpc_response_t*>(
      malloc(sizeof(mcp_jsonrpc_response_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set jsonrpc version
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;

  // Convert id
  auto id_ptr = to_c_request_id(resp.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Handle optional result/error
  // For now, leave empty until proper result/error handling is implemented
  result->result = mcp_optional_empty();
  result->error = mcp_optional_empty();

  txn.commit();
  return result;
}

/**
 * Convert C jsonrpc Response to C++ jsonrpc::Response
 */
inline jsonrpc::Response to_cpp_jsonrpc_response(
    const mcp_jsonrpc_response_t& resp) {
  jsonrpc::Response result;

  result.id = to_cpp_request_id(resp.id);

  // Handle result/error when proper conversion is available
  // if (resp.result.has_value) { ... }
  // if (resp.error.has_value) { ... }

  return result;
}

/**
 * Convert C++ jsonrpc::Notification to C jsonrpc Notification with RAII safety
 */
MCP_OWNED inline mcp_jsonrpc_notification_t* to_c_jsonrpc_notification(
    const jsonrpc::Notification& notif) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_jsonrpc_notification_t*>(
      malloc(sizeof(mcp_jsonrpc_notification_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set jsonrpc version
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;

  // Set method
  if (safe_string_dup(notif.method, &result->method, txn) != MCP_OK)
    return nullptr;

  // Handle optional params
  // For now, leave empty until JsonValue conversion is available
  auto opt = mcp_optional_empty();
  if (!opt)
    return nullptr;
  result->params = *opt;
  free(opt);

  txn.commit();
  return result;
}

/**
 * Convert C jsonrpc Notification to C++ jsonrpc::Notification
 */
inline jsonrpc::Notification to_cpp_jsonrpc_notification(
    const mcp_jsonrpc_notification_t& notif) {
  jsonrpc::Notification result;

  result.method = to_cpp_string(notif.method);

  // Handle params when JsonValue conversion is available
  // if (notif.params.has_value) { ... }

  return result;
}

/**
 * Convert C++ InitializeRequest to C InitializeRequest with RAII safety
 */
MCP_OWNED inline mcp_initialize_request_t* to_c_initialize_request(
    const InitializeRequest& req) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_initialize_request_t*>(
      malloc(sizeof(mcp_initialize_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base jsonrpc::Request fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(req.method, &result->method, txn) != MCP_OK)
    return nullptr;

  auto id_ptr = to_c_request_id(req.id);
  if (!id_ptr)
    return nullptr;
  result->id = *id_ptr;
  txn.track(id_ptr, [](void* p) { free(p); });

  // Set protocol version
  if (safe_string_dup(req.protocolVersion, &result->protocol_version, txn) !=
      MCP_OK)
    return nullptr;

  // Convert capabilities
  auto caps = to_c_client_capabilities(req.capabilities);
  if (!caps)
    return nullptr;
  result->capabilities = *caps;
  txn.track(caps, [](void* p) {
    mcp_client_capabilities_free(static_cast<mcp_client_capabilities_t*>(p));
  });

  // Handle optional client info
  if (req.clientInfo) {
    auto info = to_c_implementation(*req.clientInfo);
    if (!info)
      return nullptr;
    auto opt = mcp_optional_create(info);
    if (!opt)
      return nullptr;
    result->client_info = *opt;
    free(opt);
    txn.track(info, [](void* p) {
      mcp_implementation_free(static_cast<mcp_implementation_t*>(p));
    });
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->client_info = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C InitializeRequest to C++ InitializeRequest
 */
inline InitializeRequest to_cpp_initialize_request(
    const mcp_initialize_request_t& req) {
  InitializeRequest result;

  // Copy base fields
  result.method = to_cpp_string(req.method);
  result.id = to_cpp_request_id(req.id);

  // Set specific fields
  result.protocolVersion = to_cpp_string(req.protocol_version);
  result.capabilities = to_cpp_client_capabilities(req.capabilities);

  // Handle optional client info
  if (req.client_info.has_value) {
    auto* info = static_cast<mcp_implementation_t*>(req.client_info.value);
    if (info) {
      result.clientInfo = mcp::make_optional(to_cpp_implementation(*info));
    }
  }

  return result;
}

/**
 * Convert C++ InitializeResult to C InitializeResult with RAII safety
 */
MCP_OWNED inline mcp_initialize_result_t* to_c_initialize_result(
    const InitializeResult& res) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_initialize_result_t*>(
      malloc(sizeof(mcp_initialize_result_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set protocol version
  if (safe_string_dup(res.protocolVersion, &result->protocol_version, txn) !=
      MCP_OK)
    return nullptr;

  // Convert capabilities
  auto caps = to_c_server_capabilities(res.capabilities);
  if (!caps)
    return nullptr;
  result->capabilities = *caps;
  txn.track(caps, [](void* p) {
    mcp_server_capabilities_free(static_cast<mcp_server_capabilities_t*>(p));
  });

  // Handle optional server info
  if (res.serverInfo) {
    auto info = to_c_implementation(*res.serverInfo);
    if (!info)
      return nullptr;
    auto opt = mcp_optional_create(info);
    if (!opt)
      return nullptr;
    result->server_info = *opt;
    free(opt);
    txn.track(info, [](void* p) {
      mcp_implementation_free(static_cast<mcp_implementation_t*>(p));
    });
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->server_info = *opt;
    free(opt);
  }

  // Handle optional instructions
  if (res.instructions) {
    auto instr = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!instr)
      return nullptr;
    txn.track(instr, [](void* p) { free(p); });

    if (safe_string_dup(*res.instructions, instr, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(instr);
    if (!opt)
      return nullptr;
    result->instructions = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->instructions = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C InitializeResult to C++ InitializeResult
 */
inline InitializeResult to_cpp_initialize_result(
    const mcp_initialize_result_t& res) {
  InitializeResult result;

  result.protocolVersion = to_cpp_string(res.protocol_version);
  result.capabilities = to_cpp_server_capabilities(res.capabilities);

  // Handle optional server info
  if (res.server_info.has_value) {
    auto* info = static_cast<mcp_implementation_t*>(res.server_info.value);
    if (info) {
      result.serverInfo = mcp::make_optional(to_cpp_implementation(*info));
    }
  }

  // Handle optional instructions
  if (res.instructions.has_value) {
    auto* str = static_cast<mcp_string_t*>(res.instructions.value);
    if (str && str->data) {
      result.instructions = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

}  // namespace c_api
}  // namespace mcp

#endif  // MISSING_CONVERSIONS_PART3_H