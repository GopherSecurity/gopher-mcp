# Express Adapter for MCP OAuth Authentication

This guide shows how to integrate OAuth authentication into your MCP (Model Context Protocol) server built with Express.js using the Framework Adapter pattern.

## Table of Contents
- [Overview](#overview)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Detailed Setup](#detailed-setup)
- [API Reference](#api-reference)
- [Advanced Usage](#advanced-usage)
- [Environment Variables](#environment-variables)
- [Troubleshooting](#troubleshooting)

## Overview

The Express adapter provides a clean separation between OAuth logic (handled by `@mcp/filter-sdk`) and Express-specific integration. This pattern allows you to:

- Set up OAuth-protected MCP servers with minimal code
- Handle all OAuth flows including authorization, token exchange, and validation
- Support MCP Inspector and other OAuth clients
- Maintain framework independence in the core OAuth logic

## Installation

```bash
npm install express @mcp/filter-sdk @modelcontextprotocol/sdk
npm install --save-dev @types/express
```

## Quick Start

### Minimal Example

The simplest possible OAuth-protected MCP server:

```typescript
#!/usr/bin/env node

import express from 'express';
import { OAuthHelper } from '@mcp/filter-sdk/auth';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { setupMCPOAuth } from './express-adapter.js';
import { MCPServer } from './server.js';

// 1. Setup OAuth
const oauth = new OAuthHelper({
  serverUrl: 'http://localhost:3001',
  issuer: process.env.GOPHER_AUTH_SERVER_URL!,
  allowedScopes: ['mcp:weather']
});

// 2. Setup MCP
const mcpSDK = new Server({ name: 'minimal-server', version: '1.0.0' });
const mcp = new MCPServer(mcpSDK);

// 3. Setup Express
const app = express();
app.use(express.json());

// 4. One line to setup everything!
setupMCPOAuth(app, oauth, (req, res) => mcp.handleRequest(req, res));

// 5. Start
app.listen(3001, () => {
  console.log('Minimal MCP Server running at http://localhost:3001/mcp');
});
```

## Detailed Setup

### 1. Create the Express Adapter

First, create `express-adapter.ts`:

```typescript
import { Request, Response, NextFunction, Router } from 'express';
import { OAuthHelper } from '@mcp/filter-sdk/auth';

/**
 * Creates Express middleware for OAuth authentication
 */
export function createAuthMiddleware(oauth: OAuthHelper) {
  return async (req: Request, res: Response, next: NextFunction) => {
    try {
      // Extract token from multiple sources
      const token = oauth.extractToken(
        req.headers.authorization as string,
        req.query.access_token as string,
        req  // Pass request for session support
      );

      // Validate token
      const result = await oauth.validateToken(token);
      
      if (!result.valid) {
        return res.status(result.statusCode || 401)
          .set('WWW-Authenticate', result.wwwAuthenticate)
          .json({
            error: 'Unauthorized',
            message: result.error || 'Authentication required'
          });
      }

      // Attach auth payload to request
      (req as any).auth = result.payload;
      next();
    } catch (error: any) {
      const result = await oauth.validateToken(undefined);
      return res.status(401)
        .set('WWW-Authenticate', result.wwwAuthenticate)
        .json({
          error: 'Unauthorized',
          message: error.message
        });
    }
  };
}

/**
 * Setup all MCP OAuth endpoints and routes
 */
export function setupMCPOAuth(
  app: Express.Application,
  oauth: OAuthHelper,
  mcpHandler: (req: Request, res: Response) => Promise<void>
) {
  const router = Router();
  
  // OAuth metadata endpoints
  router.get('/.well-known/oauth-protected-resource', async (req, res) => {
    const metadata = await oauth.generateProtectedResourceMetadata();
    res.json(metadata);
  });
  
  router.get('/.well-known/openid-configuration', async (req, res) => {
    const metadata = await oauth.getDiscoveryMetadata();
    res.json(metadata);
  });
  
  // OAuth proxy endpoints
  router.get('/oauth/authorize', (req, res) => {
    const queryParams = { ...req.query };
    
    // Force consent screen
    queryParams.prompt = 'consent';
    
    // Ensure required scopes
    if (!queryParams.scope) {
      queryParams.scope = 'openid profile email mcp:weather';
    }
    
    const authUrl = oauth.buildAuthorizationUrl(queryParams as Record<string, string>);
    res.redirect(authUrl);
  });
  
  router.post('/oauth/token', async (req, res) => {
    try {
      const tokenResponse = await oauth.exchangeToken(req.body);
      res.json(tokenResponse);
    } catch (error: any) {
      res.status(400).json({
        error: 'invalid_request',
        error_description: error.message
      });
    }
  });
  
  router.get('/oauth/callback', async (req, res) => {
    const { code, state, code_verifier } = req.query;
    
    const result = await oauth.handleOAuthCallback(
      code as string,
      state as string,
      code_verifier as string,
      res
    );
    
    if (result.success) {
      res.send('<html><body><script>window.close();</script><p>Authentication successful! You can close this window.</p></body></html>');
    } else {
      res.status(400).send(`Authentication failed: ${result.error}`);
    }
  });
  
  router.post('/oauth/register', async (req, res) => {
    try {
      // Use pre-configured client for MCP Inspector
      const response = {
        client_id: process.env.GOPHER_CLIENT_ID,
        client_secret: process.env.GOPHER_CLIENT_SECRET,
        redirect_uris: [`${oauth.serverUrl}/oauth/callback`],
        token_endpoint_auth_method: 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        scope: 'openid profile email mcp:weather'
      };
      res.json(response);
    } catch (error: any) {
      res.status(400).json({
        error: 'invalid_request',
        error_description: error.message
      });
    }
  });
  
  // Protected MCP endpoint
  router.post('/mcp', createAuthMiddleware(oauth), mcpHandler);
  router.options('/mcp', createAuthMiddleware(oauth), mcpHandler);
  
  // Apply all routes
  app.use(router);
}
```

### 2. Create the MCP Server Wrapper

Create `server.ts` to handle MCP transport:

```typescript
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StreamTransport } from '@modelcontextprotocol/sdk/server/streamtransport.js';
import { Request, Response } from 'express';

export class MCPServer {
  private server: Server;
  private sessions: Map<string, any> = new Map();
  
  constructor(server: Server) {
    this.server = server;
  }
  
  async handleRequest(req: Request, res: Response) {
    // Set appropriate headers
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('Connection', 'keep-alive');
    
    // Create StreamTransport with req/res streams
    const transport = new StreamTransport(req, res);
    
    // Connect to MCP server
    await this.server.connect(transport);
    
    // Store session
    const sessionId = Math.random().toString(36);
    this.sessions.set(sessionId, transport);
    
    // Handle cleanup
    req.on('close', () => {
      this.sessions.delete(sessionId);
    });
  }
  
  getActiveSessions(): number {
    return this.sessions.size;
  }
  
  async closeAll() {
    for (const [id, transport] of this.sessions) {
      await transport.close();
      this.sessions.delete(id);
    }
  }
}
```

### 3. Complete Server Implementation

Full example with MCP tools:

```typescript
#!/usr/bin/env node

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { CallToolRequestSchema, ListToolsRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import { OAuthHelper } from "@mcp/filter-sdk/auth";
import express from "express";
import cors from "cors";
import bodyParser from "body-parser";
import cookieParser from "cookie-parser";
import dotenv from "dotenv";

import { setupMCPOAuth } from "./express-adapter.js";
import { MCPServer } from "./server.js";

// Load environment variables
dotenv.config();

// Configuration
const PORT = parseInt(process.env.SERVER_PORT || "3001", 10);
const SERVER_URL = process.env.SERVER_URL || `http://localhost:${PORT}`;
const MCP_SCOPES = ["mcp:weather"];

// 1. Initialize OAuth Helper
const oauthHelper = new OAuthHelper({
  serverUrl: SERVER_URL,
  issuer: process.env.GOPHER_AUTH_SERVER_URL!,
  jwksUri: process.env.JWKS_URI,
  tokenAudience: process.env.TOKEN_AUDIENCE,
  cacheDuration: 3600,
  autoRefresh: true,
  allowedScopes: [...MCP_SCOPES, 'openid', 'profile', 'email'],
});

// 2. Create MCP Server with tools
const mcpSDKServer = new Server(
  {
    name: "weather-server",
    version: "1.0.0",
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

// Register your MCP tools
mcpSDKServer.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: [
    {
      name: "get-weather",
      description: "Get current weather for a location",
      inputSchema: {
        type: "object",
        properties: {
          location: { type: "string", description: "City name or coordinates" }
        },
        required: ["location"]
      }
    }
  ],
}));

mcpSDKServer.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;
  
  if (name === "get-weather") {
    // Your tool implementation
    return {
      content: [
        {
          type: "text",
          text: `Weather for ${args.location}: Sunny, 72°F`
        }
      ]
    };
  }
  
  throw new Error(`Unknown tool: ${name}`);
});

// 3. Create MCP server wrapper
const mcpServer = new MCPServer(mcpSDKServer);

// 4. Create Express app
const app = express();

// Middleware
app.use(cors({ origin: true, credentials: true }));
app.use(cookieParser());
app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));

// Health check
app.get("/health", (_req, res) => {
  res.json({
    status: "healthy",
    timestamp: new Date().toISOString(),
    activeSessions: mcpServer.getActiveSessions(),
  });
});

// 5. Setup all MCP OAuth endpoints
setupMCPOAuth(app, oauthHelper, async (req, res) => {
  await mcpServer.handleRequest(req, res);
});

// 6. Start server
app.listen(PORT, () => {
  console.log(`🚀 Server: ${SERVER_URL}`);
  console.log(`📡 MCP Endpoint: ${SERVER_URL}/mcp`);
  console.log(`🔐 OAuth Metadata: ${SERVER_URL}/.well-known/oauth-protected-resource`);
  console.log(`🔑 Client ID: ${process.env.GOPHER_CLIENT_ID}`);
  console.log(`🔒 Required Scopes: ${MCP_SCOPES.join(', ')}`);
});

// Graceful shutdown
process.on('SIGINT', async () => {
  await mcpServer.closeAll();
  process.exit(0);
});
```

## API Reference

### `setupMCPOAuth(app, oauth, mcpHandler)`

Sets up all OAuth endpoints and the protected MCP endpoint.

**Parameters:**
- `app`: Express application instance
- `oauth`: OAuthHelper instance from `@mcp/filter-sdk/auth`
- `mcpHandler`: Async function to handle MCP requests

**Endpoints created:**
- `GET /.well-known/oauth-protected-resource` - OAuth metadata
- `GET /.well-known/openid-configuration` - Discovery metadata
- `GET /oauth/authorize` - Authorization endpoint (proxy)
- `POST /oauth/token` - Token endpoint (proxy)
- `GET /oauth/callback` - OAuth callback handler
- `POST /oauth/register` - Client registration
- `POST /mcp` - Protected MCP endpoint
- `OPTIONS /mcp` - MCP OPTIONS handler

### `createAuthMiddleware(oauth)`

Creates Express middleware for OAuth authentication.

**Parameters:**
- `oauth`: OAuthHelper instance

**Returns:** Express middleware function

**Usage:**
```typescript
app.post('/protected-route', createAuthMiddleware(oauth), (req, res) => {
  // Access authenticated user info
  const user = (req as any).auth;
  res.json({ user });
});
```

## Environment Variables

Required environment variables:

```env
# Server configuration
SERVER_PORT=3001
SERVER_URL=http://localhost:3001
SERVER_NAME=my-mcp-server
SERVER_VERSION=1.0.0

# OAuth configuration
GOPHER_AUTH_SERVER_URL=http://localhost:8080/realms/gopher-auth
GOPHER_CLIENT_ID=mcp_f3085016a1b746e5
GOPHER_CLIENT_SECRET=your-client-secret
TOKEN_AUDIENCE=http://localhost:3001
JWKS_URI=http://localhost:8080/realms/gopher-auth/protocol/openid-connect/certs

# Optional
JWKS_CACHE_DURATION=3600
JWKS_AUTO_REFRESH=true
REQUEST_TIMEOUT=10
```

## Advanced Usage

### Custom Authentication Logic

You can extend the authentication middleware with custom logic:

```typescript
function createCustomAuthMiddleware(oauth: OAuthHelper) {
  const baseMiddleware = createAuthMiddleware(oauth);
  
  return async (req: Request, res: Response, next: NextFunction) => {
    // Run base OAuth validation
    await baseMiddleware(req, res, async () => {
      // Additional validation
      const user = (req as any).auth;
      
      if (!user.email_verified) {
        return res.status(403).json({
          error: 'Email not verified'
        });
      }
      
      // Check custom permissions
      if (!hasPermission(user, req.path)) {
        return res.status(403).json({
          error: 'Insufficient permissions'
        });
      }
      
      next();
    });
  };
}
```

### Multiple OAuth Providers

Support multiple OAuth providers:

```typescript
const keycloakOAuth = new OAuthHelper({
  serverUrl: SERVER_URL,
  issuer: 'https://keycloak.example.com/realms/myrealm',
  // ...
});

const auth0OAuth = new OAuthHelper({
  serverUrl: SERVER_URL,
  issuer: 'https://myapp.auth0.com/',
  // ...
});

// Route to different providers
app.use('/keycloak/*', setupMCPOAuth(app, keycloakOAuth, mcpHandler));
app.use('/auth0/*', setupMCPOAuth(app, auth0OAuth, mcpHandler));
```

### Session Management

The adapter includes session support for MCP Inspector compatibility:

```typescript
import { extractSessionId, getTokenFromSession } from '@mcp/filter-sdk/auth';

app.get('/session-info', (req, res) => {
  const sessionId = extractSessionId(req);
  const token = sessionId ? getTokenFromSession(sessionId) : null;
  
  res.json({
    hasSession: !!sessionId,
    hasToken: !!token,
    sessionId: sessionId?.substring(0, 8) + '...'
  });
});
```

## Troubleshooting

### Common Issues

1. **"Invalid parameter: redirect_uri" error**
   - Ensure redirect URIs are correctly configured in your OAuth provider
   - Check that `SERVER_URL` environment variable matches your actual server URL

2. **No consent/scope page appearing**
   - The adapter automatically adds `prompt=consent` to force the consent screen
   - Verify scopes are configured as "optional" in your OAuth provider

3. **CORS errors**
   - The adapter sets up proxy endpoints to avoid CORS issues
   - Ensure `cors({ origin: true, credentials: true })` is configured

4. **Session persistence issues**
   - Sessions expire after 60 seconds by default (MCP Inspector workaround)
   - Token is stored in session cookies for MCP Inspector compatibility

### Debug Mode

Enable debug logging:

```typescript
const oauth = new OAuthHelper({
  serverUrl: SERVER_URL,
  issuer: process.env.GOPHER_AUTH_SERVER_URL!,
  debug: true  // Enable debug logging
});
```

### Testing with MCP Inspector

1. Start your server
2. Open MCP Inspector
3. Enter URL: `http://localhost:3001/mcp`
4. Click "Connect"
5. You'll be redirected to login
6. After authentication, you'll see the consent screen
7. Accept permissions to connect

## Migration Guide

### From Direct OAuth Implementation

If you have existing OAuth code, migration is simple:

**Before:**
```typescript
// Complex OAuth setup
app.get('/.well-known/oauth-protected-resource', async (req, res) => {
  // Manual metadata generation
});

app.post('/mcp', async (req, res) => {
  // Manual token validation
  const token = extractToken(req);
  if (!validateToken(token)) {
    return res.status(401).send('Unauthorized');
  }
  // Handle MCP request
});
```

**After:**
```typescript
// Simple adapter setup
const oauth = new OAuthHelper({ /* config */ });
setupMCPOAuth(app, oauth, mcpHandler);
// Done!
```

## License

MIT

## Contributing

Contributions are welcome! Please submit issues and pull requests to the repository.

## Support

For issues and questions:
- GitHub Issues: [mcp-cpp-sdk/issues](https://github.com/your-org/mcp-cpp-sdk/issues)
- Documentation: [MCP OAuth Docs](https://docs.example.com/mcp-oauth)