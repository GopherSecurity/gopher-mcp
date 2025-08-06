#include "mcp/json_serialization.h"
#include <nlohmann/json.hpp>
#include <sstream>

// Internal helper from json_bridge.cc
namespace mcp {
namespace json {
extern nlohmann::json toNlohmannJson(const JsonValue& value);
}
}

namespace mcp {
namespace json {
namespace impl {

// Helper function to handle optional fields
template<typename T>
static void addOptional(JsonObjectBuilder& builder, const std::string& key, 
                        const optional<T>& opt, 
                        JsonValue (*serializeFunc)(const T&)) {
  if (opt.has_value()) {
    builder.add(key, serializeFunc(opt.value()));
  }
}

// Serialize Error and ErrorData
JsonValue serialize_Error(const Error& error) {
  JsonObjectBuilder builder;
  builder.add("code", error.code)
         .add("message", error.message);
  
  if (error.data.has_value()) {
    builder.add("data", serialize_ErrorData(error.data.value()));
  }
  
  return builder.build();
}

JsonValue serialize_ErrorData(const ErrorData& data) {
  JsonValue result;
  
  match(data,
    [&result](std::nullptr_t) { result = JsonValue::null(); },
    [&result](bool b) { result = JsonValue(b); },
    [&result](int i) { result = JsonValue(i); },
    [&result](double d) { result = JsonValue(d); },
    [&result](const std::string& s) { result = JsonValue(s); },
    [&result](const std::vector<std::string>& vec) {
      JsonArrayBuilder builder;
      for (const auto& s : vec) {
        builder.add(s);
      }
      result = builder.build();
    },
    [&result](const std::map<std::string, std::string>& map) {
      JsonObjectBuilder builder;
      for (const auto& kv : map) {
        builder.add(kv.first, kv.second);
      }
      result = builder.build();
    }
  );
  
  return result;
}

// Serialize jsonrpc types
JsonValue serialize_RequestId(const RequestId& id) {
  JsonValue result;
  
  match(id,
    [&result](const std::string& s) { result = JsonValue(s); },
    [&result](int i) { result = JsonValue(i); }
  );
  
  return result;
}

JsonValue serialize_Request(const jsonrpc::Request& request) {
  JsonObjectBuilder builder;
  builder.add("jsonrpc", request.jsonrpc)
         .add("id", serialize_RequestId(request.id))
         .add("method", request.method);
  
  if (request.params.has_value()) {
    builder.add("params", metadataToJson(request.params.value()));
  }
  
  return builder.build();
}

JsonValue serialize_Response(const jsonrpc::Response& response) {
  JsonObjectBuilder builder;
  builder.add("jsonrpc", response.jsonrpc)
         .add("id", serialize_RequestId(response.id));
  
  if (response.result.has_value()) {
    builder.add("result", serialize_ResponseResult(response.result.value()));
  }
  
  if (response.error.has_value()) {
    builder.add("error", serialize_Error(response.error.value()));
  }
  
  return builder.build();
}

JsonValue serialize_ResponseResult(const jsonrpc::ResponseResult& result) {
  JsonValue json_result;
  
  match(result,
    [&json_result](std::nullptr_t) { 
      json_result = JsonValue::null(); 
    },
    [&json_result](bool b) { 
      json_result = JsonValue(b); 
    },
    [&json_result](int i) { 
      json_result = JsonValue(i); 
    },
    [&json_result](double d) { 
      json_result = JsonValue(d); 
    },
    [&json_result](const std::string& s) { 
      json_result = JsonValue(s); 
    },
    [&json_result](const Metadata& m) { 
      json_result = metadataToJson(m); 
    },
    [&json_result](const std::vector<ContentBlock>& blocks) {
      JsonArrayBuilder builder;
      for (const auto& block : blocks) {
        builder.add(serialize_ContentBlock(block));
      }
      json_result = builder.build();
    },
    [&json_result](const std::vector<Tool>& tools) {
      JsonArrayBuilder builder;
      for (const auto& tool : tools) {
        builder.add(serialize_Tool(tool));
      }
      json_result = builder.build();
    },
    [&json_result](const std::vector<Prompt>& prompts) {
      JsonArrayBuilder builder;
      for (const auto& prompt : prompts) {
        builder.add(serialize_Prompt(prompt));
      }
      json_result = builder.build();
    },
    [&json_result](const std::vector<Resource>& resources) {
      JsonArrayBuilder builder;
      for (const auto& resource : resources) {
        builder.add(serialize_Resource(resource));
      }
      json_result = builder.build();
    }
  );
  
  return json_result;
}

JsonValue serialize_Notification(const jsonrpc::Notification& notification) {
  JsonObjectBuilder builder;
  builder.add("jsonrpc", notification.jsonrpc)
         .add("method", notification.method);
  
  if (notification.params.has_value()) {
    builder.add("params", metadataToJson(notification.params.value()));
  }
  
  return builder.build();
}

// Serialize content types
JsonValue serialize_TextContent(const TextContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "text")
         .add("text", content.text);
  return builder.build();
}

JsonValue serialize_ImageContent(const ImageContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "image")
         .add("mimeType", content.mimeType)
         .add("data", content.data);
  return builder.build();
}

JsonValue serialize_AudioContent(const AudioContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "audio")
         .add("mimeType", content.mimeType)
         .add("data", content.data);
  return builder.build();
}

JsonValue serialize_ResourceContent(const ResourceContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "resource")
         .add("resource", serialize_Resource(content.resource));
  return builder.build();
}

JsonValue serialize_ContentBlock(const ContentBlock& block) {
  JsonValue result;
  
  match(block,
    [&result](const TextContent& text) { 
      result = serialize_TextContent(text); 
    },
    [&result](const ImageContent& image) { 
      result = serialize_ImageContent(image); 
    },
    [&result](const ResourceContent& resource) { 
      result = serialize_ResourceContent(resource); 
    }
  );
  
  return result;
}

// Serialize Tool
JsonValue serialize_Tool(const Tool& tool) {
  JsonObjectBuilder builder;
  builder.add("name", tool.name);
  
  if (tool.description.has_value()) {
    builder.add("description", tool.description.value());
  }
  
  if (tool.inputSchema.has_value()) {
    // inputSchema is nlohmann::json - convert to JsonValue
    auto json_str = tool.inputSchema.value().dump();
    builder.add("inputSchema", JsonValue::parse(json_str));
  }
  
  return builder.build();
}

// Serialize Prompt
JsonValue serialize_Prompt(const Prompt& prompt) {
  JsonObjectBuilder builder;
  builder.add("name", prompt.name);
  
  if (prompt.description.has_value()) {
    builder.add("description", prompt.description.value());
  }
  
  if (prompt.arguments.has_value()) {
    JsonArrayBuilder args;
    for (const auto& arg : prompt.arguments.value()) {
      JsonObjectBuilder argBuilder;
      argBuilder.add("name", arg.name);
      if (arg.description.has_value()) {
        argBuilder.add("description", arg.description.value());
      }
      argBuilder.add("required", arg.required);
      args.add(argBuilder.build());
    }
    builder.add("arguments", args.build());
  }
  
  return builder.build();
}

// Serialize Resource
JsonValue serialize_Resource(const Resource& resource) {
  JsonObjectBuilder builder;
  builder.add("uri", resource.uri);
  builder.add("name", resource.name);
  
  if (resource.description.has_value()) {
    builder.add("description", resource.description.value());
  }
  
  if (resource.mimeType.has_value()) {
    builder.add("mimeType", resource.mimeType.value());
  }
  
  return builder.build();
}

// Serialize Metadata
JsonValue serialize_Metadata(const Metadata& metadata) {
  return metadataToJson(metadata);
}

// Deserialize Error and ErrorData
Error deserialize_Error(const JsonValue& json) {
  Error error;
  error.code = json.at("code").getInt();
  error.message = json.at("message").getString();
  
  if (json.contains("data")) {
    error.data = deserialize_ErrorData(json["data"]);
  }
  
  return error;
}

ErrorData deserialize_ErrorData(const JsonValue& json) {
  if (json.isNull()) {
    return ErrorData(nullptr);
  } else if (json.isBoolean()) {
    return ErrorData(json.getBool());
  } else if (json.isInteger()) {
    return ErrorData(json.getInt());
  } else if (json.isFloat()) {
    return ErrorData(json.getFloat());
  } else if (json.isString()) {
    return ErrorData(json.getString());
  } else if (json.isArray()) {
    std::vector<std::string> vec;
    size_t size = json.size();
    for (size_t i = 0; i < size; ++i) {
      vec.push_back(json[i].getString());
    }
    return ErrorData(vec);
  } else if (json.isObject()) {
    std::map<std::string, std::string> map;
    for (const auto& key : json.keys()) {
      map[key] = json[key].getString();
    }
    return ErrorData(map);
  }
  
  return ErrorData(nullptr);
}

// Deserialize jsonrpc types
RequestId deserialize_RequestId(const JsonValue& json) {
  if (json.isString()) {
    return RequestId(json.getString());
  } else if (json.isInteger()) {
    return RequestId(json.getInt());
  }
  throw JsonException("Invalid RequestId type");
}

jsonrpc::Request deserialize_Request(const JsonValue& json) {
  jsonrpc::Request request;
  request.jsonrpc = json.at("jsonrpc").getString();
  request.id = deserialize_RequestId(json.at("id"));
  request.method = json.at("method").getString();
  
  if (json.contains("params")) {
    request.params = jsonToMetadata(json["params"]);
  }
  
  return request;
}

jsonrpc::Response deserialize_Response(const JsonValue& json) {
  jsonrpc::Response response;
  response.jsonrpc = json.at("jsonrpc").getString();
  response.id = deserialize_RequestId(json.at("id"));
  
  if (json.contains("result")) {
    response.result = deserialize_ResponseResult(json["result"]);
  }
  
  if (json.contains("error")) {
    response.error = deserialize_Error(json["error"]);
  }
  
  return response;
}

jsonrpc::ResponseResult deserialize_ResponseResult(const JsonValue& json) {
  if (json.isNull()) {
    return jsonrpc::ResponseResult(nullptr);
  } else if (json.isBoolean()) {
    return jsonrpc::ResponseResult(json.getBool());
  } else if (json.isInteger()) {
    return jsonrpc::ResponseResult(json.getInt());
  } else if (json.isFloat()) {
    return jsonrpc::ResponseResult(json.getFloat());
  } else if (json.isString()) {
    return jsonrpc::ResponseResult(json.getString());
  } else if (json.isObject()) {
    // Check if it's a specific type based on "type" field
    if (json.contains("type")) {
      std::string type = json["type"].getString();
      if (type == "text" || type == "image" || type == "resource") {
        // It's a single ContentBlock
        std::vector<ContentBlock> blocks;
        blocks.push_back(deserialize_ContentBlock(json));
        return jsonrpc::ResponseResult(blocks);
      }
    }
    // Otherwise treat as Metadata
    return jsonrpc::ResponseResult(jsonToMetadata(json));
  } else if (json.isArray() && json.size() > 0) {
    // Determine array type by examining first element
    const auto& first = json[0];
    
    if (first.isObject()) {
      if (first.contains("type")) {
        std::string type = first["type"].getString();
        if (type == "text" || type == "image" || type == "resource") {
          // Array of ContentBlocks
          std::vector<ContentBlock> blocks;
          size_t size = json.size();
          for (size_t i = 0; i < size; ++i) {
            blocks.push_back(deserialize_ContentBlock(json[i]));
          }
          return jsonrpc::ResponseResult(blocks);
        }
      } else if (first.contains("name") && first.contains("inputSchema")) {
        // Array of Tools
        std::vector<Tool> tools;
        size_t size = json.size();
        for (size_t i = 0; i < size; ++i) {
          tools.push_back(deserialize_Tool(json[i]));
        }
        return jsonrpc::ResponseResult(tools);
      } else if (first.contains("uri")) {
        // Array of Resources
        std::vector<Resource> resources;
        size_t size = json.size();
        for (size_t i = 0; i < size; ++i) {
          resources.push_back(deserialize_Resource(json[i]));
        }
        return jsonrpc::ResponseResult(resources);
      }
    }
    
    // Default to Metadata for unknown array types
    return jsonrpc::ResponseResult(jsonToMetadata(json));
  }
  
  // Default to null
  return jsonrpc::ResponseResult(nullptr);
}

jsonrpc::Notification deserialize_Notification(const JsonValue& json) {
  jsonrpc::Notification notification;
  notification.jsonrpc = json.at("jsonrpc").getString();
  notification.method = json.at("method").getString();
  
  if (json.contains("params")) {
    notification.params = jsonToMetadata(json["params"]);
  }
  
  return notification;
}

// Deserialize content types
ContentBlock deserialize_ContentBlock(const JsonValue& json) {
  std::string type = json.at("type").getString();
  
  if (type == "text") {
    TextContent text;
    text.text = json.at("text").getString();
    return ContentBlock(text);
  } else if (type == "image") {
    ImageContent image;
    image.mimeType = json.at("mimeType").getString();
    image.data = json.at("data").getString();
    return ContentBlock(image);
  } else if (type == "resource") {
    ResourceContent resource;
    resource.resource = deserialize_Resource(json.at("resource"));
    return ContentBlock(resource);
  }
  
  throw JsonException("Unknown content block type: " + type);
}

// Deserialize Tool
Tool deserialize_Tool(const JsonValue& json) {
  Tool tool;
  tool.name = json.at("name").getString();
  
  if (json.contains("description")) {
    tool.description = json["description"].getString();
  }
  
  if (json.contains("inputSchema")) {
    // ToolInputSchema is nlohmann::json, so we need to convert
    tool.inputSchema = toNlohmannJson(json["inputSchema"]);
  }
  
  return tool;
}

// Deserialize Resource
Resource deserialize_Resource(const JsonValue& json) {
  Resource resource;
  resource.uri = json.at("uri").getString();
  
  if (json.contains("name")) {
    resource.name = json["name"].getString();
  }
  
  if (json.contains("description")) {
    resource.description = json["description"].getString();
  }
  
  if (json.contains("mimeType")) {
    resource.mimeType = json["mimeType"].getString();
  }
  
  return resource;
}

// Deserialize Metadata
Metadata deserialize_Metadata(const JsonValue& json) {
  return jsonToMetadata(json);
}

// Deserialize Prompt
Prompt deserialize_Prompt(const JsonValue& json) {
  Prompt prompt;
  prompt.name = json.at("name").getString();
  
  if (json.contains("description")) {
    prompt.description = json["description"].getString();
  }
  
  if (json.contains("arguments")) {
    std::vector<PromptArgument> args;
    const auto& argsArray = json["arguments"];
    size_t size = argsArray.size();
    for (size_t i = 0; i < size; ++i) {
      PromptArgument arg;
      arg.name = argsArray[i].at("name").getString();
      if (argsArray[i].contains("description")) {
        arg.description = argsArray[i]["description"].getString();
      }
      if (argsArray[i].contains("required")) {
        arg.required = argsArray[i]["required"].getBool();
      }
      args.push_back(arg);
    }
    prompt.arguments = args;
  }
  
  return prompt;
}

// Deserialize Extended Content Types
TextContent deserialize_TextContent(const JsonValue& json) {
  TextContent content;
  content.text = json.at("text").getString();
  // Add annotations if present
  if (json.contains("annotations")) {
    content.annotations = deserialize_Annotations(json["annotations"]);
  }
  return content;
}

ImageContent deserialize_ImageContent(const JsonValue& json) {
  ImageContent content;
  content.data = json.at("data").getString();
  content.mimeType = json.at("mimeType").getString();
  return content;
}

AudioContent deserialize_AudioContent(const JsonValue& json) {
  AudioContent content;
  content.data = json.at("data").getString();
  content.mimeType = json.at("mimeType").getString();
  return content;
}

ResourceLink deserialize_ResourceLink(const JsonValue& json) {
  ResourceLink link;
  link.uri = json.at("uri").getString();
  link.name = json.at("name").getString();
  
  if (json.contains("description")) {
    link.description = json["description"].getString();
  }
  
  if (json.contains("mimeType")) {
    link.mimeType = json["mimeType"].getString();
  }
  
  return link;
}

EmbeddedResource deserialize_EmbeddedResource(const JsonValue& json) {
  EmbeddedResource embedded;
  embedded.resource = deserialize_Resource(json.at("resource"));
  
  if (json.contains("content")) {
    const auto& contentArray = json["content"];
    size_t size = contentArray.size();
    for (size_t i = 0; i < size; ++i) {
      embedded.content.push_back(deserialize_ContentBlock(contentArray[i]));
    }
  }
  
  return embedded;
}

ExtendedContentBlock deserialize_ExtendedContentBlock(const JsonValue& json) {
  std::string type = json.at("type").getString();
  
  if (type == "text") {
    return ExtendedContentBlock(deserialize_TextContent(json));
  } else if (type == "image") {
    return ExtendedContentBlock(deserialize_ImageContent(json));
  } else if (type == "audio") {
    return ExtendedContentBlock(deserialize_AudioContent(json));
  } else if (type == "resource") {
    return ExtendedContentBlock(deserialize_ResourceLink(json));
  } else if (type == "embedded") {
    return ExtendedContentBlock(deserialize_EmbeddedResource(json));
  }
  
  throw JsonException("Unknown extended content block type: " + type);
}

// ===== Extended Content Block Serialization =====

JsonValue serialize_ExtendedContentBlock(const ExtendedContentBlock& block) {
  JsonValue result;
  
  match(block,
    [&result](const TextContent& text) { 
      result = serialize_TextContent(text); 
    },
    [&result](const ImageContent& image) { 
      result = serialize_ImageContent(image); 
    },
    [&result](const AudioContent& audio) { 
      result = serialize_AudioContent(audio); 
    },
    [&result](const ResourceLink& link) {
      JsonObjectBuilder builder;
      builder.add("type", "resource")
             .add("uri", link.uri)
             .add("name", link.name);
      if (link.description.has_value()) {
        builder.add("description", link.description.value());
      }
      if (link.mimeType.has_value()) {
        builder.add("mimeType", link.mimeType.value());
      }
      result = builder.build();
    },
    [&result](const EmbeddedResource& embedded) {
      JsonObjectBuilder builder;
      builder.add("type", "embedded")
             .add("resource", serialize_Resource(embedded.resource));
      JsonArrayBuilder contentArray;
      for (const auto& content : embedded.content) {
        contentArray.add(serialize_ContentBlock(content));
      }
      builder.add("content", contentArray.build());
      result = builder.build();
    }
  );
  
  return result;
}

// ===== Request Types Serialization =====

// Initialize and session requests
JsonValue serialize_InitializeRequest(const InitializeRequest& request) {
  JsonObjectBuilder builder;
  builder.add("protocolVersion", request.protocolVersion)
         .add("capabilities", serialize_ClientCapabilities(request.capabilities));
  if (request.clientInfo.has_value()) {
    builder.add("clientInfo", JsonSerializer::serialize(request.clientInfo.value()));
  }
  return builder.build();
}

JsonValue serialize_PingRequest(const PingRequest& request) {
  (void)request; // Empty params
  return JsonObjectBuilder().build();
}

JsonValue serialize_CompleteRequest(const CompleteRequest& request) {
  JsonObjectBuilder builder;
  builder.add("ref", serialize_PromptReference(request.ref));
  if (request.argument.has_value()) {
    builder.add("argument", request.argument.value());
  }
  return builder.build();
}

JsonValue serialize_SetLevelRequest(const SetLevelRequest& request) {
  JsonObjectBuilder builder;
  builder.add("level", enums::LoggingLevel::to_string(request.level));
  return builder.build();
}

// Tool requests
JsonValue serialize_CallToolRequest(const CallToolRequest& request) {
  JsonObjectBuilder builder;
  builder.add("name", request.name);
  
  if (request.arguments.has_value()) {
    builder.add("arguments", serialize_Metadata(request.arguments.value()));
  }
  
  return builder.build();
}

JsonValue serialize_ListToolsRequest(const ListToolsRequest& request) {
  JsonObjectBuilder builder;
  if (request.cursor.has_value()) {
    builder.add("cursor", request.cursor.value());
  }
  return builder.build();
}

// Prompt requests
JsonValue serialize_PromptMessage(const PromptMessage& message) {
  JsonObjectBuilder builder;
  builder.add("role", enums::Role::to_string(message.role));
  
  // Handle variant content
  match(message.content,
    [&builder](const TextContent& text) {
      builder.add("content", serialize_TextContent(text));
    },
    [&builder](const ImageContent& image) {
      builder.add("content", serialize_ImageContent(image));
    },
    [&builder](const EmbeddedResource& embedded) {
      JsonObjectBuilder embBuilder;
      embBuilder.add("type", "embedded")
                .add("resource", serialize_Resource(embedded.resource));
      JsonArrayBuilder contentArray;
      for (const auto& content : embedded.content) {
        contentArray.add(serialize_ContentBlock(content));
      }
      embBuilder.add("content", contentArray.build());
      builder.add("content", embBuilder.build());
    }
  );
  
  return builder.build();
}

JsonValue serialize_GetPromptRequest(const GetPromptRequest& request) {
  JsonObjectBuilder builder;
  builder.add("name", request.name);
  
  if (request.arguments.has_value()) {
    builder.add("arguments", serialize_Metadata(request.arguments.value()));
  }
  
  return builder.build();
}

JsonValue serialize_ListPromptsRequest(const ListPromptsRequest& request) {
  JsonObjectBuilder builder;
  if (request.cursor.has_value()) {
    builder.add("cursor", request.cursor.value());
  }
  return builder.build();
}

// Resource requests
JsonValue serialize_ResourceTemplate(const ResourceTemplate& resourceTemplate) {
  JsonObjectBuilder builder;
  builder.add("uriTemplate", resourceTemplate.uriTemplate)
         .add("name", resourceTemplate.name);
  
  if (resourceTemplate.description.has_value()) {
    builder.add("description", resourceTemplate.description.value());
  }
  
  if (resourceTemplate.mimeType.has_value()) {
    builder.add("mimeType", resourceTemplate.mimeType.value());
  }
  
  return builder.build();
}

JsonValue serialize_ReadResourceRequest(const ReadResourceRequest& request) {
  JsonObjectBuilder builder;
  builder.add("uri", request.uri);
  return builder.build();
}

JsonValue serialize_ListResourcesRequest(const ListResourcesRequest& request) {
  JsonObjectBuilder builder;
  if (request.cursor.has_value()) {
    builder.add("cursor", request.cursor.value());
  }
  return builder.build();
}

JsonValue serialize_ListResourceTemplatesRequest(const ListResourceTemplatesRequest& request) {
  JsonObjectBuilder builder;
  if (request.cursor.has_value()) {
    builder.add("cursor", request.cursor.value());
  }
  return builder.build();
}

JsonValue serialize_SubscribeRequest(const SubscribeRequest& request) {
  JsonObjectBuilder builder;
  builder.add("uri", request.uri);
  return builder.build();
}

JsonValue serialize_UnsubscribeRequest(const UnsubscribeRequest& request) {
  JsonObjectBuilder builder;
  builder.add("uri", request.uri);
  return builder.build();
}

// Root requests
JsonValue serialize_Root(const Root& root) {
  JsonObjectBuilder builder;
  builder.add("uri", root.uri);
  
  if (root.name.has_value()) {
    builder.add("name", root.name.value());
  }
  
  return builder.build();
}

JsonValue serialize_ListRootsRequest(const ListRootsRequest& request) {
  (void)request; // Empty params
  return JsonObjectBuilder().build();
}

// Message requests
JsonValue serialize_CreateMessageRequest(const CreateMessageRequest& request) {
  JsonObjectBuilder builder;
  
  // Serialize messages array
  JsonArrayBuilder messages;
  for (const auto& msg : request.messages) {
    messages.add(serialize_SamplingMessage(msg));
  }
  builder.add("messages", messages.build());
  
  if (request.modelPreferences.has_value()) {
    builder.add("modelPreferences", serialize_ModelPreferences(request.modelPreferences.value()));
  }
  
  if (request.systemPrompt.has_value()) {
    builder.add("systemPrompt", request.systemPrompt.value());
  }
  
  if (request.includeContext.has_value()) {
    builder.add("includeContext", serialize_Metadata(request.includeContext.value()));
  }
  
  if (request.temperature.has_value()) {
    builder.add("temperature", request.temperature.value());
  }
  
  if (request.maxTokens.has_value()) {
    builder.add("maxTokens", request.maxTokens.value());
  }
  
  if (request.stopSequences.has_value()) {
    JsonArrayBuilder stops;
    for (const auto& seq : request.stopSequences.value()) {
      stops.add(seq);
    }
    builder.add("stopSequences", stops.build());
  }
  
  if (request.metadata.has_value()) {
    builder.add("metadata", serialize_Metadata(request.metadata.value()));
  }
  
  return builder.build();
}

JsonValue serialize_ElicitRequest(const ElicitRequest& request) {
  JsonObjectBuilder builder;
  builder.add("name", request.name);
  
  // Serialize schema
  match(request.schema,
    [&builder](const StringSchema& s) {
      JsonObjectBuilder schemaBuilder;
      schemaBuilder.add("type", "string");
      if (s.description.has_value()) {
        schemaBuilder.add("description", s.description.value());
      }
      if (s.pattern.has_value()) {
        schemaBuilder.add("pattern", s.pattern.value());
      }
      if (s.minLength.has_value()) {
        schemaBuilder.add("minLength", s.minLength.value());
      }
      if (s.maxLength.has_value()) {
        schemaBuilder.add("maxLength", s.maxLength.value());
      }
      builder.add("schema", schemaBuilder.build());
    },
    [&builder](const NumberSchema& n) {
      JsonObjectBuilder schemaBuilder;
      schemaBuilder.add("type", "number");
      if (n.description.has_value()) {
        schemaBuilder.add("description", n.description.value());
      }
      if (n.minimum.has_value()) {
        schemaBuilder.add("minimum", n.minimum.value());
      }
      if (n.maximum.has_value()) {
        schemaBuilder.add("maximum", n.maximum.value());
      }
      if (n.multipleOf.has_value()) {
        schemaBuilder.add("multipleOf", n.multipleOf.value());
      }
      builder.add("schema", schemaBuilder.build());
    },
    [&builder](const BooleanSchema& b) {
      JsonObjectBuilder schemaBuilder;
      schemaBuilder.add("type", "boolean");
      if (b.description.has_value()) {
        schemaBuilder.add("description", b.description.value());
      }
      builder.add("schema", schemaBuilder.build());
    },
    [&builder](const EnumSchema& e) {
      JsonObjectBuilder schemaBuilder;
      schemaBuilder.add("type", "string");
      if (e.description.has_value()) {
        schemaBuilder.add("description", e.description.value());
      }
      JsonArrayBuilder values;
      for (const auto& val : e.values) {
        values.add(val);
      }
      schemaBuilder.add("enum", values.build());
      builder.add("schema", schemaBuilder.build());
    }
  );
  
  if (request.prompt.has_value()) {
    builder.add("prompt", request.prompt.value());
  }
  
  return builder.build();
}

// ===== Response/Result Types Serialization =====

// Initialize and session results
JsonValue serialize_InitializeResult(const InitializeResult& result) {
  JsonObjectBuilder builder;
  builder.add("protocolVersion", result.protocolVersion)
         .add("capabilities", serialize_ServerCapabilities(result.capabilities));
  
  if (result.serverInfo.has_value()) {
    builder.add("serverInfo", JsonSerializer::serialize(result.serverInfo.value()));
  }
  
  if (result.instructions.has_value()) {
    builder.add("instructions", result.instructions.value());
  }
  
  return builder.build();
}

JsonValue serialize_CompleteResult(const CompleteResult& result) {
  JsonObjectBuilder builder;
  JsonObjectBuilder completionBuilder;
  
  JsonArrayBuilder values;
  for (const auto& val : result.completion.values) {
    values.add(val);
  }
  completionBuilder.add("values", values.build());
  
  if (result.completion.total.has_value()) {
    completionBuilder.add("total", result.completion.total.value());
  }
  
  completionBuilder.add("hasMore", result.completion.hasMore);
  builder.add("completion", completionBuilder.build());
  
  return builder.build();
}

// Tool results
JsonValue serialize_CallToolResult(const CallToolResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder content;
  for (const auto& block : result.content) {
    content.add(serialize_ExtendedContentBlock(block));
  }
  builder.add("content", content.build());
  
  if (result.isError) {
    builder.add("isError", result.isError);
  }
  
  return builder.build();
}

JsonValue serialize_ListToolsResult(const ListToolsResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder tools;
  for (const auto& tool : result.tools) {
    tools.add(serialize_Tool(tool));
  }
  builder.add("tools", tools.build());
  
  return builder.build();
}

// Prompt results
JsonValue serialize_GetPromptResult(const GetPromptResult& result) {
  JsonObjectBuilder builder;
  
  if (result.description.has_value()) {
    builder.add("description", result.description.value());
  }
  
  JsonArrayBuilder messages;
  for (const auto& msg : result.messages) {
    messages.add(serialize_PromptMessage(msg));
  }
  builder.add("messages", messages.build());
  
  return builder.build();
}

JsonValue serialize_ListPromptsResult(const ListPromptsResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder prompts;
  for (const auto& prompt : result.prompts) {
    prompts.add(serialize_Prompt(prompt));
  }
  builder.add("prompts", prompts.build());
  
  if (result.nextCursor.has_value()) {
    builder.add("nextCursor", result.nextCursor.value());
  }
  
  return builder.build();
}

// Resource results
JsonValue serialize_ReadResourceResult(const ReadResourceResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder contents;
  for (const auto& content : result.contents) {
    match(content,
      [&contents](const TextResourceContents& text) {
        contents.add(serialize_TextResourceContents(text));
      },
      [&contents](const BlobResourceContents& blob) {
        contents.add(serialize_BlobResourceContents(blob));
      }
    );
  }
  builder.add("contents", contents.build());
  
  return builder.build();
}

JsonValue serialize_ListResourcesResult(const ListResourcesResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder resources;
  for (const auto& resource : result.resources) {
    resources.add(serialize_Resource(resource));
  }
  builder.add("resources", resources.build());
  
  if (result.nextCursor.has_value()) {
    builder.add("nextCursor", result.nextCursor.value());
  }
  
  return builder.build();
}

JsonValue serialize_ListResourceTemplatesResult(const ListResourceTemplatesResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder templates;
  for (const auto& tmpl : result.resourceTemplates) {
    templates.add(serialize_ResourceTemplate(tmpl));
  }
  builder.add("resourceTemplates", templates.build());
  
  if (result.nextCursor.has_value()) {
    builder.add("nextCursor", result.nextCursor.value());
  }
  
  return builder.build();
}

// Root results
JsonValue serialize_ListRootsResult(const ListRootsResult& result) {
  JsonObjectBuilder builder;
  
  JsonArrayBuilder roots;
  for (const auto& root : result.roots) {
    roots.add(serialize_Root(root));
  }
  builder.add("roots", roots.build());
  
  return builder.build();
}

// Message results
JsonValue serialize_CreateMessageResult(const CreateMessageResult& result) {
  JsonObjectBuilder builder;
  builder.add("role", enums::Role::to_string(result.role));
  
  // Serialize content
  match(result.content,
    [&builder](const TextContent& text) {
      builder.add("content", serialize_TextContent(text));
    },
    [&builder](const ImageContent& image) {
      builder.add("content", serialize_ImageContent(image));
    },
    [&builder](const AudioContent& audio) {
      builder.add("content", serialize_AudioContent(audio));
    }
  );
  
  builder.add("model", result.model);
  
  if (result.stopReason.has_value()) {
    builder.add("stopReason", result.stopReason.value());
  }
  
  return builder.build();
}

JsonValue serialize_ElicitResult(const ElicitResult& result) {
  JsonObjectBuilder builder;
  
  match(result.value,
    [&builder](const std::string& s) {
      builder.add("value", s);
    },
    [&builder](double d) {
      builder.add("value", d);
    },
    [&builder](bool b) {
      builder.add("value", b);
    },
    [&builder](std::nullptr_t) {
      builder.addNull("value");
    }
  );
  
  return builder.build();
}

// ===== Notification Types Serialization =====

JsonValue serialize_CancelledNotification(const CancelledNotification& notification) {
  JsonObjectBuilder builder;
  builder.add("requestId", serialize_RequestId(notification.requestId));
  
  if (notification.reason.has_value()) {
    builder.add("reason", notification.reason.value());
  }
  
  return builder.build();
}

JsonValue serialize_ProgressNotification(const ProgressNotification& notification) {
  JsonObjectBuilder builder;
  // ProgressToken is the same type as RequestId
  builder.add("progressToken", serialize_RequestId(static_cast<RequestId>(notification.progressToken)))
         .add("progress", notification.progress);
  
  if (notification.total.has_value()) {
    builder.add("total", notification.total.value());
  }
  
  return builder.build();
}

JsonValue serialize_InitializedNotification(const InitializedNotification& notification) {
  (void)notification; // Empty params
  return JsonObjectBuilder().build();
}

JsonValue serialize_RootsListChangedNotification(const RootsListChangedNotification& notification) {
  (void)notification; // Empty params
  return JsonObjectBuilder().build();
}

JsonValue serialize_LoggingMessageNotification(const LoggingMessageNotification& notification) {
  JsonObjectBuilder builder;
  builder.add("level", enums::LoggingLevel::to_string(notification.level));
  
  if (notification.logger.has_value()) {
    builder.add("logger", notification.logger.value());
  }
  
  match(notification.data,
    [&builder](const std::string& s) {
      builder.add("data", s);
    },
    [&builder](const Metadata& m) {
      builder.add("data", serialize_Metadata(m));
    }
  );
  
  return builder.build();
}

JsonValue serialize_ResourceUpdatedNotification(const ResourceUpdatedNotification& notification) {
  JsonObjectBuilder builder;
  builder.add("uri", notification.uri);
  return builder.build();
}

JsonValue serialize_ResourceListChangedNotification(const ResourceListChangedNotification& notification) {
  (void)notification; // Empty params
  return JsonObjectBuilder().build();
}

JsonValue serialize_ToolListChangedNotification(const ToolListChangedNotification& notification) {
  (void)notification; // Empty params
  return JsonObjectBuilder().build();
}

JsonValue serialize_PromptListChangedNotification(const PromptListChangedNotification& notification) {
  (void)notification; // Empty params
  return JsonObjectBuilder().build();
}

// ===== Core Data Structures Serialization =====

// Resources
JsonValue serialize_ResourceContents(const ResourceContents& contents) {
  JsonObjectBuilder builder;
  
  if (contents.uri.has_value()) {
    builder.add("uri", contents.uri.value());
  }
  
  if (contents.mimeType.has_value()) {
    builder.add("mimeType", contents.mimeType.value());
  }
  
  return builder.build();
}

JsonValue serialize_TextResourceContents(const TextResourceContents& contents) {
  JsonObjectBuilder builder;
  builder.add("text", contents.text);
  
  if (contents.uri.has_value()) {
    builder.add("uri", contents.uri.value());
  }
  
  if (contents.mimeType.has_value()) {
    builder.add("mimeType", contents.mimeType.value());
  }
  
  return builder.build();
}

JsonValue serialize_BlobResourceContents(const BlobResourceContents& contents) {
  JsonObjectBuilder builder;
  builder.add("blob", contents.blob);
  
  if (contents.uri.has_value()) {
    builder.add("uri", contents.uri.value());
  }
  
  if (contents.mimeType.has_value()) {
    builder.add("mimeType", contents.mimeType.value());
  }
  
  return builder.build();
}

// Messages
JsonValue serialize_Message(const Message& message) {
  JsonObjectBuilder builder;
  builder.add("role", enums::Role::to_string(message.role));
  builder.add("content", serialize_ContentBlock(message.content));
  return builder.build();
}

JsonValue serialize_SamplingMessage(const SamplingMessage& message) {
  JsonObjectBuilder builder;
  builder.add("role", enums::Role::to_string(message.role));
  
  match(message.content,
    [&builder](const TextContent& text) {
      builder.add("content", serialize_TextContent(text));
    },
    [&builder](const ImageContent& image) {
      builder.add("content", serialize_ImageContent(image));
    },
    [&builder](const AudioContent& audio) {
      builder.add("content", serialize_AudioContent(audio));
    }
  );
  
  return builder.build();
}

JsonValue serialize_ModelPreferences(const ModelPreferences& prefs) {
  JsonObjectBuilder builder;
  
  if (prefs.hints.has_value()) {
    JsonArrayBuilder hints;
    for (const auto& hint : prefs.hints.value()) {
      hints.add(serialize_ModelHint(hint));
    }
    builder.add("hints", hints.build());
  }
  
  if (prefs.costPriority.has_value()) {
    builder.add("costPriority", prefs.costPriority.value());
  }
  
  if (prefs.speedPriority.has_value()) {
    builder.add("speedPriority", prefs.speedPriority.value());
  }
  
  if (prefs.intelligencePriority.has_value()) {
    builder.add("intelligencePriority", prefs.intelligencePriority.value());
  }
  
  return builder.build();
}

JsonValue serialize_ModelHint(const ModelHint& hint) {
  JsonObjectBuilder builder;
  
  if (hint.name.has_value()) {
    builder.add("name", hint.name.value());
  }
  
  return builder.build();
}

// Annotations
JsonValue serialize_Annotations(const Annotations& annotations) {
  JsonObjectBuilder builder;
  
  if (annotations.audience.has_value()) {
    JsonArrayBuilder audience;
    for (const auto& role : annotations.audience.value()) {
      audience.add(enums::Role::to_string(role));
    }
    builder.add("audience", audience.build());
  }
  
  if (annotations.priority.has_value()) {
    builder.add("priority", annotations.priority.value());
  }
  
  return builder.build();
}

JsonValue serialize_ToolAnnotations(const ToolAnnotations& annotations) {
  JsonObjectBuilder builder;
  
  if (annotations.audience.has_value()) {
    JsonArrayBuilder audience;
    for (const auto& role : annotations.audience.value()) {
      audience.add(enums::Role::to_string(role));
    }
    builder.add("audience", audience.build());
  }
  
  return builder.build();
}

// References
JsonValue serialize_PromptReference(const PromptReference& ref) {
  JsonObjectBuilder builder;
  builder.add("type", ref.type)
         .add("name", ref.name);
  return builder.build();
}

JsonValue serialize_ResourceTemplateReference(const ResourceTemplateReference& ref) {
  JsonObjectBuilder builder;
  builder.add("type", ref.type)
         .add("name", ref.name);
  return builder.build();
}

// ===== Capability Types Serialization =====

JsonValue serialize_ServerCapabilities(const ServerCapabilities& caps) {
  JsonObjectBuilder builder;
  
  if (caps.experimental.has_value()) {
    builder.add("experimental", serialize_Metadata(caps.experimental.value()));
  }
  
  if (caps.resources.has_value()) {
    match(caps.resources.value(),
      [&builder](bool b) {
        builder.add("resources", b);
      },
      [&builder](const ResourcesCapability& res) {
        builder.add("resources", serialize_ResourcesCapability(res));
      }
    );
  }
  
  if (caps.tools.has_value()) {
    builder.add("tools", caps.tools.value());
  }
  
  if (caps.prompts.has_value()) {
    builder.add("prompts", caps.prompts.value());
  }
  
  if (caps.logging.has_value()) {
    builder.add("logging", caps.logging.value());
  }
  
  return builder.build();
}

JsonValue serialize_ClientCapabilities(const ClientCapabilities& caps) {
  JsonObjectBuilder builder;
  
  if (caps.experimental.has_value()) {
    builder.add("experimental", serialize_Metadata(caps.experimental.value()));
  }
  
  if (caps.sampling.has_value()) {
    builder.add("sampling", serialize_SamplingParams(caps.sampling.value()));
  }
  
  if (caps.roots.has_value()) {
    builder.add("roots", serialize_RootsCapability(caps.roots.value()));
  }
  
  return builder.build();
}

JsonValue serialize_RootsCapability(const RootsCapability& cap) {
  JsonObjectBuilder builder;
  
  if (cap.listChanged.has_value()) {
    builder.add("listChanged", serialize_EmptyCapability(cap.listChanged.value()));
  }
  
  return builder.build();
}

JsonValue serialize_ResourcesCapability(const ResourcesCapability& cap) {
  JsonObjectBuilder builder;
  
  if (cap.subscribe.has_value()) {
    builder.add("subscribe", serialize_EmptyCapability(cap.subscribe.value()));
  }
  
  if (cap.listChanged.has_value()) {
    builder.add("listChanged", serialize_EmptyCapability(cap.listChanged.value()));
  }
  
  return builder.build();
}

JsonValue serialize_PromptsCapability(const PromptsCapability& cap) {
  JsonObjectBuilder builder;
  
  if (cap.listChanged.has_value()) {
    builder.add("listChanged", serialize_EmptyCapability(cap.listChanged.value()));
  }
  
  return builder.build();
}

JsonValue serialize_EmptyCapability(const EmptyCapability& cap) {
  JsonObjectBuilder builder;
  for (const auto& kv : cap) {
    // EmptyCapability is a map of string to nlohmann::json
    auto json_str = kv.second.dump();
    builder.add(kv.first, JsonValue::parse(json_str));
  }
  return builder.build();
}

JsonValue serialize_SamplingParams(const SamplingParams& params) {
  JsonObjectBuilder builder;
  
  if (params.temperature.has_value()) {
    builder.add("temperature", params.temperature.value());
  }
  
  if (params.maxTokens.has_value()) {
    builder.add("maxTokens", params.maxTokens.value());
  }
  
  if (params.stopSequences.has_value()) {
    JsonArrayBuilder stops;
    for (const auto& seq : params.stopSequences.value()) {
      stops.add(seq);
    }
    builder.add("stopSequences", stops.build());
  }
  
  if (params.metadata.has_value()) {
    builder.add("metadata", serialize_Metadata(params.metadata.value()));
  }
  
  return builder.build();
}

// ===== Helper Functions Serialization =====

// Serialize enums
// Removed enum serializers - use template specializations

// Serialize pagination info
// Removed serializePaginationInfo - use serialize(optional<Cursor>) directly

// Removed serialize(Implementation) - use template specialization

// Note: ProgressToken and RequestId are the same type, so we reuse serialize(RequestId)

// ===== Deserialization of Request Types =====

InitializeRequest deserialize_InitializeRequest(const JsonValue& json) {
  InitializeRequest request;
  request.protocolVersion = json.at("protocolVersion").getString();
  request.capabilities = deserialize_ClientCapabilities(json.at("capabilities"));
  
  if (json.contains("clientInfo")) {
    request.clientInfo = JsonDeserializer::deserialize<Implementation>(json["clientInfo"]);
  }
  
  return request;
}

PingRequest deserialize_PingRequest(const JsonValue& json) {
  (void)json;
  return PingRequest();
}

CompleteRequest deserialize_CompleteRequest(const JsonValue& json) {
  CompleteRequest request;
  request.ref = deserialize_PromptReference(json.at("ref"));
  
  if (json.contains("argument")) {
    request.argument = json["argument"].getString();
  }
  
  return request;
}

SetLevelRequest deserialize_SetLevelRequest(const JsonValue& json) {
  SetLevelRequest request;
  auto level_str = json.at("level").getString();
  auto level = enums::LoggingLevel::from_string(level_str);
  if (!level.has_value()) {
    throw JsonException("Invalid logging level: " + level_str);
  }
  request.level = level.value();
  return request;
}

CallToolRequest deserialize_CallToolRequest(const JsonValue& json) {
  CallToolRequest request;
  request.name = json.at("name").getString();
  
  if (json.contains("arguments")) {
    request.arguments = deserialize_Metadata(json["arguments"]);
  }
  
  return request;
}

ListToolsRequest deserialize_ListToolsRequest(const JsonValue& json) {
  ListToolsRequest request;
  if (json.contains("cursor")) {
    request.cursor = json["cursor"].getString();
  }
  return request;
}

PromptMessage deserialize_PromptMessage(const JsonValue& json) {
  PromptMessage message;
  
  auto role_str = json.at("role").getString();
  auto role = enums::Role::from_string(role_str);
  if (!role.has_value()) {
    throw JsonException("Invalid role: " + role_str);
  }
  message.role = role.value();
  
  const auto& content = json.at("content");
  if (content.isObject() && content.contains("type")) {
    std::string type = content["type"].getString();
    if (type == "text") {
      message.content = deserialize_TextContent(content);
    } else if (type == "image") {
      message.content = deserialize_ImageContent(content);
    } else if (type == "embedded") {
      message.content = deserialize_EmbeddedResource(content);
    }
  } else if (content.isString()) {
    // Plain text content
    TextContent text;
    text.text = content.getString();
    message.content = text;
  }
  
  return message;
}

GetPromptRequest deserialize_GetPromptRequest(const JsonValue& json) {
  GetPromptRequest request;
  request.name = json.at("name").getString();
  
  if (json.contains("arguments")) {
    request.arguments = deserialize_Metadata(json["arguments"]);
  }
  
  return request;
}

ListPromptsRequest deserialize_ListPromptsRequest(const JsonValue& json) {
  ListPromptsRequest request;
  if (json.contains("cursor")) {
    request.cursor = json["cursor"].getString();
  }
  return request;
}

ResourceTemplate deserialize_ResourceTemplate(const JsonValue& json) {
  ResourceTemplate tmpl;
  tmpl.uriTemplate = json.at("uriTemplate").getString();
  tmpl.name = json.at("name").getString();
  
  if (json.contains("description")) {
    tmpl.description = json["description"].getString();
  }
  
  if (json.contains("mimeType")) {
    tmpl.mimeType = json["mimeType"].getString();
  }
  
  return tmpl;
}

ReadResourceRequest deserialize_ReadResourceRequest(const JsonValue& json) {
  ReadResourceRequest request;
  request.uri = json.at("uri").getString();
  return request;
}

ListResourcesRequest deserialize_ListResourcesRequest(const JsonValue& json) {
  ListResourcesRequest request;
  if (json.contains("cursor")) {
    request.cursor = json["cursor"].getString();
  }
  return request;
}

ListResourceTemplatesRequest deserialize_ListResourceTemplatesRequest(const JsonValue& json) {
  ListResourceTemplatesRequest request;
  if (json.contains("cursor")) {
    request.cursor = json["cursor"].getString();
  }
  return request;
}

SubscribeRequest deserialize_SubscribeRequest(const JsonValue& json) {
  SubscribeRequest request;
  request.uri = json.at("uri").getString();
  return request;
}

UnsubscribeRequest deserialize_UnsubscribeRequest(const JsonValue& json) {
  UnsubscribeRequest request;
  request.uri = json.at("uri").getString();
  return request;
}

Root deserialize_Root(const JsonValue& json) {
  Root root;
  root.uri = json.at("uri").getString();
  
  if (json.contains("name")) {
    root.name = json["name"].getString();
  }
  
  return root;
}

ListRootsRequest deserialize_ListRootsRequest(const JsonValue& json) {
  (void)json;
  return ListRootsRequest();
}

CreateMessageRequest deserialize_CreateMessageRequest(const JsonValue& json) {
  CreateMessageRequest request;
  
  const auto& messages = json.at("messages");
  size_t size = messages.size();
  for (size_t i = 0; i < size; ++i) {
    request.messages.push_back(deserialize_SamplingMessage(messages[i]));
  }
  
  if (json.contains("modelPreferences")) {
    request.modelPreferences = deserialize_ModelPreferences(json["modelPreferences"]);
  }
  
  if (json.contains("systemPrompt")) {
    request.systemPrompt = json["systemPrompt"].getString();
  }
  
  if (json.contains("includeContext")) {
    request.includeContext = deserialize_Metadata(json["includeContext"]);
  }
  
  if (json.contains("temperature")) {
    request.temperature = json["temperature"].getFloat();
  }
  
  if (json.contains("maxTokens")) {
    request.maxTokens = json["maxTokens"].getInt();
  }
  
  if (json.contains("stopSequences")) {
    std::vector<std::string> stops;
    const auto& stopsArray = json["stopSequences"];
    size_t stopSize = stopsArray.size();
    for (size_t i = 0; i < stopSize; ++i) {
      stops.push_back(stopsArray[i].getString());
    }
    request.stopSequences = stops;
  }
  
  if (json.contains("metadata")) {
    request.metadata = deserialize_Metadata(json["metadata"]);
  }
  
  return request;
}

ElicitRequest deserialize_ElicitRequest(const JsonValue& json) {
  ElicitRequest request;
  request.name = json.at("name").getString();
  
  const auto& schema = json.at("schema");
  std::string schemaType = schema.at("type").getString();
  
  if (schemaType == "string") {
    StringSchema s;
    if (schema.contains("description")) {
      s.description = schema["description"].getString();
    }
    if (schema.contains("pattern")) {
      s.pattern = schema["pattern"].getString();
    }
    if (schema.contains("minLength")) {
      s.minLength = schema["minLength"].getInt();
    }
    if (schema.contains("maxLength")) {
      s.maxLength = schema["maxLength"].getInt();
    }
    request.schema = PrimitiveSchemaDefinition(s);
  } else if (schemaType == "number") {
    NumberSchema n;
    if (schema.contains("description")) {
      n.description = schema["description"].getString();
    }
    if (schema.contains("minimum")) {
      n.minimum = schema["minimum"].getFloat();
    }
    if (schema.contains("maximum")) {
      n.maximum = schema["maximum"].getFloat();
    }
    if (schema.contains("multipleOf")) {
      n.multipleOf = schema["multipleOf"].getFloat();
    }
    request.schema = PrimitiveSchemaDefinition(n);
  } else if (schemaType == "boolean") {
    BooleanSchema b;
    if (schema.contains("description")) {
      b.description = schema["description"].getString();
    }
    request.schema = PrimitiveSchemaDefinition(b);
  } else if (schema.contains("enum")) {
    EnumSchema e;
    if (schema.contains("description")) {
      e.description = schema["description"].getString();
    }
    const auto& enumArray = schema["enum"];
    size_t enumSize = enumArray.size();
    for (size_t i = 0; i < enumSize; ++i) {
      e.values.push_back(enumArray[i].getString());
    }
    request.schema = PrimitiveSchemaDefinition(e);
  }
  
  if (json.contains("prompt")) {
    request.prompt = json["prompt"].getString();
  }
  
  return request;
}

// ===== Deserialization of Response/Result Types =====

InitializeResult deserialize_InitializeResult(const JsonValue& json) {
  InitializeResult result;
  result.protocolVersion = json.at("protocolVersion").getString();
  result.capabilities = deserialize_ServerCapabilities(json.at("capabilities"));
  
  if (json.contains("serverInfo")) {
    result.serverInfo = JsonDeserializer::deserialize<Implementation>(json["serverInfo"]);
  }
  
  if (json.contains("instructions")) {
    result.instructions = json["instructions"].getString();
  }
  
  return result;
}

CompleteResult deserialize_CompleteResult(const JsonValue& json) {
  CompleteResult result;
  
  const auto& completion = json.at("completion");
  const auto& values = completion.at("values");
  size_t size = values.size();
  for (size_t i = 0; i < size; ++i) {
    result.completion.values.push_back(values[i].getString());
  }
  
  if (completion.contains("total")) {
    result.completion.total = completion["total"].getFloat();
  }
  
  if (completion.contains("hasMore")) {
    result.completion.hasMore = completion["hasMore"].getBool();
  }
  
  return result;
}

CallToolResult deserialize_CallToolResult(const JsonValue& json) {
  CallToolResult result;
  
  const auto& content = json.at("content");
  size_t size = content.size();
  for (size_t i = 0; i < size; ++i) {
    result.content.push_back(deserialize_ExtendedContentBlock(content[i]));
  }
  
  if (json.contains("isError")) {
    result.isError = json["isError"].getBool();
  }
  
  return result;
}

ListToolsResult deserialize_ListToolsResult(const JsonValue& json) {
  ListToolsResult result;
  
  const auto& tools = json.at("tools");
  size_t size = tools.size();
  for (size_t i = 0; i < size; ++i) {
    result.tools.push_back(deserialize_Tool(tools[i]));
  }
  
  return result;
}

GetPromptResult deserialize_GetPromptResult(const JsonValue& json) {
  GetPromptResult result;
  
  if (json.contains("description")) {
    result.description = json["description"].getString();
  }
  
  const auto& messages = json.at("messages");
  size_t size = messages.size();
  for (size_t i = 0; i < size; ++i) {
    result.messages.push_back(deserialize_PromptMessage(messages[i]));
  }
  
  return result;
}

ListPromptsResult deserialize_ListPromptsResult(const JsonValue& json) {
  ListPromptsResult result;
  
  const auto& prompts = json.at("prompts");
  size_t size = prompts.size();
  for (size_t i = 0; i < size; ++i) {
    result.prompts.push_back(deserialize_Prompt(prompts[i]));
  }
  
  if (json.contains("nextCursor")) {
    result.nextCursor = json["nextCursor"].getString();
  }
  
  return result;
}

variant<TextResourceContents, BlobResourceContents> deserialize_ResourceContents(const JsonValue& json) {
  if (json.contains("text")) {
    return deserialize_TextResourceContents(json);
  } else if (json.contains("blob")) {
    return deserialize_BlobResourceContents(json);
  }
  throw JsonException("Invalid resource contents");
}

TextResourceContents deserialize_TextResourceContents(const JsonValue& json) {
  TextResourceContents contents;
  contents.text = json.at("text").getString();
  
  if (json.contains("uri")) {
    contents.uri = json["uri"].getString();
  }
  
  if (json.contains("mimeType")) {
    contents.mimeType = json["mimeType"].getString();
  }
  
  return contents;
}

BlobResourceContents deserialize_BlobResourceContents(const JsonValue& json) {
  BlobResourceContents contents;
  contents.blob = json.at("blob").getString();
  
  if (json.contains("uri")) {
    contents.uri = json["uri"].getString();
  }
  
  if (json.contains("mimeType")) {
    contents.mimeType = json["mimeType"].getString();
  }
  
  return contents;
}

ReadResourceResult deserialize_ReadResourceResult(const JsonValue& json) {
  ReadResourceResult result;
  
  const auto& contents = json.at("contents");
  size_t size = contents.size();
  for (size_t i = 0; i < size; ++i) {
    result.contents.push_back(deserialize_ResourceContents(contents[i]));
  }
  
  return result;
}

ListResourcesResult deserialize_ListResourcesResult(const JsonValue& json) {
  ListResourcesResult result;
  
  const auto& resources = json.at("resources");
  size_t size = resources.size();
  for (size_t i = 0; i < size; ++i) {
    result.resources.push_back(deserialize_Resource(resources[i]));
  }
  
  if (json.contains("nextCursor")) {
    result.nextCursor = json["nextCursor"].getString();
  }
  
  return result;
}

ListResourceTemplatesResult deserialize_ListResourceTemplatesResult(const JsonValue& json) {
  ListResourceTemplatesResult result;
  
  const auto& templates = json.at("resourceTemplates");
  size_t size = templates.size();
  for (size_t i = 0; i < size; ++i) {
    result.resourceTemplates.push_back(deserialize_ResourceTemplate(templates[i]));
  }
  
  if (json.contains("nextCursor")) {
    result.nextCursor = json["nextCursor"].getString();
  }
  
  return result;
}

ListRootsResult deserialize_ListRootsResult(const JsonValue& json) {
  ListRootsResult result;
  
  const auto& roots = json.at("roots");
  size_t size = roots.size();
  for (size_t i = 0; i < size; ++i) {
    result.roots.push_back(deserialize_Root(roots[i]));
  }
  
  return result;
}

CreateMessageResult deserialize_CreateMessageResult(const JsonValue& json) {
  CreateMessageResult result;
  
  auto role_str = json.at("role").getString();
  auto role = enums::Role::from_string(role_str);
  if (!role.has_value()) {
    throw JsonException("Invalid role: " + role_str);
  }
  result.role = role.value();
  
  const auto& content = json.at("content");
  if (content.isObject() && content.contains("type")) {
    std::string type = content["type"].getString();
    if (type == "text") {
      result.content = deserialize_TextContent(content);
    } else if (type == "image") {
      result.content = deserialize_ImageContent(content);
    } else if (type == "audio") {
      result.content = deserialize_AudioContent(content);
    }
  }
  
  result.model = json.at("model").getString();
  
  if (json.contains("stopReason")) {
    result.stopReason = json["stopReason"].getString();
  }
  
  return result;
}

ElicitResult deserialize_ElicitResult(const JsonValue& json) {
  ElicitResult result;
  
  const auto& value = json.at("value");
  if (value.isNull()) {
    result.value = variant<std::string, double, bool, std::nullptr_t>(nullptr);
  } else if (value.isBoolean()) {
    result.value = variant<std::string, double, bool, std::nullptr_t>(value.getBool());
  } else if (value.isFloat() || value.isInteger()) {
    result.value = variant<std::string, double, bool, std::nullptr_t>(value.getFloat());
  } else if (value.isString()) {
    result.value = variant<std::string, double, bool, std::nullptr_t>(value.getString());
  }
  
  return result;
}

// ===== Deserialization of Notification Types =====

CancelledNotification deserialize_CancelledNotification(const JsonValue& json) {
  CancelledNotification notif;
  notif.requestId = deserialize_RequestId(json.at("requestId"));
  
  if (json.contains("reason")) {
    notif.reason = json["reason"].getString();
  }
  
  return notif;
}

ProgressNotification deserialize_ProgressNotification(const JsonValue& json) {
  ProgressNotification notif;
  
  const auto& token = json.at("progressToken");
  if (token.isString()) {
    notif.progressToken = ProgressToken(token.getString());
  } else if (token.isInteger()) {
    notif.progressToken = ProgressToken(token.getInt());
  }
  
  notif.progress = json.at("progress").getFloat();
  
  if (json.contains("total")) {
    notif.total = json["total"].getFloat();
  }
  
  return notif;
}

InitializedNotification deserialize_InitializedNotification(const JsonValue& json) {
  (void)json;
  return InitializedNotification();
}

RootsListChangedNotification deserialize_RootsListChangedNotification(const JsonValue& json) {
  (void)json;
  return RootsListChangedNotification();
}

LoggingMessageNotification deserialize_LoggingMessageNotification(const JsonValue& json) {
  LoggingMessageNotification notif;
  
  auto level_str = json.at("level").getString();
  auto level = enums::LoggingLevel::from_string(level_str);
  if (!level.has_value()) {
    throw JsonException("Invalid logging level: " + level_str);
  }
  notif.level = level.value();
  
  if (json.contains("logger")) {
    notif.logger = json["logger"].getString();
  }
  
  const auto& data = json.at("data");
  if (data.isString()) {
    notif.data = variant<std::string, Metadata>(data.getString());
  } else if (data.isObject()) {
    notif.data = variant<std::string, Metadata>(deserialize_Metadata(data));
  }
  
  return notif;
}

ResourceUpdatedNotification deserialize_ResourceUpdatedNotification(const JsonValue& json) {
  ResourceUpdatedNotification notif;
  notif.uri = json.at("uri").getString();
  return notif;
}

ResourceListChangedNotification deserialize_ResourceListChangedNotification(const JsonValue& json) {
  (void)json;
  return ResourceListChangedNotification();
}

ToolListChangedNotification deserialize_ToolListChangedNotification(const JsonValue& json) {
  (void)json;
  return ToolListChangedNotification();
}

PromptListChangedNotification deserialize_PromptListChangedNotification(const JsonValue& json) {
  (void)json;
  return PromptListChangedNotification();
}

// ===== Deserialization of Core Data Structures =====

Message deserialize_Message(const JsonValue& json) {
  Message message;
  
  auto role_str = json.at("role").getString();
  auto role = enums::Role::from_string(role_str);
  if (!role.has_value()) {
    throw JsonException("Invalid role: " + role_str);
  }
  message.role = role.value();
  
  message.content = deserialize_ContentBlock(json.at("content"));
  
  return message;
}

SamplingMessage deserialize_SamplingMessage(const JsonValue& json) {
  SamplingMessage message;
  
  auto role_str = json.at("role").getString();
  auto role = enums::Role::from_string(role_str);
  if (!role.has_value()) {
    throw JsonException("Invalid role: " + role_str);
  }
  message.role = role.value();
  
  const auto& content = json.at("content");
  if (content.isObject() && content.contains("type")) {
    std::string type = content["type"].getString();
    if (type == "text") {
      message.content = deserialize_TextContent(content);
    } else if (type == "image") {
      message.content = deserialize_ImageContent(content);
    } else if (type == "audio") {
      message.content = deserialize_AudioContent(content);
    }
  }
  
  return message;
}

ModelPreferences deserialize_ModelPreferences(const JsonValue& json) {
  ModelPreferences prefs;
  
  if (json.contains("hints")) {
    std::vector<ModelHint> hints;
    const auto& hintsArray = json["hints"];
    size_t size = hintsArray.size();
    for (size_t i = 0; i < size; ++i) {
      hints.push_back(deserialize_ModelHint(hintsArray[i]));
    }
    prefs.hints = hints;
  }
  
  if (json.contains("costPriority")) {
    prefs.costPriority = json["costPriority"].getFloat();
  }
  
  if (json.contains("speedPriority")) {
    prefs.speedPriority = json["speedPriority"].getFloat();
  }
  
  if (json.contains("intelligencePriority")) {
    prefs.intelligencePriority = json["intelligencePriority"].getFloat();
  }
  
  return prefs;
}

ModelHint deserialize_ModelHint(const JsonValue& json) {
  ModelHint hint;
  
  if (json.contains("name")) {
    hint.name = json["name"].getString();
  }
  
  return hint;
}

Annotations deserialize_Annotations(const JsonValue& json) {
  Annotations annotations;
  
  if (json.contains("audience")) {
    std::vector<enums::Role::Value> audience;
    const auto& audienceArray = json["audience"];
    size_t size = audienceArray.size();
    for (size_t i = 0; i < size; ++i) {
      auto role = enums::Role::from_string(audienceArray[i].getString());
      if (role.has_value()) {
        audience.push_back(role.value());
      }
    }
    annotations.audience = audience;
  }
  
  if (json.contains("priority")) {
    annotations.priority = json["priority"].getFloat();
  }
  
  return annotations;
}

ToolAnnotations deserialize_ToolAnnotations(const JsonValue& json) {
  ToolAnnotations annotations;
  
  if (json.contains("audience")) {
    std::vector<enums::Role::Value> audience;
    const auto& audienceArray = json["audience"];
    size_t size = audienceArray.size();
    for (size_t i = 0; i < size; ++i) {
      auto role = enums::Role::from_string(audienceArray[i].getString());
      if (role.has_value()) {
        audience.push_back(role.value());
      }
    }
    annotations.audience = audience;
  }
  
  return annotations;
}

PromptReference deserialize_PromptReference(const JsonValue& json) {
  PromptReference ref;
  ref.type = json.at("type").getString();
  ref.name = json.at("name").getString();
  return ref;
}

ResourceTemplateReference deserialize_ResourceTemplateReference(const JsonValue& json) {
  ResourceTemplateReference ref;
  ref.type = json.at("type").getString();
  ref.name = json.at("name").getString();
  return ref;
}

// ===== Deserialization of Capability Types =====

ServerCapabilities deserialize_ServerCapabilities(const JsonValue& json) {
  ServerCapabilities caps;
  
  if (json.contains("experimental")) {
    caps.experimental = deserialize_Metadata(json["experimental"]);
  }
  
  if (json.contains("resources")) {
    const auto& res = json["resources"];
    if (res.isBoolean()) {
      caps.resources = variant<bool, ResourcesCapability>(res.getBool());
    } else if (res.isObject()) {
      caps.resources = variant<bool, ResourcesCapability>(deserialize_ResourcesCapability(res));
    }
  }
  
  if (json.contains("tools")) {
    caps.tools = json["tools"].getBool();
  }
  
  if (json.contains("prompts")) {
    caps.prompts = json["prompts"].getBool();
  }
  
  if (json.contains("logging")) {
    caps.logging = json["logging"].getBool();
  }
  
  return caps;
}

ClientCapabilities deserialize_ClientCapabilities(const JsonValue& json) {
  ClientCapabilities caps;
  
  if (json.contains("experimental")) {
    caps.experimental = deserialize_Metadata(json["experimental"]);
  }
  
  if (json.contains("sampling")) {
    caps.sampling = deserialize_SamplingParams(json["sampling"]);
  }
  
  if (json.contains("roots")) {
    caps.roots = deserialize_RootsCapability(json["roots"]);
  }
  
  return caps;
}

RootsCapability deserialize_RootsCapability(const JsonValue& json) {
  RootsCapability cap;
  
  if (json.contains("listChanged")) {
    cap.listChanged = deserialize_EmptyCapability(json["listChanged"]);
  }
  
  return cap;
}

ResourcesCapability deserialize_ResourcesCapability(const JsonValue& json) {
  ResourcesCapability cap;
  
  if (json.contains("subscribe")) {
    cap.subscribe = deserialize_EmptyCapability(json["subscribe"]);
  }
  
  if (json.contains("listChanged")) {
    cap.listChanged = deserialize_EmptyCapability(json["listChanged"]);
  }
  
  return cap;
}

PromptsCapability deserialize_PromptsCapability(const JsonValue& json) {
  PromptsCapability cap;
  
  if (json.contains("listChanged")) {
    cap.listChanged = deserialize_EmptyCapability(json["listChanged"]);
  }
  
  return cap;
}

EmptyCapability deserialize_EmptyCapability(const JsonValue& json) {
  EmptyCapability cap;
  
  for (const auto& key : json.keys()) {
    // Convert JsonValue to nlohmann::json
    cap[key] = toNlohmannJson(json[key]);
  }
  
  return cap;
}

SamplingParams deserialize_SamplingParams(const JsonValue& json) {
  SamplingParams params;
  
  if (json.contains("temperature")) {
    params.temperature = json["temperature"].getFloat();
  }
  
  if (json.contains("maxTokens")) {
    params.maxTokens = json["maxTokens"].getInt();
  }
  
  if (json.contains("stopSequences")) {
    std::vector<std::string> stops;
    const auto& stopsArray = json["stopSequences"];
    size_t size = stopsArray.size();
    for (size_t i = 0; i < size; ++i) {
      stops.push_back(stopsArray[i].getString());
    }
    params.stopSequences = stops;
  }
  
  if (json.contains("metadata")) {
    params.metadata = deserialize_Metadata(json["metadata"]);
  }
  
  return params;
}

// ===== Helper Functions Deserialization =====

// All helper functions removed - using template specializations in header

}  // namespace impl
}  // namespace json
}  // namespace mcp