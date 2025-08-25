/**
 * @file mcp_c_api_json.cc
 * @brief JSON serialization/deserialization for MCP C API types using opaque
 * handles
 *
 * This implementation uses the opaque handle API with RAII for safe memory
 * management. It provides JSON conversion for all MCP C API types through the
 * public C API functions.
 */

#include "mcp/c_api/mcp_c_api_json.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "mcp/c_api/mcp_c_collections.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_types_api.h"
#include "mcp/c_api/mcp_raii.h"

namespace {

// Helper to convert C string to std::string safely
std::string safe_string(const char* str) {
  return str ? std::string(str) : std::string();
}

// Helper to allocate and copy string
char* alloc_string(const std::string& str) {
  if (str.empty())
    return nullptr;
  char* result = static_cast<char*>(mcp_malloc(str.size() + 1));
  if (result) {
    std::strcpy(result, str.c_str());
  }
  return result;
}

}  // anonymous namespace

extern "C" {

// ============================================================================
// JSON Value Operations
// ============================================================================

MCP_API mcp_json_value_t mcp_json_parse(const char* json_string) MCP_NOEXCEPT {
  if (!json_string)
    return nullptr;

  // For now, we'll create a simple string value as parsing is complex
  // In a full implementation, this would parse the JSON string
  return mcp_json_create_string(json_string);
}

MCP_API char* mcp_json_stringify(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json)
    return nullptr;

  // Simple implementation - just return type name for now
  // In a full implementation, this would serialize to JSON
  mcp_json_type_t type = mcp_json_get_type(json);
  switch (type) {
    case MCP_JSON_TYPE_NULL:
      return alloc_string("null");
    case MCP_JSON_TYPE_BOOL:
      return alloc_string(mcp_json_get_bool(json) ? "true" : "false");
    case MCP_JSON_TYPE_NUMBER: {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%.6f", mcp_json_get_number(json));
      return alloc_string(buffer);
    }
    case MCP_JSON_TYPE_STRING:
      return alloc_string(std::string("\"") +
                          safe_string(mcp_json_get_string(json)) + "\"");
    case MCP_JSON_TYPE_ARRAY:
      return alloc_string("[array]");
    case MCP_JSON_TYPE_OBJECT:
      return alloc_string("{object}");
    default:
      return nullptr;
  }
}

// mcp_json_free is already defined in mcp_c_collections_impl.cc
// Commenting out to avoid duplicate symbol error
// MCP_API void mcp_json_free(mcp_json_value_t json) MCP_NOEXCEPT {
//     // The JSON value is already managed by the collections API
//     // This is here for API completeness but delegates to the collections API
//     if (json) {
//         // Note: mcp_json_value_t is managed by mcp_c_collections, so we just
//         // ensure proper cleanup through that API
//         // The actual free is handled when the JSON value goes out of scope
//     }
// }

// ============================================================================
// Request ID JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t mcp_request_id_to_json(const mcp_request_id_t* id)
    MCP_NOEXCEPT {
  if (!id || !*id)
    return nullptr;

  // Use the opaque handle API to get the type and value
  mcp_request_id_type_t type = mcp_request_id_get_type(*id);

  if (type == MCP_REQUEST_ID_TYPE_STRING) {
    const char* str = mcp_request_id_get_string(*id);
    // Debug: print the string value
    // fprintf(stderr, "DEBUG: request_id string value: '%s'\n", str ? str :
    // "(null)");
    return mcp_json_create_string(str ? str : "");
  } else if (type == MCP_REQUEST_ID_TYPE_NUMBER) {
    int64_t num = mcp_request_id_get_number(*id);
    return mcp_json_create_number(static_cast<double>(num));
  }

  return mcp_json_create_null();
}

MCP_API mcp_request_id_t* mcp_request_id_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT {
  if (!json)
    return nullptr;

  mcp_json_type_t type = mcp_json_get_type(json);
  mcp_request_id_t* result = nullptr;

  if (type == MCP_JSON_TYPE_STRING) {
    const char* str = mcp_json_get_string(json);
    mcp_request_id_t id = mcp_request_id_create_string(str ? str : "");
    if (id) {
      result =
          static_cast<mcp_request_id_t*>(mcp_malloc(sizeof(mcp_request_id_t)));
      if (result) {
        *result = id;
      } else {
        mcp_request_id_free(id);
      }
    }
  } else if (type == MCP_JSON_TYPE_NUMBER) {
    double num = mcp_json_get_number(json);
    mcp_request_id_t id =
        mcp_request_id_create_number(static_cast<int64_t>(num));
    if (id) {
      result =
          static_cast<mcp_request_id_t*>(mcp_malloc(sizeof(mcp_request_id_t)));
      if (result) {
        *result = id;
      } else {
        mcp_request_id_free(id);
      }
    }
  }

  return result;
}

// ============================================================================
// Progress Token JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t
mcp_progress_token_to_json(const mcp_progress_token_t* token) MCP_NOEXCEPT {
  if (!token)
    return nullptr;

  mcp_progress_token_type_t type = mcp_progress_token_get_type(*token);

  if (type == MCP_PROGRESS_TOKEN_TYPE_STRING) {
    const char* str = mcp_progress_token_get_string(*token);
    return mcp_json_create_string(str ? str : "");
  } else if (type == MCP_PROGRESS_TOKEN_TYPE_NUMBER) {
    int64_t num = mcp_progress_token_get_number(*token);
    return mcp_json_create_number(static_cast<double>(num));
  }

  return mcp_json_create_null();
}

MCP_API mcp_progress_token_t* mcp_progress_token_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json)
    return nullptr;

  mcp_json_type_t type = mcp_json_get_type(json);
  mcp_progress_token_t* result = nullptr;

  if (type == MCP_JSON_TYPE_STRING) {
    const char* str = mcp_json_get_string(json);
    mcp_progress_token_t token =
        mcp_progress_token_create_string(str ? str : "");
    if (token) {
      result = static_cast<mcp_progress_token_t*>(
          mcp_malloc(sizeof(mcp_progress_token_t)));
      if (result) {
        *result = token;
      } else {
        mcp_progress_token_free(token);
      }
    }
  } else if (type == MCP_JSON_TYPE_NUMBER) {
    double num = mcp_json_get_number(json);
    mcp_progress_token_t token =
        mcp_progress_token_create_number(static_cast<int64_t>(num));
    if (token) {
      result = static_cast<mcp_progress_token_t*>(
          mcp_malloc(sizeof(mcp_progress_token_t)));
      if (result) {
        *result = token;
      } else {
        mcp_progress_token_free(token);
      }
    }
  }

  if (result) {
  }
  return result;
}

// ============================================================================
// Content Block JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t
mcp_content_block_to_json(const mcp_content_block_t* block) MCP_NOEXCEPT {
  if (!block)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  mcp_content_block_type_t type = mcp_content_block_get_type(*block);

  if (type == MCP_CONTENT_BLOCK_TYPE_TEXT) {
    mcp_json_object_set(obj, "type", mcp_json_create_string("text"));
    const char* text = mcp_content_block_get_text(*block);
    mcp_json_object_set(obj, "text", mcp_json_create_string(text ? text : ""));
  } else if (type == MCP_CONTENT_BLOCK_TYPE_IMAGE) {
    mcp_json_object_set(obj, "type", mcp_json_create_string("image"));
    const char* data = nullptr;
    const char* mime_type = nullptr;
    mcp_content_block_get_image(*block, &data, &mime_type);
    if (data) {
      mcp_json_object_set(obj, "data", mcp_json_create_string(data));
    }
    if (mime_type) {
      mcp_json_object_set(obj, "mimeType", mcp_json_create_string(mime_type));
    }
  }

  return obj;
}

MCP_API mcp_content_block_t* mcp_content_block_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t type_val = mcp_json_object_get(json, "type");
  if (!type_val)
    return nullptr;

  const char* type_str = mcp_json_get_string(type_val);
  if (!type_str)
    return nullptr;

  mcp_content_block_t* result = nullptr;
  mcp_content_block_t block = nullptr;

  if (std::strcmp(type_str, "text") == 0) {
    mcp_json_value_t text_val = mcp_json_object_get(json, "text");
    const char* text = text_val ? mcp_json_get_string(text_val) : "";
    block = mcp_content_block_create_text(text);
  } else if (std::strcmp(type_str, "image") == 0) {
    mcp_json_value_t data_val = mcp_json_object_get(json, "data");
    mcp_json_value_t mime_val = mcp_json_object_get(json, "mimeType");
    const char* data = data_val ? mcp_json_get_string(data_val) : "";
    const char* mime = mime_val ? mcp_json_get_string(mime_val) : "image/png";
    block = mcp_content_block_create_image(data, mime);
  }

  if (block) {
    result = static_cast<mcp_content_block_t*>(
        mcp_malloc(sizeof(mcp_content_block_t)));
    if (result) {
      *result = block;
    } else {
      mcp_content_block_free(block);
    }
  }

  return result;
}

// ============================================================================
// Tool JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t mcp_tool_to_json(const mcp_tool_t* tool) MCP_NOEXCEPT {
  if (!tool)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  const char* name = mcp_tool_get_name(*tool);
  const char* description = mcp_tool_get_description(*tool);

  if (name) {
    mcp_json_object_set(obj, "name", mcp_json_create_string(name));
  }
  if (description) {
    mcp_json_object_set(obj, "description",
                        mcp_json_create_string(description));
  }

  // Note: Input schema serialization would be added here if needed

  return obj;
}

MCP_API mcp_tool_t* mcp_tool_from_json(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t name_val = mcp_json_object_get(json, "name");
  mcp_json_value_t desc_val = mcp_json_object_get(json, "description");

  const char* name = name_val ? mcp_json_get_string(name_val) : "";
  const char* description = desc_val ? mcp_json_get_string(desc_val) : "";

  mcp_tool_t tool = mcp_tool_create(name, description);
  if (!tool)
    return nullptr;

  mcp_tool_t* result = static_cast<mcp_tool_t*>(mcp_malloc(sizeof(mcp_tool_t)));
  if (result) {
    *result = tool;
  } else {
    mcp_tool_free(tool);
  }

  return result;
}

// ============================================================================
// Prompt JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t mcp_prompt_to_json(const mcp_prompt_t* prompt)
    MCP_NOEXCEPT {
  if (!prompt)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  const char* name = mcp_prompt_get_name(*prompt);
  const char* description = mcp_prompt_get_description(*prompt);

  if (name) {
    mcp_json_object_set(obj, "name", mcp_json_create_string(name));
  }
  if (description) {
    mcp_json_object_set(obj, "description",
                        mcp_json_create_string(description));
  }

  // Add arguments array
  size_t arg_count = mcp_prompt_get_argument_count(*prompt);
  if (arg_count > 0) {
    mcp_json_value_t args_array = mcp_json_create_array();
    // Note: Would iterate through arguments here if API provided access
    mcp_json_object_set(obj, "arguments", args_array);
  }

  return obj;
}

MCP_API mcp_prompt_t* mcp_prompt_from_json(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t name_val = mcp_json_object_get(json, "name");
  mcp_json_value_t desc_val = mcp_json_object_get(json, "description");

  const char* name = name_val ? mcp_json_get_string(name_val) : "";
  const char* description = desc_val ? mcp_json_get_string(desc_val) : "";

  mcp_prompt_t prompt = mcp_prompt_create(name, description);
  if (!prompt)
    return nullptr;

  // Parse arguments if present
  mcp_json_value_t args_val = mcp_json_object_get(json, "arguments");
  if (args_val && mcp_json_get_type(args_val) == MCP_JSON_TYPE_ARRAY) {
    size_t arg_count = mcp_json_array_size(args_val);
    for (size_t i = 0; i < arg_count; ++i) {
      mcp_json_value_t arg = mcp_json_array_get(args_val, i);
      if (arg && mcp_json_get_type(arg) == MCP_JSON_TYPE_OBJECT) {
        mcp_json_value_t arg_name_val = mcp_json_object_get(arg, "name");
        mcp_json_value_t arg_desc_val = mcp_json_object_get(arg, "description");
        mcp_json_value_t arg_req_val = mcp_json_object_get(arg, "required");

        const char* arg_name =
            arg_name_val ? mcp_json_get_string(arg_name_val) : "";
        const char* arg_desc =
            arg_desc_val ? mcp_json_get_string(arg_desc_val) : "";
        mcp_bool_t required =
            arg_req_val ? mcp_json_get_bool(arg_req_val) : MCP_FALSE;

        mcp_prompt_add_argument(prompt, arg_name, arg_desc, required);
      }
    }
  }

  mcp_prompt_t* result =
      static_cast<mcp_prompt_t*>(mcp_malloc(sizeof(mcp_prompt_t)));
  if (result) {
    *result = prompt;
  } else {
    mcp_prompt_free(prompt);
  }

  return result;
}

// ============================================================================
// Message JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t mcp_message_to_json(const mcp_message_t* message)
    MCP_NOEXCEPT {
  if (!message)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  const char* role = mcp_message_get_role(*message);
  if (role) {
    mcp_json_object_set(obj, "role", mcp_json_create_string(role));
  }

  // Add content array
  size_t content_count = mcp_message_get_content_count(*message);
  if (content_count > 0) {
    mcp_json_value_t content_array = mcp_json_create_array();
    // Note: Would iterate through content blocks here if API provided access
    mcp_json_object_set(obj, "content", content_array);
  }

  return obj;
}

MCP_API mcp_message_t* mcp_message_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t role_val = mcp_json_object_get(json, "role");
  const char* role = role_val ? mcp_json_get_string(role_val) : "user";

  mcp_message_t message = mcp_message_create(role);
  if (!message)
    return nullptr;

  // Parse content array if present
  mcp_json_value_t content_val = mcp_json_object_get(json, "content");
  if (content_val && mcp_json_get_type(content_val) == MCP_JSON_TYPE_ARRAY) {
    size_t content_count = mcp_json_array_size(content_val);
    for (size_t i = 0; i < content_count; ++i) {
      mcp_json_value_t block_json = mcp_json_array_get(content_val, i);
      mcp_content_block_t* block = mcp_content_block_from_json(block_json);
      if (block) {
        mcp_message_add_content(message, *block);
        mcp_content_block_free(*block);
        mcp_free(block);
      }
    }
  }

  mcp_message_t* result =
      static_cast<mcp_message_t*>(mcp_malloc(sizeof(mcp_message_t)));
  if (result) {
    *result = message;
  } else {
    mcp_message_free(message);
  }

  return result;
}

// ============================================================================
// Error JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t
mcp_jsonrpc_error_to_json(const mcp_jsonrpc_error_t* error) MCP_NOEXCEPT {
  if (!error)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  int32_t code = mcp_error_get_code(*error);
  const char* message = mcp_error_get_message(*error);
  const char* data = mcp_error_get_data(*error);

  mcp_json_object_set(obj, "code", mcp_json_create_number(code));
  if (message) {
    mcp_json_object_set(obj, "message", mcp_json_create_string(message));
  }
  if (data) {
    // Parse data as JSON if possible, otherwise treat as string
    mcp_json_value_t data_json = mcp_json_parse(data);
    if (data_json) {
      mcp_json_object_set(obj, "data", data_json);
    } else {
      mcp_json_object_set(obj, "data", mcp_json_create_string(data));
    }
  }

  return obj;
}

MCP_API mcp_jsonrpc_error_t* mcp_jsonrpc_error_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t code_val = mcp_json_object_get(json, "code");
  mcp_json_value_t message_val = mcp_json_object_get(json, "message");

  int32_t code =
      code_val ? static_cast<int32_t>(mcp_json_get_number(code_val)) : 0;
  const char* message = message_val ? mcp_json_get_string(message_val) : "";

  mcp_error_t error = mcp_error_create(code, message);
  if (!error)
    return nullptr;

  // Set data if present
  mcp_json_value_t data_val = mcp_json_object_get(json, "data");
  if (data_val) {
    char* data_str = mcp_json_stringify(data_val);
    if (data_str) {
      mcp_error_set_data(error, data_str);
      mcp_free(data_str);
    }
  }

  mcp_jsonrpc_error_t* result = static_cast<mcp_jsonrpc_error_t*>(
      mcp_malloc(sizeof(mcp_jsonrpc_error_t)));
  if (result) {
    *result = error;
  } else {
    mcp_error_free(error);
  }

  return result;
}

// ============================================================================
// JSON-RPC Request/Response/Notification JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t
mcp_jsonrpc_request_to_json(const mcp_jsonrpc_request_t* req) MCP_NOEXCEPT {
  if (!req)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  mcp_json_object_set(obj, "jsonrpc", mcp_json_create_string("2.0"));

  // Add method
  const char* method = mcp_jsonrpc_request_get_method(*req);
  if (method) {
    mcp_json_object_set(obj, "method", mcp_json_create_string(method));
  }

  // Add id
  mcp_request_id_t id = mcp_jsonrpc_request_get_id(*req);
  if (id) {
    mcp_json_value_t id_json = mcp_request_id_to_json(&id);
    if (id_json) {
      mcp_json_object_set(obj, "id", id_json);
    }
  }

  // Add params if present
  const char* params_json = mcp_jsonrpc_request_get_params(*req);
  if (params_json) {
    mcp_json_value_t params = mcp_json_parse(params_json);
    if (params) {
      mcp_json_object_set(obj, "params", params);
    }
  }

  return obj;
}

MCP_API mcp_jsonrpc_request_t* mcp_jsonrpc_request_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  // Get jsonrpc version (default to 2.0)
  mcp_json_value_t jsonrpc_val = mcp_json_object_get(json, "jsonrpc");
  const char* jsonrpc = jsonrpc_val ? mcp_json_get_string(jsonrpc_val) : "2.0";

  // Get method
  mcp_json_value_t method_val = mcp_json_object_get(json, "method");
  const char* method = method_val ? mcp_json_get_string(method_val) : "";

  mcp_jsonrpc_request_t request = mcp_jsonrpc_request_create(jsonrpc, method);
  if (!request)
    return nullptr;

  // Set id if present
  mcp_json_value_t id_val = mcp_json_object_get(json, "id");
  if (id_val) {
    mcp_request_id_t* id = mcp_request_id_from_json(id_val);
    if (id) {
      mcp_jsonrpc_request_set_id(request, *id);
      // Note: request takes ownership of *id
      mcp_free(id);  // Only free the wrapper, not the id itself
    }
  }

  // Set params if present
  mcp_json_value_t params_val = mcp_json_object_get(json, "params");
  if (params_val) {
    char* params_str = mcp_json_stringify(params_val);
    if (params_str) {
      mcp_jsonrpc_request_set_params(request, params_str);
      mcp_free(params_str);
    }
  }

  mcp_jsonrpc_request_t* result = static_cast<mcp_jsonrpc_request_t*>(
      mcp_malloc(sizeof(mcp_jsonrpc_request_t)));
  if (result) {
    *result = request;
  } else {
    mcp_jsonrpc_request_free(request);
  }

  return result;
}

MCP_API mcp_json_value_t
mcp_jsonrpc_response_to_json(const mcp_jsonrpc_response_t* resp) MCP_NOEXCEPT {
  if (!resp)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  mcp_json_object_set(obj, "jsonrpc", mcp_json_create_string("2.0"));

  // Add id
  mcp_request_id_t id = mcp_jsonrpc_response_get_id(*resp);
  if (id) {
    mcp_json_value_t id_json = mcp_request_id_to_json(&id);
    if (id_json) {
      mcp_json_object_set(obj, "id", id_json);
    }
  }

  // Add result or error
  mcp_error_t error = mcp_jsonrpc_response_get_error(*resp);
  if (error) {
    mcp_json_value_t error_json = mcp_jsonrpc_error_to_json(&error);
    if (error_json) {
      mcp_json_object_set(obj, "error", error_json);
    }
  } else {
    const char* result_json = mcp_jsonrpc_response_get_result(*resp);
    if (result_json) {
      mcp_json_value_t result = mcp_json_parse(result_json);
      if (result) {
        mcp_json_object_set(obj, "result", result);
      }
    }
  }

  return obj;
}

MCP_API mcp_jsonrpc_response_t* mcp_jsonrpc_response_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  // Get jsonrpc version (default to 2.0)
  mcp_json_value_t jsonrpc_val = mcp_json_object_get(json, "jsonrpc");
  const char* jsonrpc = jsonrpc_val ? mcp_json_get_string(jsonrpc_val) : "2.0";

  mcp_jsonrpc_response_t response = mcp_jsonrpc_response_create(jsonrpc);
  if (!response)
    return nullptr;

  // Set id if present
  mcp_json_value_t id_val = mcp_json_object_get(json, "id");
  if (id_val) {
    mcp_request_id_t* id = mcp_request_id_from_json(id_val);
    if (id) {
      mcp_jsonrpc_response_set_id(response, *id);
      // Note: response takes ownership of *id
      mcp_free(id);  // Only free the wrapper, not the id itself
    }
  }

  // Set result or error
  mcp_json_value_t error_val = mcp_json_object_get(json, "error");
  if (error_val) {
    mcp_jsonrpc_error_t* error = mcp_jsonrpc_error_from_json(error_val);
    if (error) {
      mcp_jsonrpc_response_set_error(response, *error);
      // Note: response takes ownership of *error
      mcp_free(error);  // Only free the wrapper, not the error itself
    }
  } else {
    mcp_json_value_t result_val = mcp_json_object_get(json, "result");
    if (result_val) {
      char* result_str = mcp_json_stringify(result_val);
      if (result_str) {
        mcp_jsonrpc_response_set_result(response, result_str);
        mcp_free(result_str);
      }
    }
  }

  mcp_jsonrpc_response_t* result = static_cast<mcp_jsonrpc_response_t*>(
      mcp_malloc(sizeof(mcp_jsonrpc_response_t)));
  if (result) {
    *result = response;
  } else {
    mcp_jsonrpc_response_free(response);
  }

  return result;
}

MCP_API mcp_json_value_t mcp_jsonrpc_notification_to_json(
    const mcp_jsonrpc_notification_t* notif) MCP_NOEXCEPT {
  if (!notif)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  mcp_json_object_set(obj, "jsonrpc", mcp_json_create_string("2.0"));

  // Add method
  const char* method = mcp_jsonrpc_notification_get_method(*notif);
  if (method) {
    mcp_json_object_set(obj, "method", mcp_json_create_string(method));
  }

  // Add params if present
  const char* params_json = mcp_jsonrpc_notification_get_params(*notif);
  if (params_json) {
    mcp_json_value_t params = mcp_json_parse(params_json);
    if (params) {
      mcp_json_object_set(obj, "params", params);
    }
  }

  return obj;
}

MCP_API mcp_jsonrpc_notification_t* mcp_jsonrpc_notification_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  // Get jsonrpc version (default to 2.0)
  mcp_json_value_t jsonrpc_val = mcp_json_object_get(json, "jsonrpc");
  const char* jsonrpc = jsonrpc_val ? mcp_json_get_string(jsonrpc_val) : "2.0";

  // Get method
  mcp_json_value_t method_val = mcp_json_object_get(json, "method");
  const char* method = method_val ? mcp_json_get_string(method_val) : "";

  mcp_jsonrpc_notification_t notification =
      mcp_jsonrpc_notification_create(jsonrpc, method);
  if (!notification)
    return nullptr;

  // Set params if present
  mcp_json_value_t params_val = mcp_json_object_get(json, "params");
  if (params_val) {
    char* params_str = mcp_json_stringify(params_val);
    if (params_str) {
      mcp_jsonrpc_notification_set_params(notification, params_str);
      mcp_free(params_str);
    }
  }

  mcp_jsonrpc_notification_t* result = static_cast<mcp_jsonrpc_notification_t*>(
      mcp_malloc(sizeof(mcp_jsonrpc_notification_t)));
  if (result) {
    *result = notification;
  } else {
    mcp_jsonrpc_notification_free(notification);
  }

  return result;
}

// ============================================================================
// Initialize Request/Response JSON Conversion
// ============================================================================

MCP_API mcp_json_value_t mcp_initialize_request_to_json(
    const mcp_initialize_request_t* req) MCP_NOEXCEPT {
  if (!req)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  // Add protocol version
  const char* version = mcp_initialize_request_get_protocol_version(*req);
  if (version) {
    mcp_json_object_set(obj, "protocolVersion",
                        mcp_json_create_string(version));
  }

  // Add client info - construct from the name and version
  const char* client_name = mcp_initialize_request_get_client_name(*req);
  const char* client_version = mcp_initialize_request_get_client_version(*req);
  if (client_name && client_version) {
    mcp_json_value_t info_json = mcp_json_create_object();
    if (info_json) {
      mcp_json_object_set(info_json, "name",
                          mcp_json_create_string(client_name));
      mcp_json_object_set(info_json, "version",
                          mcp_json_create_string(client_version));
      mcp_json_object_set(obj, "clientInfo", info_json);
    }
  }

  // TODO: Add capabilities when API is available
  // The current API doesn't have mcp_initialize_request_get_capabilities

  return obj;
}

MCP_API mcp_initialize_request_t* mcp_initialize_request_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  // Get protocol version
  mcp_json_value_t version_val = mcp_json_object_get(json, "protocolVersion");
  const char* protocol_version =
      version_val ? mcp_json_get_string(version_val) : "1.0";

  // Get client info for name and version
  const char* client_name = "unknown";
  const char* client_version = "0.0.0";

  mcp_json_value_t info_val = mcp_json_object_get(json, "clientInfo");
  if (info_val && mcp_json_get_type(info_val) == MCP_JSON_TYPE_OBJECT) {
    mcp_json_value_t name_val = mcp_json_object_get(info_val, "name");
    if (name_val) {
      const char* name = mcp_json_get_string(name_val);
      if (name)
        client_name = name;
    }

    mcp_json_value_t ver_val = mcp_json_object_get(info_val, "version");
    if (ver_val) {
      const char* ver = mcp_json_get_string(ver_val);
      if (ver)
        client_version = ver;
    }
  }

  // Create the request with all 3 required parameters
  mcp_initialize_request_t request = mcp_initialize_request_create(
      protocol_version, client_name, client_version);
  if (!request)
    return nullptr;

  // Note: The current API doesn't have setters for capabilities
  // This would need to be added to the type implementation if needed

  mcp_initialize_request_t* result = static_cast<mcp_initialize_request_t*>(
      mcp_malloc(sizeof(mcp_initialize_request_t)));
  if (result) {
    *result = request;
  } else {
    mcp_initialize_request_free(request);
  }

  return result;
}

MCP_API mcp_json_value_t mcp_initialize_result_to_json(
    const mcp_initialize_result_t* result) MCP_NOEXCEPT {
  if (!result)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  // Add protocol version
  const char* version = mcp_initialize_result_get_protocol_version(*result);
  if (version) {
    mcp_json_object_set(obj, "protocolVersion",
                        mcp_json_create_string(version));
  }

  // Add server info if present
  mcp_implementation_t server_info =
      mcp_initialize_result_get_server_info(*result);
  if (server_info) {
    mcp_json_value_t info_json = mcp_implementation_to_json(&server_info);
    if (info_json) {
      mcp_json_object_set(obj, "serverInfo", info_json);
    }
  }

  // Add capabilities if present
  mcp_server_capabilities_t caps =
      mcp_initialize_result_get_capabilities(*result);
  if (caps) {
    mcp_json_value_t caps_json = mcp_server_capabilities_to_json(&caps);
    if (caps_json) {
      mcp_json_object_set(obj, "capabilities", caps_json);
    }
  }

  return obj;
}

MCP_API mcp_initialize_result_t* mcp_initialize_result_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  // Get protocol version
  mcp_json_value_t version_val = mcp_json_object_get(json, "protocolVersion");
  const char* protocol_version =
      version_val ? mcp_json_get_string(version_val) : "1.0";

  // Create the result with protocol version
  mcp_initialize_result_t init_result =
      mcp_initialize_result_create(protocol_version);
  if (!init_result)
    return nullptr;

  // Set server info if present
  mcp_json_value_t info_val = mcp_json_object_get(json, "serverInfo");
  if (info_val && mcp_json_get_type(info_val) == MCP_JSON_TYPE_OBJECT) {
    mcp_implementation_t* impl = mcp_implementation_from_json(info_val);
    if (impl) {
      mcp_initialize_result_set_server_info(init_result, *impl);
      mcp_implementation_free(
          *impl);  // server_info doesn't take ownership, need to free
      mcp_free(impl);
    }
  }

  // Set capabilities if present
  mcp_json_value_t caps_val = mcp_json_object_get(json, "capabilities");
  if (caps_val) {
    mcp_server_capabilities_t* caps =
        mcp_server_capabilities_from_json(caps_val);
    if (caps) {
      mcp_initialize_result_set_capabilities(init_result, *caps);
      mcp_server_capabilities_free(
          *caps);  // capabilities doesn't take ownership, need to free
      mcp_free(caps);
    }
  }

  mcp_initialize_result_t* result_ptr = static_cast<mcp_initialize_result_t*>(
      mcp_malloc(sizeof(mcp_initialize_result_t)));
  if (result_ptr) {
    *result_ptr = init_result;
  } else {
    mcp_initialize_result_free(init_result);
  }

  return result_ptr;
}

// ============================================================================
// Other Type JSON Conversions
// ============================================================================

MCP_API mcp_json_value_t mcp_role_to_json(mcp_role_t role) MCP_NOEXCEPT {
  const char* role_str = "";
  switch (role) {
    case MCP_ROLE_USER:
      role_str = "user";
      break;
    case MCP_ROLE_ASSISTANT:
      role_str = "assistant";
      break;
    default:
      role_str = "unknown";
      break;
  }
  return mcp_json_create_string(role_str);
}

MCP_API mcp_role_t mcp_role_from_json(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_STRING) {
    return MCP_ROLE_USER;
  }

  const char* role_str = mcp_json_get_string(json);
  if (!role_str)
    return MCP_ROLE_USER;

  if (std::strcmp(role_str, "assistant") == 0) {
    return MCP_ROLE_ASSISTANT;
  }
  return MCP_ROLE_USER;
}

MCP_API mcp_json_value_t mcp_logging_level_to_json(mcp_logging_level_t level)
    MCP_NOEXCEPT {
  const char* level_str = "";
  switch (level) {
    case MCP_LOG_DEBUG:
      level_str = "debug";
      break;
    case MCP_LOG_INFO:
      level_str = "info";
      break;
    case MCP_LOG_WARNING:
      level_str = "warning";
      break;
    case MCP_LOG_ERROR:
      level_str = "error";
      break;
    default:
      level_str = "info";
      break;
  }
  return mcp_json_create_string(level_str);
}

MCP_API mcp_logging_level_t mcp_logging_level_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_STRING) {
    return MCP_LOG_INFO;
  }

  const char* level_str = mcp_json_get_string(json);
  if (!level_str)
    return MCP_LOG_INFO;

  if (std::strcmp(level_str, "debug") == 0)
    return MCP_LOG_DEBUG;
  if (std::strcmp(level_str, "warning") == 0)
    return MCP_LOG_WARNING;
  if (std::strcmp(level_str, "error") == 0)
    return MCP_LOG_ERROR;
  return MCP_LOG_INFO;
}

MCP_API mcp_json_value_t mcp_resource_to_json(const mcp_resource_t* resource)
    MCP_NOEXCEPT {
  if (!resource)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  const char* uri = mcp_resource_get_uri(*resource);
  const char* name = mcp_resource_get_name(*resource);
  const char* description = mcp_resource_get_description(*resource);
  const char* mime_type = mcp_resource_get_mime_type(*resource);

  if (uri) {
    mcp_json_object_set(obj, "uri", mcp_json_create_string(uri));
  }
  if (name) {
    mcp_json_object_set(obj, "name", mcp_json_create_string(name));
  }
  if (description) {
    mcp_json_object_set(obj, "description",
                        mcp_json_create_string(description));
  }
  if (mime_type) {
    mcp_json_object_set(obj, "mimeType", mcp_json_create_string(mime_type));
  }

  return obj;
}

MCP_API mcp_resource_t* mcp_resource_from_json(mcp_json_value_t json)
    MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t uri_val = mcp_json_object_get(json, "uri");
  mcp_json_value_t name_val = mcp_json_object_get(json, "name");

  const char* uri = uri_val ? mcp_json_get_string(uri_val) : "";
  const char* name = name_val ? mcp_json_get_string(name_val) : "";

  mcp_resource_t resource = mcp_resource_create(uri, name);
  if (!resource)
    return nullptr;

  // Set optional fields
  mcp_json_value_t desc_val = mcp_json_object_get(json, "description");
  if (desc_val) {
    const char* desc = mcp_json_get_string(desc_val);
    if (desc)
      mcp_resource_set_description(resource, desc);
  }

  mcp_json_value_t mime_val = mcp_json_object_get(json, "mimeType");
  if (mime_val) {
    const char* mime = mcp_json_get_string(mime_val);
    if (mime)
      mcp_resource_set_mime_type(resource, mime);
  }

  mcp_resource_t* result =
      static_cast<mcp_resource_t*>(mcp_malloc(sizeof(mcp_resource_t)));
  if (result) {
    *result = resource;
  } else {
    mcp_resource_free(resource);
  }

  return result;
}

MCP_API mcp_json_value_t
mcp_implementation_to_json(const mcp_implementation_t* impl) MCP_NOEXCEPT {
  if (!impl)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  const char* name = mcp_implementation_get_name(*impl);
  const char* version = mcp_implementation_get_version(*impl);

  if (name) {
    mcp_json_object_set(obj, "name", mcp_json_create_string(name));
  }
  if (version) {
    mcp_json_object_set(obj, "version", mcp_json_create_string(version));
  }

  return obj;
}

MCP_API mcp_implementation_t* mcp_implementation_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_json_value_t name_val = mcp_json_object_get(json, "name");
  mcp_json_value_t version_val = mcp_json_object_get(json, "version");

  const char* name = name_val ? mcp_json_get_string(name_val) : "";
  const char* version =
      version_val ? mcp_json_get_string(version_val) : "1.0.0";

  mcp_implementation_t impl = mcp_implementation_create(name, version);
  if (!impl)
    return nullptr;

  mcp_implementation_t* result = static_cast<mcp_implementation_t*>(
      mcp_malloc(sizeof(mcp_implementation_t)));
  if (result) {
    *result = impl;
  } else {
    mcp_implementation_free(impl);
  }

  return result;
}

MCP_API mcp_json_value_t mcp_client_capabilities_to_json(
    const mcp_client_capabilities_t* caps) MCP_NOEXCEPT {
  if (!caps)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  // Add roots capability
  if (mcp_client_capabilities_has_roots(*caps)) {
    mcp_json_value_t roots_obj = mcp_json_create_object();
    mcp_json_object_set(roots_obj, "listChanged", mcp_json_create_bool(true));
    mcp_json_object_set(obj, "roots", roots_obj);
  }

  // Add sampling capability
  if (mcp_client_capabilities_has_sampling(*caps)) {
    mcp_json_value_t sampling_obj = mcp_json_create_object();
    mcp_json_object_set(obj, "sampling", sampling_obj);
  }

  return obj;
}

MCP_API mcp_client_capabilities_t* mcp_client_capabilities_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_client_capabilities_t caps = mcp_client_capabilities_create();
  if (!caps)
    return nullptr;

  // Check for roots
  mcp_json_value_t roots_val = mcp_json_object_get(json, "roots");
  if (roots_val) {
    mcp_client_capabilities_set_roots(caps, true);
  }

  // Check for sampling
  mcp_json_value_t sampling_val = mcp_json_object_get(json, "sampling");
  if (sampling_val) {
    mcp_client_capabilities_set_sampling(caps, true);
  }

  mcp_client_capabilities_t* result = static_cast<mcp_client_capabilities_t*>(
      mcp_malloc(sizeof(mcp_client_capabilities_t)));
  if (result) {
    *result = caps;
  } else {
    mcp_client_capabilities_free(caps);
  }

  return result;
}

MCP_API mcp_json_value_t mcp_server_capabilities_to_json(
    const mcp_server_capabilities_t* caps) MCP_NOEXCEPT {
  if (!caps)
    return nullptr;

  mcp_json_value_t obj = mcp_json_create_object();
  if (!obj)
    return nullptr;

  // Add logging capability
  if (mcp_server_capabilities_has_logging(*caps)) {
    mcp_json_value_t logging_obj = mcp_json_create_object();
    mcp_json_object_set(obj, "logging", logging_obj);
  }

  // Add prompts capability
  if (mcp_server_capabilities_has_prompts(*caps)) {
    mcp_json_value_t prompts_obj = mcp_json_create_object();
    mcp_json_object_set(prompts_obj, "listChanged", mcp_json_create_bool(true));
    mcp_json_object_set(obj, "prompts", prompts_obj);
  }

  // Add resources capability
  if (mcp_server_capabilities_has_resources(*caps)) {
    mcp_json_value_t resources_obj = mcp_json_create_object();
    mcp_json_object_set(resources_obj, "subscribe", mcp_json_create_bool(true));
    mcp_json_object_set(resources_obj, "listChanged",
                        mcp_json_create_bool(true));
    mcp_json_object_set(obj, "resources", resources_obj);
  }

  // Add tools capability
  if (mcp_server_capabilities_has_tools(*caps)) {
    mcp_json_value_t tools_obj = mcp_json_create_object();
    mcp_json_object_set(tools_obj, "listChanged", mcp_json_create_bool(true));
    mcp_json_object_set(obj, "tools", tools_obj);
  }

  return obj;
}

MCP_API mcp_server_capabilities_t* mcp_server_capabilities_from_json(
    mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_OBJECT)
    return nullptr;

  mcp_server_capabilities_t caps = mcp_server_capabilities_create();
  if (!caps)
    return nullptr;

  // Check for logging
  mcp_json_value_t logging_val = mcp_json_object_get(json, "logging");
  if (logging_val) {
    mcp_server_capabilities_set_logging(caps, true);
  }

  // Check for prompts
  mcp_json_value_t prompts_val = mcp_json_object_get(json, "prompts");
  if (prompts_val) {
    mcp_server_capabilities_set_prompts(caps, true);
  }

  // Check for resources
  mcp_json_value_t resources_val = mcp_json_object_get(json, "resources");
  if (resources_val) {
    mcp_server_capabilities_set_resources(caps, true);
  }

  // Check for tools
  mcp_json_value_t tools_val = mcp_json_object_get(json, "tools");
  if (tools_val) {
    mcp_server_capabilities_set_tools(caps, true);
  }

  mcp_server_capabilities_t* result = static_cast<mcp_server_capabilities_t*>(
      mcp_malloc(sizeof(mcp_server_capabilities_t)));
  if (result) {
    *result = caps;
  } else {
    mcp_server_capabilities_free(caps);
  }

  return result;
}

MCP_API mcp_json_value_t mcp_string_to_json(mcp_string_t str) MCP_NOEXCEPT {
  if (!str.data)
    return mcp_json_create_null();
  return mcp_json_create_string(str.data);
}

MCP_API mcp_string_t mcp_string_from_json(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || mcp_json_get_type(json) != MCP_JSON_TYPE_STRING) {
    return mcp_string_t{nullptr, 0};
  }

  const char* str = mcp_json_get_string(json);
  if (!str) {
    return mcp_string_t{nullptr, 0};
  }

  size_t len = std::strlen(str);
  char* data = alloc_string(str);

  return mcp_string_t{data, len};
}

}  // extern "C"