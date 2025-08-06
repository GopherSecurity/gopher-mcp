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
JsonValue JsonSerializer::serialize(const Error& error) {
  JsonObjectBuilder builder;
  builder.add("code", error.code)
         .add("message", error.message);
  
  if (error.data.has_value()) {
    builder.add("data", serialize(error.data.value()));
  }
  
  return builder.build();
}

JsonValue JsonSerializer::serialize(const ErrorData& data) {
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
JsonValue JsonSerializer::serialize(const RequestId& id) {
  JsonValue result;
  
  match(id,
    [&result](const std::string& s) { result = JsonValue(s); },
    [&result](int i) { result = JsonValue(i); }
  );
  
  return result;
}

JsonValue JsonSerializer::serialize(const jsonrpc::Request& request) {
  JsonObjectBuilder builder;
  builder.add("jsonrpc", request.jsonrpc)
         .add("id", serialize(request.id))
         .add("method", request.method);
  
  if (request.params.has_value()) {
    builder.add("params", metadataToJson(request.params.value()));
  }
  
  return builder.build();
}

JsonValue JsonSerializer::serialize(const jsonrpc::Response& response) {
  JsonObjectBuilder builder;
  builder.add("jsonrpc", response.jsonrpc)
         .add("id", serialize(response.id));
  
  if (response.result.has_value()) {
    builder.add("result", serialize(response.result.value()));
  }
  
  if (response.error.has_value()) {
    builder.add("error", serialize(response.error.value()));
  }
  
  return builder.build();
}

JsonValue JsonSerializer::serialize(const jsonrpc::ResponseResult& result) {
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
        builder.add(serialize(block));
      }
      json_result = builder.build();
    },
    [&json_result](const std::vector<Tool>& tools) {
      JsonArrayBuilder builder;
      for (const auto& tool : tools) {
        builder.add(serialize(tool));
      }
      json_result = builder.build();
    },
    [&json_result](const std::vector<Prompt>& prompts) {
      JsonArrayBuilder builder;
      for (const auto& prompt : prompts) {
        builder.add(serialize(prompt));
      }
      json_result = builder.build();
    },
    [&json_result](const std::vector<Resource>& resources) {
      JsonArrayBuilder builder;
      for (const auto& resource : resources) {
        builder.add(serialize(resource));
      }
      json_result = builder.build();
    }
  );
  
  return json_result;
}

JsonValue JsonSerializer::serialize(const jsonrpc::Notification& notification) {
  JsonObjectBuilder builder;
  builder.add("jsonrpc", notification.jsonrpc)
         .add("method", notification.method);
  
  if (notification.params.has_value()) {
    builder.add("params", metadataToJson(notification.params.value()));
  }
  
  return builder.build();
}

// Serialize content types
JsonValue JsonSerializer::serialize(const TextContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "text")
         .add("text", content.text);
  return builder.build();
}

JsonValue JsonSerializer::serialize(const ImageContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "image")
         .add("mimeType", content.mimeType)
         .add("data", content.data);
  return builder.build();
}

JsonValue JsonSerializer::serialize(const AudioContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "audio")
         .add("mimeType", content.mimeType)
         .add("data", content.data);
  return builder.build();
}

JsonValue JsonSerializer::serialize(const ResourceContent& content) {
  JsonObjectBuilder builder;
  builder.add("type", "resource")
         .add("resource", serialize(content.resource));
  return builder.build();
}

JsonValue JsonSerializer::serialize(const ContentBlock& block) {
  JsonValue result;
  
  match(block,
    [&result](const TextContent& text) { 
      result = serialize(text); 
    },
    [&result](const ImageContent& image) { 
      result = serialize(image); 
    },
    [&result](const ResourceContent& resource) { 
      result = serialize(resource); 
    }
  );
  
  return result;
}

// Serialize Tool
JsonValue JsonSerializer::serialize(const Tool& tool) {
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
JsonValue JsonSerializer::serialize(const Prompt& prompt) {
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
JsonValue JsonSerializer::serialize(const Resource& resource) {
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
JsonValue JsonSerializer::serialize(const Metadata& metadata) {
  return metadataToJson(metadata);
}

// Deserialize Error and ErrorData
Error JsonDeserializer::deserializeError(const JsonValue& json) {
  Error error;
  error.code = json.at("code").getInt();
  error.message = json.at("message").getString();
  
  if (json.contains("data")) {
    error.data = deserializeErrorData(json["data"]);
  }
  
  return error;
}

ErrorData JsonDeserializer::deserializeErrorData(const JsonValue& json) {
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
RequestId JsonDeserializer::deserializeRequestId(const JsonValue& json) {
  if (json.isString()) {
    return RequestId(json.getString());
  } else if (json.isInteger()) {
    return RequestId(json.getInt());
  }
  throw JsonException("Invalid RequestId type");
}

jsonrpc::Request JsonDeserializer::deserializeRequest(const JsonValue& json) {
  jsonrpc::Request request;
  request.jsonrpc = json.at("jsonrpc").getString();
  request.id = deserializeRequestId(json.at("id"));
  request.method = json.at("method").getString();
  
  if (json.contains("params")) {
    request.params = jsonToMetadata(json["params"]);
  }
  
  return request;
}

jsonrpc::Response JsonDeserializer::deserializeResponse(const JsonValue& json) {
  jsonrpc::Response response;
  response.jsonrpc = json.at("jsonrpc").getString();
  response.id = deserializeRequestId(json.at("id"));
  
  if (json.contains("result")) {
    response.result = deserializeResponseResult(json["result"]);
  }
  
  if (json.contains("error")) {
    response.error = deserializeError(json["error"]);
  }
  
  return response;
}

jsonrpc::ResponseResult JsonDeserializer::deserializeResponseResult(const JsonValue& json) {
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
        blocks.push_back(deserializeContentBlock(json));
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
            blocks.push_back(deserializeContentBlock(json[i]));
          }
          return jsonrpc::ResponseResult(blocks);
        }
      } else if (first.contains("name") && first.contains("inputSchema")) {
        // Array of Tools
        std::vector<Tool> tools;
        size_t size = json.size();
        for (size_t i = 0; i < size; ++i) {
          tools.push_back(deserializeTool(json[i]));
        }
        return jsonrpc::ResponseResult(tools);
      } else if (first.contains("uri")) {
        // Array of Resources
        std::vector<Resource> resources;
        size_t size = json.size();
        for (size_t i = 0; i < size; ++i) {
          resources.push_back(deserializeResource(json[i]));
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

jsonrpc::Notification JsonDeserializer::deserializeNotification(const JsonValue& json) {
  jsonrpc::Notification notification;
  notification.jsonrpc = json.at("jsonrpc").getString();
  notification.method = json.at("method").getString();
  
  if (json.contains("params")) {
    notification.params = jsonToMetadata(json["params"]);
  }
  
  return notification;
}

// Deserialize content types
ContentBlock JsonDeserializer::deserializeContentBlock(const JsonValue& json) {
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
    resource.resource = deserializeResource(json.at("resource"));
    return ContentBlock(resource);
  }
  
  throw JsonException("Unknown content block type: " + type);
}

// Deserialize Tool
Tool JsonDeserializer::deserializeTool(const JsonValue& json) {
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
Resource JsonDeserializer::deserializeResource(const JsonValue& json) {
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
Metadata JsonDeserializer::deserializeMetadata(const JsonValue& json) {
  return jsonToMetadata(json);
}

}  // namespace json
}  // namespace mcp