/**
 * @file mcp_c_api_json.cc
 * @brief JSON serialization/deserialization implementation for MCP C types
 *
 * Provides conversion between MCP C types and JSON representations
 * for cross-language interoperability and protocol communication.
 */

#include <cstring>
#include <string>

#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/types.h"

namespace mcp {
namespace c_api {

// Helper to convert C string to std::string safely
static std::string safe_string(const char* str) {
  return str ? std::string(str) : std::string();
}

// Helper to allocate and copy string
static char* alloc_string(const std::string& str) {
  if (str.empty())
    return nullptr;
  char* result = static_cast<char*>(std::malloc(str.size() + 1));
  if (result) {
    std::strcpy(result, str.c_str());
  }
  return result;
}

// JSON value handle implementation
struct mcp_json_value_handle {
  mcp::json::JsonValue value;

  explicit mcp_json_value_handle(const mcp::json::JsonValue& v) : value(v) {}
  explicit mcp_json_value_handle(mcp::json::JsonValue&& v)
      : value(std::move(v)) {}
};

// Content block serialization
extern "C" mcp_json_value_t mcp_content_block_to_json(
    const mcp_content_block_t* block) {
  if (!block)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();

    switch (block->type) {
      case MCP_CONTENT_TEXT: {
        json["type"] = "text";
        if (block->text.text) {
          json["text"] = safe_string(block->text.text);
        }
        // Add annotations if present
        if (block->text.annotations.has_value) {
          mcp::json::JsonValue annotations = mcp::json::JsonValue::object();
          if (block->text.annotations.value.audience_count > 0) {
            mcp::json::JsonValue audience = mcp::json::JsonValue::array();
            for (size_t i = 0; i < block->text.annotations.value.audience_count;
                 ++i) {
              audience.push_back(block->text.annotations.value.audience[i] ==
                                         MCP_ROLE_USER
                                     ? "user"
                                     : "assistant");
            }
            annotations["audience"] = audience;
          }
          if (block->text.annotations.value.priority_set) {
            annotations["priority"] = block->text.annotations.value.priority;
          }
          json["annotations"] = annotations;
        }
        break;
      }
      case MCP_CONTENT_IMAGE: {
        json["type"] = "image";
        if (block->image.data) {
          json["data"] = safe_string(block->image.data);
        }
        if (block->image.mime_type) {
          json["mimeType"] = safe_string(block->image.mime_type);
        }
        break;
      }
      case MCP_CONTENT_AUDIO: {
        json["type"] = "audio";
        if (block->audio.data) {
          json["data"] = safe_string(block->audio.data);
        }
        if (block->audio.mime_type) {
          json["mimeType"] = safe_string(block->audio.mime_type);
        }
        break;
      }
      case MCP_CONTENT_RESOURCE: {
        json["type"] = "resource";
        mcp::json::JsonValue resource = mcp::json::JsonValue::object();
        if (block->resource.resource.uri) {
          resource["uri"] = safe_string(block->resource.resource.uri);
        }
        if (block->resource.resource.name) {
          resource["name"] = safe_string(block->resource.resource.name);
        }
        if (block->resource.resource.description) {
          resource["description"] =
              safe_string(block->resource.resource.description);
        }
        if (block->resource.resource.mime_type) {
          resource["mimeType"] =
              safe_string(block->resource.resource.mime_type);
        }
        json["resource"] = resource;
        break;
      }
      case MCP_CONTENT_EMBEDDED: {
        json["type"] = "embedded";
        mcp::json::JsonValue resource = mcp::json::JsonValue::object();
        if (block->embedded.resource.uri) {
          resource["uri"] = safe_string(block->embedded.resource.uri);
        }
        if (block->embedded.resource.name) {
          resource["name"] = safe_string(block->embedded.resource.name);
        }
        if (block->embedded.resource.description) {
          resource["description"] =
              safe_string(block->embedded.resource.description);
        }
        if (block->embedded.resource.mime_type) {
          resource["mimeType"] =
              safe_string(block->embedded.resource.mime_type);
        }
        json["resource"] = resource;

        // Add nested content
        if (block->embedded.content_count > 0 && block->embedded.content) {
          mcp::json::JsonValue content = mcp::json::JsonValue::array();
          for (size_t i = 0; i < block->embedded.content_count; ++i) {
            mcp_json_value_t nested =
                mcp_content_block_to_json(&block->embedded.content[i]);
            if (nested) {
              auto* handle = reinterpret_cast<mcp_json_value_handle*>(nested);
              content.push_back(handle->value);
              delete handle;
            }
          }
          json["content"] = content;
        }
        break;
      }
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Content block deserialization
extern "C" mcp_content_block_t* mcp_content_block_from_json(
    mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const mcp::json::JsonValue& value = handle->value;

    if (!value.is_object())
      return nullptr;

    auto* block = static_cast<mcp_content_block_t*>(
        std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block)
      return nullptr;

    // Get type field
    auto type_it = value.find("type");
    if (type_it == value.end() || !type_it->is_string()) {
      std::free(block);
      return nullptr;
    }

    std::string type_str = type_it->get<std::string>();

    if (type_str == "text") {
      block->type = MCP_CONTENT_TEXT;
      auto text_it = value.find("text");
      if (text_it != value.end() && text_it->is_string()) {
        block->text.text = alloc_string(text_it->get<std::string>());
      }

      // Parse annotations if present
      auto ann_it = value.find("annotations");
      if (ann_it != value.end() && ann_it->is_object()) {
        block->text.annotations.has_value = true;

        auto aud_it = ann_it->find("audience");
        if (aud_it != ann_it->end() && aud_it->is_array()) {
          size_t count = aud_it->size();
          if (count > 0) {
            block->text.annotations.value.audience = static_cast<mcp_role_t*>(
                std::calloc(count, sizeof(mcp_role_t)));
            block->text.annotations.value.audience_count = count;

            for (size_t i = 0; i < count; ++i) {
              if ((*aud_it)[i].is_string()) {
                std::string role = (*aud_it)[i].get<std::string>();
                block->text.annotations.value.audience[i] =
                    (role == "user") ? MCP_ROLE_USER : MCP_ROLE_ASSISTANT;
              }
            }
          }
        }

        auto pri_it = ann_it->find("priority");
        if (pri_it != ann_it->end() && pri_it->is_number()) {
          block->text.annotations.value.priority = pri_it->get<double>();
          block->text.annotations.value.priority_set = true;
        }
      }
    } else if (type_str == "image") {
      block->type = MCP_CONTENT_IMAGE;
      auto data_it = value.find("data");
      if (data_it != value.end() && data_it->is_string()) {
        block->image.data = alloc_string(data_it->get<std::string>());
      }
      auto mime_it = value.find("mimeType");
      if (mime_it != value.end() && mime_it->is_string()) {
        block->image.mime_type = alloc_string(mime_it->get<std::string>());
      }
    } else if (type_str == "audio") {
      block->type = MCP_CONTENT_AUDIO;
      auto data_it = value.find("data");
      if (data_it != value.end() && data_it->is_string()) {
        block->audio.data = alloc_string(data_it->get<std::string>());
      }
      auto mime_it = value.find("mimeType");
      if (mime_it != value.end() && mime_it->is_string()) {
        block->audio.mime_type = alloc_string(mime_it->get<std::string>());
      }
    } else if (type_str == "resource") {
      block->type = MCP_CONTENT_RESOURCE;
      auto res_it = value.find("resource");
      if (res_it != value.end() && res_it->is_object()) {
        auto uri_it = res_it->find("uri");
        if (uri_it != res_it->end() && uri_it->is_string()) {
          block->resource.resource.uri =
              alloc_string(uri_it->get<std::string>());
        }
        auto name_it = res_it->find("name");
        if (name_it != res_it->end() && name_it->is_string()) {
          block->resource.resource.name =
              alloc_string(name_it->get<std::string>());
        }
        auto desc_it = res_it->find("description");
        if (desc_it != res_it->end() && desc_it->is_string()) {
          block->resource.resource.description =
              alloc_string(desc_it->get<std::string>());
        }
        auto mime_it = res_it->find("mimeType");
        if (mime_it != res_it->end() && mime_it->is_string()) {
          block->resource.resource.mime_type =
              alloc_string(mime_it->get<std::string>());
        }
      }
    } else if (type_str == "embedded") {
      block->type = MCP_CONTENT_EMBEDDED;

      // Parse resource
      auto res_it = value.find("resource");
      if (res_it != value.end() && res_it->is_object()) {
        auto uri_it = res_it->find("uri");
        if (uri_it != res_it->end() && uri_it->is_string()) {
          block->embedded.resource.uri =
              alloc_string(uri_it->get<std::string>());
        }
        auto name_it = res_it->find("name");
        if (name_it != res_it->end() && name_it->is_string()) {
          block->embedded.resource.name =
              alloc_string(name_it->get<std::string>());
        }
        auto desc_it = res_it->find("description");
        if (desc_it != res_it->end() && desc_it->is_string()) {
          block->embedded.resource.description =
              alloc_string(desc_it->get<std::string>());
        }
        auto mime_it = res_it->find("mimeType");
        if (mime_it != res_it->end() && mime_it->is_string()) {
          block->embedded.resource.mime_type =
              alloc_string(mime_it->get<std::string>());
        }
      }

      // Parse nested content
      auto content_it = value.find("content");
      if (content_it != value.end() && content_it->is_array()) {
        size_t count = content_it->size();
        if (count > 0) {
          block->embedded.content = static_cast<mcp_content_block_t*>(
              std::calloc(count, sizeof(mcp_content_block_t)));
          block->embedded.content_count = count;

          for (size_t i = 0; i < count; ++i) {
            mcp_json_value_handle nested_handle((*content_it)[i]);
            mcp_content_block_t* nested = mcp_content_block_from_json(
                reinterpret_cast<mcp_json_value_t>(&nested_handle));
            if (nested) {
              block->embedded.content[i] = *nested;
              std::free(nested);
            }
          }
        }
      }
    } else {
      std::free(block);
      return nullptr;
    }

    return block;
  } catch (...) {
    return nullptr;
  }
}

// Tool serialization
extern "C" mcp_json_value_t mcp_tool_to_json(const mcp_tool_t* tool) {
  if (!tool)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();

    if (tool->name) {
      json["name"] = safe_string(tool->name);
    }
    if (tool->description) {
      json["description"] = safe_string(tool->description);
    }
    if (tool->input_schema) {
      auto* schema_handle =
          reinterpret_cast<mcp_json_value_handle*>(tool->input_schema);
      json["inputSchema"] = schema_handle->value;
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Tool deserialization
extern "C" mcp_tool_t* mcp_tool_from_json(mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const mcp::json::JsonValue& value = handle->value;

    if (!value.is_object())
      return nullptr;

    auto* tool = static_cast<mcp_tool_t*>(std::calloc(1, sizeof(mcp_tool_t)));
    if (!tool)
      return nullptr;

    auto name_it = value.find("name");
    if (name_it != value.end() && name_it->is_string()) {
      tool->name = alloc_string(name_it->get<std::string>());
    }

    auto desc_it = value.find("description");
    if (desc_it != value.end() && desc_it->is_string()) {
      tool->description = alloc_string(desc_it->get<std::string>());
    }

    auto schema_it = value.find("inputSchema");
    if (schema_it != value.end()) {
      tool->input_schema = reinterpret_cast<mcp_json_value_t>(
          new mcp_json_value_handle(schema_it.value()));
    }

    return tool;
  } catch (...) {
    return nullptr;
  }
}

// Prompt serialization
extern "C" mcp_json_value_t mcp_prompt_to_json(const mcp_prompt_t* prompt) {
  if (!prompt)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();

    if (prompt->name) {
      json["name"] = safe_string(prompt->name);
    }
    if (prompt->description) {
      json["description"] = safe_string(prompt->description);
    }

    if (prompt->argument_count > 0 && prompt->arguments) {
      mcp::json::JsonValue args = mcp::json::JsonValue::array();
      for (size_t i = 0; i < prompt->argument_count; ++i) {
        mcp::json::JsonValue arg = mcp::json::JsonValue::object();
        if (prompt->arguments[i].name) {
          arg["name"] = safe_string(prompt->arguments[i].name);
        }
        if (prompt->arguments[i].description) {
          arg["description"] = safe_string(prompt->arguments[i].description);
        }
        arg["required"] = prompt->arguments[i].required;
        args.push_back(arg);
      }
      json["arguments"] = args;
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Prompt deserialization
extern "C" mcp_prompt_t* mcp_prompt_from_json(mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const mcp::json::JsonValue& value = handle->value;

    if (!value.is_object())
      return nullptr;

    auto* prompt =
        static_cast<mcp_prompt_t*>(std::calloc(1, sizeof(mcp_prompt_t)));
    if (!prompt)
      return nullptr;

    auto name_it = value.find("name");
    if (name_it != value.end() && name_it->is_string()) {
      prompt->name = alloc_string(name_it->get<std::string>());
    }

    auto desc_it = value.find("description");
    if (desc_it != value.end() && desc_it->is_string()) {
      prompt->description = alloc_string(desc_it->get<std::string>());
    }

    auto args_it = value.find("arguments");
    if (args_it != value.end() && args_it->is_array()) {
      size_t count = args_it->size();
      if (count > 0) {
        prompt->arguments = static_cast<mcp_prompt_argument_t*>(
            std::calloc(count, sizeof(mcp_prompt_argument_t)));
        prompt->argument_count = count;

        for (size_t i = 0; i < count; ++i) {
          const auto& arg = (*args_it)[i];
          if (arg.is_object()) {
            auto arg_name = arg.find("name");
            if (arg_name != arg.end() && arg_name->is_string()) {
              prompt->arguments[i].name =
                  alloc_string(arg_name->get<std::string>());
            }
            auto arg_desc = arg.find("description");
            if (arg_desc != arg.end() && arg_desc->is_string()) {
              prompt->arguments[i].description =
                  alloc_string(arg_desc->get<std::string>());
            }
            auto arg_req = arg.find("required");
            if (arg_req != arg.end() && arg_req->is_boolean()) {
              prompt->arguments[i].required = arg_req->get<bool>();
            }
          }
        }
      }
    }

    return prompt;
  } catch (...) {
    return nullptr;
  }
}

// Resource serialization
extern "C" mcp_json_value_t mcp_resource_to_json(
    const mcp_resource_t* resource) {
  if (!resource)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();

    if (resource->uri) {
      json["uri"] = safe_string(resource->uri);
    }
    if (resource->name) {
      json["name"] = safe_string(resource->name);
    }
    if (resource->description) {
      json["description"] = safe_string(resource->description);
    }
    if (resource->mime_type) {
      json["mimeType"] = safe_string(resource->mime_type);
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Resource deserialization
extern "C" mcp_resource_t* mcp_resource_from_json(mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const mcp::json::JsonValue& value = handle->value;

    if (!value.is_object())
      return nullptr;

    auto* resource =
        static_cast<mcp_resource_t*>(std::calloc(1, sizeof(mcp_resource_t)));
    if (!resource)
      return nullptr;

    auto uri_it = value.find("uri");
    if (uri_it != value.end() && uri_it->is_string()) {
      resource->uri = alloc_string(uri_it->get<std::string>());
    }

    auto name_it = value.find("name");
    if (name_it != value.end() && name_it->is_string()) {
      resource->name = alloc_string(name_it->get<std::string>());
    }

    auto desc_it = value.find("description");
    if (desc_it != value.end() && desc_it->is_string()) {
      resource->description = alloc_string(desc_it->get<std::string>());
    }

    auto mime_it = value.find("mimeType");
    if (mime_it != value.end() && mime_it->is_string()) {
      resource->mime_type = alloc_string(mime_it->get<std::string>());
    }

    return resource;
  } catch (...) {
    return nullptr;
  }
}

// Message serialization
extern "C" mcp_json_value_t mcp_message_to_json(const mcp_message_t* message) {
  if (!message)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();

    json["role"] = (message->role == MCP_ROLE_USER) ? "user" : "assistant";

    if (message->content) {
      mcp_json_value_t content_json =
          mcp_content_block_to_json(message->content);
      if (content_json) {
        auto* handle = reinterpret_cast<mcp_json_value_handle*>(content_json);
        json["content"] = handle->value;
        delete handle;
      }
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Message deserialization
extern "C" mcp_message_t* mcp_message_from_json(mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const mcp::json::JsonValue& value = handle->value;

    if (!value.is_object())
      return nullptr;

    auto* message =
        static_cast<mcp_message_t*>(std::calloc(1, sizeof(mcp_message_t)));
    if (!message)
      return nullptr;

    auto role_it = value.find("role");
    if (role_it != value.end() && role_it->is_string()) {
      std::string role = role_it->get<std::string>();
      message->role = (role == "user") ? MCP_ROLE_USER : MCP_ROLE_ASSISTANT;
    }

    auto content_it = value.find("content");
    if (content_it != value.end()) {
      mcp_json_value_handle content_handle(content_it.value());
      message->content = mcp_content_block_from_json(
          reinterpret_cast<mcp_json_value_t>(&content_handle));
    }

    return message;
  } catch (...) {
    return nullptr;
  }
}

// Error serialization
extern "C" mcp_json_value_t mcp_error_to_json(const mcp_error_t* error) {
  if (!error)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();

    json["code"] = error->code;
    if (error->message) {
      json["message"] = safe_string(error->message);
    }
    if (error->data) {
      auto* data_handle = reinterpret_cast<mcp_json_value_handle*>(error->data);
      json["data"] = data_handle->value;
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

// Error deserialization
extern "C" mcp_error_t* mcp_error_from_json(mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    const mcp::json::JsonValue& value = handle->value;

    if (!value.is_object())
      return nullptr;

    auto* error =
        static_cast<mcp_error_t*>(std::calloc(1, sizeof(mcp_error_t)));
    if (!error)
      return nullptr;

    auto code_it = value.find("code");
    if (code_it != value.end() && code_it->is_number_integer()) {
      error->code = code_it->get<int>();
    }

    auto msg_it = value.find("message");
    if (msg_it != value.end() && msg_it->is_string()) {
      error->message = alloc_string(msg_it->get<std::string>());
    }

    auto data_it = value.find("data");
    if (data_it != value.end()) {
      error->data = reinterpret_cast<mcp_json_value_t>(
          new mcp_json_value_handle(data_it.value()));
    }

    return error;
  } catch (...) {
    return nullptr;
  }
}

// JSON value manipulation
extern "C" mcp_json_value_t mcp_json_parse(const char* json_string) {
  if (!json_string)
    return nullptr;

  try {
    mcp::json::JsonValue value = mcp::json::JsonValue::parse(json_string);
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(value)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" char* mcp_json_stringify(mcp_json_value_t json) {
  if (!json)
    return nullptr;

  try {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    std::string str = handle->value.dump();
    return alloc_string(str);
  } catch (...) {
    return nullptr;
  }
}

extern "C" void mcp_json_free(mcp_json_value_t json) {
  if (json) {
    auto* handle = reinterpret_cast<mcp_json_value_handle*>(json);
    delete handle;
  }
}

extern "C" mcp_json_value_t mcp_json_create_object() {
  try {
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(mcp::json::JsonValue::object()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_json_create_array() {
  try {
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(mcp::json::JsonValue::array()));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_json_create_string(const char* value) {
  if (!value)
    return nullptr;
  try {
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(mcp::json::JsonValue(safe_string(value))));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_json_create_number(double value) {
  try {
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(mcp::json::JsonValue(value)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_json_create_bool(bool value) {
  try {
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(mcp::json::JsonValue(value)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_json_create_null() {
  try {
    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(mcp::json::JsonValue(nullptr)));
  } catch (...) {
    return nullptr;
  }
}

// Schema serialization helpers
extern "C" mcp_json_value_t mcp_string_schema_to_json(
    const mcp_string_schema_t* schema) {
  if (!schema)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();
    json["type"] = "string";

    if (schema->description) {
      json["description"] = safe_string(schema->description);
    }
    if (schema->pattern) {
      json["pattern"] = safe_string(schema->pattern);
    }
    if (schema->min_length_set) {
      json["minLength"] = schema->min_length;
    }
    if (schema->max_length_set) {
      json["maxLength"] = schema->max_length;
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_number_schema_to_json(
    const mcp_number_schema_t* schema) {
  if (!schema)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();
    json["type"] = "number";

    if (schema->description) {
      json["description"] = safe_string(schema->description);
    }
    if (schema->minimum_set) {
      json["minimum"] = schema->minimum;
    }
    if (schema->maximum_set) {
      json["maximum"] = schema->maximum;
    }
    if (schema->multiple_of_set) {
      json["multipleOf"] = schema->multiple_of;
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_boolean_schema_to_json(
    const mcp_boolean_schema_t* schema) {
  if (!schema)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();
    json["type"] = "boolean";

    if (schema->description) {
      json["description"] = safe_string(schema->description);
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

extern "C" mcp_json_value_t mcp_enum_schema_to_json(
    const mcp_enum_schema_t* schema) {
  if (!schema)
    return nullptr;

  try {
    mcp::json::JsonValue json = mcp::json::JsonValue::object();
    json["type"] = "enum";

    if (schema->description) {
      json["description"] = safe_string(schema->description);
    }

    if (schema->value_count > 0 && schema->values) {
      mcp::json::JsonValue values = mcp::json::JsonValue::array();
      for (size_t i = 0; i < schema->value_count; ++i) {
        if (schema->values[i]) {
          values.push_back(safe_string(schema->values[i]));
        }
      }
      json["values"] = values;
    }

    return reinterpret_cast<mcp_json_value_t>(
        new mcp_json_value_handle(std::move(json)));
  } catch (...) {
    return nullptr;
  }
}

}  // namespace c_api
}  // namespace mcp