/**
 * @file mcp-server-with-auth.ts
 * @brief Comprehensive MCP server example with authentication
 *
 * This example demonstrates:
 * - MCP server with authentication middleware
 * - Scope-based authorization for different operations
 * - OAuth metadata endpoints for discovery
 * - Integration with filter chain for request processing
 * - Real-world usage patterns and best practices
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ErrorCode,
  ListResourcesRequestSchema,
  ListToolsRequestSchema,
  ReadResourceRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';
import * as authModule from '../src/auth';
import type {
  AuthClientConfig,
  ValidationOptions,
  ValidationResult,
  OAuthMetadata,
  TokenPayload
} from '../src/auth-types';

/**
 * Authentication middleware for MCP server
 */
class AuthenticationMiddleware {
  private authClient: authModule.McpAuthClient | null = null;
  private initialized = false;
  
  constructor(private config: AuthClientConfig) {}
  
  async initialize(): Promise<void> {
    if (this.initialized) {
      return;
    }
    
    // Check if auth is available
    if (!authModule.isAuthAvailable()) {
      console.error('Warning: Authentication module not available, running without auth');
      return;
    }
    
    try {
      this.authClient = new authModule.McpAuthClient(this.config);
      await this.authClient.initialize();
      this.initialized = true;
      console.error('✅ Authentication middleware initialized');
    } catch (error) {
      console.error('Failed to initialize auth middleware:', error);
      // Continue without auth rather than failing completely
    }
  }
  
  /**
   * Validate a token and return the payload
   */
  async validateToken(
    token: string | undefined,
    requiredScopes?: string
  ): Promise<{ valid: boolean; payload?: TokenPayload; error?: string }> {
    if (!this.authClient) {
      // No auth configured, allow access
      return { valid: true };
    }
    
    if (!token) {
      return { valid: false, error: 'No token provided' };
    }
    
    try {
      // Validation options
      const options: ValidationOptions = {
        scopes: requiredScopes,
        audience: 'mcp-server',
        clockSkew: 60
      };
      
      // Validate token
      const result = await this.authClient.validateToken(token, options);
      
      if (!result.valid) {
        return {
          valid: false,
          error: result.errorMessage || authModule.errorCodeToString(result.errorCode)
        };
      }
      
      // Extract payload for additional processing
      const payload = await this.authClient.extractPayload(token);
      
      return { valid: true, payload };
      
    } catch (error: any) {
      return {
        valid: false,
        error: error.message || 'Token validation failed'
      };
    }
  }
  
  /**
   * Generate WWW-Authenticate header for unauthorized responses
   */
  async generateAuthChallenge(error?: string, errorDescription?: string): Promise<string> {
    if (!this.authClient) {
      return 'Bearer realm="mcp-server"';
    }
    
    return await this.authClient.generateWwwAuthenticate({
      realm: 'mcp-server',
      error,
      errorDescription,
      scope: 'read write admin'
    });
  }
  
  /**
   * Cleanup resources
   */
  async destroy(): Promise<void> {
    if (this.authClient) {
      await this.authClient.destroy();
      this.authClient = null;
      this.initialized = false;
    }
  }
}

/**
 * Example MCP server with authentication
 */
class AuthenticatedMcpServer {
  private server: Server;
  private authMiddleware: AuthenticationMiddleware;
  
  constructor() {
    // Authentication configuration
    const authConfig: AuthClientConfig = {
      jwksUri: process.env.JWKS_URI || 'https://auth.example.com/.well-known/jwks.json',
      issuer: process.env.TOKEN_ISSUER || 'https://auth.example.com',
      cacheDuration: 3600,
      autoRefresh: true
    };
    
    this.authMiddleware = new AuthenticationMiddleware(authConfig);
    this.server = new Server(
      {
        name: 'mcp-server-with-auth',
        version: '1.0.0',
      },
      {
        capabilities: {
          resources: {},
          tools: {},
        },
      }
    );
    
    this.setupHandlers();
  }
  
  /**
   * Extract token from request metadata
   */
  private extractToken(_meta?: any): string | undefined {
    // Look for token in various places
    if (_meta?.authorization) {
      // Bearer token in authorization header
      const auth = _meta.authorization;
      if (auth.startsWith('Bearer ')) {
        return auth.slice(7);
      }
    }
    
    // Could also check for token in other locations
    if (_meta?.token) {
      return _meta.token;
    }
    
    return undefined;
  }
  
  /**
   * Setup request handlers with authentication
   */
  private setupHandlers(): void {
    // OAuth metadata endpoint (public)
    this.server.setRequestHandler(
      { method: 'oauth/metadata' } as any,
      async () => {
        const metadata: OAuthMetadata = {
          issuer: process.env.TOKEN_ISSUER || 'https://auth.example.com',
          authorizationEndpoint: 'https://auth.example.com/authorize',
          tokenEndpoint: 'https://auth.example.com/token',
          jwksUri: process.env.JWKS_URI || 'https://auth.example.com/.well-known/jwks.json',
          responseTypesSupported: ['code'],
          subjectTypesSupported: ['public'],
          idTokenSigningAlgValuesSupported: ['RS256'],
          scopesSupported: ['read', 'write', 'admin'],
          codeChallengeMethodsSupported: ['S256'],
          requirePkce: true
        };
        
        return { metadata };
      }
    );
    
    // List available tools (requires read scope)
    this.server.setRequestHandler(ListToolsRequestSchema, async (_request, extra) => {
      const token = this.extractToken(extra?._meta);
      const validation = await this.authMiddleware.validateToken(token, 'read');
      
      if (!validation.valid) {
        const authHeader = await this.authMiddleware.generateAuthChallenge(
          'invalid_token',
          validation.error
        );
        
        throw {
          code: ErrorCode.InvalidRequest,
          message: 'Authentication required',
          data: { 'WWW-Authenticate': authHeader }
        };
      }
      
      // Return tools based on user's scopes
      const tools: any[] = [
        {
          name: 'get_data',
          description: 'Get data (requires read scope)',
          inputSchema: {
            type: 'object',
            properties: {
              id: { type: 'string' }
            },
            required: ['id']
          },
        }
      ];
      
      // Add admin tools if user has admin scope
      if (validation.payload?.scopes?.includes('admin')) {
        tools.push({
          name: 'manage_users',
          description: 'Manage users (requires admin scope)',
          inputSchema: {
            type: 'object',
            properties: {
              operation: { type: 'string', enum: ['create', 'delete', 'update'] },
              userId: { type: 'string' }
            },
            required: ['operation', 'userId']
          },
        });
      }
      
      return { tools };
    });
    
    // Handle tool calls with scope-based authorization
    this.server.setRequestHandler(CallToolRequestSchema, async (request, extra) => {
      const token = this.extractToken(extra?._meta);
      
      // Different tools require different scopes
      const toolScopes: { [key: string]: string } = {
        'get_data': 'read',
        'update_data': 'write',
        'manage_users': 'admin'
      };
      
      const requiredScope = toolScopes[request.params.name] || 'read';
      const validation = await this.authMiddleware.validateToken(token, requiredScope);
      
      if (!validation.valid) {
        const authHeader = await this.authMiddleware.generateAuthChallenge(
          'insufficient_scope',
          `This operation requires ${requiredScope} scope`
        );
        
        throw {
          code: ErrorCode.InvalidRequest,
          message: `Insufficient permissions for ${request.params.name}`,
          data: { 'WWW-Authenticate': authHeader }
        };
      }
      
      // Log the authenticated user and action
      console.error(`User ${validation.payload?.subject} called tool ${request.params.name}`);
      
      // Handle the tool call based on the name
      switch (request.params.name) {
        case 'get_data':
          return {
            content: [
              {
                type: 'text',
                text: `Data for ID ${request.params.arguments?.id}: Sample data content`,
              },
            ],
          };
          
        case 'manage_users':
          return {
            content: [
              {
                type: 'text',
                text: `Admin action ${request.params.arguments?.operation} for user ${request.params.arguments?.userId} completed`,
              },
            ],
          };
          
        default:
          throw {
            code: ErrorCode.MethodNotFound,
            message: `Unknown tool: ${request.params.name}`,
          };
      }
    });
    
    // List resources (public but filters based on auth)
    this.server.setRequestHandler(ListResourcesRequestSchema, async (_request, extra) => {
      const token = this.extractToken(extra?._meta);
      const validation = await this.authMiddleware.validateToken(token);
      
      const resources = [
        {
          uri: 'resource://public/info',
          name: 'Public Information',
          description: 'Publicly accessible information',
        }
      ];
      
      // Add protected resources if authenticated
      if (validation.valid) {
        resources.push({
          uri: 'resource://protected/data',
          name: 'Protected Data',
          description: 'Requires authentication to access',
        });
        
        // Add admin resources if user has admin scope
        if (validation.payload?.scopes?.includes('admin')) {
          resources.push({
            uri: 'resource://admin/config',
            name: 'Admin Configuration',
            description: 'System configuration (admin only)',
          });
        }
      }
      
      return { resources };
    });
    
    // Read resource with authentication
    this.server.setRequestHandler(ReadResourceRequestSchema, async (request, extra) => {
      const token = this.extractToken(extra?._meta);
      
      // Check if resource requires authentication
      if (request.params.uri.startsWith('resource://protected/')) {
        const validation = await this.authMiddleware.validateToken(token, 'read');
        
        if (!validation.valid) {
          const authHeader = await this.authMiddleware.generateAuthChallenge(
            'invalid_token',
            'This resource requires authentication'
          );
          
          throw {
            code: ErrorCode.InvalidRequest,
            message: 'Authentication required for protected resources',
            data: { 'WWW-Authenticate': authHeader }
          };
        }
      }
      
      if (request.params.uri.startsWith('resource://admin/')) {
        const validation = await this.authMiddleware.validateToken(token, 'admin');
        
        if (!validation.valid) {
          const authHeader = await this.authMiddleware.generateAuthChallenge(
            'insufficient_scope',
            'Admin scope required'
          );
          
          throw {
            code: ErrorCode.InvalidRequest,
            message: 'Admin privileges required',
            data: { 'WWW-Authenticate': authHeader }
          };
        }
      }
      
      // Return resource content based on URI
      const resourceContent: { [key: string]: string } = {
        'resource://public/info': 'This is public information accessible to everyone.',
        'resource://protected/data': 'This is protected data requiring authentication.',
        'resource://admin/config': 'System configuration: { debug: true, maxConnections: 100 }'
      };
      
      const content = resourceContent[request.params.uri];
      
      if (!content) {
        throw {
          code: ErrorCode.InvalidRequest,
          message: `Resource not found: ${request.params.uri}`,
        };
      }
      
      return {
        contents: [
          {
            uri: request.params.uri,
            mimeType: 'text/plain',
            text: content,
          },
        ],
      };
    });
  }
  
  /**
   * Start the server
   */
  async start(): Promise<void> {
    // Initialize authentication
    await this.authMiddleware.initialize();
    
    // Create and connect transport
    const transport = new StdioServerTransport();
    await this.server.connect(transport);
    
    console.error('🚀 MCP Server with Authentication started');
    console.error('📋 Configuration:');
    console.error(`   JWKS URI: ${process.env.JWKS_URI || 'https://auth.example.com/.well-known/jwks.json'}`);
    console.error(`   Issuer: ${process.env.TOKEN_ISSUER || 'https://auth.example.com'}`);
    console.error('\n🔒 Authentication is ENABLED');
    console.error('   - Public endpoints: oauth/metadata, list resources');
    console.error('   - Protected endpoints require Bearer token');
    console.error('   - Different operations require different scopes:');
    console.error('     • read: Access to data and tools');
    console.error('     • write: Modify data');
    console.error('     • admin: User management and configuration');
    
    // Handle graceful shutdown
    process.on('SIGINT', async () => {
      console.error('\n⏹️  Shutting down server...');
      await this.authMiddleware.destroy();
      await this.server.close();
      process.exit(0);
    });
  }
}

/**
 * Usage example showing how to interact with the authenticated server
 */
async function demonstrateUsage(): Promise<void> {
  console.log('\n📖 Usage Examples:');
  console.log('=' .repeat(60));
  
  console.log('\n1. Start the server:');
  console.log('   $ JWKS_URI=https://your-auth.com/jwks TOKEN_ISSUER=https://your-auth.com \\');
  console.log('     npx tsx examples/mcp-server-with-auth.ts');
  
  console.log('\n2. Connect without authentication (limited access):');
  console.log('   $ mcp-client connect stdio://path/to/server');
  console.log('   > list-resources');
  console.log('   # Shows only public resources');
  
  console.log('\n3. Connect with authentication:');
  console.log('   $ export MCP_AUTH_TOKEN="your-jwt-token"');
  console.log('   $ mcp-client connect stdio://path/to/server \\');
  console.log('     --meta \'{"authorization": "Bearer $MCP_AUTH_TOKEN"}\'');
  console.log('   > list-resources');
  console.log('   # Shows public and protected resources');
  
  console.log('\n4. Call protected tools:');
  console.log('   > call-tool get_data {"id": "123"}');
  console.log('   # Requires read scope');
  console.log('   > call-tool manage_users {"operation": "create", "userId": "new-user"}');
  console.log('   # Requires admin scope');
  
  console.log('\n5. OAuth discovery:');
  console.log('   > request oauth/metadata');
  console.log('   # Returns OAuth configuration for client setup');
  
  console.log('\n' + '=' .repeat(60));
}

/**
 * Main entry point
 */
async function main() {
  // Show usage examples if running with --help
  if (process.argv.includes('--help')) {
    await demonstrateUsage();
    process.exit(0);
  }
  
  // Create and start server
  const server = new AuthenticatedMcpServer();
  await server.start();
}

// Run the server
if (require.main === module) {
  main().catch((error) => {
    console.error('Fatal error:', error);
    process.exit(1);
  });
}

export { AuthenticatedMcpServer, AuthenticationMiddleware };