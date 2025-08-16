/**
 * @file mcp_c_api_builders.cc
 * @brief Builder pattern implementations for MCP C types
 * 
 * Provides fluent builder APIs for constructing complex MCP types
 * in a type-safe and ergonomic manner.
 */

#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_bridge.h"
#include <cstdlib>
#include <cstring>

namespace mcp {
namespace c_api {

// Helper to allocate and copy string
static char* alloc_string(const char* str) {
    if (!str) return nullptr;
    size_t len = std::strlen(str);
    char* result = static_cast<char*>(std::malloc(len + 1));
    if (result) {
        std::strcpy(result, str);
    }
    return result;
}

// Content block builders
extern "C" mcp_content_block_t* mcp_text_content_create(const char* text) {
    if (!text) return nullptr;
    
    auto* block = static_cast<mcp_content_block_t*>(std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block) return nullptr;
    
    auto* text_content = static_cast<mcp_text_content_t*>(std::calloc(1, sizeof(mcp_text_content_t)));
    if (!text_content) {
        std::free(block);
        return nullptr;
    }
    
    text_content->type.data = "text";
    text_content->type.length = 4;
    text_content->text.data = alloc_string(text);
    text_content->text.length = std::strlen(text);
    
    if (!text_content->text.data) {
        std::free(text_content);
        std::free(block);
        return nullptr;
    }
    
    block->type = MCP_CONTENT_TEXT;
    block->content.text = text_content;
    
    return block;
}

extern "C" mcp_content_block_t* mcp_text_content_with_annotations(
    const char* text,
    const mcp_role_t* audience,
    size_t audience_count,
    double priority
) {
    auto* block = mcp_text_content_create(text);
    if (!block) return nullptr;
    
    if (audience && audience_count > 0) {
        block->text.annotations.has_value = true;
        block->text.annotations.value.audience = static_cast<mcp_role_t*>(
            std::malloc(audience_count * sizeof(mcp_role_t)));
        if (block->text.annotations.value.audience) {
            std::memcpy(block->text.annotations.value.audience, audience,
                       audience_count * sizeof(mcp_role_t));
            block->text.annotations.value.audience_count = audience_count;
        }
    }
    
    if (priority >= 0.0 && priority <= 1.0) {
        block->text.annotations.has_value = true;
        block->text.annotations.value.priority = priority;
        block->text.annotations.value.priority_set = true;
    }
    
    return block;
}

extern "C" mcp_content_block_t* mcp_image_content_create(
    const char* data,
    const char* mime_type
) {
    if (!data || !mime_type) return nullptr;
    
    auto* block = static_cast<mcp_content_block_t*>(std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block) return nullptr;
    
    auto* image_content = static_cast<mcp_image_content_t*>(std::calloc(1, sizeof(mcp_image_content_t)));
    if (!image_content) {
        std::free(block);
        return nullptr;
    }
    
    image_content->type.data = "image";
    image_content->type.length = 5;
    image_content->data.data = alloc_string(data);
    image_content->data.length = std::strlen(data);
    image_content->mime_type.data = alloc_string(mime_type);
    image_content->mime_type.length = std::strlen(mime_type);
    
    if (!image_content->data.data || !image_content->mime_type.data) {
        if (image_content->data.data) std::free(const_cast<char*>(image_content->data.data));
        if (image_content->mime_type.data) std::free(const_cast<char*>(image_content->mime_type.data));
        std::free(image_content);
        std::free(block);
        return nullptr;
    }
    
    block->type = MCP_CONTENT_IMAGE;
    block->content.image = image_content;
    
    return block;
}

extern "C" mcp_content_block_t* mcp_audio_content_create(
    const char* data,
    const char* mime_type
) {
    if (!data || !mime_type) return nullptr;
    
    auto* block = static_cast<mcp_content_block_t*>(std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block) return nullptr;
    
    auto* audio_content = static_cast<mcp_audio_content_t*>(std::calloc(1, sizeof(mcp_audio_content_t)));
    if (!audio_content) {
        std::free(block);
        return nullptr;
    }
    
    audio_content->type.data = "audio";
    audio_content->type.length = 5;
    audio_content->data.data = alloc_string(data);
    audio_content->data.length = std::strlen(data);
    audio_content->mime_type.data = alloc_string(mime_type);
    audio_content->mime_type.length = std::strlen(mime_type);
    
    if (!audio_content->data.data || !audio_content->mime_type.data) {
        if (audio_content->data.data) std::free(const_cast<char*>(audio_content->data.data));
        if (audio_content->mime_type.data) std::free(const_cast<char*>(audio_content->mime_type.data));
        std::free(audio_content);
        std::free(block);
        return nullptr;
    }
    
    block->type = MCP_CONTENT_AUDIO;
    block->content.audio = audio_content;
    
    return block;
}

extern "C" mcp_content_block_t* mcp_resource_content_create(const mcp_resource_t* resource) {
    if (!resource) return nullptr;
    
    auto* block = static_cast<mcp_content_block_t*>(std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block) return nullptr;
    
    block->type = MCP_CONTENT_RESOURCE;
    block->resource.resource.uri = alloc_string(resource->uri);
    block->resource.resource.name = alloc_string(resource->name);
    block->resource.resource.description = alloc_string(resource->description);
    block->resource.resource.mime_type = alloc_string(resource->mime_type);
    
    return block;
}

extern "C" mcp_content_block_t* mcp_embedded_resource_create(
    const mcp_resource_t* resource,
    const mcp_content_block_t* content,
    size_t content_count
) {
    if (!resource) return nullptr;
    
    auto* block = static_cast<mcp_content_block_t*>(std::calloc(1, sizeof(mcp_content_block_t)));
    if (!block) return nullptr;
    
    block->type = MCP_CONTENT_EMBEDDED;
    block->embedded.resource.uri = alloc_string(resource->uri);
    block->embedded.resource.name = alloc_string(resource->name);
    block->embedded.resource.description = alloc_string(resource->description);
    block->embedded.resource.mime_type = alloc_string(resource->mime_type);
    
    if (content && content_count > 0) {
        block->embedded.content = static_cast<mcp_content_block_t*>(
            std::malloc(content_count * sizeof(mcp_content_block_t)));
        if (block->embedded.content) {
            // Deep copy each content block
            for (size_t i = 0; i < content_count; ++i) {
                mcp_content_block_t* copied = mcp_content_block_copy(&content[i]);
                if (copied) {
                    block->embedded.content[i] = *copied;
                    std::free(copied);
                }
            }
            block->embedded.content_count = content_count;
        }
    }
    
    return block;
}

// Resource builders
extern "C" mcp_resource_t* mcp_resource_create(
    const char* uri,
    const char* name
) {
    if (!uri || !name) return nullptr;
    
    auto* resource = static_cast<mcp_resource_t*>(std::calloc(1, sizeof(mcp_resource_t)));
    if (!resource) return nullptr;
    
    resource->uri = alloc_string(uri);
    resource->name = alloc_string(name);
    
    if (!resource->uri || !resource->name) {
        std::free(resource->uri);
        std::free(resource->name);
        std::free(resource);
        return nullptr;
    }
    
    return resource;
}

extern "C" mcp_resource_t* mcp_resource_with_details(
    const char* uri,
    const char* name,
    const char* description,
    const char* mime_type
) {
    auto* resource = mcp_resource_create(uri, name);
    if (!resource) return nullptr;
    
    resource->description = alloc_string(description);
    resource->mime_type = alloc_string(mime_type);
    
    return resource;
}

// Tool builders
extern "C" mcp_tool_t* mcp_tool_create(const char* name) {
    if (!name) return nullptr;
    
    auto* tool = static_cast<mcp_tool_t*>(std::calloc(1, sizeof(mcp_tool_t)));
    if (!tool) return nullptr;
    
    tool->name = alloc_string(name);
    if (!tool->name) {
        std::free(tool);
        return nullptr;
    }
    
    return tool;
}

extern "C" mcp_tool_t* mcp_tool_with_description(
    const char* name,
    const char* description
) {
    auto* tool = mcp_tool_create(name);
    if (!tool) return nullptr;
    
    tool->description = alloc_string(description);
    return tool;
}

extern "C" mcp_tool_t* mcp_tool_with_schema(
    const char* name,
    mcp_json_value_t schema
) {
    auto* tool = mcp_tool_create(name);
    if (!tool) return nullptr;
    
    tool->input_schema = schema;
    return tool;
}

extern "C" mcp_tool_t* mcp_tool_complete(
    const char* name,
    const char* description,
    mcp_json_value_t schema
) {
    auto* tool = mcp_tool_create(name);
    if (!tool) return nullptr;
    
    tool->description = alloc_string(description);
    tool->input_schema = schema;
    return tool;
}

// Prompt builders
extern "C" mcp_prompt_t* mcp_prompt_create(const char* name) {
    if (!name) return nullptr;
    
    auto* prompt = static_cast<mcp_prompt_t*>(std::calloc(1, sizeof(mcp_prompt_t)));
    if (!prompt) return nullptr;
    
    prompt->name = alloc_string(name);
    if (!prompt->name) {
        std::free(prompt);
        return nullptr;
    }
    
    return prompt;
}

extern "C" mcp_prompt_t* mcp_prompt_with_description(
    const char* name,
    const char* description
) {
    auto* prompt = mcp_prompt_create(name);
    if (!prompt) return nullptr;
    
    prompt->description = alloc_string(description);
    return prompt;
}

extern "C" mcp_prompt_t* mcp_prompt_with_arguments(
    const char* name,
    const mcp_prompt_argument_t* arguments,
    size_t argument_count
) {
    auto* prompt = mcp_prompt_create(name);
    if (!prompt) return nullptr;
    
    if (arguments && argument_count > 0) {
        prompt->arguments = static_cast<mcp_prompt_argument_t*>(
            std::malloc(argument_count * sizeof(mcp_prompt_argument_t)));
        if (prompt->arguments) {
            for (size_t i = 0; i < argument_count; ++i) {
                prompt->arguments[i].name = alloc_string(arguments[i].name);
                prompt->arguments[i].description = alloc_string(arguments[i].description);
                prompt->arguments[i].required = arguments[i].required;
            }
            prompt->argument_count = argument_count;
        }
    }
    
    return prompt;
}

// Message builders
extern "C" mcp_message_t* mcp_message_create(
    mcp_role_t role,
    mcp_content_block_t* content
) {
    if (!content) return nullptr;
    
    auto* message = static_cast<mcp_message_t*>(std::calloc(1, sizeof(mcp_message_t)));
    if (!message) return nullptr;
    
    message->role = role;
    message->content = content;
    
    return message;
}

extern "C" mcp_message_t* mcp_user_message(const char* text) {
    auto* content = mcp_text_content_create(text);
    if (!content) return nullptr;
    
    return mcp_message_create(MCP_ROLE_USER, content);
}

extern "C" mcp_message_t* mcp_assistant_message(const char* text) {
    auto* content = mcp_text_content_create(text);
    if (!content) return nullptr;
    
    return mcp_message_create(MCP_ROLE_ASSISTANT, content);
}

// Error builders
extern "C" mcp_error_t* mcp_error_create(int code, const char* message) {
    if (!message) return nullptr;
    
    auto* error = static_cast<mcp_error_t*>(std::calloc(1, sizeof(mcp_error_t)));
    if (!error) return nullptr;
    
    error->code = code;
    error->message = alloc_string(message);
    if (!error->message) {
        std::free(error);
        return nullptr;
    }
    
    return error;
}

extern "C" mcp_error_t* mcp_error_with_data(
    int code,
    const char* message,
    mcp_json_value_t data
) {
    auto* error = mcp_error_create(code, message);
    if (!error) return nullptr;
    
    error->data = data;
    return error;
}

// Schema builders
extern "C" mcp_string_schema_t* mcp_string_schema_create() {
    return static_cast<mcp_string_schema_t*>(std::calloc(1, sizeof(mcp_string_schema_t)));
}

extern "C" mcp_string_schema_t* mcp_string_schema_with_constraints(
    const char* description,
    const char* pattern,
    int min_length,
    int max_length
) {
    auto* schema = mcp_string_schema_create();
    if (!schema) return nullptr;
    
    schema->description = alloc_string(description);
    schema->pattern = alloc_string(pattern);
    
    if (min_length >= 0) {
        schema->min_length = min_length;
        schema->min_length_set = true;
    }
    if (max_length >= 0) {
        schema->max_length = max_length;
        schema->max_length_set = true;
    }
    
    return schema;
}

extern "C" mcp_number_schema_t* mcp_number_schema_create() {
    return static_cast<mcp_number_schema_t*>(std::calloc(1, sizeof(mcp_number_schema_t)));
}

extern "C" mcp_number_schema_t* mcp_number_schema_with_constraints(
    const char* description,
    double minimum,
    double maximum,
    double multiple_of
) {
    auto* schema = mcp_number_schema_create();
    if (!schema) return nullptr;
    
    schema->description = alloc_string(description);
    
    if (!std::isnan(minimum)) {
        schema->minimum = minimum;
        schema->minimum_set = true;
    }
    if (!std::isnan(maximum)) {
        schema->maximum = maximum;
        schema->maximum_set = true;
    }
    if (!std::isnan(multiple_of) && multiple_of > 0) {
        schema->multiple_of = multiple_of;
        schema->multiple_of_set = true;
    }
    
    return schema;
}

extern "C" mcp_boolean_schema_t* mcp_boolean_schema_create() {
    return static_cast<mcp_boolean_schema_t*>(std::calloc(1, sizeof(mcp_boolean_schema_t)));
}

extern "C" mcp_boolean_schema_t* mcp_boolean_schema_with_description(
    const char* description
) {
    auto* schema = mcp_boolean_schema_create();
    if (!schema) return nullptr;
    
    schema->description = alloc_string(description);
    return schema;
}

extern "C" mcp_enum_schema_t* mcp_enum_schema_create(
    const char** values,
    size_t value_count
) {
    if (!values || value_count == 0) return nullptr;
    
    auto* schema = static_cast<mcp_enum_schema_t*>(std::calloc(1, sizeof(mcp_enum_schema_t)));
    if (!schema) return nullptr;
    
    schema->values = static_cast<char**>(std::malloc(value_count * sizeof(char*)));
    if (!schema->values) {
        std::free(schema);
        return nullptr;
    }
    
    for (size_t i = 0; i < value_count; ++i) {
        schema->values[i] = alloc_string(values[i]);
    }
    schema->value_count = value_count;
    
    return schema;
}

extern "C" mcp_enum_schema_t* mcp_enum_schema_with_description(
    const char** values,
    size_t value_count,
    const char* description
) {
    auto* schema = mcp_enum_schema_create(values, value_count);
    if (!schema) return nullptr;
    
    schema->description = alloc_string(description);
    return schema;
}

// Request builders
extern "C" mcp_request_t* mcp_request_create(
    mcp_request_id_t id,
    const char* method
) {
    if (!method) return nullptr;
    
    auto* request = static_cast<mcp_request_t*>(std::calloc(1, sizeof(mcp_request_t)));
    if (!request) return nullptr;
    
    request->jsonrpc = alloc_string("2.0");
    request->id = id;
    request->method = alloc_string(method);
    
    if (!request->jsonrpc || !request->method) {
        std::free(request->jsonrpc);
        std::free(request->method);
        std::free(request);
        return nullptr;
    }
    
    return request;
}

extern "C" mcp_request_t* mcp_request_with_params(
    mcp_request_id_t id,
    const char* method,
    mcp_json_value_t params
) {
    auto* request = mcp_request_create(id, method);
    if (!request) return nullptr;
    
    request->params = params;
    return request;
}

// Response builders
extern "C" mcp_response_t* mcp_response_success(
    mcp_request_id_t id,
    mcp_json_value_t result
) {
    auto* response = static_cast<mcp_response_t*>(std::calloc(1, sizeof(mcp_response_t)));
    if (!response) return nullptr;
    
    response->jsonrpc = alloc_string("2.0");
    response->id = id;
    response->result = result;
    
    if (!response->jsonrpc) {
        std::free(response->jsonrpc);
        std::free(response);
        return nullptr;
    }
    
    return response;
}

extern "C" mcp_response_t* mcp_response_error(
    mcp_request_id_t id,
    const mcp_error_t* error
) {
    if (!error) return nullptr;
    
    auto* response = static_cast<mcp_response_t*>(std::calloc(1, sizeof(mcp_response_t)));
    if (!response) return nullptr;
    
    response->jsonrpc = alloc_string("2.0");
    response->id = id;
    response->error = static_cast<mcp_error_t*>(std::malloc(sizeof(mcp_error_t)));
    
    if (!response->jsonrpc || !response->error) {
        std::free(response->jsonrpc);
        std::free(response->error);
        std::free(response);
        return nullptr;
    }
    
    // Copy error
    response->error->code = error->code;
    response->error->message = alloc_string(error->message);
    response->error->data = error->data;  // Shallow copy of JSON handle
    
    return response;
}

// Notification builders
extern "C" mcp_notification_t* mcp_notification_create(const char* method) {
    if (!method) return nullptr;
    
    auto* notification = static_cast<mcp_notification_t*>(std::calloc(1, sizeof(mcp_notification_t)));
    if (!notification) return nullptr;
    
    notification->jsonrpc = alloc_string("2.0");
    notification->method = alloc_string(method);
    
    if (!notification->jsonrpc || !notification->method) {
        std::free(notification->jsonrpc);
        std::free(notification->method);
        std::free(notification);
        return nullptr;
    }
    
    return notification;
}

extern "C" mcp_notification_t* mcp_notification_with_params(
    const char* method,
    mcp_json_value_t params
) {
    auto* notification = mcp_notification_create(method);
    if (!notification) return nullptr;
    
    notification->params = params;
    return notification;
}

// Implementation info builder
extern "C" mcp_implementation_t* mcp_implementation_create(
    const char* name,
    const char* version
) {
    if (!name || !version) return nullptr;
    
    auto* impl = static_cast<mcp_implementation_t*>(std::calloc(1, sizeof(mcp_implementation_t)));
    if (!impl) return nullptr;
    
    impl->name = alloc_string(name);
    impl->version = alloc_string(version);
    
    if (!impl->name || !impl->version) {
        std::free(impl->name);
        std::free(impl->version);
        std::free(impl);
        return nullptr;
    }
    
    return impl;
}

// Root builder
extern "C" mcp_root_t* mcp_root_create(const char* uri, const char* name) {
    if (!uri) return nullptr;
    
    auto* root = static_cast<mcp_root_t*>(std::calloc(1, sizeof(mcp_root_t)));
    if (!root) return nullptr;
    
    root->uri = alloc_string(uri);
    root->name = alloc_string(name);
    
    if (!root->uri) {
        std::free(root->uri);
        std::free(root->name);
        std::free(root);
        return nullptr;
    }
    
    return root;
}

// Model hint builder
extern "C" mcp_model_hint_t* mcp_model_hint_create(const char* name) {
    if (!name) return nullptr;
    
    auto* hint = static_cast<mcp_model_hint_t*>(std::calloc(1, sizeof(mcp_model_hint_t)));
    if (!hint) return nullptr;
    
    hint->name = alloc_string(name);
    if (!hint->name) {
        std::free(hint);
        return nullptr;
    }
    
    return hint;
}

// Model preferences builder
extern "C" mcp_model_preferences_t* mcp_model_preferences_create() {
    return static_cast<mcp_model_preferences_t*>(std::calloc(1, sizeof(mcp_model_preferences_t)));
}

extern "C" mcp_model_preferences_t* mcp_model_preferences_with_priorities(
    double cost_priority,
    double speed_priority,
    double intelligence_priority
) {
    auto* prefs = mcp_model_preferences_create();
    if (!prefs) return nullptr;
    
    if (cost_priority >= 0.0 && cost_priority <= 1.0) {
        prefs->cost_priority = cost_priority;
        prefs->cost_priority_set = true;
    }
    if (speed_priority >= 0.0 && speed_priority <= 1.0) {
        prefs->speed_priority = speed_priority;
        prefs->speed_priority_set = true;
    }
    if (intelligence_priority >= 0.0 && intelligence_priority <= 1.0) {
        prefs->intelligence_priority = intelligence_priority;
        prefs->intelligence_priority_set = true;
    }
    
    return prefs;
}

} // namespace c_api
} // namespace mcp