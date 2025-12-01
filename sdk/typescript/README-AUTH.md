# @mcp/filter-sdk Authentication

The @mcp/filter-sdk provides two usage patterns for authentication, making it compatible with different coding styles and requirements.

## Two Usage Patterns

### Pattern 1: Express-Style APIs (NEW)

Similar to `gopher-auth-sdk-nodejs`, this pattern provides explicit control through two main APIs:

1. **`registerOAuthRoutes(app, options)`** - Registers OAuth discovery and proxy endpoints
2. **`expressMiddleware(options)`** - Creates authentication middleware for Express

```typescript
import express from 'express';
import { McpExpressAuth } from '@mcp/filter-sdk/auth';

const app = express();
const auth = new McpExpressAuth();

// API 1: Register OAuth routes
auth.registerOAuthRoutes(app, {
  serverUrl: 'http://localhost:3001',
  allowedScopes: ['mcp:weather', 'openid']
});

// API 2: Use authentication middleware
app.all('/mcp',
  auth.expressMiddleware({
    publicMethods: ['initialize'],
    toolScopes: {
      'get-forecast': ['mcp:weather']
    }
  }),
  mcpHandler
);
```

### Pattern 2: Simple AuthenticatedMcpServer (EXISTING)

The original pattern for quick setup with minimal configuration:

```typescript
import { AuthenticatedMcpServer } from '@mcp/filter-sdk/auth';

const server = new AuthenticatedMcpServer();
server.register(tools);
await server.start();
```

## Configuration

Both patterns support configuration through:
- Constructor parameters
- Environment variables
- Mixed approach

### Environment Variables

```bash
# OAuth Server Configuration
GOPHER_AUTH_SERVER_URL=http://localhost:8080/realms/gopher-auth
GOPHER_CLIENT_ID=mcp-server
GOPHER_CLIENT_SECRET=secret

# Optional
JWKS_URI=http://localhost:8080/realms/gopher-auth/protocol/openid-connect/certs
TOKEN_ISSUER=http://localhost:8080/realms/gopher-auth
TOKEN_AUDIENCE=mcp-server
JWKS_CACHE_DURATION=3600
REQUEST_TIMEOUT=10
```

## Express Pattern Details

### registerOAuthRoutes

Creates these endpoints:
- `/.well-known/oauth-protected-resource` - Protected resource metadata (RFC 9728)
- `/.well-known/oauth-authorization-server` - OAuth metadata proxy (RFC 8414)
- `/realms/:realm/clients-registrations/openid-connect` - Client registration proxy

Options:
```typescript
interface OAuthProxyOptions {
  serverUrl: string;           // Your MCP server URL
  allowedScopes?: string[];    // Scopes to allow (default: ['openid'])
}
```

### expressMiddleware

Creates Express middleware for token validation:

Options:
```typescript
interface ExpressMiddlewareOptions {
  audience?: string | string[];              // Expected audience(s)
  publicPaths?: string[];                   // Paths without auth
  publicMethods?: string[];                 // MCP methods without auth
  toolScopes?: Record<string, string[]>;    // Tool-specific scopes
}
```

## Complete Examples

### Express Pattern with Full Control

```typescript
import express from 'express';
import { McpExpressAuth } from '@mcp/filter-sdk/auth';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';

const app = express();
app.use(express.json());

// Initialize auth
const auth = new McpExpressAuth({
  issuer: 'http://localhost:8080/realms/gopher-auth',
  jwksUri: 'http://localhost:8080/realms/gopher-auth/protocol/openid-connect/certs',
  tokenAudience: 'mcp-server'
});

// Register OAuth routes
auth.registerOAuthRoutes(app, {
  serverUrl: 'http://localhost:3001',
  allowedScopes: ['mcp:weather', 'openid']
});

// Create MCP server
const mcpServer = new Server(
  { name: 'weather-server', version: '1.0.0' },
  { capabilities: { tools: {} } }
);

// Protected MCP endpoint
app.all('/mcp',
  auth.expressMiddleware({
    publicMethods: ['initialize'],
    toolScopes: {
      'get-forecast': ['mcp:weather'],
      'get-alerts': ['mcp:weather', 'mcp:admin']
    }
  }),
  async (req, res) => {
    const user = (req as any).auth;
    console.log('User:', user?.subject);
    // Handle MCP request
    await handleMcpRequest(req, res);
  }
);

// Additional authenticated API
app.get('/api/status',
  auth.expressMiddleware(),
  (req, res) => {
    const user = (req as any).auth;
    res.json({ user: user?.subject, status: 'ok' });
  }
);

app.listen(3001);
```

### Simple Pattern (Backward Compatible)

```typescript
import { AuthenticatedMcpServer, Tool } from '@mcp/filter-sdk/auth';
import dotenv from 'dotenv';

dotenv.config();

const tools: Tool[] = [
  {
    name: 'get-weather',
    description: 'Get weather',
    inputSchema: { /* ... */ },
    handler: async (req) => { /* ... */ }
  }
];

// Simple setup - config from environment
const server = new AuthenticatedMcpServer();
server.register(tools);
await server.start();
```

### Hybrid Usage

You can use both patterns together:

```typescript
import { AuthenticatedMcpServer, McpExpressAuth } from '@mcp/filter-sdk/auth';

// Use AuthenticatedMcpServer for MCP
const mcpServer = new AuthenticatedMcpServer({
  transport: 'http',
  serverPort: 3001
});
mcpServer.register(tools);

// Also use Express auth for custom endpoints
const auth = new McpExpressAuth();
const app = express();

auth.registerOAuthRoutes(app, {
  serverUrl: 'http://localhost:3001',
  allowedScopes: ['mcp:weather']
});

app.get('/custom-api',
  auth.expressMiddleware(),
  (req, res) => {
    // Custom authenticated endpoint
  }
);
```

## Migration Guide

### From No Auth to OAuth

Before:
```typescript
const server = new Server(...);
// No authentication
```

After (Simple):
```typescript
const server = new AuthenticatedMcpServer();
// OAuth enabled
```

After (Express):
```typescript
const auth = new McpExpressAuth();
auth.registerOAuthRoutes(app, options);
app.all('/mcp', auth.expressMiddleware(options), handler);
```

### From gopher-auth-sdk-nodejs

The Express pattern APIs are designed to be similar:

gopher-auth-sdk-nodejs:
```typescript
const auth = new GopherAuth(config);
auth.registerOAuthRoutes(app, options);
app.all('/mcp', auth.expressMiddleware(options), handler);
```

@mcp/filter-sdk:
```typescript
const auth = new McpExpressAuth(config);
auth.registerOAuthRoutes(app, options);
app.all('/mcp', auth.expressMiddleware(options), handler);
```

## Benefits

- **Pattern 1 (Express)**: Full control, explicit configuration, custom endpoints
- **Pattern 2 (Simple)**: Quick setup, convention over configuration, minimal code

Choose the pattern that best fits your needs. Both are fully supported and can be used together.