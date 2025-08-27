/**
 * @file mcp_c_types_impl.cc
 * @brief Implementation of FFI-safe C API types for Gopher MCP library
 *
 * This file implements all the opaque types and their accessor functions
 * defined in mcp_c_types.h. Uses C++ internally but exposes pure C interface.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/types.h"

/* ============================================================================
 * Thread-local Error Handling
 * ============================================================================
 */

namespace {
thread_local mcp_error_info_t g_last_error = {};
thread_local bool g_has_error = false;

void set_last_error(mcp_result_t code,
                    const char* message,
                    const char* file = __FILE__,
                    int line = __LINE__) {
  g_last_error.code = code;
  if (message) {
    strncpy(g_last_error.message, message, sizeof(g_last_error.message) - 1);
    g_last_error.message[sizeof(g_last_error.message) - 1] = '\0';
  }
  if (file) {
    strncpy(g_last_error.file, file, sizeof(g_last_error.file) - 1);
    g_last_error.file[sizeof(g_last_error.file) - 1] = '\0';
  }
  g_last_error.line = line;
  g_has_error = true;
}

void clear_last_error() {
  g_has_error = false;
  memset(&g_last_error, 0, sizeof(g_last_error));
}
}  // namespace

extern "C" {

// Implemented in mcp_c_memory_impl.cc
// MCP_API const mcp_error_info_t* mcp_get_last_error(void) MCP_NOEXCEPT {
//     return g_has_error ? &g_last_error : nullptr;
// }

/* ============================================================================
 * Request ID Implementation (variant<string, int64_t>)
 * ============================================================================
 */

struct mcp_request_id_impl {
  std::variant<std::string, int64_t> value;

  mcp_request_id_impl(const char* str) : value(std::string(str)) {}
  mcp_request_id_impl(int64_t num) : value(num) {}
};

MCP_API mcp_request_id_t mcp_request_id_create_string(const char* str)
    MCP_NOEXCEPT {
  if (!str) {
    set_last_error(MCP_ERROR_NULL_POINTER, "String cannot be null");
    return nullptr;
  }
  try {
    return new mcp_request_id_impl(str);
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API mcp_request_id_t mcp_request_id_create_number(int64_t num)
    MCP_NOEXCEPT {
  try {
    return new mcp_request_id_impl(num);
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_request_id_free(mcp_request_id_t id) MCP_NOEXCEPT {
  delete id;
}

MCP_API mcp_bool_t mcp_request_id_is_string(mcp_request_id_t id) MCP_NOEXCEPT {
  if (!id)
    return MCP_FALSE;
  return std::holds_alternative<std::string>(id->value) ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_bool_t mcp_request_id_is_number(mcp_request_id_t id) MCP_NOEXCEPT {
  if (!id)
    return MCP_FALSE;
  return std::holds_alternative<int64_t>(id->value) ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_request_id_type_t mcp_request_id_get_type(mcp_request_id_t id)
    MCP_NOEXCEPT {
  if (!id)
    return MCP_REQUEST_ID_TYPE_STRING;
  return std::holds_alternative<std::string>(id->value)
             ? MCP_REQUEST_ID_TYPE_STRING
             : MCP_REQUEST_ID_TYPE_NUMBER;
}

MCP_API const char* mcp_request_id_get_string(mcp_request_id_t id)
    MCP_NOEXCEPT {
  if (!id || !std::holds_alternative<std::string>(id->value)) {
    return nullptr;
  }
  return std::get<std::string>(id->value).c_str();
}

MCP_API int64_t mcp_request_id_get_number(mcp_request_id_t id) MCP_NOEXCEPT {
  if (!id || !std::holds_alternative<int64_t>(id->value)) {
    return 0;
  }
  return std::get<int64_t>(id->value);
}

/* ============================================================================
 * Progress Token Implementation (variant<string, int64_t>)
 * ============================================================================
 */

struct mcp_progress_token_impl {
  std::variant<std::string, int64_t> value;

  mcp_progress_token_impl(const char* str) : value(std::string(str)) {}
  mcp_progress_token_impl(int64_t num) : value(num) {}
};

MCP_API mcp_progress_token_t mcp_progress_token_create_string(const char* str)
    MCP_NOEXCEPT {
  if (!str) {
    set_last_error(MCP_ERROR_NULL_POINTER, "String cannot be null");
    return nullptr;
  }
  try {
    return new mcp_progress_token_impl(str);
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API mcp_progress_token_t mcp_progress_token_create_number(int64_t num)
    MCP_NOEXCEPT {
  try {
    return new mcp_progress_token_impl(num);
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_progress_token_free(mcp_progress_token_t token) MCP_NOEXCEPT {
  delete token;
}

MCP_API mcp_progress_token_type_t
mcp_progress_token_get_type(mcp_progress_token_t token) MCP_NOEXCEPT {
  if (!token)
    return MCP_PROGRESS_TOKEN_TYPE_STRING;
  return std::holds_alternative<std::string>(token->value)
             ? MCP_PROGRESS_TOKEN_TYPE_STRING
             : MCP_PROGRESS_TOKEN_TYPE_NUMBER;
}

MCP_API const char* mcp_progress_token_get_string(mcp_progress_token_t token)
    MCP_NOEXCEPT {
  if (!token || !std::holds_alternative<std::string>(token->value)) {
    return nullptr;
  }
  return std::get<std::string>(token->value).c_str();
}

MCP_API int64_t mcp_progress_token_get_number(mcp_progress_token_t token)
    MCP_NOEXCEPT {
  if (!token || !std::holds_alternative<int64_t>(token->value)) {
    return 0;
  }
  return std::get<int64_t>(token->value);
}

/* ============================================================================
 * Content Block Implementation
 * ============================================================================
 */

struct mcp_content_block_impl {
  mcp_content_block_type_t type;
  std::variant<std::string,                          // text
               std::pair<std::string, std::string>,  // image (data, mimeType)
               mcp::ResourceContent                  // resource
               >
      content;

  mcp_content_block_impl(const char* text)
      : type(MCP_CONTENT_BLOCK_TYPE_TEXT), content(std::string(text)) {}

  mcp_content_block_impl(const char* data, const char* mime)
      : type(MCP_CONTENT_BLOCK_TYPE_IMAGE),
        content(std::make_pair(std::string(data), std::string(mime))) {}
};

MCP_API mcp_content_block_t mcp_content_block_create_text(const char* text)
    MCP_NOEXCEPT {
  if (!text) {
    set_last_error(MCP_ERROR_NULL_POINTER, "Text cannot be null");
    return nullptr;
  }
  try {
    return new mcp_content_block_impl(text);
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API mcp_content_block_t mcp_content_block_create_image(
    const char* data, const char* mime_type) MCP_NOEXCEPT {
  if (!data || !mime_type) {
    set_last_error(MCP_ERROR_NULL_POINTER,
                   "Image data and mime type cannot be null");
    return nullptr;
  }
  try {
    return new mcp_content_block_impl(data, mime_type);
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_content_block_free(mcp_content_block_t block) MCP_NOEXCEPT {
  delete block;
}

MCP_API mcp_content_block_type_t
mcp_content_block_get_type(mcp_content_block_t block) MCP_NOEXCEPT {
  if (!block)
    return MCP_CONTENT_BLOCK_TYPE_TEXT;
  return block->type;
}

MCP_API const char* mcp_content_block_get_text(mcp_content_block_t block)
    MCP_NOEXCEPT {
  if (!block || block->type != MCP_CONTENT_BLOCK_TYPE_TEXT) {
    return nullptr;
  }
  return std::get<std::string>(block->content).c_str();
}

MCP_API mcp_bool_t mcp_content_block_get_image(mcp_content_block_t block,
                                               const char** out_data,
                                               const char** out_mime_type)
    MCP_NOEXCEPT {
  if (!block || block->type != MCP_CONTENT_BLOCK_TYPE_IMAGE) {
    return MCP_FALSE;
  }
  auto& pair = std::get<std::pair<std::string, std::string>>(block->content);
  if (out_data)
    *out_data = pair.first.c_str();
  if (out_mime_type)
    *out_mime_type = pair.second.c_str();
  return MCP_TRUE;
}

/* ============================================================================
 * Tool Implementation
 * ============================================================================
 */

struct mcp_tool_input_schema_impl {
  std::string type;
  std::unordered_map<std::string, std::string> properties;
  std::vector<std::string> required;
};

struct mcp_tool_impl {
  std::string name;
  std::string description;
  std::unique_ptr<mcp_tool_input_schema_impl> input_schema;
};

MCP_API mcp_tool_t mcp_tool_create(const char* name,
                                   const char* description) MCP_NOEXCEPT {
  if (!name) {
    set_last_error(MCP_ERROR_NULL_POINTER, "Tool name cannot be null");
    return nullptr;
  }
  try {
    auto tool = new mcp_tool_impl;
    tool->name = name;
    if (description)
      tool->description = description;
    return tool;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_tool_free(mcp_tool_t tool) MCP_NOEXCEPT { delete tool; }

MCP_API const char* mcp_tool_get_name(mcp_tool_t tool) MCP_NOEXCEPT {
  if (!tool)
    return nullptr;
  return tool->name.c_str();
}

MCP_API const char* mcp_tool_get_description(mcp_tool_t tool) MCP_NOEXCEPT {
  if (!tool)
    return nullptr;
  return tool->description.empty() ? nullptr : tool->description.c_str();
}

MCP_API mcp_tool_input_schema_t mcp_tool_input_schema_create(void)
    MCP_NOEXCEPT {
  try {
    return new mcp_tool_input_schema_impl;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_tool_input_schema_free(mcp_tool_input_schema_t schema)
    MCP_NOEXCEPT {
  delete schema;
}

MCP_API void mcp_tool_input_schema_set_type(mcp_tool_input_schema_t schema,
                                            const char* type) MCP_NOEXCEPT {
  if (!schema || !type)
    return;
  schema->type = type;
}

MCP_API void mcp_tool_input_schema_add_property(mcp_tool_input_schema_t schema,
                                                const char* name,
                                                const char* json_schema)
    MCP_NOEXCEPT {
  if (!schema || !name || !json_schema)
    return;
  schema->properties[name] = json_schema;
}

MCP_API void mcp_tool_input_schema_add_required(mcp_tool_input_schema_t schema,
                                                const char* name) MCP_NOEXCEPT {
  if (!schema || !name)
    return;
  schema->required.push_back(name);
}

MCP_API const char* mcp_tool_input_schema_get_type(
    mcp_tool_input_schema_t schema) MCP_NOEXCEPT {
  if (!schema)
    return nullptr;
  return schema->type.empty() ? nullptr : schema->type.c_str();
}

MCP_API size_t mcp_tool_input_schema_get_property_count(
    mcp_tool_input_schema_t schema) MCP_NOEXCEPT {
  if (!schema)
    return 0;
  return schema->properties.size();
}

MCP_API size_t mcp_tool_input_schema_get_required_count(
    mcp_tool_input_schema_t schema) MCP_NOEXCEPT {
  if (!schema)
    return 0;
  return schema->required.size();
}

MCP_API void mcp_tool_set_input_schema(
    mcp_tool_t tool, mcp_tool_input_schema_t schema) MCP_NOEXCEPT {
  if (!tool || !schema)
    return;
  tool->input_schema.reset(schema);
}

/* ============================================================================
 * Prompt Implementation
 * ============================================================================
 */

struct mcp_prompt_argument_impl {
  std::string name;
  std::string description;
  bool required;
};

struct mcp_prompt_impl {
  std::string name;
  std::string description;
  std::vector<mcp_prompt_argument_impl> arguments;
};

MCP_API mcp_prompt_t mcp_prompt_create(const char* name,
                                       const char* description) MCP_NOEXCEPT {
  if (!name) {
    set_last_error(MCP_ERROR_NULL_POINTER, "Prompt name cannot be null");
    return nullptr;
  }
  try {
    auto prompt = new mcp_prompt_impl;
    prompt->name = name;
    if (description)
      prompt->description = description;
    return prompt;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_prompt_free(mcp_prompt_t prompt) MCP_NOEXCEPT {
  delete prompt;
}

MCP_API const char* mcp_prompt_get_name(mcp_prompt_t prompt) MCP_NOEXCEPT {
  if (!prompt)
    return nullptr;
  return prompt->name.c_str();
}

MCP_API const char* mcp_prompt_get_description(mcp_prompt_t prompt)
    MCP_NOEXCEPT {
  if (!prompt)
    return nullptr;
  return prompt->description.empty() ? nullptr : prompt->description.c_str();
}

MCP_API void mcp_prompt_add_argument(mcp_prompt_t prompt,
                                     const char* name,
                                     const char* description,
                                     mcp_bool_t required) MCP_NOEXCEPT {
  if (!prompt || !name)
    return;
  try {
    mcp_prompt_argument_impl arg;
    arg.name = name;
    if (description)
      arg.description = description;
    arg.required = (required == MCP_TRUE);
    prompt->arguments.push_back(std::move(arg));
  } catch (...) {
    // Silently fail on OOM
  }
}

MCP_API size_t mcp_prompt_get_argument_count(mcp_prompt_t prompt) MCP_NOEXCEPT {
  if (!prompt)
    return 0;
  return prompt->arguments.size();
}

/* ============================================================================
 * Error Implementation
 * ============================================================================
 */

struct mcp_error_impl {
  int32_t code;
  std::string message;
  std::string data;  // JSON string
};

MCP_API mcp_error_t mcp_error_create(int32_t code,
                                     const char* message) MCP_NOEXCEPT {
  if (!message) {
    set_last_error(MCP_ERROR_NULL_POINTER, "Error message cannot be null");
    return nullptr;
  }
  try {
    auto error = new mcp_error_impl;
    error->code = code;
    error->message = message;
    return error;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_error_free(mcp_error_t error) MCP_NOEXCEPT { delete error; }

MCP_API int32_t mcp_error_get_code(mcp_error_t error) MCP_NOEXCEPT {
  if (!error)
    return 0;
  return error->code;
}

MCP_API const char* mcp_error_get_message(mcp_error_t error) MCP_NOEXCEPT {
  if (!error)
    return nullptr;
  return error->message.c_str();
}

MCP_API void mcp_error_set_data(mcp_error_t error,
                                const char* json_data) MCP_NOEXCEPT {
  if (!error || !json_data)
    return;
  error->data = json_data;
}

MCP_API const char* mcp_error_get_data(mcp_error_t error) MCP_NOEXCEPT {
  if (!error || error->data.empty())
    return nullptr;
  return error->data.c_str();
}

/* ============================================================================
 * Message Implementation
 * ============================================================================
 */

struct mcp_message_impl {
  std::string role;
  std::vector<mcp_content_block_t> content;
};

MCP_API mcp_message_t mcp_message_create(const char* role) MCP_NOEXCEPT {
  if (!role) {
    set_last_error(MCP_ERROR_NULL_POINTER, "Message role cannot be null");
    return nullptr;
  }
  try {
    auto msg = new mcp_message_impl;
    msg->role = role;
    return msg;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_message_free(mcp_message_t message) MCP_NOEXCEPT {
  if (!message)
    return;
  // Free all content blocks
  for (auto block : message->content) {
    mcp_content_block_free(block);
  }
  delete message;
}

MCP_API const char* mcp_message_get_role(mcp_message_t message) MCP_NOEXCEPT {
  if (!message)
    return nullptr;
  return message->role.c_str();
}

MCP_API void mcp_message_add_content(mcp_message_t message,
                                     mcp_content_block_t content) MCP_NOEXCEPT {
  if (!message || !content)
    return;
  try {
    message->content.push_back(content);
  } catch (...) {
    // Silently fail on OOM
  }
}

MCP_API size_t mcp_message_get_content_count(mcp_message_t message)
    MCP_NOEXCEPT {
  if (!message)
    return 0;
  return message->content.size();
}

MCP_API mcp_content_block_t mcp_message_get_content(mcp_message_t message,
                                                    size_t index) MCP_NOEXCEPT {
  if (!message || index >= message->content.size())
    return nullptr;
  return message->content[index];
}

/* ============================================================================
 * JSON-RPC Request Implementation
 * ============================================================================
 */

struct mcp_jsonrpc_request_impl {
  std::string jsonrpc;
  std::string method;
  std::unique_ptr<mcp_request_id_impl> id;
  std::string params;  // JSON string
};

MCP_API mcp_jsonrpc_request_t mcp_jsonrpc_request_create(
    const char* jsonrpc, const char* method) MCP_NOEXCEPT {
  if (!jsonrpc || !method) {
    set_last_error(MCP_ERROR_NULL_POINTER,
                   "JSONRPC version and method cannot be null");
    return nullptr;
  }
  try {
    auto req = new mcp_jsonrpc_request_impl;
    req->jsonrpc = jsonrpc;
    req->method = method;
    return req;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_jsonrpc_request_free(mcp_jsonrpc_request_t request)
    MCP_NOEXCEPT {
  delete request;
}

MCP_API const char* mcp_jsonrpc_request_get_jsonrpc(
    mcp_jsonrpc_request_t request) MCP_NOEXCEPT {
  if (!request)
    return nullptr;
  return request->jsonrpc.c_str();
}

MCP_API const char* mcp_jsonrpc_request_get_method(
    mcp_jsonrpc_request_t request) MCP_NOEXCEPT {
  if (!request)
    return nullptr;
  return request->method.c_str();
}

MCP_API void mcp_jsonrpc_request_set_id(mcp_jsonrpc_request_t request,
                                        mcp_request_id_t id) MCP_NOEXCEPT {
  if (!request || !id)
    return;
  request->id.reset(id);
}

MCP_API void mcp_jsonrpc_request_set_params(
    mcp_jsonrpc_request_t request, const char* json_params) MCP_NOEXCEPT {
  if (!request || !json_params)
    return;
  request->params = json_params;
}

/* ============================================================================
 * JSON-RPC Response Implementation
 * ============================================================================
 */

struct mcp_jsonrpc_response_impl {
  std::string jsonrpc;
  std::unique_ptr<mcp_request_id_impl> id;
  std::string result;  // JSON string
  std::unique_ptr<mcp_error_impl> error;
};

MCP_API mcp_jsonrpc_response_t mcp_jsonrpc_response_create(const char* jsonrpc)
    MCP_NOEXCEPT {
  if (!jsonrpc) {
    set_last_error(MCP_ERROR_NULL_POINTER, "JSONRPC version cannot be null");
    return nullptr;
  }
  try {
    auto resp = new mcp_jsonrpc_response_impl;
    resp->jsonrpc = jsonrpc;
    return resp;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_jsonrpc_response_free(mcp_jsonrpc_response_t response)
    MCP_NOEXCEPT {
  delete response;
}

MCP_API const char* mcp_jsonrpc_response_get_jsonrpc(
    mcp_jsonrpc_response_t response) MCP_NOEXCEPT {
  if (!response)
    return nullptr;
  return response->jsonrpc.c_str();
}

MCP_API void mcp_jsonrpc_response_set_id(mcp_jsonrpc_response_t response,
                                         mcp_request_id_t id) MCP_NOEXCEPT {
  if (!response || !id)
    return;
  response->id.reset(id);
}

MCP_API void mcp_jsonrpc_response_set_result(
    mcp_jsonrpc_response_t response, const char* json_result) MCP_NOEXCEPT {
  if (!response || !json_result)
    return;
  response->result = json_result;
  response->error.reset();  // Clear error if setting result
}

MCP_API void mcp_jsonrpc_response_set_error(mcp_jsonrpc_response_t response,
                                            mcp_error_t error) MCP_NOEXCEPT {
  if (!response || !error)
    return;
  response->error.reset(error);
  response->result.clear();  // Clear result if setting error
}

MCP_API const char* mcp_jsonrpc_response_get_result(
    mcp_jsonrpc_response_t response) MCP_NOEXCEPT {
  if (!response || response->result.empty())
    return nullptr;
  return response->result.c_str();
}

MCP_API mcp_error_t
mcp_jsonrpc_response_get_error(mcp_jsonrpc_response_t response) MCP_NOEXCEPT {
  if (!response)
    return nullptr;
  return response->error.get();
}

/* ============================================================================
 * Initialize Request/Response Implementation
 * ============================================================================
 */

struct mcp_initialize_request_impl {
  std::string protocol_version;
  std::string client_name;
  std::string client_version;
};

MCP_API mcp_initialize_request_t
mcp_initialize_request_create(const char* protocol_version,
                              const char* client_name,
                              const char* client_version) MCP_NOEXCEPT {
  if (!protocol_version || !client_name || !client_version) {
    set_last_error(MCP_ERROR_NULL_POINTER,
                   "Initialize request parameters cannot be null");
    return nullptr;
  }
  try {
    auto req = new mcp_initialize_request_impl;
    req->protocol_version = protocol_version;
    req->client_name = client_name;
    req->client_version = client_version;
    return req;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_initialize_request_free(mcp_initialize_request_t request)
    MCP_NOEXCEPT {
  delete request;
}

MCP_API const char* mcp_initialize_request_get_protocol_version(
    mcp_initialize_request_t request) MCP_NOEXCEPT {
  if (!request)
    return nullptr;
  return request->protocol_version.c_str();
}

MCP_API const char* mcp_initialize_request_get_client_name(
    mcp_initialize_request_t request) MCP_NOEXCEPT {
  if (!request)
    return nullptr;
  return request->client_name.c_str();
}

MCP_API const char* mcp_initialize_request_get_client_version(
    mcp_initialize_request_t request) MCP_NOEXCEPT {
  if (!request)
    return nullptr;
  return request->client_version.c_str();
}

struct mcp_initialize_response_impl {
  std::string protocol_version;
  std::string server_name;
  std::string server_version;
};

MCP_API mcp_initialize_response_t
mcp_initialize_response_create(const char* protocol_version,
                               const char* server_name,
                               const char* server_version) MCP_NOEXCEPT {
  if (!protocol_version || !server_name || !server_version) {
    set_last_error(MCP_ERROR_NULL_POINTER,
                   "Initialize response parameters cannot be null");
    return nullptr;
  }
  try {
    auto resp = new mcp_initialize_response_impl;
    resp->protocol_version = protocol_version;
    resp->server_name = server_name;
    resp->server_version = server_version;
    return resp;
  } catch (const std::exception& e) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
    return nullptr;
  }
}

MCP_API void mcp_initialize_response_free(mcp_initialize_response_t response)
    MCP_NOEXCEPT {
  delete response;
}

MCP_API const char* mcp_initialize_response_get_protocol_version(
    mcp_initialize_response_t response) MCP_NOEXCEPT {
  if (!response)
    return nullptr;
  return response->protocol_version.c_str();
}

MCP_API const char* mcp_initialize_response_get_server_name(
    mcp_initialize_response_t response) MCP_NOEXCEPT {
  if (!response)
    return nullptr;
  return response->server_name.c_str();
}

MCP_API const char* mcp_initialize_response_get_server_version(
    mcp_initialize_response_t response) MCP_NOEXCEPT {
  if (!response)
    return nullptr;
  return response->server_version.c_str();
}

/* ============================================================================
 * Collection Implementations - Moved to mcp_c_collections_impl.cc
 * ============================================================================
 */

#if 0  // Moved to mcp_c_collections_impl.cc
struct mcp_list_impl {
    mcp_type_id_t element_type;
    std::vector<void*> items;
};

MCP_API mcp_list_t mcp_list_create(mcp_type_id_t element_type) MCP_NOEXCEPT {
    try {
        auto list = new mcp_list_impl;
        list->element_type = element_type;
        return list;
    } catch (const std::exception& e) {
        set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
        return nullptr;
    }
}

MCP_API void mcp_list_free(mcp_list_t list) MCP_NOEXCEPT {
    delete list;
}

MCP_API mcp_result_t mcp_list_append(mcp_list_t list, void* item) MCP_NOEXCEPT {
    if (!list) return MCP_ERROR_NULL_POINTER;
    try {
        list->items.push_back(item);
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API size_t mcp_list_size(mcp_list_t list) MCP_NOEXCEPT {
    if (!list) return 0;
    return list->items.size();
}

MCP_API void* mcp_list_get(mcp_list_t list, size_t index) MCP_NOEXCEPT {
    if (!list || index >= list->items.size()) return nullptr;
    return list->items[index];
}

MCP_API mcp_result_t mcp_list_clear(mcp_list_t list) MCP_NOEXCEPT {
    if (!list) return MCP_ERROR_NULL_POINTER;
    list->items.clear();
    return MCP_OK;
}

struct mcp_map_impl {
    mcp_type_id_t value_type;
    std::unordered_map<std::string, void*> items;
};

MCP_API mcp_map_t mcp_map_create(mcp_type_id_t value_type) MCP_NOEXCEPT {
    try {
        auto map = new mcp_map_impl;
        map->value_type = value_type;
        return map;
    } catch (const std::exception& e) {
        set_last_error(MCP_ERROR_OUT_OF_MEMORY, e.what());
        return nullptr;
    }
}

MCP_API void mcp_map_free(mcp_map_t map) MCP_NOEXCEPT {
    delete map;
}

MCP_API mcp_result_t mcp_map_set(mcp_map_t map, const char* key, void* value) MCP_NOEXCEPT {
    if (!map || !key) return MCP_ERROR_NULL_POINTER;
    try {
        map->items[key] = value;
        return MCP_OK;
    } catch (...) {
        return MCP_ERROR_OUT_OF_MEMORY;
    }
}

MCP_API void* mcp_map_get(mcp_map_t map, const char* key) MCP_NOEXCEPT {
    if (!map || !key) return nullptr;
    auto it = map->items.find(key);
    return (it != map->items.end()) ? it->second : nullptr;
}

MCP_API mcp_bool_t mcp_map_has(mcp_map_t map, const char* key) MCP_NOEXCEPT {
    if (!map || !key) return MCP_FALSE;
    return map->items.find(key) != map->items.end() ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_result_t mcp_map_remove(mcp_map_t map, const char* key) MCP_NOEXCEPT {
    if (!map || !key) return MCP_ERROR_NULL_POINTER;
    map->items.erase(key);
    return MCP_OK;
}

MCP_API size_t mcp_map_size(mcp_map_t map) MCP_NOEXCEPT {
    if (!map) return 0;
    return map->items.size();
}

MCP_API mcp_result_t mcp_map_clear(mcp_map_t map) MCP_NOEXCEPT {
    if (!map) return MCP_ERROR_NULL_POINTER;
    map->items.clear();
    return MCP_OK;
}

/* ============================================================================
 * JSON Value Implementation (simplified)
 * ============================================================================ */

struct mcp_json_value_impl {
    std::variant<
        std::monostate,  // null
        bool,
        double,
        std::string,
        std::vector<mcp_json_value_t>,  // array
        std::unordered_map<std::string, mcp_json_value_t>  // object
    > value;
};

MCP_API mcp_json_value_t mcp_json_create_null(void) MCP_NOEXCEPT {
    try {
        auto json = new mcp_json_value_impl;
        json->value = std::monostate{};
        return json;
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_json_value_t mcp_json_create_bool(mcp_bool_t value) MCP_NOEXCEPT {
    try {
        auto json = new mcp_json_value_impl;
        json->value = (value == MCP_TRUE);
        return json;
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_json_value_t mcp_json_create_number(double value) MCP_NOEXCEPT {
    try {
        auto json = new mcp_json_value_impl;
        json->value = value;
        return json;
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_json_value_t mcp_json_create_string(const char* value) MCP_NOEXCEPT {
    if (!value) return nullptr;
    try {
        auto json = new mcp_json_value_impl;
        json->value = std::string(value);
        return json;
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_json_value_t mcp_json_create_array(void) MCP_NOEXCEPT {
    try {
        auto json = new mcp_json_value_impl;
        json->value = std::vector<mcp_json_value_t>{};
        return json;
    } catch (...) {
        return nullptr;
    }
}

MCP_API mcp_json_value_t mcp_json_create_object(void) MCP_NOEXCEPT {
    try {
        auto json = new mcp_json_value_impl;
        json->value = std::unordered_map<std::string, mcp_json_value_t>{};
        return json;
    } catch (...) {
        return nullptr;
    }
}

MCP_API void mcp_json_free(mcp_json_value_t json) MCP_NOEXCEPT {
    if (!json) return;
    
    // Recursively free nested values
    if (std::holds_alternative<std::vector<mcp_json_value_t>>(json->value)) {
        auto& arr = std::get<std::vector<mcp_json_value_t>>(json->value);
        for (auto item : arr) {
            mcp_json_free(item);
        }
    } else if (std::holds_alternative<std::unordered_map<std::string, mcp_json_value_t>>(json->value)) {
        auto& obj = std::get<std::unordered_map<std::string, mcp_json_value_t>>(json->value);
        for (auto& [key, val] : obj) {
            mcp_json_free(val);
        }
    }
    
    delete json;
}

// Note: mcp_json_release is implemented in mcp_c_collections_impl.cc

/* ============================================================================
 * Metadata Implementation
 * ============================================================================ */

struct mcp_metadata_impl {
    std::unordered_map<std::string, std::string> data;
};

MCP_API mcp_metadata_t mcp_metadata_create(void) MCP_NOEXCEPT {
    try {
        return new mcp_metadata_impl;
    } catch (...) {
        return nullptr;
    }
}

MCP_API void mcp_metadata_free(mcp_metadata_t metadata) MCP_NOEXCEPT {
    delete metadata;
}

MCP_API mcp_result_t mcp_metadata_from_json(mcp_metadata_t metadata, mcp_json_value_t json) MCP_NOEXCEPT {
    // Simplified - would need proper JSON parsing
    return MCP_ERROR_NOT_IMPLEMENTED;
}

MCP_API mcp_json_value_t mcp_metadata_to_json(mcp_metadata_t metadata) MCP_NOEXCEPT {
    // Simplified - would need proper JSON serialization
    return nullptr;
}

#endif  // End of moved collection/JSON implementations

/* ============================================================================
 * Resource Implementation
 * ============================================================================
 */

struct mcp_resource_impl {
  std::string uri;
  std::string name;
  std::string description;
  std::string mime_type;

  mcp_resource_impl(const char* u, const char* n)
      : uri(u ? u : ""), name(n ? n : "") {}
};

MCP_API mcp_resource_t mcp_resource_create(const char* uri,
                                           const char* name) MCP_NOEXCEPT {
  if (!uri || !name) {
    set_last_error(MCP_ERROR_INVALID_ARGUMENT, "URI and name are required");
    return nullptr;
  }
  try {
    return new mcp_resource_impl(uri, name);
  } catch (...) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, "Failed to allocate resource");
    return nullptr;
  }
}

MCP_API void mcp_resource_free(mcp_resource_t resource) MCP_NOEXCEPT {
  delete resource;
}

MCP_API const char* mcp_resource_get_uri(mcp_resource_t resource) MCP_NOEXCEPT {
  if (!resource)
    return nullptr;
  return resource->uri.c_str();
}

MCP_API const char* mcp_resource_get_name(mcp_resource_t resource)
    MCP_NOEXCEPT {
  if (!resource)
    return nullptr;
  return resource->name.c_str();
}

MCP_API void mcp_resource_set_description(
    mcp_resource_t resource, const char* description) MCP_NOEXCEPT {
  if (!resource)
    return;
  resource->description = description ? description : "";
}

MCP_API const char* mcp_resource_get_description(mcp_resource_t resource)
    MCP_NOEXCEPT {
  if (!resource)
    return nullptr;
  return resource->description.empty() ? nullptr
                                       : resource->description.c_str();
}

MCP_API void mcp_resource_set_mime_type(mcp_resource_t resource,
                                        const char* mime_type) MCP_NOEXCEPT {
  if (!resource)
    return;
  resource->mime_type = mime_type ? mime_type : "";
}

MCP_API const char* mcp_resource_get_mime_type(mcp_resource_t resource)
    MCP_NOEXCEPT {
  if (!resource)
    return nullptr;
  return resource->mime_type.empty() ? nullptr : resource->mime_type.c_str();
}

/* ============================================================================
 * Implementation (Server/Client Info) Implementation
 * ============================================================================
 */

struct mcp_implementation_impl {
  std::string name;
  std::string title;
  std::string version;

  mcp_implementation_impl(const char* n, const char* v)
      : name(n ? n : ""), version(v ? v : "") {}
};

MCP_API mcp_implementation_t
mcp_implementation_create(const char* name, const char* version) MCP_NOEXCEPT {
  if (!name || !version) {
    set_last_error(MCP_ERROR_INVALID_ARGUMENT, "Name and version are required");
    return nullptr;
  }
  try {
    return new mcp_implementation_impl(name, version);
  } catch (...) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY,
                   "Failed to allocate implementation");
    return nullptr;
  }
}

MCP_API void mcp_implementation_free(mcp_implementation_t impl) MCP_NOEXCEPT {
  delete impl;
}

MCP_API const char* mcp_implementation_get_name(mcp_implementation_t impl)
    MCP_NOEXCEPT {
  if (!impl)
    return nullptr;
  return impl->name.c_str();
}

MCP_API const char* mcp_implementation_get_version(mcp_implementation_t impl)
    MCP_NOEXCEPT {
  if (!impl)
    return nullptr;
  return impl->version.c_str();
}

MCP_API void mcp_implementation_set_title(mcp_implementation_t impl,
                                          const char* title) MCP_NOEXCEPT {
  if (!impl)
    return;
  impl->title = title ? title : "";
}

MCP_API const char* mcp_implementation_get_title(mcp_implementation_t impl)
    MCP_NOEXCEPT {
  if (!impl)
    return nullptr;
  return impl->title.empty() ? nullptr : impl->title.c_str();
}

/* ============================================================================
 * Capabilities Implementation
 * ============================================================================
 */

struct mcp_client_capabilities_impl {
  bool has_roots = false;
  bool has_sampling = false;
  bool roots_list_changed = false;
  // Add more capabilities as needed
};

MCP_API mcp_client_capabilities_t mcp_client_capabilities_create(void)
    MCP_NOEXCEPT {
  try {
    return new mcp_client_capabilities_impl();
  } catch (...) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY,
                   "Failed to allocate client capabilities");
    return nullptr;
  }
}

MCP_API void mcp_client_capabilities_free(mcp_client_capabilities_t caps)
    MCP_NOEXCEPT {
  delete caps;
}

MCP_API mcp_bool_t
mcp_client_capabilities_has_roots(mcp_client_capabilities_t caps) MCP_NOEXCEPT {
  if (!caps)
    return MCP_FALSE;
  return caps->has_roots ? MCP_TRUE : MCP_FALSE;
}

MCP_API void mcp_client_capabilities_set_roots(
    mcp_client_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT {
  if (!caps)
    return;
  caps->has_roots = (enabled == MCP_TRUE);
}

MCP_API mcp_bool_t mcp_client_capabilities_has_sampling(
    mcp_client_capabilities_t caps) MCP_NOEXCEPT {
  if (!caps)
    return MCP_FALSE;
  return caps->has_sampling ? MCP_TRUE : MCP_FALSE;
}

MCP_API void mcp_client_capabilities_set_sampling(
    mcp_client_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT {
  if (!caps)
    return;
  caps->has_sampling = (enabled == MCP_TRUE);
}

struct mcp_server_capabilities_impl {
  bool has_tools = false;
  bool has_prompts = false;
  bool has_resources = false;
  bool has_logging = false;
  bool tools_list_changed = false;
  bool prompts_list_changed = false;
  bool resources_subscribe = false;
  bool resources_list_changed = false;
};

MCP_API mcp_server_capabilities_t mcp_server_capabilities_create(void)
    MCP_NOEXCEPT {
  try {
    return new mcp_server_capabilities_impl();
  } catch (...) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY,
                   "Failed to allocate server capabilities");
    return nullptr;
  }
}

MCP_API void mcp_server_capabilities_free(mcp_server_capabilities_t caps)
    MCP_NOEXCEPT {
  delete caps;
}

MCP_API mcp_bool_t
mcp_server_capabilities_has_tools(mcp_server_capabilities_t caps) MCP_NOEXCEPT {
  if (!caps)
    return MCP_FALSE;
  return caps->has_tools ? MCP_TRUE : MCP_FALSE;
}

MCP_API void mcp_server_capabilities_set_tools(
    mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT {
  if (!caps)
    return;
  caps->has_tools = (enabled == MCP_TRUE);
}

MCP_API mcp_bool_t mcp_server_capabilities_has_prompts(
    mcp_server_capabilities_t caps) MCP_NOEXCEPT {
  if (!caps)
    return MCP_FALSE;
  return caps->has_prompts ? MCP_TRUE : MCP_FALSE;
}

MCP_API void mcp_server_capabilities_set_prompts(
    mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT {
  if (!caps)
    return;
  caps->has_prompts = (enabled == MCP_TRUE);
}

MCP_API mcp_bool_t mcp_server_capabilities_has_resources(
    mcp_server_capabilities_t caps) MCP_NOEXCEPT {
  if (!caps)
    return MCP_FALSE;
  return caps->has_resources ? MCP_TRUE : MCP_FALSE;
}

MCP_API void mcp_server_capabilities_set_resources(
    mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT {
  if (!caps)
    return;
  caps->has_resources = (enabled == MCP_TRUE);
}

MCP_API mcp_bool_t mcp_server_capabilities_has_logging(
    mcp_server_capabilities_t caps) MCP_NOEXCEPT {
  if (!caps)
    return MCP_FALSE;
  return caps->has_logging ? MCP_TRUE : MCP_FALSE;
}

MCP_API void mcp_server_capabilities_set_logging(
    mcp_server_capabilities_t caps, mcp_bool_t enabled) MCP_NOEXCEPT {
  if (!caps)
    return;
  caps->has_logging = (enabled == MCP_TRUE);
}

/* ============================================================================
 * Initialize Result Implementation
 * ============================================================================
 */

struct mcp_initialize_result_impl {
  std::string protocol_version;
  std::unique_ptr<mcp_implementation_impl> server_info;
  std::unique_ptr<mcp_server_capabilities_impl> capabilities;

  mcp_initialize_result_impl(const char* version)
      : protocol_version(version ? version : "1.0") {}
};

MCP_API mcp_initialize_result_t
mcp_initialize_result_create(const char* protocol_version) MCP_NOEXCEPT {
  try {
    return new mcp_initialize_result_impl(protocol_version);
  } catch (...) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY,
                   "Failed to allocate initialize result");
    return nullptr;
  }
}

MCP_API void mcp_initialize_result_free(mcp_initialize_result_t result)
    MCP_NOEXCEPT {
  delete result;
}

MCP_API const char* mcp_initialize_result_get_protocol_version(
    mcp_initialize_result_t result) MCP_NOEXCEPT {
  if (!result)
    return nullptr;
  return result->protocol_version.c_str();
}

MCP_API void mcp_initialize_result_set_server_info(
    mcp_initialize_result_t result, mcp_implementation_t info) MCP_NOEXCEPT {
  if (!result || !info)
    return;
  result->server_info.reset(new mcp_implementation_impl(*info));
}

MCP_API mcp_implementation_t mcp_initialize_result_get_server_info(
    mcp_initialize_result_t result) MCP_NOEXCEPT {
  if (!result || !result->server_info)
    return nullptr;
  return result->server_info.get();
}

MCP_API void mcp_initialize_result_set_capabilities(
    mcp_initialize_result_t result,
    mcp_server_capabilities_t caps) MCP_NOEXCEPT {
  if (!result || !caps)
    return;
  result->capabilities.reset(new mcp_server_capabilities_impl(*caps));
}

MCP_API mcp_server_capabilities_t mcp_initialize_result_get_capabilities(
    mcp_initialize_result_t result) MCP_NOEXCEPT {
  if (!result || !result->capabilities)
    return nullptr;
  return result->capabilities.get();
}

/* ============================================================================
 * JSON-RPC Notification Implementation
 * ============================================================================
 */

struct mcp_jsonrpc_notification_impl {
  std::string jsonrpc;
  std::string method;
  std::string params_json;

  mcp_jsonrpc_notification_impl(const char* rpc, const char* m)
      : jsonrpc(rpc ? rpc : "2.0"), method(m ? m : "") {}
};

MCP_API mcp_jsonrpc_notification_t mcp_jsonrpc_notification_create(
    const char* jsonrpc, const char* method) MCP_NOEXCEPT {
  if (!method) {
    set_last_error(MCP_ERROR_INVALID_ARGUMENT, "Method is required");
    return nullptr;
  }
  try {
    return new mcp_jsonrpc_notification_impl(jsonrpc, method);
  } catch (...) {
    set_last_error(MCP_ERROR_OUT_OF_MEMORY, "Failed to allocate notification");
    return nullptr;
  }
}

MCP_API void mcp_jsonrpc_notification_free(
    mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT {
  delete notification;
}

MCP_API const char* mcp_jsonrpc_notification_get_jsonrpc(
    mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT {
  if (!notification)
    return nullptr;
  return notification->jsonrpc.c_str();
}

MCP_API const char* mcp_jsonrpc_notification_get_method(
    mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT {
  if (!notification)
    return nullptr;
  return notification->method.c_str();
}

MCP_API void mcp_jsonrpc_notification_set_params(
    mcp_jsonrpc_notification_t notification,
    const char* json_params) MCP_NOEXCEPT {
  if (!notification)
    return;
  notification->params_json = json_params ? json_params : "";
}

MCP_API const char* mcp_jsonrpc_notification_get_params(
    mcp_jsonrpc_notification_t notification) MCP_NOEXCEPT {
  if (!notification || notification->params_json.empty())
    return nullptr;
  return notification->params_json.c_str();
}

/* ============================================================================
 * Additional JSON-RPC Request/Response Functions
 * ============================================================================
 */

MCP_API mcp_request_id_t
mcp_jsonrpc_request_get_id(mcp_jsonrpc_request_t request) MCP_NOEXCEPT {
  if (!request || !request->id)
    return nullptr;
  return request->id.get();
}

MCP_API const char* mcp_jsonrpc_request_get_params(
    mcp_jsonrpc_request_t request) MCP_NOEXCEPT {
  if (!request || request->params.empty())
    return nullptr;
  return request->params.c_str();
}

MCP_API mcp_request_id_t
mcp_jsonrpc_response_get_id(mcp_jsonrpc_response_t response) MCP_NOEXCEPT {
  if (!response || !response->id)
    return nullptr;
  return response->id.get();
}

}  // extern "C"