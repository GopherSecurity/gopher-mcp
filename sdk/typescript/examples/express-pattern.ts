/**
 * Example: Express Pattern with registerOAuthRoutes and expressMiddleware
 * 
 * This shows how to use @mcp/filter-sdk with Express-style APIs
 * similar to gopher-auth-sdk-nodejs
 */

import express from 'express';
import { McpExpressAuth } from '@mcp/filter-sdk/auth';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import dotenv from 'dotenv';

// Load environment variables
dotenv.config();

async function startExpressServer() {
  const app = express();
  app.use(express.json());
  
  // Initialize Express-style auth
  const auth = new McpExpressAuth({
    jwksUri: process.env.JWKS_URI || process.env.GOPHER_AUTH_SERVER_URL + '/protocol/openid-connect/certs',
    tokenIssuer: process.env.TOKEN_ISSUER || process.env.GOPHER_AUTH_SERVER_URL,
    tokenAudience: process.env.TOKEN_AUDIENCE,
    requireAuth: true,
  });
  
  // API 1: Register OAuth proxy routes
  // This handles OAuth discovery, metadata, and client registration
  auth.registerOAuthRoutes(app, {
    serverUrl: 'http://localhost:3001',
    allowedScopes: ['mcp:weather', 'openid'],
  });
  
  // Create MCP server
  const mcpServer = new Server(
    {
      name: 'example-server',
      version: '1.0.0',
    },
    {
      capabilities: {
        tools: {},
      },
    }
  );
  
  // API 2: Use Express middleware for authentication
  // Protect the MCP endpoint with OAuth
  app.all('/mcp',
    auth.expressMiddleware({
      publicPaths: ['/.well-known'],
      publicMethods: ['initialize'],
      toolScopes: {
        'get-forecast': ['mcp:weather'],
        'get-weather-alerts': ['mcp:weather'],
      }
    }),
    async (req, res) => {
      // Access auth context
      const authContext = (req as any).auth;
      console.log('Authenticated user:', authContext?.sub);
      
      // Handle MCP request
      // ... your MCP handler here ...
      
      res.json({
        jsonrpc: '2.0',
        result: { authenticated: true, user: authContext?.sub },
        id: req.body?.id,
      });
    }
  );
  
  // Health check endpoint
  app.get('/health', (req, res) => {
    res.json({
      status: 'healthy',
      auth: 'enabled',
      pattern: 'express',
    });
  });
  
  // Start server
  app.listen(3001, () => {
    console.log('╔════════════════════════════════════════════════════════════════╗');
    console.log('║          MCP Server with Express Pattern Auth                 ║');
    console.log('╚════════════════════════════════════════════════════════════════╝');
    console.log('');
    console.log('🚀 Server URL: http://localhost:3001');
    console.log('📡 MCP Endpoint: http://localhost:3001/mcp');
    console.log('🔐 OAuth Metadata: http://localhost:3001/.well-known/oauth-protected-resource');
    console.log('💚 Health Check: http://localhost:3001/health');
    console.log('');
    console.log('✨ Using Express pattern with:');
    console.log('   - registerOAuthRoutes() for OAuth proxy');
    console.log('   - expressMiddleware() for authentication');
  });
}

// Run the server
if (require.main === module) {
  startExpressServer().catch(console.error);
}