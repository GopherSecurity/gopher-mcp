# MCP DSL Implementation Examples

This document demonstrates practical implementations of the MCP Declarative API Definition (DSL) across multiple programming languages, showing how the same MCP server can be defined using language-specific idioms while maintaining consistency.

## 1. Python Implementation (FastMCP-style)

```python
from mcp import Server, Tool, Resource, Prompt
from typing import Optional, Dict, Any
import asyncio

# Create server with declarative configuration
server = Server(
    name="knowledge-base",
    version="1.0.0",
    capabilities={
        "tools": True,
        "resources": True, 
        "prompts": True,
        "sampling": True
    }
)

# Tool definition with decorators
@server.tool(
    description="Search the knowledge base",
    schema={
        "query": {"type": "string", "required": True},
        "limit": {"type": "integer", "default": 10},
        "filters": {"type": "object"}
    }
)
async def search_knowledge(query: str, limit: int = 10, filters: Optional[Dict] = None) -> Dict[str, Any]:
    """Search implementation"""
    results = await database.search(query, limit, filters)
    return {"results": results, "count": len(results)}

# Resource definition with patterns
@server.resource(
    uri_pattern="knowledge://{topic}/{subtopic}",
    description="Access knowledge articles",
    mime_type="text/markdown"
)
async def get_knowledge_article(topic: str, subtopic: str) -> Resource:
    """Fetch knowledge article"""
    content = await database.get_article(topic, subtopic)
    return Resource(
        uri=f"knowledge://{topic}/{subtopic}",
        name=f"{topic} - {subtopic}",
        content=content
    )

# Prompt template with variables
@server.prompt(
    name="analyze_topic",
    description="Analyze a knowledge topic",
    arguments=["topic", "depth"]
)
def analyze_topic_prompt(topic: str, depth: str = "medium") -> str:
    return f"""
    Analyze the topic '{topic}' with {depth} depth.
    Consider:
    - Historical context
    - Current relevance
    - Future implications
    - Related topics
    """

# Middleware for authentication
@server.middleware
async def authenticate(request, next):
    token = request.headers.get("Authorization")
    if not validate_token(token):
        raise AuthenticationError("Invalid token")
    return await next(request)

# Run the server
if __name__ == "__main__":
    server.run(transport="stdio")
```

## 2. TypeScript Implementation (Decorator-based)

```typescript
import { McpServer, Tool, Resource, Prompt, Middleware } from '@mcp/sdk';

// Server configuration using decorators
@McpServer({
  name: 'knowledge-base',
  version: '1.0.0',
  capabilities: {
    tools: true,
    resources: true,
    prompts: true,
    sampling: true
  }
})
class KnowledgeBaseServer {
  
  // Tool with schema validation
  @Tool({
    description: 'Search the knowledge base',
    schema: {
      type: 'object',
      properties: {
        query: { type: 'string' },
        limit: { type: 'number', default: 10 },
        filters: { type: 'object' }
      },
      required: ['query']
    }
  })
  async searchKnowledge(
    query: string,
    limit: number = 10,
    filters?: Record<string, any>
  ): Promise<{ results: any[], count: number }> {
    const results = await this.database.search(query, limit, filters);
    return { results, count: results.length };
  }

  // Resource with URI pattern
  @Resource({
    uriPattern: 'knowledge://{topic}/{subtopic}',
    description: 'Access knowledge articles',
    mimeType: 'text/markdown'
  })
  async getKnowledgeArticle(
    topic: string,
    subtopic: string
  ): Promise<ResourceContent> {
    const content = await this.database.getArticle(topic, subtopic);
    return {
      uri: `knowledge://${topic}/${subtopic}`,
      name: `${topic} - ${subtopic}`,
      content
    };
  }

  // Prompt template
  @Prompt({
    name: 'analyze_topic',
    description: 'Analyze a knowledge topic',
    arguments: ['topic', 'depth']
  })
  analyzeTopicPrompt(topic: string, depth: string = 'medium'): string {
    return `
      Analyze the topic '${topic}' with ${depth} depth.
      Consider:
      - Historical context
      - Current relevance
      - Future implications
      - Related topics
    `;
  }

  // Middleware for rate limiting
  @Middleware()
  async rateLimit(ctx: Context, next: Next): Promise<void> {
    const key = ctx.request.clientId;
    if (await this.rateLimiter.isExceeded(key)) {
      throw new RateLimitError('Too many requests');
    }
    await next();
  }
}

// Bootstrap server
const server = new KnowledgeBaseServer();
server.listen({ transport: 'http', port: 8080 });
```

## 3. C++ Implementation (Template-based)

```cpp
#include <mcp/server.h>
#include <mcp/declarative.h>

using namespace mcp;

// Define server with compile-time configuration
class KnowledgeBaseServer : public McpServer<
    WithTools,
    WithResources,
    WithPrompts,
    WithSampling
> {
public:
    KnowledgeBaseServer() : McpServer("knowledge-base", "1.0.0") {}

    // Tool definition using templates
    MCP_TOOL(SearchKnowledge,
        Description("Search the knowledge base"),
        Parameters<
            Required<String>("query"),
            Optional<Integer>("limit", 10),
            Optional<Object>("filters")
        >
    )
    auto searchKnowledge(
        const std::string& query,
        int limit,
        const optional<json>& filters
    ) -> Task<json> {
        auto results = co_await database_.search(query, limit, filters);
        co_return json{
            {"results", results},
            {"count", results.size()}
        };
    }

    // Resource with pattern matching
    MCP_RESOURCE(KnowledgeArticle,
        UriPattern("knowledge://{topic}/{subtopic}"),
        Description("Access knowledge articles"),
        MimeType("text/markdown")
    )
    auto getKnowledgeArticle(
        const std::string& topic,
        const std::string& subtopic
    ) -> Task<Resource> {
        auto content = co_await database_.getArticle(topic, subtopic);
        co_return Resource{
            .uri = fmt::format("knowledge://{}/{}", topic, subtopic),
            .name = fmt::format("{} - {}", topic, subtopic),
            .content = content
        };
    }

    // Prompt template with compile-time validation
    MCP_PROMPT(AnalyzeTopic,
        Name("analyze_topic"),
        Description("Analyze a knowledge topic"),
        Arguments<String("topic"), String("depth", "medium")>
    )
    std::string analyzeTopicPrompt(
        const std::string& topic,
        const std::string& depth
    ) {
        return fmt::format(R"(
            Analyze the topic '{}' with {} depth.
            Consider:
            - Historical context
            - Current relevance
            - Future implications
            - Related topics
        )", topic, depth);
    }

    // Middleware using CRTP
    template<typename Next>
    auto authenticationMiddleware(Request& req, Next&& next) -> Task<Response> {
        auto token = req.headers["Authorization"];
        if (!validateToken(token)) {
            throw AuthenticationError("Invalid token");
        }
        co_return co_await next(req);
    }

private:
    Database database_;
};

// Main entry point
int main() {
    KnowledgeBaseServer server;
    server.listen(TransportType::Stdio);
    return server.run();
}
```

## 4. Java Implementation (Annotation-based)

```java
import io.mcp.server.*;
import io.mcp.annotations.*;

@McpServer(
    name = "knowledge-base",
    version = "1.0.0",
    capabilities = {
        Capability.TOOLS,
        Capability.RESOURCES,
        Capability.PROMPTS,
        Capability.SAMPLING
    }
)
public class KnowledgeBaseServer extends McpServerBase {

    @Tool(description = "Search the knowledge base")
    public CompletableFuture<SearchResult> searchKnowledge(
        @Required @Param("query") String query,
        @Param(value = "limit", defaultValue = "10") int limit,
        @Optional @Param("filters") Map<String, Object> filters
    ) {
        return database.search(query, limit, filters)
            .thenApply(results -> new SearchResult(results, results.size()));
    }

    @Resource(
        uriPattern = "knowledge://{topic}/{subtopic}",
        description = "Access knowledge articles",
        mimeType = "text/markdown"
    )
    public CompletableFuture<ResourceContent> getKnowledgeArticle(
        @PathParam("topic") String topic,
        @PathParam("subtopic") String subtopic
    ) {
        return database.getArticle(topic, subtopic)
            .thenApply(content -> ResourceContent.builder()
                .uri(String.format("knowledge://%s/%s", topic, subtopic))
                .name(String.format("%s - %s", topic, subtopic))
                .content(content)
                .build());
    }

    @Prompt(
        name = "analyze_topic",
        description = "Analyze a knowledge topic"
    )
    public String analyzeTopicPrompt(
        @Arg("topic") String topic,
        @Arg(value = "depth", defaultValue = "medium") String depth
    ) {
        return String.format("""
            Analyze the topic '%s' with %s depth.
            Consider:
            - Historical context
            - Current relevance
            - Future implications
            - Related topics
            """, topic, depth);
    }

    @Middleware(priority = 100)
    public void authenticate(RequestContext ctx, MiddlewareChain chain) {
        String token = ctx.getHeader("Authorization");
        if (!validateToken(token)) {
            throw new AuthenticationException("Invalid token");
        }
        chain.proceed(ctx);
    }

    public static void main(String[] args) {
        KnowledgeBaseServer server = new KnowledgeBaseServer();
        server.listen(Transport.stdio());
    }
}
```

## 5. Unified YAML Schema

```yaml
# mcp-server.yaml - Universal configuration file
server:
  name: knowledge-base
  version: 1.0.0
  description: Knowledge base MCP server
  capabilities:
    - tools
    - resources
    - prompts
    - sampling

tools:
  - name: search_knowledge
    description: Search the knowledge base
    parameters:
      query:
        type: string
        required: true
        description: Search query
      limit:
        type: integer
        default: 10
        description: Maximum results
      filters:
        type: object
        required: false
        description: Search filters
    handler: handlers.searchKnowledge
    middleware:
      - authenticate
      - rateLimit

resources:
  - pattern: "knowledge://{topic}/{subtopic}"
    description: Access knowledge articles
    mimeType: text/markdown
    handler: handlers.getKnowledgeArticle
    cache:
      ttl: 3600
      key: "${topic}-${subtopic}"

prompts:
  - name: analyze_topic
    description: Analyze a knowledge topic
    arguments:
      - name: topic
        type: string
        required: true
      - name: depth
        type: string
        default: medium
        enum: [shallow, medium, deep]
    template: |
      Analyze the topic '${topic}' with ${depth} depth.
      Consider:
      - Historical context
      - Current relevance
      - Future implications
      - Related topics

middleware:
  - name: authenticate
    type: auth
    config:
      strategy: bearer
      validator: validators.tokenValidator
  
  - name: rateLimit
    type: rateLimit
    config:
      strategy: sliding-window
      limit: 100
      window: 60
      keyExtractor: request.clientId

  - name: cache
    type: cache
    config:
      backend: redis
      ttl: 300
      keyPattern: "${method}-${params}"

transports:
  - type: stdio
    enabled: true
  
  - type: http
    enabled: true
    config:
      port: 8080
      host: 0.0.0.0
      cors:
        origins: ["*"]
        methods: ["POST"]
      
  - type: websocket
    enabled: false
    config:
      port: 8081
      path: /ws

deployment:
  scaling:
    minInstances: 1
    maxInstances: 10
    targetCPU: 70
  
  health:
    endpoint: /health
    interval: 30
    timeout: 5
  
  monitoring:
    metrics: true
    tracing: true
    logging:
      level: info
      format: json
```

## 6. Cross-Language Code Generation

The DSL can generate language-specific implementations from the YAML schema:

```bash
# Generate implementations for all languages
mcp-codegen --schema mcp-server.yaml --languages all --output ./generated

# Generate specific language
mcp-codegen --schema mcp-server.yaml --language python --output ./generated/python

# Generate with custom templates
mcp-codegen --schema mcp-server.yaml --template ./templates/fastapi --output ./generated
```

## 7. Runtime Configuration Override

All implementations support runtime configuration override:

```python
# Python
server = Server.from_yaml("mcp-server.yaml")
server.override(transport="websocket", port=9090)

# TypeScript
const server = McpServer.fromYaml("mcp-server.yaml");
server.override({ transport: "websocket", port: 9090 });

# C++
auto server = McpServer::fromYaml("mcp-server.yaml");
server.override(Transport::WebSocket, 9090);

# Java
McpServer server = McpServer.fromYaml("mcp-server.yaml");
server.override(Transport.WEBSOCKET, 9090);
```

## 8. Progressive Enhancement Pattern

Start simple and add complexity as needed:

### Stage 1: Basic Tool
```python
@server.tool("Simple search")
def search(query: str):
    return database.search(query)
```

### Stage 2: Add Schema
```python
@server.tool(
    description="Search with filters",
    schema={"query": str, "filters": dict}
)
def search(query: str, filters: dict = None):
    return database.search(query, filters)
```

### Stage 3: Add Middleware
```python
@server.tool(
    description="Authenticated search",
    schema={"query": str, "filters": dict},
    middleware=[authenticate, rate_limit]
)
async def search(query: str, filters: dict = None):
    return await database.search(query, filters)
```

### Stage 4: Full Configuration
```python
@server.tool(
    description="Production search",
    schema=SearchSchema,
    middleware=[authenticate, rate_limit, cache],
    metrics=True,
    tracing=True,
    timeout=30
)
async def search(params: SearchParams) -> SearchResult:
    async with tracer.span("search"):
        return await database.search(params)
```

## Summary

This implementation demonstrates how the MCP DSL can be:
- **Consistent**: Same concepts across all languages
- **Idiomatic**: Uses language-specific patterns
- **Progressive**: Simple to complex as needed
- **Portable**: YAML schema works everywhere
- **Extensible**: Easy to add new features
- **Type-safe**: Compile-time validation where possible
- **Runtime-flexible**: Override configuration at runtime