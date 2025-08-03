#include "mcp/json.h"
#include "mcp/types.h"
#include <nlohmann/json.hpp>

using namespace mcp;
using json = nlohmann::json;

namespace mcp {

// Enum serialization
void to_json(json& j, const enums::Role::Value& role) {
  j = enums::Role::to_string(role);
}

void from_json(const json& j, enums::Role::Value& role) {
  auto opt = enums::Role::from_string(j.get<std::string>());
  if (opt.has_value()) {
    role = opt.value();
  } else {
    throw std::runtime_error("Invalid role value: " + j.get<std::string>());
  }
}

void to_json(json& j, const enums::LoggingLevel::Value& level) {
  j = enums::LoggingLevel::to_string(level);
}

void from_json(const json& j, enums::LoggingLevel::Value& level) {
  auto opt = enums::LoggingLevel::from_string(j.get<std::string>());
  if (opt.has_value()) {
    level = opt.value();
  } else {
    throw std::runtime_error("Invalid logging level: " + j.get<std::string>());
  }
}

// Annotations
void to_json(json& j, const Annotations& ann) {
  j = json::object();
  if (ann.audience.has_value()) {
    j["audience"] = json::array();
    for (const auto& role : ann.audience.value()) {
      j["audience"].push_back(enums::Role::to_string(role));
    }
  }
  if (ann.priority.has_value()) {
    j["priority"] = ann.priority.value();
  }
}

void from_json(const json& j, Annotations& ann) {
  if (j.contains("audience")) {
    std::vector<enums::Role::Value> audience;
    for (const auto& role_str : j.at("audience")) {
      auto opt = enums::Role::from_string(role_str.get<std::string>());
      if (opt.has_value()) {
        audience.push_back(opt.value());
      }
    }
    ann.audience = audience;
  }
  if (j.contains("priority")) {
    ann.priority = j.at("priority").get<double>();
  }
}

// TextContent
void to_json(json& j, const TextContent& content) {
  j = json{{"type", content.type}, {"text", content.text}};
  if (content.annotations.has_value()) {
    to_json(j["annotations"], content.annotations.value());
  }
}

void from_json(const json& j, TextContent& content) {
  if (j.contains("type")) {
    content.type = j.at("type").get<std::string>();
  }
  content.text = j.at("text").get<std::string>();
  if (j.contains("annotations")) {
    Annotations ann;
    from_json(j.at("annotations"), ann);
    content.annotations = ann;
  }
}

// ImageContent
void to_json(json& j, const ImageContent& content) {
  j = json{{"type", content.type}, {"data", content.data}, {"mimeType", content.mimeType}};
}

void from_json(const json& j, ImageContent& content) {
  if (j.contains("type")) {
    content.type = j.at("type").get<std::string>();
  }
  content.data = j.at("data").get<std::string>();
  content.mimeType = j.at("mimeType").get<std::string>();
}

// AudioContent
void to_json(json& j, const AudioContent& content) {
  j = json{{"type", content.type}, {"data", content.data}, {"mimeType", content.mimeType}};
}

void from_json(const json& j, AudioContent& content) {
  if (j.contains("type")) {
    content.type = j.at("type").get<std::string>();
  }
  content.data = j.at("data").get<std::string>();
  content.mimeType = j.at("mimeType").get<std::string>();
}

// Resource
void to_json(json& j, const Resource& resource) {
  j = json{{"uri", resource.uri}, {"name", resource.name}};
  if (resource.description.has_value()) {
    j["description"] = resource.description.value();
  }
  if (resource.mimeType.has_value()) {
    j["mimeType"] = resource.mimeType.value();
  }
}

void from_json(const json& j, Resource& resource) {
  resource.uri = j.at("uri").get<std::string>();
  resource.name = j.at("name").get<std::string>();
  if (j.contains("description")) {
    resource.description = j.at("description").get<std::string>();
  }
  if (j.contains("mimeType")) {
    resource.mimeType = j.at("mimeType").get<std::string>();
  }
}

// ResourceContent
void to_json(json& j, const ResourceContent& content) {
  j = json{{"type", content.type}};
  to_json(j["resource"], content.resource);
}

void from_json(const json& j, ResourceContent& content) {
  if (j.contains("type")) {
    content.type = j.at("type").get<std::string>();
  }
  from_json(j.at("resource"), content.resource);
}

// ResourceLink
void to_json(json& j, const ResourceLink& link) {
  j = json{{"type", link.type}};
  // Add resource fields
  j["uri"] = link.uri;
  j["name"] = link.name;
  if (link.description.has_value()) {
    j["description"] = link.description.value();
  }
  if (link.mimeType.has_value()) {
    j["mimeType"] = link.mimeType.value();
  }
}

void from_json(const json& j, ResourceLink& link) {
  if (j.contains("type")) {
    link.type = j.at("type").get<std::string>();
  }
  link.uri = j.at("uri").get<std::string>();
  link.name = j.at("name").get<std::string>();
  if (j.contains("description")) {
    link.description = j.at("description").get<std::string>();
  }
  if (j.contains("mimeType")) {
    link.mimeType = j.at("mimeType").get<std::string>();
  }
}

// EmbeddedResource
void to_json(json& j, const EmbeddedResource& resource) {
  j = json{{"type", resource.type}};
  to_json(j["resource"], resource.resource);
  j["content"] = json::array();
  for (const auto& block : resource.content) {
    json content_json;
    to_json(content_json, block);
    j["content"].push_back(content_json);
  }
}

void from_json(const json& j, EmbeddedResource& resource) {
  if (j.contains("type")) {
    resource.type = j.at("type").get<std::string>();
  }
  from_json(j.at("resource"), resource.resource);
  if (j.contains("content")) {
    for (const auto& content_json : j.at("content")) {
      ContentBlock block;
      from_json(content_json, block);
      resource.content.push_back(block);
    }
  }
}

// Tool
void to_json(json& j, const Tool& tool) {
  j = json{{"name", tool.name}};
  if (tool.description.has_value()) {
    j["description"] = tool.description.value();
  }
  if (tool.inputSchema.has_value()) {
    j["inputSchema"] = tool.inputSchema.value();
  }
  if (tool.parameters.has_value()) {
    j["parameters"] = json::array();
    for (const auto& param : tool.parameters.value()) {
      json param_json = {{"name", param.name}, {"type", param.type}};
      if (param.description.has_value()) {
        param_json["description"] = param.description.value();
      }
      param_json["required"] = param.required;
      j["parameters"].push_back(param_json);
    }
  }
}

void from_json(const json& j, Tool& tool) {
  tool.name = j.at("name").get<std::string>();
  if (j.contains("description")) {
    tool.description = j.at("description").get<std::string>();
  }
  if (j.contains("inputSchema")) {
    tool.inputSchema = j.at("inputSchema");
  }
  if (j.contains("parameters")) {
    std::vector<ToolParameter> params;
    for (const auto& param_json : j.at("parameters")) {
      ToolParameter param;
      param.name = param_json.at("name").get<std::string>();
      param.type = param_json.at("type").get<std::string>();
      if (param_json.contains("description")) {
        param.description = param_json.at("description").get<std::string>();
      }
      if (param_json.contains("required")) {
        param.required = param_json.at("required").get<bool>();
      }
      params.push_back(param);
    }
    tool.parameters = params;
  }
}

// Error
void to_json(json& j, const Error& err) {
  j = json{{"code", err.code}, {"message", err.message}};
  if (err.data.has_value()) {
    // Handle variant data - need to handle each type explicitly
    match(err.data.value(),
      [&j](std::nullptr_t) { j["data"] = nullptr; },
      [&j](bool val) { j["data"] = val; },
      [&j](int val) { j["data"] = val; },
      [&j](double val) { j["data"] = val; },
      [&j](const std::string& val) { j["data"] = val; },
      [&j](const std::vector<variant<std::nullptr_t, bool, int, double, std::string>>& vec) {
        json arr = json::array();
        for (const auto& item : vec) {
          match(item,
            [&arr](std::nullptr_t) { arr.push_back(nullptr); },
            [&arr](bool v) { arr.push_back(v); },
            [&arr](int v) { arr.push_back(v); },
            [&arr](double v) { arr.push_back(v); },
            [&arr](const std::string& v) { arr.push_back(v); }
          );
        }
        j["data"] = arr;
      },
      [&j](const std::map<std::string, variant<std::nullptr_t, bool, int, double, std::string>>& map) {
        json obj = json::object();
        for (const auto& kv : map) {
          const std::string& key = kv.first;
          const auto& value = kv.second;
          match(value,
            [&obj, &key](std::nullptr_t) { obj[key] = nullptr; },
            [&obj, &key](bool v) { obj[key] = v; },
            [&obj, &key](int v) { obj[key] = v; },
            [&obj, &key](double v) { obj[key] = v; },
            [&obj, &key](const std::string& v) { obj[key] = v; }
          );
        }
        j["data"] = obj;
      }
    );
  }
}

void from_json(const json& j, Error& err) {
  err.code = j.at("code").get<int>();
  err.message = j.at("message").get<std::string>();
  if (j.contains("data")) {
    const auto& data = j.at("data");
    if (data.is_null()) {
      err.data = nullptr;
    } else if (data.is_boolean()) {
      err.data = data.get<bool>();
    } else if (data.is_number_integer()) {
      err.data = data.get<int>();
    } else if (data.is_number_float()) {
      err.data = data.get<double>();
    } else if (data.is_string()) {
      err.data = data.get<std::string>();
    }
    // TODO: Handle vector and map cases
  }
}

// ServerCapabilities
void to_json(json& j, const ServerCapabilities& caps) {
  j = json::object();
  if (caps.experimental.has_value()) {
    j["experimental"] = caps.experimental.value();
  }
  if (caps.resources.has_value()) {
    match(caps.resources.value(),
      [&j](bool val) { j["resources"] = val; },
      [&j](const ResourcesCapability& res_caps) { 
        json res_json = json::object();
        if (res_caps.subscribe.has_value()) {
          res_json["subscribe"] = res_caps.subscribe.value();
        }
        if (res_caps.listChanged.has_value()) {
          res_json["listChanged"] = res_caps.listChanged.value();
        }
        j["resources"] = res_json;
      }
    );
  }
  if (caps.tools.has_value()) {
    j["tools"] = caps.tools.value();
  }
  if (caps.prompts.has_value()) {
    j["prompts"] = caps.prompts.value();
  }
  if (caps.logging.has_value()) {
    j["logging"] = caps.logging.value();
  }
}

void from_json(const json& j, ServerCapabilities& caps) {
  if (j.contains("experimental")) {
    caps.experimental = j.at("experimental").get<Metadata>();
  }
  if (j.contains("resources")) {
    const auto& res = j.at("resources");
    if (res.is_boolean()) {
      caps.resources = res.get<bool>();
    } else if (res.is_object()) {
      ResourcesCapability res_caps;
      if (res.contains("subscribe")) {
        res_caps.subscribe = res.at("subscribe").get<EmptyCapability>();
      }
      if (res.contains("listChanged")) {
        res_caps.listChanged = res.at("listChanged").get<EmptyCapability>();
      }
      caps.resources = res_caps;
    }
  }
  if (j.contains("tools")) {
    caps.tools = j.at("tools").get<bool>();
  }
  if (j.contains("prompts")) {
    caps.prompts = j.at("prompts").get<bool>();
  }
  if (j.contains("logging")) {
    caps.logging = j.at("logging").get<bool>();
  }
}

// ClientCapabilities
void to_json(json& j, const ClientCapabilities& caps) {
  j = json::object();
  if (caps.experimental.has_value()) {
    j["experimental"] = caps.experimental.value();
  }
  if (caps.sampling.has_value()) {
    // Serialize SamplingParams
    const auto& params = caps.sampling.value();
    json sampling = json::object();
    if (params.temperature.has_value()) {
      sampling["temperature"] = params.temperature.value();
    }
    if (params.maxTokens.has_value()) {
      sampling["maxTokens"] = params.maxTokens.value();
    }
    if (params.stopSequences.has_value()) {
      sampling["stopSequences"] = params.stopSequences.value();
    }
    if (params.metadata.has_value()) {
      sampling["metadata"] = params.metadata.value();
    }
    j["sampling"] = sampling;
  }
  if (caps.roots.has_value()) {
    json roots_json = json::object();
    if (caps.roots.value().listChanged.has_value()) {
      roots_json["listChanged"] = caps.roots.value().listChanged.value();
    }
    j["roots"] = roots_json;
  }
}

void from_json(const json& j, ClientCapabilities& caps) {
  if (j.contains("experimental")) {
    caps.experimental = j.at("experimental").get<Metadata>();
  }
  if (j.contains("sampling")) {
    SamplingParams params;
    const auto& sampling = j.at("sampling");
    if (sampling.contains("temperature")) {
      params.temperature = sampling.at("temperature").get<double>();
    }
    if (sampling.contains("maxTokens")) {
      params.maxTokens = sampling.at("maxTokens").get<int>();
    }
    if (sampling.contains("stopSequences")) {
      params.stopSequences = sampling.at("stopSequences").get<std::vector<std::string>>();
    }
    if (sampling.contains("metadata")) {
      params.metadata = sampling.at("metadata").get<Metadata>();
    }
    caps.sampling = params;
  }
  if (j.contains("roots")) {
    RootsCapability roots;
    const auto& roots_json = j.at("roots");
    if (roots_json.contains("listChanged")) {
      roots.listChanged = roots_json.at("listChanged").get<EmptyCapability>();
    }
    caps.roots = roots;
  }
}

// ContentBlock (variant)
void to_json(json& j, const ContentBlock& block) {
  match(block,
    [&j](const TextContent& content) { to_json(j, content); },
    [&j](const ImageContent& content) { to_json(j, content); },
    [&j](const ResourceContent& content) { to_json(j, content); }
  );
}

void from_json(const json& j, ContentBlock& block) {
  if (!j.contains("type")) {
    throw std::runtime_error("ContentBlock missing 'type' field");
  }
  
  const std::string type = j.at("type").get<std::string>();
  
  if (type == "text") {
    TextContent content;
    from_json(j, content);
    block = content;
  } else if (type == "image") {
    ImageContent content;
    from_json(j, content);
    block = content;
  } else if (type == "resource") {
    ResourceContent content;
    from_json(j, content);
    block = content;
  } else {
    throw std::runtime_error("Unknown ContentBlock type: " + type);
  }
}

// ExtendedContentBlock (variant)
void to_json(json& j, const ExtendedContentBlock& block) {
  match(block,
    [&j](const TextContent& content) { to_json(j, content); },
    [&j](const ImageContent& content) { to_json(j, content); },
    [&j](const AudioContent& content) { to_json(j, content); },
    [&j](const ResourceLink& link) { to_json(j, link); },
    [&j](const EmbeddedResource& resource) { to_json(j, resource); }
  );
}

void from_json(const json& j, ExtendedContentBlock& block) {
  if (!j.contains("type")) {
    throw std::runtime_error("ExtendedContentBlock missing 'type' field");
  }
  
  const std::string type = j.at("type").get<std::string>();
  
  if (type == "text") {
    TextContent content;
    from_json(j, content);
    block = content;
  } else if (type == "image") {
    ImageContent content;
    from_json(j, content);
    block = content;
  } else if (type == "audio") {
    AudioContent content;
    from_json(j, content);
    block = content;
  } else if (type == "resource") {
    ResourceLink link;
    from_json(j, link);
    block = link;
  } else if (type == "embedded") {
    EmbeddedResource resource;
    from_json(j, resource);
    block = resource;
  } else {
    throw std::runtime_error("Unknown ExtendedContentBlock type: " + type);
  }
}

// Implementation
void to_json(json& j, const Implementation& impl) {
  j = json{{"name", impl.name}, {"version", impl.version}};
  if (impl._meta.has_value()) {
    j["_meta"] = impl._meta.value();
  }
}

void from_json(const json& j, Implementation& impl) {
  impl.name = j.at("name").get<std::string>();
  impl.version = j.at("version").get<std::string>();
  if (j.contains("_meta")) {
    impl._meta = j.at("_meta").get<Metadata>();
  }
}

// InitializeRequest
void to_json(json& j, const InitializeRequest& req) {
  j = json{
    {"jsonrpc", req.jsonrpc},
    {"method", req.method},
    {"id", nullptr}  // Will be overridden
  };
  
  // Handle RequestId variant
  match(req.id,
    [&j](const std::string& id) { j["id"] = id; },
    [&j](int id) { j["id"] = id; }
  );
  
  json params = {
    {"protocolVersion", req.protocolVersion}
  };
  to_json(params["capabilities"], req.capabilities);
  if (req.clientInfo.has_value()) {
    to_json(params["clientInfo"], req.clientInfo.value());
  }
  j["params"] = params;
}

void from_json(const json& j, InitializeRequest& req) {
  req.jsonrpc = j.at("jsonrpc").get<std::string>();
  req.method = j.at("method").get<std::string>();
  
  // Handle RequestId
  const auto& id = j.at("id");
  if (id.is_string()) {
    req.id = id.get<std::string>();
  } else if (id.is_number_integer()) {
    req.id = id.get<int>();
  }
  
  if (j.contains("params")) {
    const auto& params = j.at("params");
    req.protocolVersion = params.at("protocolVersion").get<std::string>();
    from_json(params.at("capabilities"), req.capabilities);
    if (params.contains("clientInfo")) {
      Implementation info;
      from_json(params.at("clientInfo"), info);
      req.clientInfo = info;
    }
  }
}

// InitializeResult
void to_json(json& j, const InitializeResult& result) {
  j = json{
    {"protocolVersion", result.protocolVersion}
  };
  to_json(j["capabilities"], result.capabilities);
  if (result.serverInfo.has_value()) {
    to_json(j["serverInfo"], result.serverInfo.value());
  }
  if (result.instructions.has_value()) {
    j["instructions"] = result.instructions.value();
  }
}

void from_json(const json& j, InitializeResult& result) {
  result.protocolVersion = j.at("protocolVersion").get<std::string>();
  from_json(j.at("capabilities"), result.capabilities);
  if (j.contains("serverInfo")) {
    Implementation info;
    from_json(j.at("serverInfo"), info);
    result.serverInfo = info;
  }
  if (j.contains("instructions")) {
    result.instructions = j.at("instructions").get<std::string>();
  }
}

// CallToolResult
void to_json(json& j, const CallToolResult& result) {
  j = json::object();
  j["content"] = json::array();
  for (const auto& block : result.content) {
    json content_json;
    to_json(content_json, block);
    j["content"].push_back(content_json);
  }
  if (result.isError) {
    j["isError"] = result.isError;
  }
}

void from_json(const json& j, CallToolResult& result) {
  if (j.contains("content")) {
    for (const auto& content_json : j.at("content")) {
      ExtendedContentBlock block;
      from_json(content_json, block);
      result.content.push_back(block);
    }
  }
  if (j.contains("isError")) {
    result.isError = j.at("isError").get<bool>();
  }
}

// PromptMessage
void to_json(json& j, const PromptMessage& msg) {
  j = json{{"role", nullptr}};
  to_json(j["role"], msg.role);
  
  match(msg.content,
    [&j](const TextContent& content) { to_json(j["content"], content); },
    [&j](const ImageContent& content) { to_json(j["content"], content); },
    [&j](const EmbeddedResource& resource) { to_json(j["content"], resource); }
  );
}

void from_json(const json& j, PromptMessage& msg) {
  from_json(j.at("role"), msg.role);
  
  if (j.contains("content")) {
    const auto& content = j.at("content");
    if (!content.contains("type")) {
      throw std::runtime_error("PromptMessage content missing 'type' field");
    }
    
    const std::string type = content.at("type").get<std::string>();
    if (type == "text") {
      TextContent text;
      from_json(content, text);
      msg.content = text;
    } else if (type == "image") {
      ImageContent image;
      from_json(content, image);
      msg.content = image;
    } else if (type == "embedded") {
      EmbeddedResource resource;
      from_json(content, resource);
      msg.content = resource;
    }
  }
}

// Prompt
void to_json(json& j, const Prompt& prompt) {
  j = json{{"name", prompt.name}};
  if (prompt.description.has_value()) {
    j["description"] = prompt.description.value();
  }
  if (prompt.arguments.has_value()) {
    j["arguments"] = json::array();
    for (const auto& arg : prompt.arguments.value()) {
      json arg_json = {{"name", arg.name}};
      if (arg.description.has_value()) {
        arg_json["description"] = arg.description.value();
      }
      arg_json["required"] = arg.required;
      j["arguments"].push_back(arg_json);
    }
  }
}

void from_json(const json& j, Prompt& prompt) {
  prompt.name = j.at("name").get<std::string>();
  if (j.contains("description")) {
    prompt.description = j.at("description").get<std::string>();
  }
  if (j.contains("arguments")) {
    std::vector<PromptArgument> args;
    for (const auto& arg_json : j.at("arguments")) {
      PromptArgument arg;
      arg.name = arg_json.at("name").get<std::string>();
      if (arg_json.contains("description")) {
        arg.description = arg_json.at("description").get<std::string>();
      }
      if (arg_json.contains("required")) {
        arg.required = arg_json.at("required").get<bool>();
      }
      args.push_back(arg);
    }
    prompt.arguments = args;
  }
}

// ResourceTemplate
void to_json(json& j, const ResourceTemplate& tmpl) {
  j = json{
    {"uriTemplate", tmpl.uriTemplate},
    {"name", tmpl.name}
  };
  if (tmpl.description.has_value()) {
    j["description"] = tmpl.description.value();
  }
  if (tmpl.mimeType.has_value()) {
    j["mimeType"] = tmpl.mimeType.value();
  }
  if (tmpl._meta.has_value()) {
    j["_meta"] = tmpl._meta.value();
  }
}

void from_json(const json& j, ResourceTemplate& tmpl) {
  tmpl.uriTemplate = j.at("uriTemplate").get<std::string>();
  tmpl.name = j.at("name").get<std::string>();
  if (j.contains("description")) {
    tmpl.description = j.at("description").get<std::string>();
  }
  if (j.contains("mimeType")) {
    tmpl.mimeType = j.at("mimeType").get<std::string>();
  }
  if (j.contains("_meta")) {
    tmpl._meta = j.at("_meta").get<Metadata>();
  }
}

// Root
void to_json(json& j, const Root& root) {
  j = json{{"uri", root.uri}};
  if (root.name.has_value()) {
    j["name"] = root.name.value();
  }
}

void from_json(const json& j, Root& root) {
  root.uri = j.at("uri").get<std::string>();
  if (j.contains("name")) {
    root.name = j.at("name").get<std::string>();
  }
}

// SamplingMessage
void to_json(json& j, const SamplingMessage& msg) {
  j = json{{"role", nullptr}};
  to_json(j["role"], msg.role);
  
  match(msg.content,
    [&j](const TextContent& content) { to_json(j["content"], content); },
    [&j](const ImageContent& content) { to_json(j["content"], content); },
    [&j](const AudioContent& content) { to_json(j["content"], content); }
  );
}

void from_json(const json& j, SamplingMessage& msg) {
  from_json(j.at("role"), msg.role);
  
  if (j.contains("content")) {
    const auto& content = j.at("content");
    if (!content.contains("type")) {
      throw std::runtime_error("SamplingMessage content missing 'type' field");
    }
    
    const std::string type = content.at("type").get<std::string>();
    if (type == "text") {
      TextContent text;
      from_json(content, text);
      msg.content = text;
    } else if (type == "image") {
      ImageContent image;
      from_json(content, image);
      msg.content = image;
    } else if (type == "audio") {
      AudioContent audio;
      from_json(content, audio);
      msg.content = audio;
    }
  }
}

// Metadata - map<string, MetadataValue> where MetadataValue is a variant
void to_json(json& j, const Metadata& metadata) {
  j = json::object();
  for (const auto& kv : metadata) {
    const std::string& key = kv.first;
    const MetadataValue& value = kv.second;
    match(value,
      [&j, &key](std::nullptr_t) { j[key] = nullptr; },
      [&j, &key](const std::string& v) { j[key] = v; },
      [&j, &key](int64_t v) { j[key] = v; },
      [&j, &key](double v) { j[key] = v; },
      [&j, &key](bool v) { j[key] = v; }
    );
  }
}

void from_json(const json& j, Metadata& metadata) {
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& key = it.key();
    const auto& value = it.value();
    
    if (value.is_null()) {
      metadata[key] = nullptr;
    } else if (value.is_string()) {
      metadata[key] = value.get<std::string>();
    } else if (value.is_number_integer()) {
      metadata[key] = value.get<int64_t>();
    } else if (value.is_number_float()) {
      metadata[key] = value.get<double>();
    } else if (value.is_boolean()) {
      metadata[key] = value.get<bool>();
    }
  }
}

// ModelPreferences
void to_json(json& j, const ModelPreferences& prefs) {
  j = json::object();
  if (prefs.hints.has_value()) {
    j["hints"] = json::array();
    for (const auto& hint : prefs.hints.value()) {
      json hint_json = json::object();
      if (hint.name.has_value()) {
        hint_json["name"] = hint.name.value();
      }
      j["hints"].push_back(hint_json);
    }
  }
  if (prefs.costPriority.has_value()) {
    j["costPriority"] = prefs.costPriority.value();
  }
  if (prefs.speedPriority.has_value()) {
    j["speedPriority"] = prefs.speedPriority.value();
  }
  if (prefs.intelligencePriority.has_value()) {
    j["intelligencePriority"] = prefs.intelligencePriority.value();
  }
}

void from_json(const json& j, ModelPreferences& prefs) {
  if (j.contains("hints")) {
    std::vector<ModelHint> hints;
    for (const auto& hint_json : j.at("hints")) {
      ModelHint hint;
      if (hint_json.contains("name")) {
        hint.name = hint_json.at("name").get<std::string>();
      }
      hints.push_back(hint);
    }
    prefs.hints = hints;
  }
  if (j.contains("costPriority")) {
    prefs.costPriority = j.at("costPriority").get<double>();
  }
  if (j.contains("speedPriority")) {
    prefs.speedPriority = j.at("speedPriority").get<double>();
  }
  if (j.contains("intelligencePriority")) {
    prefs.intelligencePriority = j.at("intelligencePriority").get<double>();
  }
}

// CreateMessageResult
void to_json(json& j, const CreateMessageResult& result) {
  // Serialize role and content from base class
  to_json(j["role"], result.role);
  match(result.content,
    [&j](const TextContent& content) { to_json(j["content"], content); },
    [&j](const ImageContent& content) { to_json(j["content"], content); },
    [&j](const AudioContent& content) { to_json(j["content"], content); }
  );
  
  // Add additional fields
  j["model"] = result.model;
  if (result.stopReason.has_value()) {
    j["stopReason"] = result.stopReason.value();
  }
}

void from_json(const json& j, CreateMessageResult& result) {
  // Deserialize base class fields
  from_json(j.at("role"), result.role);
  
  if (j.contains("content")) {
    const auto& content = j.at("content");
    if (!content.contains("type")) {
      throw std::runtime_error("CreateMessageResult content missing 'type' field");
    }
    
    const std::string type = content.at("type").get<std::string>();
    if (type == "text") {
      TextContent text;
      from_json(content, text);
      result.content = text;
    } else if (type == "image") {
      ImageContent image;
      from_json(content, image);
      result.content = image;
    } else if (type == "audio") {
      AudioContent audio;
      from_json(content, audio);
      result.content = audio;
    }
  }
  
  result.model = j.at("model").get<std::string>();
  if (j.contains("stopReason")) {
    result.stopReason = j.at("stopReason").get<std::string>();
  }
}

}  // namespace mcp