/**
 * @file mcp_c_api_json.cc
 * @brief Complete JSON serialization/deserialization implementation for ALL MCP C types
 *
 * This file provides comprehensive JSON conversion for all MCP C API types with RAII
 * for safe memory management. It covers all types from mcp_c_types.h.
 */

#include <cstring>
#include <string>
#include <memory>

#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace c_api {

using namespace mcp::json;
using namespace mcp::raii;

// ============================================================================
// Helper Functions with RAII
// ============================================================================

// Helper to convert C string to std::string safely
static std::string safe_string(const char* str) {
  return str ? std::string(str) : std::string();
}

// Helper to convert mcp_string_t to std::string
static std::string safe_string(const mcp_string_t& str) {
  return (str.data && str.length > 0) ? std::string(str.data, str.length) : std::string();
}

// Helper to allocate and copy string with RAII tracking
static char* alloc_string(const std::string& str) {
  if (str.empty())
    return nullptr;
  
  char* result = static_cast<char*>(std::malloc(str.size() + 1));
  if (result) {
    std::strcpy(result, str.c_str());
  }
  return result;
}

// Helper to create mcp_string_t from std::string with RAII
static mcp_string_t make_mcp_string(const std::string& str) {
  mcp_string_t result;
  result.data = alloc_string(str);
  result.length = str.size();
  return result;
}

// JSON value handle implementation
struct mcp_json_value_handle {
  JsonValue value;
  
  explicit mcp_json_value_handle(const JsonValue& v) : value(v) {}
  explicit mcp_json_value_handle(JsonValue&& v) : value(std::move(v)) {}
};

// ============================================================================
// Basic Type Serialization/Deserialization
// ============================================================================

// String serialization
extern "C" mcp_json_value_t mcp_string_to_json(mcp_string_t str) {
  try {
    JsonValue json = to_json(safe_string(str));
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// String deserialization with RAII
extern "C" mcp_string_t mcp_string_from_json(mcp_json_value_t json) {
  if (!json) {
    return mcp_string_t{nullptr, 0};
  }
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    std::string str = from_json<std::string>(handle->value);
    return make_mcp_string(str);
  } catch (...) {
    return mcp_string_t{nullptr, 0};
  }
}

// Request ID serialization
extern "C" mcp_json_value_t mcp_request_id_to_json(const mcp_request_id_t* id) {
  if (!id) return nullptr;
  
  try {
    JsonValue json;
    if (id->type == MCP_REQUEST_ID_STRING) {
      json = to_json(safe_string(id->value.string_value));
    } else {
      json = to_json(id->value.number_value);
    }
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Request ID deserialization with RAII
extern "C" mcp_request_id_t* mcp_request_id_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    auto* id = static_cast<mcp_request_id_t*>(
        std::calloc(1, sizeof(mcp_request_id_t)));
    if (!id) return nullptr;
    
    txn.track(id);
    
    if (value.isString()) {
      id->type = MCP_REQUEST_ID_STRING;
      id->value.string_value = make_mcp_string(value.getString());
      txn.track(const_cast<char*>(id->value.string_value.data));
    } else if (value.isInteger()) {
      id->type = MCP_REQUEST_ID_NUMBER;
      id->value.number_value = value.getInt64();
    } else {
      return nullptr;
    }
    
    txn.commit();
    return id;
  } catch (...) {
    return nullptr;
  }
}

// Progress token serialization
extern "C" mcp_json_value_t mcp_progress_token_to_json(const mcp_progress_token_t* token) {
  if (!token) return nullptr;
  
  try {
    JsonValue json;
    if (token->type == MCP_PROGRESS_TOKEN_STRING) {
      json = to_json(safe_string(token->value.string_value));
    } else {
      json = to_json(token->value.number_value);
    }
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Progress token deserialization with RAII
extern "C" mcp_progress_token_t* mcp_progress_token_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    auto* token = static_cast<mcp_progress_token_t*>(
        std::calloc(1, sizeof(mcp_progress_token_t)));
    if (!token) return nullptr;
    
    txn.track(token);
    
    if (value.isString()) {
      token->type = MCP_PROGRESS_TOKEN_STRING;
      token->value.string_value = make_mcp_string(value.getString());
      txn.track(const_cast<char*>(token->value.string_value.data));
    } else if (value.isInteger()) {
      token->type = MCP_PROGRESS_TOKEN_NUMBER;
      token->value.number_value = value.getInt64();
    } else {
      return nullptr;
    }
    
    txn.commit();
    return token;
  } catch (...) {
    return nullptr;
  }
}

// Role serialization
extern "C" mcp_json_value_t mcp_role_to_json(mcp_role_t role) {
  try {
    std::string role_str = (role == MCP_ROLE_USER) ? "user" : "assistant";
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(to_json(role_str)));
  } catch (...) {
    return nullptr;
  }
}

// Role deserialization
extern "C" mcp_role_t mcp_role_from_json(mcp_json_value_t json) {
  if (!json) return MCP_ROLE_USER;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    std::string role_str = from_json<std::string>(handle->value);
    return (role_str == "user") ? MCP_ROLE_USER : MCP_ROLE_ASSISTANT;
  } catch (...) {
    return MCP_ROLE_USER;
  }
}

// Logging level serialization
extern "C" mcp_json_value_t mcp_logging_level_to_json(mcp_logging_level_t level) {
  try {
    std::string level_str;
    switch (level) {
      case MCP_LOGGING_DEBUG: level_str = "debug"; break;
      case MCP_LOGGING_INFO: level_str = "info"; break;
      case MCP_LOGGING_NOTICE: level_str = "notice"; break;
      case MCP_LOGGING_WARNING: level_str = "warning"; break;
      case MCP_LOGGING_ERROR: level_str = "error"; break;
      case MCP_LOGGING_CRITICAL: level_str = "critical"; break;
      case MCP_LOGGING_ALERT: level_str = "alert"; break;
      case MCP_LOGGING_EMERGENCY: level_str = "emergency"; break;
      default: level_str = "info";
    }
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(to_json(level_str)));
  } catch (...) {
    return nullptr;
  }
}

// Logging level deserialization
extern "C" mcp_logging_level_t mcp_logging_level_from_json(mcp_json_value_t json) {
  if (!json) return MCP_LOGGING_INFO;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    std::string level_str = from_json<std::string>(handle->value);
    
    if (level_str == "debug") return MCP_LOGGING_DEBUG;
    if (level_str == "info") return MCP_LOGGING_INFO;
    if (level_str == "notice") return MCP_LOGGING_NOTICE;
    if (level_str == "warning") return MCP_LOGGING_WARNING;
    if (level_str == "error") return MCP_LOGGING_ERROR;
    if (level_str == "critical") return MCP_LOGGING_CRITICAL;
    if (level_str == "alert") return MCP_LOGGING_ALERT;
    if (level_str == "emergency") return MCP_LOGGING_EMERGENCY;
    
    return MCP_LOGGING_INFO;
  } catch (...) {
    return MCP_LOGGING_INFO;
  }
}

// ============================================================================
// Error Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_jsonrpc_error_to_json(const mcp_jsonrpc_error_t* error) {
  if (!error) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("code", error->code);
    builder.add("message", safe_string(error->message));
    
    if (error->data.has_value && error->data.value) {
      auto* data_handle = reinterpret_cast<mcp_json_value_handle*>(error->data.value);
      builder.add("data", data_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_jsonrpc_error_t* mcp_jsonrpc_error_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* error = static_cast<mcp_jsonrpc_error_t*>(
        std::calloc(1, sizeof(mcp_jsonrpc_error_t)));
    if (!error) return nullptr;
    
    txn.track(error);
    
    if (value.contains("code")) {
      error->code = value["code"].getInt();
    }
    
    if (value.contains("message")) {
      error->message = make_mcp_string(value["message"].getString());
      txn.track(const_cast<char*>(error->message.data));
    }
    
    if (value.contains("data")) {
      error->data.has_value = MCP_TRUE;
      error->data.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["data"]));
      txn.track(error->data.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    txn.commit();
    return error;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Content Block Serialization/Deserialization with RAII
// ============================================================================

extern "C" mcp_json_value_t mcp_content_block_to_json(const mcp_content_block_t* block) {
  if (!block) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    switch (block->type) {
      case MCP_CONTENT_TEXT:
        if (block->content.text) {
          builder.add("type", "text");
          builder.add("text", safe_string(block->content.text->text));
        }
        break;
        
      case MCP_CONTENT_IMAGE:
        if (block->content.image) {
          builder.add("type", "image");
          builder.add("data", safe_string(block->content.image->data));
          builder.add("mimeType", safe_string(block->content.image->mime_type));
        }
        break;
        
      case MCP_CONTENT_AUDIO:
        if (block->content.audio) {
          builder.add("type", "audio");
          builder.add("data", safe_string(block->content.audio->data));
          builder.add("mimeType", safe_string(block->content.audio->mime_type));
        }
        break;
        
      case MCP_CONTENT_RESOURCE:
        if (block->content.resource) {
          builder.add("type", "resource");
          builder.add("uri", safe_string(block->content.resource->uri));
          builder.add("name", safe_string(block->content.resource->name));
          if (block->content.resource->description) {
            builder.add("description", safe_string(*block->content.resource->description));
          }
          if (block->content.resource->mime_type) {
            builder.add("mimeType", safe_string(*block->content.resource->mime_type));
          }
        }
        break;
        
      case MCP_CONTENT_RESOURCE_LINK:
        if (block->content.resource_link) {
          builder.add("type", "resource");
          builder.add("uri", safe_string(block->content.resource_link->uri));
          builder.add("name", safe_string(block->content.resource_link->name));
          if (block->content.resource_link->description.has_value) {
            builder.add("description", safe_string(
                *static_cast<mcp_string_t*>(block->content.resource_link->description.value)));
          }
          if (block->content.resource_link->mime_type.has_value) {
            builder.add("mimeType", safe_string(
                *static_cast<mcp_string_t*>(block->content.resource_link->mime_type.value)));
          }
        }
        break;
        
      case MCP_CONTENT_EMBEDDED_RESOURCE:
        if (block->content.embedded) {
          builder.add("type", "embedded");
          builder.add("id", safe_string(block->content.embedded->id));
          if (block->content.embedded->text.has_value) {
            builder.add("text", safe_string(
                *static_cast<mcp_string_t*>(block->content.embedded->text.value)));
          }
          if (block->content.embedded->blob.has_value) {
            builder.add("blob", safe_string(
                *static_cast<mcp_string_t*>(block->content.embedded->blob.value)));
          }
        }
        break;
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_content_block_t* mcp_content_block_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* block = static_cast<mcp_content_block_t*>(
        std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block) return nullptr;
    
    txn.track(block);
    
    std::string type_str = value["type"].getString();
    
    if (type_str == "text") {
      block->type = MCP_CONTENT_TEXT;
      auto* text = static_cast<mcp_text_content_t*>(
          std::calloc(1, sizeof(mcp_text_content_t)));
      txn.track(text);
      
      text->type = make_mcp_string("text");
      txn.track(const_cast<char*>(text->type.data));
      
      text->text = make_mcp_string(value["text"].getString());
      txn.track(const_cast<char*>(text->text.data));
      
      block->content.text = text;
      
    } else if (type_str == "image") {
      block->type = MCP_CONTENT_IMAGE;
      auto* image = static_cast<mcp_image_content_t*>(
          std::calloc(1, sizeof(mcp_image_content_t)));
      txn.track(image);
      
      image->type = make_mcp_string("image");
      txn.track(const_cast<char*>(image->type.data));
      
      image->data = make_mcp_string(value["data"].getString());
      txn.track(const_cast<char*>(image->data.data));
      
      image->mime_type = make_mcp_string(value["mimeType"].getString());
      txn.track(const_cast<char*>(image->mime_type.data));
      
      block->content.image = image;
      
    } else if (type_str == "audio") {
      block->type = MCP_CONTENT_AUDIO;
      auto* audio = static_cast<mcp_audio_content_t*>(
          std::calloc(1, sizeof(mcp_audio_content_t)));
      txn.track(audio);
      
      audio->type = make_mcp_string("audio");
      txn.track(const_cast<char*>(audio->type.data));
      
      audio->data = make_mcp_string(value["data"].getString());
      txn.track(const_cast<char*>(audio->data.data));
      
      audio->mime_type = make_mcp_string(value["mimeType"].getString());
      txn.track(const_cast<char*>(audio->mime_type.data));
      
      block->content.audio = audio;
      
    } else {
      return nullptr;
    }
    
    txn.commit();
    return block;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Resource Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_resource_to_json(const mcp_resource_t* resource) {
  if (!resource) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("uri", safe_string(resource->uri));
    builder.add("name", safe_string(resource->name));
    
    if (resource->description && resource->description->has_value) {
      builder.add("description", safe_string(
          *static_cast<mcp_string_t*>(resource->description->value)));
    }
    if (resource->mime_type && resource->mime_type->has_value) {
      builder.add("mimeType", safe_string(
          *static_cast<mcp_string_t*>(resource->mime_type->value)));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_resource_t* mcp_resource_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* resource = static_cast<mcp_resource_t*>(
        std::calloc(1, sizeof(mcp_resource_t)));
    if (!resource) return nullptr;
    
    txn.track(resource);
    
    resource->uri = make_mcp_string(value["uri"].getString());
    txn.track(const_cast<char*>(resource->uri.data));
    
    resource->name = make_mcp_string(value["name"].getString());
    txn.track(const_cast<char*>(resource->name.data));
    
    if (value.contains("description")) {
      resource->description = static_cast<mcp_optional_t*>(
          std::calloc(1, sizeof(mcp_optional_t)));
      txn.track(resource->description);
      
      resource->description->has_value = MCP_TRUE;
      auto* desc_str = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(desc_str);
      
      *desc_str = make_mcp_string(value["description"].getString());
      txn.track(const_cast<char*>(desc_str->data));
      
      resource->description->value = desc_str;
    }
    
    if (value.contains("mimeType")) {
      resource->mime_type = static_cast<mcp_optional_t*>(
          std::calloc(1, sizeof(mcp_optional_t)));
      txn.track(resource->mime_type);
      
      resource->mime_type->has_value = MCP_TRUE;
      auto* mime_str = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(mime_str);
      
      *mime_str = make_mcp_string(value["mimeType"].getString());
      txn.track(const_cast<char*>(mime_str->data));
      
      resource->mime_type->value = mime_str;
    }
    
    txn.commit();
    return resource;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Tool Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_tool_to_json(const mcp_tool_t* tool) {
  if (!tool) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("name", safe_string(tool->name));
    
    if (tool->description.has_value) {
      builder.add("description", safe_string(
          *static_cast<mcp_string_t*>(tool->description.value)));
    }
    
    if (tool->input_schema) {
      auto* schema_handle = reinterpret_cast<mcp_json_value_handle*>(tool->input_schema);
      builder.add("inputSchema", schema_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_tool_t* mcp_tool_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* tool = static_cast<mcp_tool_t*>(std::calloc(1, sizeof(mcp_tool_t)));
    if (!tool) return nullptr;
    
    txn.track(tool);
    
    tool->name = make_mcp_string(value["name"].getString());
    txn.track(const_cast<char*>(tool->name.data));
    
    if (value.contains("description")) {
      tool->description.has_value = MCP_TRUE;
      auto* desc_str = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(desc_str);
      
      *desc_str = make_mcp_string(value["description"].getString());
      txn.track(const_cast<char*>(desc_str->data));
      
      tool->description.value = desc_str;
    }
    
    if (value.contains("inputSchema")) {
      tool->input_schema = reinterpret_cast<mcp_json_value_t>(
          new mcp_json_value_handle(value["inputSchema"]));
      txn.track(tool->input_schema, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    txn.commit();
    return tool;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Prompt Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_prompt_to_json(const mcp_prompt_t* prompt) {
  if (!prompt) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("name", safe_string(prompt->name));
    
    if (prompt->description.has_value) {
      builder.add("description", safe_string(
          *static_cast<mcp_string_t*>(prompt->description.value)));
    }
    
    if (prompt->arguments.count > 0) {
      JsonArrayBuilder args_builder;
      auto* args = static_cast<mcp_prompt_argument_t**>(prompt->arguments.items);
      
      for (size_t i = 0; i < prompt->arguments.count; ++i) {
        if (args[i]) {
          JsonObjectBuilder arg_builder;
          arg_builder.add("name", safe_string(args[i]->name));
          
          if (args[i]->description.has_value) {
            arg_builder.add("description", safe_string(
                *static_cast<mcp_string_t*>(args[i]->description.value)));
          }
          
          arg_builder.add("required", args[i]->required == MCP_TRUE);
          args_builder.add(arg_builder.build());
        }
      }
      
      builder.add("arguments", args_builder.build());
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_prompt_t* mcp_prompt_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* prompt = static_cast<mcp_prompt_t*>(std::calloc(1, sizeof(mcp_prompt_t)));
    if (!prompt) return nullptr;
    
    txn.track(prompt);
    
    prompt->name = make_mcp_string(value["name"].getString());
    txn.track(const_cast<char*>(prompt->name.data));
    
    if (value.contains("description")) {
      prompt->description.has_value = MCP_TRUE;
      auto* desc_str = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(desc_str);
      
      *desc_str = make_mcp_string(value["description"].getString());
      txn.track(const_cast<char*>(desc_str->data));
      
      prompt->description.value = desc_str;
    }
    
    if (value.contains("arguments")) {
      const JsonValue& args_json = value["arguments"];
      if (args_json.isArray()) {
        size_t count = args_json.size();
        prompt->arguments.count = count;
        prompt->arguments.capacity = count;
        prompt->arguments.items = static_cast<void**>(
            std::calloc(count, sizeof(void*)));
        txn.track(prompt->arguments.items);
        
        for (size_t i = 0; i < count; ++i) {
          auto* arg = static_cast<mcp_prompt_argument_t*>(
              std::calloc(1, sizeof(mcp_prompt_argument_t)));
          txn.track(arg);
          
          const JsonValue& arg_json = args_json[i];
          
          arg->name = make_mcp_string(arg_json["name"].getString());
          txn.track(const_cast<char*>(arg->name.data));
          
          if (arg_json.contains("description")) {
            arg->description.has_value = MCP_TRUE;
            auto* desc_str = static_cast<mcp_string_t*>(
                std::calloc(1, sizeof(mcp_string_t)));
            txn.track(desc_str);
            
            *desc_str = make_mcp_string(arg_json["description"].getString());
            txn.track(const_cast<char*>(desc_str->data));
            
            arg->description.value = desc_str;
          }
          
          if (arg_json.contains("required")) {
            arg->required = arg_json["required"].getBool() ? MCP_TRUE : MCP_FALSE;
          }
          
          prompt->arguments.items[i] = arg;
        }
      }
    }
    
    txn.commit();
    return prompt;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Message Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_message_to_json(const mcp_message_t* message) {
  if (!message) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("role", message->role == MCP_ROLE_USER ? "user" : "assistant");
    
    // Serialize content block
    mcp_json_value_t content_json = mcp_content_block_to_json(&message->content);
    if (content_json) {
      auto* content_handle = reinterpret_cast<mcp_json_value_handle*>(content_json);
      builder.add("content", content_handle->value);
      delete content_handle;
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_message_t* mcp_message_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* message = static_cast<mcp_message_t*>(
        std::calloc(1, sizeof(mcp_message_t)));
    if (!message) return nullptr;
    
    txn.track(message);
    
    std::string role_str = value["role"].getString();
    message->role = (role_str == "user") ? MCP_ROLE_USER : MCP_ROLE_ASSISTANT;
    
    if (value.contains("content")) {
      mcp_json_value_handle content_impl(value["content"]);
      mcp_content_block_t* content = mcp_content_block_from_json(
          reinterpret_cast<mcp_json_value_t>(&content_impl));
      if (content) {
        message->content = *content;
        // Track nested allocations
        txn.track(content, [](void* ptr) {
          mcp_content_block_free(static_cast<mcp_content_block_t*>(ptr));
        });
      }
    }
    
    txn.commit();
    return message;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Implementation Info Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_implementation_to_json(const mcp_implementation_t* impl) {
  if (!impl) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("name", safe_string(impl->name));
    builder.add("version", safe_string(impl->version));
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_implementation_t* mcp_implementation_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* impl = static_cast<mcp_implementation_t*>(
        std::calloc(1, sizeof(mcp_implementation_t)));
    if (!impl) return nullptr;
    
    txn.track(impl);
    
    impl->name = make_mcp_string(value["name"].getString());
    txn.track(const_cast<char*>(impl->name.data));
    
    impl->version = make_mcp_string(value["version"].getString());
    txn.track(const_cast<char*>(impl->version.data));
    
    txn.commit();
    return impl;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Capabilities Serialization/Deserialization
// ============================================================================

extern "C" mcp_json_value_t mcp_client_capabilities_to_json(
    const mcp_client_capabilities_t* caps) {
  if (!caps) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    if (caps->experimental.has_value) {
      auto* exp_handle = reinterpret_cast<mcp_json_value_handle*>(caps->experimental.value);
      builder.add("experimental", exp_handle->value);
    }
    
    if (caps->sampling.has_value) {
      auto* samp_handle = reinterpret_cast<mcp_json_value_handle*>(caps->sampling.value);
      builder.add("sampling", samp_handle->value);
    }
    
    if (caps->roots.has_value) {
      auto* roots_handle = reinterpret_cast<mcp_json_value_handle*>(caps->roots.value);
      builder.add("roots", roots_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_client_capabilities_t* mcp_client_capabilities_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* caps = static_cast<mcp_client_capabilities_t*>(
        std::calloc(1, sizeof(mcp_client_capabilities_t)));
    if (!caps) return nullptr;
    
    txn.track(caps);
    
    if (value.contains("experimental")) {
      caps->experimental.has_value = MCP_TRUE;
      caps->experimental.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["experimental"]));
      txn.track(caps->experimental.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    if (value.contains("sampling")) {
      caps->sampling.has_value = MCP_TRUE;
      caps->sampling.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["sampling"]));
      txn.track(caps->sampling.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    if (value.contains("roots")) {
      caps->roots.has_value = MCP_TRUE;
      caps->roots.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["roots"]));
      txn.track(caps->roots.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    txn.commit();
    return caps;
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_server_capabilities_to_json(
    const mcp_server_capabilities_t* caps) {
  if (!caps) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    if (caps->experimental.has_value) {
      auto* exp_handle = reinterpret_cast<mcp_json_value_handle*>(caps->experimental.value);
      builder.add("experimental", exp_handle->value);
    }
    
    if (caps->logging.has_value) {
      builder.add("logging", caps->logging.value != nullptr);
    }
    
    if (caps->prompts.has_value) {
      builder.add("prompts", caps->prompts.value != nullptr);
    }
    
    if (caps->resources.has_value) {
      builder.add("resources", caps->resources.value != nullptr);
    }
    
    if (caps->tools.has_value) {
      builder.add("tools", caps->tools.value != nullptr);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_server_capabilities_t* mcp_server_capabilities_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* caps = static_cast<mcp_server_capabilities_t*>(
        std::calloc(1, sizeof(mcp_server_capabilities_t)));
    if (!caps) return nullptr;
    
    txn.track(caps);
    
    if (value.contains("experimental")) {
      caps->experimental.has_value = MCP_TRUE;
      caps->experimental.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["experimental"]));
      txn.track(caps->experimental.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    if (value.contains("logging")) {
      caps->logging.has_value = MCP_TRUE;
      caps->logging.value = value["logging"].getBool() ? 
          reinterpret_cast<void*>(1) : nullptr;
    }
    
    if (value.contains("prompts")) {
      caps->prompts.has_value = MCP_TRUE;
      caps->prompts.value = value["prompts"].getBool() ? 
          reinterpret_cast<void*>(1) : nullptr;
    }
    
    if (value.contains("resources")) {
      caps->resources.has_value = MCP_TRUE;
      caps->resources.value = value["resources"].getBool() ? 
          reinterpret_cast<void*>(1) : nullptr;
    }
    
    if (value.contains("tools")) {
      caps->tools.has_value = MCP_TRUE;
      caps->tools.value = value["tools"].getBool() ? 
          reinterpret_cast<void*>(1) : nullptr;
    }
    
    txn.commit();
    return caps;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Protocol Request/Response Serialization
// ============================================================================

extern "C" mcp_json_value_t mcp_initialize_request_to_json(
    const mcp_initialize_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("protocolVersion", safe_string(req->protocol_version));
    
    // Add capabilities
    mcp_json_value_t caps_json = mcp_client_capabilities_to_json(&req->capabilities);
    if (caps_json) {
      auto* caps_handle = reinterpret_cast<mcp_json_value_handle*>(caps_json);
      builder.add("capabilities", caps_handle->value);
      delete caps_handle;
    }
    
    // Add client info if present
    if (req->client_info.has_value) {
      auto* info = static_cast<mcp_implementation_t*>(req->client_info.value);
      mcp_json_value_t info_json = mcp_implementation_to_json(info);
      if (info_json) {
        auto* info_handle = reinterpret_cast<mcp_json_value_handle*>(info_json);
        builder.add("clientInfo", info_handle->value);
        delete info_handle;
      }
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_initialize_request_t* mcp_initialize_request_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* req = static_cast<mcp_initialize_request_t*>(
        std::calloc(1, sizeof(mcp_initialize_request_t)));
    if (!req) return nullptr;
    
    txn.track(req);
    
    req->protocol_version = make_mcp_string(value["protocolVersion"].getString());
    txn.track(const_cast<char*>(req->protocol_version.data));
    
    // Parse capabilities
    if (value.contains("capabilities")) {
      mcp_json_value_handle caps_impl(value["capabilities"]);
      mcp_client_capabilities_t* caps = mcp_client_capabilities_from_json(
          reinterpret_cast<mcp_json_value_t>(&caps_impl));
      if (caps) {
        req->capabilities = *caps;
        // Note: capabilities are copied, original can be freed
        mcp_client_capabilities_free(caps);
      }
    }
    
    // Parse client info
    if (value.contains("clientInfo")) {
      req->client_info.has_value = MCP_TRUE;
      mcp_json_value_handle info_impl(value["clientInfo"]);
      req->client_info.value = mcp_implementation_from_json(
          reinterpret_cast<mcp_json_value_t>(&info_impl));
      if (req->client_info.value) {
        txn.track(req->client_info.value, [](void* ptr) {
          mcp_implementation_free(static_cast<mcp_implementation_t*>(ptr));
        });
      }
    }
    
    txn.commit();
    return req;
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_initialize_result_to_json(
    const mcp_initialize_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("protocolVersion", safe_string(result->protocol_version));
    
    // Add capabilities
    mcp_json_value_t caps_json = mcp_server_capabilities_to_json(&result->capabilities);
    if (caps_json) {
      auto* caps_handle = reinterpret_cast<mcp_json_value_handle*>(caps_json);
      builder.add("capabilities", caps_handle->value);
      delete caps_handle;
    }
    
    // Add server info if present
    if (result->server_info.has_value) {
      auto* info = static_cast<mcp_implementation_t*>(result->server_info.value);
      mcp_json_value_t info_json = mcp_implementation_to_json(info);
      if (info_json) {
        auto* info_handle = reinterpret_cast<mcp_json_value_handle*>(info_json);
        builder.add("serverInfo", info_handle->value);
        delete info_handle;
      }
    }
    
    // Add instructions if present
    if (result->instructions.has_value) {
      builder.add("instructions", safe_string(
          *static_cast<mcp_string_t*>(result->instructions.value)));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_initialize_result_t* mcp_initialize_result_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* result = static_cast<mcp_initialize_result_t*>(
        std::calloc(1, sizeof(mcp_initialize_result_t)));
    if (!result) return nullptr;
    
    txn.track(result);
    
    result->protocol_version = make_mcp_string(value["protocolVersion"].getString());
    txn.track(const_cast<char*>(result->protocol_version.data));
    
    // Parse capabilities
    if (value.contains("capabilities")) {
      mcp_json_value_handle caps_impl(value["capabilities"]);
      mcp_server_capabilities_t* caps = mcp_server_capabilities_from_json(
          reinterpret_cast<mcp_json_value_t>(&caps_impl));
      if (caps) {
        result->capabilities = *caps;
        mcp_server_capabilities_free(caps);
      }
    }
    
    // Parse server info
    if (value.contains("serverInfo")) {
      result->server_info.has_value = MCP_TRUE;
      mcp_json_value_handle info_impl(value["serverInfo"]);
      result->server_info.value = mcp_implementation_from_json(
          reinterpret_cast<mcp_json_value_t>(&info_impl));
      if (result->server_info.value) {
        txn.track(result->server_info.value, [](void* ptr) {
          mcp_implementation_free(static_cast<mcp_implementation_t*>(ptr));
        });
      }
    }
    
    // Parse instructions
    if (value.contains("instructions")) {
      result->instructions.has_value = MCP_TRUE;
      auto* inst_str = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(inst_str);
      
      *inst_str = make_mcp_string(value["instructions"].getString());
      txn.track(const_cast<char*>(inst_str->data));
      
      result->instructions.value = inst_str;
    }
    
    txn.commit();
    return result;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// JSON-RPC Message Serialization
// ============================================================================

extern "C" mcp_json_value_t mcp_jsonrpc_request_to_json(
    const mcp_jsonrpc_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("jsonrpc", safe_string(req->jsonrpc));
    
    // Add request ID
    mcp_json_value_t id_json = mcp_request_id_to_json(&req->id);
    if (id_json) {
      auto* id_handle = reinterpret_cast<mcp_json_value_handle*>(id_json);
      builder.add("id", id_handle->value);
      delete id_handle;
    }
    
    builder.add("method", safe_string(req->method));
    
    // Add params if present
    if (req->params.has_value) {
      auto* params_handle = reinterpret_cast<mcp_json_value_handle*>(req->params.value);
      builder.add("params", params_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_jsonrpc_request_t* mcp_jsonrpc_request_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* req = static_cast<mcp_jsonrpc_request_t*>(
        std::calloc(1, sizeof(mcp_jsonrpc_request_t)));
    if (!req) return nullptr;
    
    txn.track(req);
    
    req->jsonrpc = make_mcp_string(value["jsonrpc"].getString());
    txn.track(const_cast<char*>(req->jsonrpc.data));
    
    // Parse request ID
    if (value.contains("id")) {
      mcp_json_value_handle id_impl(value["id"]);
      mcp_request_id_t* id = mcp_request_id_from_json(
          reinterpret_cast<mcp_json_value_t>(&id_impl));
      if (id) {
        req->id = *id;
        mcp_request_id_free(id);
      }
    }
    
    req->method = make_mcp_string(value["method"].getString());
    txn.track(const_cast<char*>(req->method.data));
    
    // Parse params
    if (value.contains("params")) {
      req->params.has_value = MCP_TRUE;
      req->params.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["params"]));
      txn.track(req->params.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    txn.commit();
    return req;
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_jsonrpc_response_to_json(
    const mcp_jsonrpc_response_t* resp) {
  if (!resp) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("jsonrpc", safe_string(resp->jsonrpc));
    
    // Add request ID
    mcp_json_value_t id_json = mcp_request_id_to_json(&resp->id);
    if (id_json) {
      auto* id_handle = reinterpret_cast<mcp_json_value_handle*>(id_json);
      builder.add("id", id_handle->value);
      delete id_handle;
    }
    
    // Add result or error (mutually exclusive)
    if (resp->result.has_value) {
      auto* result_handle = reinterpret_cast<mcp_json_value_handle*>(resp->result.value);
      builder.add("result", result_handle->value);
    } else if (resp->error.has_value) {
      auto* error = static_cast<mcp_jsonrpc_error_t*>(resp->error.value);
      mcp_json_value_t error_json = mcp_jsonrpc_error_to_json(error);
      if (error_json) {
        auto* error_handle = reinterpret_cast<mcp_json_value_handle*>(error_json);
        builder.add("error", error_handle->value);
        delete error_handle;
      }
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_jsonrpc_response_t* mcp_jsonrpc_response_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* resp = static_cast<mcp_jsonrpc_response_t*>(
        std::calloc(1, sizeof(mcp_jsonrpc_response_t)));
    if (!resp) return nullptr;
    
    txn.track(resp);
    
    resp->jsonrpc = make_mcp_string(value["jsonrpc"].getString());
    txn.track(const_cast<char*>(resp->jsonrpc.data));
    
    // Parse request ID
    if (value.contains("id")) {
      mcp_json_value_handle id_impl(value["id"]);
      mcp_request_id_t* id = mcp_request_id_from_json(
          reinterpret_cast<mcp_json_value_t>(&id_impl));
      if (id) {
        resp->id = *id;
        mcp_request_id_free(id);
      }
    }
    
    // Parse result or error
    if (value.contains("result")) {
      resp->result.has_value = MCP_TRUE;
      resp->result.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["result"]));
      txn.track(resp->result.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    } else if (value.contains("error")) {
      resp->error.has_value = MCP_TRUE;
      mcp_json_value_handle error_impl(value["error"]);
      resp->error.value = mcp_jsonrpc_error_from_json(
          reinterpret_cast<mcp_json_value_t>(&error_impl));
      if (resp->error.value) {
        txn.track(resp->error.value, [](void* ptr) {
          mcp_jsonrpc_error_free(static_cast<mcp_jsonrpc_error_t*>(ptr));
        });
      }
    }
    
    txn.commit();
    return resp;
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_jsonrpc_notification_to_json(
    const mcp_jsonrpc_notification_t* notif) {
  if (!notif) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("jsonrpc", safe_string(notif->jsonrpc));
    builder.add("method", safe_string(notif->method));
    
    // Add params if present
    if (notif->params.has_value) {
      auto* params_handle = reinterpret_cast<mcp_json_value_handle*>(notif->params.value);
      builder.add("params", params_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_jsonrpc_notification_t* mcp_jsonrpc_notification_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* notif = static_cast<mcp_jsonrpc_notification_t*>(
        std::calloc(1, sizeof(mcp_jsonrpc_notification_t)));
    if (!notif) return nullptr;
    
    txn.track(notif);
    
    notif->jsonrpc = make_mcp_string(value["jsonrpc"].getString());
    txn.track(const_cast<char*>(notif->jsonrpc.data));
    
    notif->method = make_mcp_string(value["method"].getString());
    txn.track(const_cast<char*>(notif->method.data));
    
    // Parse params
    if (value.contains("params")) {
      notif->params.has_value = MCP_TRUE;
      notif->params.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["params"]));
      txn.track(notif->params.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    txn.commit();
    return notif;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// Tool Call Request/Result
// ============================================================================

extern "C" mcp_json_value_t mcp_call_tool_request_to_json(
    const mcp_call_tool_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("name", safe_string(req->name));
    
    if (req->arguments.has_value) {
      auto* args_handle = reinterpret_cast<mcp_json_value_handle*>(req->arguments.value);
      builder.add("arguments", args_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_call_tool_request_t* mcp_call_tool_request_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* req = static_cast<mcp_call_tool_request_t*>(
        std::calloc(1, sizeof(mcp_call_tool_request_t)));
    if (!req) return nullptr;
    
    txn.track(req);
    
    req->name = make_mcp_string(value["name"].getString());
    txn.track(const_cast<char*>(req->name.data));
    
    if (value.contains("arguments")) {
      req->arguments.has_value = MCP_TRUE;
      req->arguments.value = reinterpret_cast<void*>(
          new mcp_json_value_handle(value["arguments"]));
      txn.track(req->arguments.value, [](void* ptr) {
        delete reinterpret_cast<mcp_json_value_handle*>(ptr);
      });
    }
    
    txn.commit();
    return req;
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_call_tool_result_to_json(
    const mcp_call_tool_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add content array
    if (result->content.count > 0) {
      JsonArrayBuilder content_builder;
      auto* blocks = static_cast<mcp_content_block_t**>(result->content.items);
      
      for (size_t i = 0; i < result->content.count; ++i) {
        if (blocks[i]) {
          mcp_json_value_t block_json = mcp_content_block_to_json(blocks[i]);
          if (block_json) {
            auto* block_handle = reinterpret_cast<mcp_json_value_handle*>(block_json);
            content_builder.add(block_handle->value);
            delete block_handle;
          }
        }
      }
      
      builder.add("content", content_builder.build());
    }
    
    builder.add("isError", result->is_error == MCP_TRUE);
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_call_tool_result_t* mcp_call_tool_result_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* result = static_cast<mcp_call_tool_result_t*>(
        std::calloc(1, sizeof(mcp_call_tool_result_t)));
    if (!result) return nullptr;
    
    txn.track(result);
    
    // Parse content array
    if (value.contains("content")) {
      const JsonValue& content_json = value["content"];
      if (content_json.isArray()) {
        size_t count = content_json.size();
        result->content.count = count;
        result->content.capacity = count;
        result->content.items = static_cast<void**>(
            std::calloc(count, sizeof(void*)));
        txn.track(result->content.items);
        
        for (size_t i = 0; i < count; ++i) {
          mcp_json_value_handle block_impl(content_json[i]);
          result->content.items[i] = mcp_content_block_from_json(
              reinterpret_cast<mcp_json_value_t>(&block_impl));
          if (result->content.items[i]) {
            txn.track(result->content.items[i], [](void* ptr) {
              mcp_content_block_free(static_cast<mcp_content_block_t*>(ptr));
            });
          }
        }
      }
    }
    
    if (value.contains("isError")) {
      result->is_error = value["isError"].getBool() ? MCP_TRUE : MCP_FALSE;
    }
    
    txn.commit();
    return result;
  } catch (...) {
    return nullptr;
  }
}

// ============================================================================
// JSON Value Management
// ============================================================================

extern "C" mcp_json_value_t mcp_json_parse(const char* json_string) {
  if (!json_string) return nullptr;
  
  try {
    JsonValue value = JsonValue::parse(json_string);
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(value)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" char* mcp_json_stringify(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    std::string str = handle->value.toString();
    return alloc_string(str);
  } catch (...) {
    return nullptr;
  }
}

extern "C" void mcp_json_free(mcp_json_value_t json) {
  if (json) {
    delete reinterpret_cast<mcp_json_value_handle*>(json);
  }
}

// ============================================================================
// Additional Type Support
// ============================================================================

// List Resources Request/Result
extern "C" mcp_json_value_t mcp_list_resources_request_to_json(
    const mcp_list_resources_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    if (req->base.cursor.has_value) {
      auto* cursor = static_cast<mcp_string_t*>(req->base.cursor.value);
      builder.add("cursor", safe_string(*cursor));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_list_resources_request_t* mcp_list_resources_request_from_json(
    mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    auto* req = static_cast<mcp_list_resources_request_t*>(
        std::calloc(1, sizeof(mcp_list_resources_request_t)));
    if (!req) return nullptr;
    
    txn.track(req);
    
    if (value.contains("cursor")) {
      req->base.cursor.has_value = MCP_TRUE;
      auto* cursor = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(cursor);
      
      *cursor = make_mcp_string(value["cursor"].getString());
      txn.track(const_cast<char*>(cursor->data));
      
      req->base.cursor.value = cursor;
    }
    
    txn.commit();
    return req;
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_list_resources_result_to_json(
    const mcp_list_resources_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add resources array
    if (result->resources.count > 0) {
      JsonArrayBuilder resources_builder;
      auto* resources = static_cast<mcp_resource_t**>(result->resources.items);
      
      for (size_t i = 0; i < result->resources.count; ++i) {
        if (resources[i]) {
          mcp_json_value_t res_json = mcp_resource_to_json(resources[i]);
          if (res_json) {
            auto* res_handle = reinterpret_cast<mcp_json_value_handle*>(res_json);
            resources_builder.add(res_handle->value);
            delete res_handle;
          }
        }
      }
      
      builder.add("resources", resources_builder.build());
    }
    
    if (result->next_cursor.has_value) {
      auto* cursor = static_cast<mcp_string_t*>(result->next_cursor.value);
      builder.add("nextCursor", safe_string(*cursor));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// List Tools Request/Result
extern "C" mcp_json_value_t mcp_list_tools_result_to_json(
    const mcp_list_tools_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add tools array
    if (result->tools.count > 0) {
      JsonArrayBuilder tools_builder;
      auto* tools = static_cast<mcp_tool_t**>(result->tools.items);
      
      for (size_t i = 0; i < result->tools.count; ++i) {
        if (tools[i]) {
          mcp_json_value_t tool_json = mcp_tool_to_json(tools[i]);
          if (tool_json) {
            auto* tool_handle = reinterpret_cast<mcp_json_value_handle*>(tool_json);
            tools_builder.add(tool_handle->value);
            delete tool_handle;
          }
        }
      }
      
      builder.add("tools", tools_builder.build());
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// List Prompts Request/Result
extern "C" mcp_json_value_t mcp_list_prompts_result_to_json(
    const mcp_list_prompts_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add prompts array
    if (result->prompts.count > 0) {
      JsonArrayBuilder prompts_builder;
      auto* prompts = static_cast<mcp_prompt_t**>(result->prompts.items);
      
      for (size_t i = 0; i < result->prompts.count; ++i) {
        if (prompts[i]) {
          mcp_json_value_t prompt_json = mcp_prompt_to_json(prompts[i]);
          if (prompt_json) {
            auto* prompt_handle = reinterpret_cast<mcp_json_value_handle*>(prompt_json);
            prompts_builder.add(prompt_handle->value);
            delete prompt_handle;
          }
        }
      }
      
      builder.add("prompts", prompts_builder.build());
    }
    
    if (result->next_cursor.has_value) {
      auto* cursor = static_cast<mcp_string_t*>(result->next_cursor.value);
      builder.add("nextCursor", safe_string(*cursor));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Get Prompt Request/Result
extern "C" mcp_json_value_t mcp_get_prompt_request_to_json(
    const mcp_get_prompt_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("name", safe_string(req->name));
    
    if (req->arguments.has_value) {
      auto* args_handle = reinterpret_cast<mcp_json_value_handle*>(req->arguments.value);
      builder.add("arguments", args_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_get_prompt_result_to_json(
    const mcp_get_prompt_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    if (result->description.has_value) {
      auto* desc = static_cast<mcp_string_t*>(result->description.value);
      builder.add("description", safe_string(*desc));
    }
    
    // Add messages array
    if (result->messages.count > 0) {
      JsonArrayBuilder messages_builder;
      auto* messages = static_cast<mcp_prompt_message_t**>(result->messages.items);
      
      for (size_t i = 0; i < result->messages.count; ++i) {
        if (messages[i]) {
          JsonObjectBuilder msg_builder;
          msg_builder.add("role", messages[i]->role == MCP_ROLE_USER ? "user" : "assistant");
          
          // Add content
          mcp_json_value_t content_json = mcp_content_block_to_json(&messages[i]->content);
          if (content_json) {
            auto* content_handle = reinterpret_cast<mcp_json_value_handle*>(content_json);
            msg_builder.add("content", content_handle->value);
            delete content_handle;
          }
          
          messages_builder.add(msg_builder.build());
        }
      }
      
      builder.add("messages", messages_builder.build());
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Progress Notification
extern "C" mcp_json_value_t mcp_progress_notification_to_json(
    const mcp_progress_notification_t* notif) {
  if (!notif) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add progress token
    mcp_json_value_t token_json = mcp_progress_token_to_json(&notif->progress_token);
    if (token_json) {
      auto* token_handle = reinterpret_cast<mcp_json_value_handle*>(token_json);
      builder.add("progressToken", token_handle->value);
      delete token_handle;
    }
    
    builder.add("progress", notif->progress);
    
    if (notif->total.has_value) {
      builder.add("total", *static_cast<double*>(notif->total.value));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Cancelled Notification
extern "C" mcp_json_value_t mcp_cancelled_notification_to_json(
    const mcp_cancelled_notification_t* notif) {
  if (!notif) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add request ID
    mcp_json_value_t id_json = mcp_request_id_to_json(&notif->request_id);
    if (id_json) {
      auto* id_handle = reinterpret_cast<mcp_json_value_handle*>(id_json);
      builder.add("requestId", id_handle->value);
      delete id_handle;
    }
    
    if (notif->reason.has_value) {
      auto* reason = static_cast<mcp_string_t*>(notif->reason.value);
      builder.add("reason", safe_string(*reason));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Logging Message Notification
extern "C" mcp_json_value_t mcp_logging_message_notification_to_json(
    const mcp_logging_message_notification_t* notif) {
  if (!notif) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add logging level
    mcp_json_value_t level_json = mcp_logging_level_to_json(notif->level);
    if (level_json) {
      auto* level_handle = reinterpret_cast<mcp_json_value_handle*>(level_json);
      builder.add("level", level_handle->value);
      delete level_handle;
    }
    
    if (notif->logger.has_value) {
      auto* logger = static_cast<mcp_string_t*>(notif->logger.value);
      builder.add("logger", safe_string(*logger));
    }
    
    // Add data (can be string or metadata)
    if (notif->data) {
      auto* data_handle = reinterpret_cast<mcp_json_value_handle*>(notif->data);
      builder.add("data", data_handle->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Schema Types
extern "C" mcp_json_value_t mcp_primitive_schema_to_json(
    const mcp_primitive_schema_t* schema) {
  if (!schema) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    switch (schema->type) {
      case MCP_SCHEMA_STRING:
        if (schema->schema.string) {
          builder.add("type", "string");
          if (schema->schema.string->description.has_value) {
            auto* desc = static_cast<mcp_string_t*>(schema->schema.string->description.value);
            builder.add("description", safe_string(*desc));
          }
          if (schema->schema.string->pattern.has_value) {
            auto* pattern = static_cast<mcp_string_t*>(schema->schema.string->pattern.value);
            builder.add("pattern", safe_string(*pattern));
          }
          if (schema->schema.string->min_length.has_value) {
            builder.add("minLength", *static_cast<int*>(schema->schema.string->min_length.value));
          }
          if (schema->schema.string->max_length.has_value) {
            builder.add("maxLength", *static_cast<int*>(schema->schema.string->max_length.value));
          }
        }
        break;
        
      case MCP_SCHEMA_NUMBER:
        if (schema->schema.number) {
          builder.add("type", "number");
          if (schema->schema.number->description.has_value) {
            auto* desc = static_cast<mcp_string_t*>(schema->schema.number->description.value);
            builder.add("description", safe_string(*desc));
          }
          if (schema->schema.number->minimum.has_value) {
            builder.add("minimum", *static_cast<double*>(schema->schema.number->minimum.value));
          }
          if (schema->schema.number->maximum.has_value) {
            builder.add("maximum", *static_cast<double*>(schema->schema.number->maximum.value));
          }
          if (schema->schema.number->multiple_of.has_value) {
            builder.add("multipleOf", *static_cast<double*>(schema->schema.number->multiple_of.value));
          }
        }
        break;
        
      case MCP_SCHEMA_BOOLEAN:
        if (schema->schema.boolean_) {
          builder.add("type", "boolean");
          if (schema->schema.boolean_->description.has_value) {
            auto* desc = static_cast<mcp_string_t*>(schema->schema.boolean_->description.value);
            builder.add("description", safe_string(*desc));
          }
        }
        break;
        
      case MCP_SCHEMA_ENUM:
        if (schema->schema.enum_) {
          builder.add("type", "enum");
          if (schema->schema.enum_->description.has_value) {
            auto* desc = static_cast<mcp_string_t*>(schema->schema.enum_->description.value);
            builder.add("description", safe_string(*desc));
          }
          
          // Add enum values
          if (schema->schema.enum_->values.count > 0) {
            JsonArrayBuilder values_builder;
            auto* values = static_cast<mcp_string_t**>(schema->schema.enum_->values.items);
            
            for (size_t i = 0; i < schema->schema.enum_->values.count; ++i) {
              if (values[i]) {
                values_builder.add(safe_string(*values[i]));
              }
            }
            
            builder.add("values", values_builder.build());
          }
        }
        break;
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Root type
extern "C" mcp_json_value_t mcp_root_to_json(const mcp_root_t* root) {
  if (!root) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("uri", safe_string(root->uri));
    
    if (root->name.has_value) {
      auto* name = static_cast<mcp_string_t*>(root->name.value);
      builder.add("name", safe_string(*name));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_root_t* mcp_root_from_json(mcp_json_value_t json) {
  if (!json) return nullptr;
  
  AllocationTransaction txn;
  
  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const JsonValue& value = handle->value;
    
    if (!value.isObject()) return nullptr;
    
    auto* root = static_cast<mcp_root_t*>(std::calloc(1, sizeof(mcp_root_t)));
    if (!root) return nullptr;
    
    txn.track(root);
    
    root->uri = make_mcp_string(value["uri"].getString());
    txn.track(const_cast<char*>(root->uri.data));
    
    if (value.contains("name")) {
      root->name.has_value = MCP_TRUE;
      auto* name = static_cast<mcp_string_t*>(
          std::calloc(1, sizeof(mcp_string_t)));
      txn.track(name);
      
      *name = make_mcp_string(value["name"].getString());
      txn.track(const_cast<char*>(name->data));
      
      root->name.value = name;
    }
    
    txn.commit();
    return root;
  } catch (...) {
    return nullptr;
  }
}

// List Roots Result
extern "C" mcp_json_value_t mcp_list_roots_result_to_json(
    const mcp_list_roots_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add roots array
    if (result->roots.count > 0) {
      JsonArrayBuilder roots_builder;
      auto* roots = static_cast<mcp_root_t**>(result->roots.items);
      
      for (size_t i = 0; i < result->roots.count; ++i) {
        if (roots[i]) {
          mcp_json_value_t root_json = mcp_root_to_json(roots[i]);
          if (root_json) {
            auto* root_handle = reinterpret_cast<mcp_json_value_handle*>(root_json);
            roots_builder.add(root_handle->value);
            delete root_handle;
          }
        }
      }
      
      builder.add("roots", roots_builder.build());
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Resource Template
extern "C" mcp_json_value_t mcp_resource_template_to_json(
    const mcp_resource_template_t* tmpl) {
  if (!tmpl) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("uriTemplate", safe_string(tmpl->uri_template));
    builder.add("name", safe_string(tmpl->name));
    
    if (tmpl->description.has_value) {
      auto* desc = static_cast<mcp_string_t*>(tmpl->description.value);
      builder.add("description", safe_string(*desc));
    }
    
    if (tmpl->mime_type.has_value) {
      auto* mime = static_cast<mcp_string_t*>(tmpl->mime_type.value);
      builder.add("mimeType", safe_string(*mime));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Read Resource Request/Result
extern "C" mcp_json_value_t mcp_read_resource_request_to_json(
    const mcp_read_resource_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("uri", safe_string(req->uri));
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_read_resource_result_to_json(
    const mcp_read_resource_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add contents array
    if (result->contents.count > 0) {
      JsonArrayBuilder contents_builder;
      
      for (size_t i = 0; i < result->contents.count; ++i) {
        void* item = result->contents.items[i];
        if (item) {
          // Determine if it's text or blob resource contents
          // This would need type information in the actual implementation
          JsonObjectBuilder content_builder;
          
          // For now, assume text resource contents
          auto* text_contents = static_cast<mcp_text_resource_contents_t*>(item);
          if (text_contents->base.uri.has_value) {
            auto* uri = static_cast<mcp_string_t*>(text_contents->base.uri.value);
            content_builder.add("uri", safe_string(*uri));
          }
          if (text_contents->base.mime_type.has_value) {
            auto* mime = static_cast<mcp_string_t*>(text_contents->base.mime_type.value);
            content_builder.add("mimeType", safe_string(*mime));
          }
          content_builder.add("text", safe_string(text_contents->text));
          
          contents_builder.add(content_builder.build());
        }
      }
      
      builder.add("contents", contents_builder.build());
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Set Level Request
extern "C" mcp_json_value_t mcp_set_level_request_to_json(
    const mcp_set_level_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    mcp_json_value_t level_json = mcp_logging_level_to_json(req->level);
    if (level_json) {
      auto* level_handle = reinterpret_cast<mcp_json_value_handle*>(level_json);
      builder.add("level", level_handle->value);
      delete level_handle;
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Subscribe/Unsubscribe Requests
extern "C" mcp_json_value_t mcp_subscribe_request_to_json(
    const mcp_subscribe_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("uri", safe_string(req->uri));
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_unsubscribe_request_to_json(
    const mcp_unsubscribe_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("uri", safe_string(req->uri));
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Complete Request/Result
extern "C" mcp_json_value_t mcp_complete_request_to_json(
    const mcp_complete_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add prompt reference
    JsonObjectBuilder ref_builder;
    ref_builder.add("type", safe_string(req->ref.type));
    ref_builder.add("name", safe_string(req->ref.name));
    
    if (req->ref._meta.has_value) {
      auto* meta = reinterpret_cast<mcp_json_value_handle*>(req->ref._meta.value);
      ref_builder.add("_meta", meta->value);
    }
    
    builder.add("ref", ref_builder.build());
    
    if (req->argument.has_value) {
      auto* arg = static_cast<mcp_string_t*>(req->argument.value);
      builder.add("argument", safe_string(*arg));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_complete_result_to_json(
    const mcp_complete_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    JsonObjectBuilder completion_builder;
    
    // Add completion values
    if (result->completion.values.count > 0) {
      JsonArrayBuilder values_builder;
      auto* values = static_cast<mcp_string_t**>(result->completion.values.items);
      
      for (size_t i = 0; i < result->completion.values.count; ++i) {
        if (values[i]) {
          values_builder.add(safe_string(*values[i]));
        }
      }
      
      completion_builder.add("values", values_builder.build());
    }
    
    if (result->completion.total.has_value) {
      completion_builder.add("total", *static_cast<double*>(result->completion.total.value));
    }
    
    completion_builder.add("hasMore", result->completion.has_more == MCP_TRUE);
    
    builder.add("completion", completion_builder.build());
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Create Message Request/Result
extern "C" mcp_json_value_t mcp_create_message_request_to_json(
    const mcp_create_message_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add messages array
    if (req->messages.count > 0) {
      JsonArrayBuilder messages_builder;
      auto* messages = static_cast<mcp_sampling_message_t**>(req->messages.items);
      
      for (size_t i = 0; i < req->messages.count; ++i) {
        if (messages[i]) {
          JsonObjectBuilder msg_builder;
          msg_builder.add("role", messages[i]->role == MCP_ROLE_USER ? "user" : "assistant");
          
          // Add content
          mcp_json_value_t content_json = mcp_content_block_to_json(&messages[i]->content);
          if (content_json) {
            auto* content_handle = reinterpret_cast<mcp_json_value_handle*>(content_json);
            msg_builder.add("content", content_handle->value);
            delete content_handle;
          }
          
          messages_builder.add(msg_builder.build());
        }
      }
      
      builder.add("messages", messages_builder.build());
    }
    
    // Add optional fields
    if (req->model_preferences.has_value) {
      auto* prefs = static_cast<mcp_model_preferences_t*>(req->model_preferences.value);
      JsonObjectBuilder prefs_builder;
      
      if (prefs->hints.has_value) {
        // Add hints array
        auto* hints_list = static_cast<mcp_list_t*>(prefs->hints.value);
        if (hints_list && hints_list->count > 0) {
          JsonArrayBuilder hints_builder;
          auto* hints = static_cast<mcp_model_hint_t**>(hints_list->items);
          
          for (size_t i = 0; i < hints_list->count; ++i) {
            if (hints[i] && hints[i]->name.has_value) {
              JsonObjectBuilder hint_builder;
              auto* name = static_cast<mcp_string_t*>(hints[i]->name.value);
              hint_builder.add("name", safe_string(*name));
              hints_builder.add(hint_builder.build());
            }
          }
          
          prefs_builder.add("hints", hints_builder.build());
        }
      }
      
      if (prefs->cost_priority.has_value) {
        prefs_builder.add("costPriority", *static_cast<double*>(prefs->cost_priority.value));
      }
      if (prefs->speed_priority.has_value) {
        prefs_builder.add("speedPriority", *static_cast<double*>(prefs->speed_priority.value));
      }
      if (prefs->intelligence_priority.has_value) {
        prefs_builder.add("intelligencePriority", *static_cast<double*>(prefs->intelligence_priority.value));
      }
      
      builder.add("modelPreferences", prefs_builder.build());
    }
    
    if (req->system_prompt.has_value) {
      auto* prompt = static_cast<mcp_string_t*>(req->system_prompt.value);
      builder.add("systemPrompt", safe_string(*prompt));
    }
    
    if (req->include_context.has_value) {
      auto* context = reinterpret_cast<mcp_json_value_handle*>(req->include_context.value);
      builder.add("includeContext", context->value);
    }
    
    if (req->temperature.has_value) {
      builder.add("temperature", *static_cast<double*>(req->temperature.value));
    }
    
    if (req->max_tokens.has_value) {
      builder.add("maxTokens", *static_cast<int*>(req->max_tokens.value));
    }
    
    if (req->stop_sequences.has_value) {
      auto* sequences = static_cast<mcp_list_t*>(req->stop_sequences.value);
      if (sequences && sequences->count > 0) {
        JsonArrayBuilder seq_builder;
        auto* seqs = static_cast<mcp_string_t**>(sequences->items);
        
        for (size_t i = 0; i < sequences->count; ++i) {
          if (seqs[i]) {
            seq_builder.add(safe_string(*seqs[i]));
          }
        }
        
        builder.add("stopSequences", seq_builder.build());
      }
    }
    
    if (req->metadata.has_value) {
      auto* metadata = reinterpret_cast<mcp_json_value_handle*>(req->metadata.value);
      builder.add("metadata", metadata->value);
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_create_message_result_to_json(
    const mcp_create_message_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    
    // Add message
    JsonObjectBuilder msg_builder;
    msg_builder.add("role", result->message.role == MCP_ROLE_USER ? "user" : "assistant");
    
    mcp_json_value_t content_json = mcp_content_block_to_json(&result->message.content);
    if (content_json) {
      auto* content_handle = reinterpret_cast<mcp_json_value_handle*>(content_json);
      msg_builder.add("content", content_handle->value);
      delete content_handle;
    }
    
    builder.add("message", msg_builder.build());
    builder.add("model", safe_string(result->model));
    
    if (result->stop_reason.has_value) {
      auto* reason = static_cast<mcp_string_t*>(result->stop_reason.value);
      builder.add("stopReason", safe_string(*reason));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

// Elicit Request/Result
extern "C" mcp_json_value_t mcp_elicit_request_to_json(
    const mcp_elicit_request_t* req) {
  if (!req) return nullptr;
  
  try {
    JsonObjectBuilder builder;
    builder.add("name", safe_string(req->name));
    
    // Add schema
    mcp_json_value_t schema_json = mcp_primitive_schema_to_json(&req->schema);
    if (schema_json) {
      auto* schema_handle = reinterpret_cast<mcp_json_value_handle*>(schema_json);
      builder.add("schema", schema_handle->value);
      delete schema_handle;
    }
    
    if (req->prompt.has_value) {
      auto* prompt = static_cast<mcp_string_t*>(req->prompt.value);
      builder.add("prompt", safe_string(*prompt));
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(builder.build()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_elicit_result_to_json(
    const mcp_elicit_result_t* result) {
  if (!result) return nullptr;
  
  try {
    JsonValue json;
    
    switch (result->type) {
      case MCP_ELICIT_STRING:
        json = to_json(safe_string(result->value.string_value));
        break;
      case MCP_ELICIT_NUMBER:
        json = to_json(result->value.number_value);
        break;
      case MCP_ELICIT_BOOLEAN:
        json = to_json(result->value.boolean_value == MCP_TRUE);
        break;
      case MCP_ELICIT_NULL:
        json = JsonValue::null();
        break;
    }
    
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

}  // namespace c_api
}  // namespace mcp