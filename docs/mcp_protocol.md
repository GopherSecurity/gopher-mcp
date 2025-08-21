# MCP Protocol Documentation

The Model Context Protocol (MCP) is a JSON-RPC based protocol for communication between AI models and external tools/resources. This document covers the C++ SDK's implementation of the MCP protocol.

## Protocol Overview

```
┌─────────────────────────────────────────────────────────┐
│                    MCP Protocol                         │
├─────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────-┐   │
│  │              JSON-RPC 2.0 Layer                  │   │
│  │  • Request/Response correlation                  │   │
│  │  • Notification handling                         │   │
│  │  • Error responses                               │   │
│  └─────────────────────────────────────────────────-┘   │
├─────────────────────────────────────────────────────────┤
│  ┌────────────────────────────────────────────────-─┐   │
│  │            MCP Message Types                     │   │
│  │  • Initialize/InitializeResult                   │   │
│  │  • Resources/Tools/Prompts                       │   │
│  │  • Progress notifications                        │   │
│  └─────────────────────────────────────────────────-┘   │
├─────────────────────────────────────────────────────────┤
│  ┌────────────────────────────────────────────────-─┐   │
│  │          Capability Negotiation                  │   │
│  │  • Client/Server capabilities                    │   │
│  │  • Feature discovery                             │   │
│  │  • Version compatibility                         │   │
│  └─────────────────────────────────────────────────-┘   │
└─────────────────────────────────────────────────────────┘
```

## JSON-RPC Implementation

### Message Types

The MCP protocol uses JSON-RPC 2.0 with three message types:

#### Request
```cpp
struct Request {
    variant<string, int> id;      // Request identifier
    string method;                 // Method name
    optional<Metadata> params;     // Method parameters
};

// Example JSON
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "resources/read",
    "params": {
        "uri": "file:///example.txt"
    }
}
```

#### Response
```cpp
struct Response {
    variant<string, int> id;       // Matching request ID
    optional<Result> result;       // Success result
    optional<Error> error;         // Error information
};

// Success response
{
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
        "contents": [...]
    }
}

// Error response
{
    "jsonrpc": "2.0",
    "id": 1,
    "error": {
        "code": -32601,
        "message": "Method not found"
    }
}
```

#### Notification
```cpp
struct Notification {
    string method;                 // Method name
    optional<Metadata> params;     // Method parameters
    // No id field - notifications don't expect responses
};

// Example JSON
{
    "jsonrpc": "2.0",
    "method": "notifications/progress",
    "params": {
        "token": "abc123",
        "progress": 0.5
    }
}
```

## MCP Type System

### Core Types (`types.h`)

#### Metadata
Flexible key-value store for parameters and additional data:
```cpp
using Metadata = std::map<std::string, MetadataValue>;
using MetadataValue = variant<
    bool,
    int64_t,
    double,
    string,
    vector<MetadataValue>,
    map<string, MetadataValue>
>;
```

#### Resource Types
```cpp
struct Resource {
    string uri;                    // Resource identifier
    string name;                   // Human-readable name
    optional<string> description;  // Description
    optional<string> mimeType;     // MIME type
};

struct ResourceTemplate {
    string uriTemplate;            // URI template with variables
    string name;
    optional<string> description;
    optional<string> mimeType;
};

// Resource contents
struct TextResourceContents {
    string uri;
    string text;                   // Text content
    optional<string> mimeType;
};

struct BlobResourceContents {
    string uri;
    string blob;                   // Base64 encoded binary
    optional<string> mimeType;
};
```

#### Tool Types
```cpp
struct Tool {
    string name;                   // Tool identifier
    optional<string> description;
    optional<JsonSchema> inputSchema;  // JSON Schema for arguments
};

struct CallToolResult {
    vector<ExtendedContentBlock> content;  // Tool output
    bool isError = false;          // Error indicator
};
```

#### Prompt Types
```cpp
struct Prompt {
    string name;                   // Prompt identifier
    optional<string> description;
    vector<PromptArgument> arguments;  // Required arguments
};

struct GetPromptResult {
    optional<string> description;
    vector<Message> messages;      // Prompt messages
};
```

## Protocol Flow

### Initialization Sequence

```
Client                              Server
  │                                   │
  ├──── initialize ──────────────────>│
  │     • protocolVersion             │
  │     • capabilities                │
  │     • clientInfo                  │
  │                                   │
  │<─── InitializeResult ─────────────┤
  │     • protocolVersion             │
  │     • capabilities                │
  │     • serverInfo                  │
  │                                   │
  ├──── initialized ─────────────────>│
  │     (notification)                │
  │                                   │
```

**Implementation:**
```cpp
// Client sends initialize request
InitializeRequest request;
request.protocolVersion = "2024-11-05";
request.capabilities.sampling = {
    .supported = true
};
request.clientInfo = {
    .name = "mcp-cpp-client",
    .version = "1.0.0"
};

auto response = client->sendRequest("initialize", request);

// Server handles initialize
jsonrpc::Response handleInitialize(const jsonrpc::Request& request) {
    InitializeResult result;
    result.protocolVersion = "2024-11-05";
    result.capabilities = server_capabilities_;
    result.serverInfo = {
        .name = "mcp-cpp-server",
        .version = "1.0.0"
    };
    
    return jsonrpc::Response(request.id, result);
}
```

### Resource Operations

#### List Resources
```cpp
// Request
{
    "method": "resources/list",
    "params": {
        "cursor": "page2"  // Optional pagination
    }
}

// Response
{
    "result": {
        "resources": [
            {
                "uri": "file:///data.txt",
                "name": "Data File",
                "mimeType": "text/plain"
            }
        ],
        "nextCursor": "page3"  // Optional
    }
}
```

#### Read Resource
```cpp
// Request
{
    "method": "resources/read",
    "params": {
        "uri": "file:///data.txt"
    }
}

// Response
{
    "result": {
        "contents": [
            {
                "uri": "file:///data.txt",
                "text": "File contents...",
                "mimeType": "text/plain"
            }
        ]
    }
}
```

### Tool Operations

#### List Tools
```cpp
ListToolsResult McpServer::handleListTools() {
    ListToolsResult result;
    
    for (const auto& tool : registered_tools_) {
        result.tools.push_back(tool);
    }
    
    return result;
}
```

#### Call Tool
```cpp
CallToolResult McpServer::handleCallTool(
    const string& name,
    const optional<Metadata>& arguments) {
    
    auto handler = tool_handlers_.find(name);
    if (handler != tool_handlers_.end()) {
        return handler->second(name, arguments);
    }
    
    // Tool not found
    CallToolResult error;
    error.isError = true;
    error.content.push_back(
        TextContent("Tool not found: " + name)
    );
    return error;
}
```

### Progress Tracking

```cpp
// Server sends progress notification
void sendProgress(const ProgressToken& token, double progress) {
    ProgressNotification notification;
    notification.progressToken = token;
    notification.progress = progress;
    notification.total = 1.0;
    
    sendNotification("notifications/progress", notification);
}

// Client handles progress
void handleProgressNotification(const ProgressNotification& notif) {
    auto callback = progress_callbacks_.find(notif.progressToken);
    if (callback != progress_callbacks_.end()) {
        callback->second(notif.progress / notif.total);
    }
}
```

## Builders Pattern

The SDK uses a fluent builder pattern for constructing MCP types:

### Request Builder
```cpp
auto request = make<jsonrpc::Request>(generateId())
    .method("resources/read")
    .params(Metadata{
        {"uri", "file:///example.txt"}
    })
    .build();
```

### Response Builder
```cpp
// Success response
auto response = make<jsonrpc::Response>(request.id)
    .result(ReadResourceResult{...})
    .build();

// Error response
auto response = make<jsonrpc::Response>(request.id)
    .error(make<Error>(-32601, "Method not found")
        .data({{"method", method_name}})
        .build())
    .build();
```

### Tool Builder
```cpp
auto tool = make<Tool>("calculator")
    .description("Performs calculations")
    .inputSchema(JsonSchema{
        {"type", "object"},
        {"properties", {
            {"expression", {
                {"type", "string"},
                {"description", "Math expression"}
            }}
        }},
        {"required", {"expression"}}
    })
    .build();
```

## JSON Serialization

### Serialization Layer (`json/json_serialization.h`)

The SDK provides automatic JSON serialization for all MCP types:

```cpp
// Serialize to JSON
jsonrpc::Request request = ...;
std::string json = json::serialize(request);

// Deserialize from JSON
auto result = json::deserialize<jsonrpc::Request>(json);
if (result.isError()) {
    // Handle parse error
    auto error = result.error();
}
auto request = result.value();
```

### Custom Type Serialization

```cpp
// Define serialization for custom type
template<>
struct JsonSerializer<MyCustomType> {
    static JsonValue toJson(const MyCustomType& value) {
        JsonObject obj;
        obj["field1"] = value.field1;
        obj["field2"] = value.field2;
        return obj;
    }
    
    static Result<MyCustomType> fromJson(const JsonValue& json) {
        if (!json.isObject()) {
            return Error("Expected object");
        }
        
        MyCustomType result;
        result.field1 = json["field1"].asString();
        result.field2 = json["field2"].asInt();
        return result;
    }
};
```

## Error Handling

### Standard JSON-RPC Error Codes
```cpp
enum ErrorCode {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603
};
```

### MCP-Specific Error Codes
```cpp
enum McpErrorCode {
    ResourceNotFound = -32001,
    ToolExecutionFailed = -32002,
    Unauthorized = -32003,
    RateLimitExceeded = -32004
};
```

### Error Response Creation
```cpp
jsonrpc::Response createErrorResponse(
    const RequestId& id,
    int code,
    const string& message,
    const optional<Metadata>& data = nullopt) {
    
    return make<jsonrpc::Response>(id)
        .error(make<Error>(code, message)
            .data(data)
            .build())
        .build();
}
```

## Capability Negotiation

### Client Capabilities
```cpp
struct ClientCapabilities {
    optional<RootsCapability> roots;
    optional<SamplingCapability> sampling;
    optional<ExperimentalCapabilities> experimental;
};

// Example usage
ClientCapabilities caps;
caps.sampling = {
    .supported = true
};
caps.experimental = {
    {"streaming", true},
    {"batching", true}
};
```

### Server Capabilities
```cpp
struct ServerCapabilities {
    optional<LoggingCapability> logging;
    optional<PromptsCapability> prompts;
    optional<ResourcesCapability> resources;
    optional<ToolsCapability> tools;
};

// Configure server capabilities
ServerCapabilities caps;
caps.resources = {
    .subscribe = true,     // Support subscriptions
    .listChanged = true    // Support change notifications
};
caps.tools = {
    .listChanged = true    // Support tool list changes
};
```

## Session Management

### Session Context
```cpp
class SessionContext {
    string session_id_;
    optional<Implementation> client_info_;
    set<string> resource_subscriptions_;
    
public:
    // Track client capabilities after initialization
    void setClientInfo(const Implementation& info) {
        client_info_ = info;
    }
    
    // Manage resource subscriptions
    void subscribe(const string& uri) {
        resource_subscriptions_.insert(uri);
    }
    
    void unsubscribe(const string& uri) {
        resource_subscriptions_.erase(uri);
    }
    
    bool isSubscribed(const string& uri) const {
        return resource_subscriptions_.count(uri) > 0;
    }
};
```

## Protocol Extensions

### Custom Methods

Register custom methods while maintaining protocol compatibility:

```cpp
class ExtendedMcpServer : public McpServer {
    void registerCustomMethods() {
        // Standard MCP method
        registerRequestHandler("resources/read", 
            [this](auto& req, auto& session) {
                return handleReadResource(req, session);
            });
        
        // Custom extension method
        registerRequestHandler("x-custom/analyze",
            [this](auto& req, auto& session) {
                return handleCustomAnalyze(req, session);
            });
    }
};
```

### Protocol Version Compatibility

```cpp
bool isCompatibleVersion(const string& client_version) {
    // Parse semantic version
    auto client_parts = parseVersion(client_version);
    auto server_parts = parseVersion(PROTOCOL_VERSION);
    
    // Major version must match
    if (client_parts.major != server_parts.major) {
        return false;
    }
    
    // Minor version compatibility
    if (client_parts.minor > server_parts.minor) {
        // Client newer than server - may not be compatible
        return false;
    }
    
    return true;
}
```

## Best Practices

### 1. Request ID Generation
```cpp
RequestId generateRequestId() {
    static atomic<int> counter{1};
    return counter.fetch_add(1);
}
```

### 2. Timeout Management
```cpp
future<Response> sendRequestWithTimeout(
    const Request& request,
    chrono::milliseconds timeout) {
    
    auto future = sendRequest(request);
    
    if (future.wait_for(timeout) == future_status::timeout) {
        // Cancel request
        cancelRequest(request.id);
        throw TimeoutException("Request timed out");
    }
    
    return future;
}
```

### 3. Batch Processing
```cpp
vector<Response> processBatch(const vector<Request>& requests) {
    vector<future<Response>> futures;
    
    // Send all requests concurrently
    for (const auto& request : requests) {
        futures.push_back(sendRequestAsync(request));
    }
    
    // Collect responses
    vector<Response> responses;
    for (auto& future : futures) {
        responses.push_back(future.get());
    }
    
    return responses;
}
```

### 4. Resource Caching
```cpp
class CachedResourceManager {
    struct CacheEntry {
        ReadResourceResult result;
        chrono::steady_clock::time_point timestamp;
    };
    
    map<string, CacheEntry> cache_;
    chrono::seconds cache_ttl_{60};
    
    ReadResourceResult readResource(const string& uri) {
        auto now = chrono::steady_clock::now();
        
        // Check cache
        auto it = cache_.find(uri);
        if (it != cache_.end()) {
            if (now - it->second.timestamp < cache_ttl_) {
                return it->second.result;
            }
        }
        
        // Fetch and cache
        auto result = fetchResource(uri);
        cache_[uri] = {result, now};
        return result;
    }
};
```

## Testing

### Protocol Compliance Testing
```cpp
TEST(McpProtocol, InitializeSequence) {
    MockTransport transport;
    McpClient client(transport);
    
    // Expect initialize request
    transport.expectWrite(R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "test-client",
                "version": "1.0.0"
            }
        }
    })");
    
    // Inject response
    transport.injectRead(R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "protocolVersion": "2024-11-05",
            "capabilities": {}
        }
    })");
    
    auto result = client.initialize();
    EXPECT_EQ(result.protocolVersion, "2024-11-05");
}
```