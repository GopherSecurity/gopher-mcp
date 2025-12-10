/**
 * Example: Using Both Patterns Together
 * 
 * This shows how you can use both the simple pattern (AuthenticatedMcpServer)
 * and the Express pattern (registerOAuthRoutes + expressMiddleware) in the same project
 */

import dotenv from 'dotenv';
import express from 'express';
import { AuthenticatedMcpServer, McpExpressAuth, Tool } from '@mcp/filter-sdk/auth';

// Load environment variables
dotenv.config();

// ================================================================
// Pattern 1: Simple mode for quick setup (STDIO transport)
// ================================================================
function startSimpleServer() {
  const tools: Tool[] = [
    {
      name: 'get-weather',
      description: 'Get weather',
      inputSchema: { type: 'object', properties: { location: { type: 'string' } } },
      handler: async (req) => ({
        content: [{ type: 'text', text: `Weather in ${req.params.location}` }]
      })
    }
  ];
  
  const server = new AuthenticatedMcpServer({
    transport: 'stdio',  // Use STDIO transport
    requireAuth: true,
  });
  
  server.register(tools);
  server.start().then(() => {
    console.log('✅ Simple pattern server started (STDIO)');
  });
}

// ================================================================
// Pattern 2: Express mode for HTTP with full control
// ================================================================
async function startExpressServer() {
  const app = express();
  app.use(express.json());
  
  // Initialize Express-style auth
  const auth = new McpExpressAuth();
  
  // Register OAuth routes
  auth.registerOAuthRoutes(app, {
    serverUrl: 'http://localhost:3001',
    allowedScopes: ['mcp:weather', 'openid'],
  });
  
  // Protected endpoint with authentication
  app.post('/api/weather',
    auth.expressMiddleware({
      toolScopes: {
        'get-forecast': ['mcp:weather'],
      }
    }),
    async (req, res) => {
      const user = (req as any).auth;
      res.json({
        message: 'Authenticated weather API',
        user: user?.sub,
        location: req.body.location,
      });
    }
  );
  
  // MCP endpoint
  app.all('/mcp',
    auth.expressMiddleware({
      publicMethods: ['initialize'],
      toolScopes: {
        'get-forecast': ['mcp:weather'],
        'get-weather-alerts': ['mcp:weather'],
      }
    }),
    async (req, res) => {
      // Handle MCP protocol
      res.json({
        jsonrpc: '2.0',
        result: { authenticated: true },
        id: req.body?.id,
      });
    }
  );
  
  app.listen(3001, () => {
    console.log('✅ Express pattern server started (HTTP)');
    console.log('   - OAuth routes: /.well-known/*');
    console.log('   - MCP endpoint: /mcp');
    console.log('   - API endpoint: /api/weather');
  });
}

// ================================================================
// Pattern 3: Hybrid - Use AuthenticatedMcpServer with Express
// ================================================================
async function startHybridServer() {
  const app = express();
  app.use(express.json());
  
  // Use AuthenticatedMcpServer for MCP functionality
  const mcpServer = new AuthenticatedMcpServer({
    transport: 'http',
    serverPort: 3002,
    requireAuth: true,
  });
  
  // But also use Express auth for additional endpoints
  const auth = new McpExpressAuth();
  
  // Register OAuth routes
  auth.registerOAuthRoutes(app, {
    serverUrl: 'http://localhost:3002',
    allowedScopes: ['mcp:weather', 'openid'],
  });
  
  // Add custom authenticated endpoints
  app.get('/api/status',
    auth.expressMiddleware(),
    (req, res) => {
      const user = (req as any).auth;
      res.json({
        status: 'healthy',
        authenticated: true,
        user: user?.sub,
      });
    }
  );
  
  // Register tools with MCP server
  mcpServer.register([
    {
      name: 'hybrid-tool',
      description: 'Tool in hybrid mode',
      inputSchema: { type: 'object' },
      handler: async () => ({
        content: [{ type: 'text', text: 'Hybrid mode response' }]
      })
    }
  ]);
  
  // Start MCP server (which starts Express internally)
  await mcpServer.start();
  
  console.log('✅ Hybrid pattern server started');
  console.log('   - MCP server with authentication');
  console.log('   - Additional Express endpoints');
}

// ================================================================
// Main: Choose which pattern to run
// ================================================================
async function main() {
  const mode = process.env.MODE || 'express';
  
  console.log('╔════════════════════════════════════════════════════════════════╗');
  console.log('║             @mcp/filter-sdk - Both Patterns Demo              ║');
  console.log('╚════════════════════════════════════════════════════════════════╝');
  console.log('');
  console.log(`Running in ${mode} mode`);
  console.log('');
  
  switch (mode) {
    case 'simple':
      startSimpleServer();
      break;
      
    case 'express':
      await startExpressServer();
      break;
      
    case 'hybrid':
      await startHybridServer();
      break;
      
    case 'all':
      // Run all patterns (on different ports)
      startSimpleServer();
      await startExpressServer();
      // Note: Don't run hybrid with others as it may conflict
      break;
      
    default:
      console.error('Unknown mode. Use MODE=simple|express|hybrid|all');
      process.exit(1);
  }
}

// Run if this is the main module
if (require.main === module) {
  main().catch(console.error);
}

export { startSimpleServer, startExpressServer, startHybridServer };