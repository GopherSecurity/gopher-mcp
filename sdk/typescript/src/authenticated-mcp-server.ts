/**
 * @file authenticated-mcp-server.ts
 * @brief MCP Server with built-in authentication support using StreamableHTTPServerTransport
 *
 * Provides a simple, configuration-driven MCP server with JWT authentication
 * Compatible with gopher-auth-sdk-nodejs patterns
 */

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ErrorCode
} from "@modelcontextprotocol/sdk/types.js";
import express, { Express, Request, Response } from "express";
import cors from "cors";
import bodyParser from "body-parser";
import { McpAuthClient } from "./mcp-auth-api";
import type { AuthClientConfig, ValidationOptions } from "./auth-types";
import { randomUUID } from "crypto";
import { IncomingMessage, ServerResponse } from "node:http";

export interface Tool {
  name: string;
  description: string;
  inputSchema: any;
  handler: (request: any) => Promise<any>;
}

export interface AuthenticatedMcpServerConfig {
  serverName?: string;
  serverVersion?: string;
  serverUrl?: string;
  serverPort?: number;
  
  // Authentication settings - compatible with both old and new env vars
  jwksUri?: string;
  tokenIssuer?: string;
  tokenAudience?: string;
  authServerUrl?: string;  // For legacy GOPHER_AUTH_SERVER_URL
  clientId?: string;        // For legacy GOPHER_CLIENT_ID
  clientSecret?: string;    // For legacy GOPHER_CLIENT_SECRET
  
  cacheDuration?: number;
  autoRefresh?: boolean;
  requireAuth?: boolean;
  requireAuthOnConnect?: boolean;  // Require auth for initialize/connect
  clockSkew?: number;
  toolScopes?: Record<string, string>;
  
  // Transport settings
  transport?: 'stdio' | 'http';
  mcpEndpoint?: string;
  corsOrigin?: string | string[] | boolean;
  
  // MCP settings
  publicMethods?: string[];
  
  // Additional scopes to allow beyond standard ones
  additionalAllowedScopes?: string[];
}

/**
 * MCP Server with integrated authentication
 * Follows gopher-auth-sdk-nodejs patterns
 * 
 * @example
 * ```typescript
 * const server = new AuthenticatedMcpServer();
 * server.register(tools);
 * server.start();
 * ```
 */
export class AuthenticatedMcpServer {
  private server: Server;
  private authClient: McpAuthClient | null = null;
  private tools: Tool[] = [];
  private config: AuthenticatedMcpServerConfig;
  private app?: Express;
  private transports: Map<string, StreamableHTTPServerTransport> = new Map();
  
  constructor(config?: AuthenticatedMcpServerConfig) {
    // Merge provided config with environment variables
    const env = process.env;
    this.config = {
      // Server identification
      serverName: config?.serverName || env['SERVER_NAME'] || "mcp-server",
      serverVersion: config?.serverVersion || env['SERVER_VERSION'] || "1.0.0",
      serverUrl: config?.serverUrl || env['SERVER_URL'] || `http://localhost:${env['SERVER_PORT'] || '3001'}`,
      serverPort: config?.serverPort || parseInt(env['SERVER_PORT'] || env['HTTP_PORT'] || "3001"),
      
      // Authentication - support both new and legacy env vars
      jwksUri: config?.jwksUri || env['JWKS_URI'],
      tokenIssuer: config?.tokenIssuer || env['TOKEN_ISSUER'],
      tokenAudience: config?.tokenAudience || env['TOKEN_AUDIENCE'],
      authServerUrl: config?.authServerUrl || env['GOPHER_AUTH_SERVER_URL'],
      clientId: config?.clientId || env['GOPHER_CLIENT_ID'],
      clientSecret: config?.clientSecret || env['GOPHER_CLIENT_SECRET'],
      
      cacheDuration: config?.cacheDuration || parseInt(env['CACHE_DURATION'] || "3600"),
      autoRefresh: config?.autoRefresh ?? (env['AUTO_REFRESH'] !== "false"),
      requireAuth: config?.requireAuth ?? (env['REQUIRE_AUTH'] === "true"),
      requireAuthOnConnect: config?.requireAuthOnConnect ?? (env['REQUIRE_AUTH_ON_CONNECT'] === "true"),
      clockSkew: config?.clockSkew || parseInt(env['CLOCK_SKEW'] || "60"),
      toolScopes: config?.toolScopes || this.parseToolScopes(),
      
      // Transport and endpoint
      transport: config?.transport || (env['TRANSPORT_MODE'] as 'stdio' | 'http') || 'stdio',
      mcpEndpoint: config?.mcpEndpoint || '/mcp',
      corsOrigin: config?.corsOrigin ?? (env['CORS_ORIGIN'] || "*"),
      
      // MCP settings
      // If requireAuthOnConnect is true, don't include 'initialize' in publicMethods
      // This ensures users must authenticate when clicking "Connect" in MCP Inspector
      publicMethods: config?.publicMethods || (
        config?.requireAuthOnConnect || env['REQUIRE_AUTH_ON_CONNECT'] === "true" 
          ? []  // No public methods - auth required for everything
          : ['initialize']  // Allow initialize without auth
      )
    };
    
    this.server = new Server(
      {
        name: this.config.serverName || "mcp-server",
        version: this.config.serverVersion || "1.0.0",
      },
      {
        capabilities: {
          tools: {},
        },
      }
    );
  }
  
  /**
   * Parse tool scopes from environment variables
   * Format: TOOL_SCOPES_<TOOL_NAME>=scope1,scope2
   */
  private parseToolScopes(): Record<string, string> {
    const scopes: Record<string, string> = {};
    
    // Look for TOOL_SCOPES_* environment variables
    Object.keys(process.env).forEach(key => {
      if (key.startsWith('TOOL_SCOPES_')) {
        const toolName = key
          .replace('TOOL_SCOPES_', '')
          .toLowerCase()
          .replace(/_/g, '-');
        const scopeValue = process.env[key];
        
        // Only add if there are actual scopes (not empty string)
        if (scopeValue && scopeValue.trim()) {
          scopes[toolName] = scopeValue;
        }
      }
    });
    
    return scopes;
  }
  
  /**
   * Extract unique MCP scopes from tool configurations
   */
  private extractScopesFromTools(): string[] {
    const scopes = new Set<string>();
    
    // Add scopes from environment variables
    const toolScopes = this.config.toolScopes || {};
    Object.values(toolScopes).forEach(scopeString => {
      if (scopeString) {
        // Split comma-separated scopes and add each one
        scopeString.split(',').forEach(scope => {
          const trimmed = scope.trim();
          if (trimmed) {
            scopes.add(trimmed);
          }
        });
      }
    });
    
    return Array.from(scopes);
  }
  
  /**
   * Register tools with the server
   */
  register(tools: Tool[]): void {
    this.tools = tools;
    this.setupHandlers();
  }
  
  /**
   * Initialize authentication if configured
   */
  private async initializeAuth(): Promise<void> {
    // Enable auth if JWKS URI or auth server URL is configured, regardless of REQUIRE_AUTH setting
    const jwksUri = this.config.jwksUri || 
                   (this.config.authServerUrl ? `${this.config.authServerUrl}/protocol/openid-connect/certs` : null);
    
    // Only skip auth if no JWKS URI is configured
    if (!jwksUri) {
      console.error("⚠️  Authentication disabled");
      console.error("   Reason: No JWKS_URI or GOPHER_AUTH_SERVER_URL configured");
      return;
    }
    
    // Show that authentication WOULD be enabled if the C library was available
    console.error("🔐 Authentication configuration detected");
    console.error(`   JWKS URI: ${jwksUri}`);
    console.error(`   Issuer: ${this.config.tokenIssuer || this.config.authServerUrl || "https://auth.example.com"}`);
    
    try {
      const issuer = this.config.tokenIssuer || this.config.authServerUrl || "https://auth.example.com";
      
      const authConfig: AuthClientConfig = {
        jwksUri: jwksUri,
        issuer: issuer,
        cacheDuration: this.config.cacheDuration || 3600,
        autoRefresh: this.config.autoRefresh !== false
      };
      
      this.authClient = new McpAuthClient(authConfig);
      await this.authClient.initialize();
      console.error("✅ Authentication initialized successfully");
      console.error(`   JWKS: ${authConfig.jwksUri}`);
      console.error(`   Issuer: ${authConfig.issuer}`);
      
      // Note if REQUIRE_AUTH is false but auth is still enabled
      if (!this.config.requireAuth) {
        console.error("   Note: REQUIRE_AUTH=false, but auth is enabled for tools with scopes");
      }
    } catch (error: any) {
      // Check if it's because the C library isn't available
      if (error.message?.includes('Authentication support not available')) {
        console.error("⚠️  Authentication C library not available");
        console.error("   The server is configured for authentication but the C library is not loaded");
        console.error("   Authentication would be ENABLED if the library was available");
        console.error("   Tools with scopes would require authentication:");
        this.tools.forEach(tool => {
          const scopes = this.getRequiredScopes(tool.name);
          if (scopes) {
            console.error(`     - ${tool.name}: Requires ${scopes}`);
          }
        });
      } else {
        console.error("⚠️  Authentication initialization failed:", error.message || error);
      }
      
      // Continue without auth in development
      if (process.env['NODE_ENV'] === "production" && !error.message?.includes('not available')) {
        throw error;
      }
    }
  }
  
  /**
   * Extract token from request or transport
   */
  private extractToken(request: any): string | undefined {
    // Check various locations for token
    if (request.params?.token) {
      return request.params.token;
    }
    
    // Check meta fields (for backward compatibility)
    const meta = request.meta || request._meta;
    if (meta?.authorization) {
      const auth = meta.authorization;
      if (typeof auth === 'string' && auth.startsWith('Bearer ')) {
        return auth.slice(7);
      }
      return auth;
    }
    
    // Check if authorization header was stored on any active transport
    // This is a workaround since MCP doesn't support auth headers natively
    for (const transport of this.transports.values()) {
      const authHeader = (transport as any).authorizationHeader;
      if (authHeader) {
        if (typeof authHeader === 'string' && authHeader.startsWith('Bearer ')) {
          return authHeader.slice(7);
        }
        return authHeader;
      }
    }
    
    return undefined;
  }
  
  /**
   * Check if tool requires authentication
   */
  private requiresAuth(toolName: string): boolean {
    // Check if tool has specific scopes configured
    if (this.config.toolScopes && this.config.toolScopes[toolName]) {
      return true;
    }
    
    // Fall back to global auth requirement
    return this.config.requireAuth || false;
  }
  
  /**
   * Get required scopes for a tool
   */
  private getRequiredScopes(toolName: string): string | undefined {
    return this.config.toolScopes?.[toolName];
  }
  
  /**
   * Setup request handlers
   */
  private setupHandlers(): void {
    // List tools handler
    this.server.setRequestHandler(ListToolsRequestSchema, async () => ({
      tools: this.tools.map(tool => ({
        name: tool.name,
        description: tool.description,
        inputSchema: tool.inputSchema,
      }))
    }));
    
    // Call tool handler
    this.server.setRequestHandler(CallToolRequestSchema, async (request) => {
      const tool = this.tools.find(t => t.name === request.params.name);
      if (!tool) {
        throw {
          code: ErrorCode.MethodNotFound,
          message: `Tool not found: ${request.params.name}`
        };
      }
      
      // Check authentication if required
      if (this.authClient && this.requiresAuth(tool.name)) {
        const token = this.extractToken(request);
        
        if (!token) {
          throw {
            code: ErrorCode.InvalidRequest,
            message: "Authentication required"
          };
        }
        
        const requiredScopes = this.getRequiredScopes(tool.name);
        const validationOptions: ValidationOptions = requiredScopes ? {
          scopes: requiredScopes,
          audience: this.config.tokenAudience,
          clockSkew: this.config.clockSkew || 60
        } : {
          audience: this.config.tokenAudience,
          clockSkew: this.config.clockSkew || 60
        };
        
        const validation = await this.authClient.validateToken(token, validationOptions);
        
        if (!validation.valid) {
          throw {
            code: ErrorCode.InvalidRequest,
            message: `Authentication failed: ${validation.errorMessage}`
          };
        }
        
        console.error(`✅ Authenticated call to ${tool.name}`);
      }
      
      // Execute tool
      return await tool.handler(request);
    });
  }
  
  /**
   * Check if a request is an initialize request
   */
  private isInitializeRequest(body: any): boolean {
    return body && body.method === 'initialize';
  }
  
  /**
   * Handle MCP requests with StreamableHTTPServerTransport
   * Follows the per-session pattern from gopher-auth-sdk-nodejs
   */
  private async handleMcpRequest(req: Request, res: Response): Promise<void> {
    try {
      // Cast Express Request/Response to Node.js native types
      const nodeReq = req as unknown as IncomingMessage;
      const nodeRes = res as unknown as ServerResponse;

      // Check for existing session ID in headers
      const sessionId = req.headers['mcp-session-id'] as string | undefined;
      const method = req.body?.method || req.method;

      console.error(`📨 Request: ${method} [session: ${sessionId || 'none'}]`);

      let transport: StreamableHTTPServerTransport;

      if (sessionId && this.transports.has(sessionId)) {
        // Reuse existing transport for this session
        transport = this.transports.get(sessionId)!;
        console.error(`♻️  Reusing transport for session: ${sessionId}`);
      } else if (!sessionId && this.isInitializeRequest(req.body)) {
        // New initialization request - check authentication FIRST
        console.error('🆕 Creating new transport for initialization');
        
        // Check if authentication is required for initialize
        const isPublicMethod = this.config.publicMethods?.includes('initialize') || false;
        
        if (!isPublicMethod && this.authClient) {
          // Authentication is required for initialize
          const authHeader = req.headers.authorization as string;
          
          if (!authHeader || !authHeader.startsWith('Bearer ')) {
            console.error('❌ Authentication required but no token provided');
            res.status(401).json({
              jsonrpc: '2.0',
              error: {
                code: ErrorCode.InvalidRequest,
                message: 'Authentication required. Please provide a Bearer token in the Authorization header.'
              },
              id: req.body?.id || null
            });
            return;
          }
          
          // Extract and validate token
          const token = authHeader.slice(7);
          const validationOptions: ValidationOptions = {
            audience: this.config.tokenAudience,
            clockSkew: this.config.clockSkew || 60
          };
          
          try {
            const validation = await this.authClient.validateToken(token, validationOptions);
            
            if (!validation.valid) {
              console.error(`❌ Token validation failed: ${validation.errorMessage}`);
              res.status(401).json({
                jsonrpc: '2.0',
                error: {
                  code: ErrorCode.InvalidRequest,
                  message: `Authentication failed: ${validation.errorMessage || 'Invalid token'}`
                },
                id: req.body?.id || null
              });
              return;
            }
            
            console.error('✅ Authentication successful for initialize request');
          } catch (error: any) {
            console.error(`❌ Token validation error: ${error.message}`);
            res.status(401).json({
              jsonrpc: '2.0',
              error: {
                code: ErrorCode.InvalidRequest,
                message: `Authentication error: ${error.message}`
              },
              id: req.body?.id || null
            });
            return;
          }
        }

        transport = new StreamableHTTPServerTransport({
          sessionIdGenerator: () => randomUUID(),
          onsessioninitialized: (newSessionId: string) => {
            // Store the transport by session ID when session is initialized
            console.error(`✅ Session initialized with ID: ${newSessionId}`);
            this.transports.set(newSessionId, transport);
          }
        });

        // Set up onclose handler to clean up transport when closed
        transport.onclose = () => {
          const sid = transport.sessionId;
          if (sid && this.transports.has(sid)) {
            console.error(`🗑️  Transport closed for session ${sid}, removing from map`);
            this.transports.delete(sid);
          }
        };

        // Connect the transport to the MCP server BEFORE handling the request
        await this.server.connect(transport);
      } else {
        // Invalid request - no session ID or not initialization request
        console.error(`❌ Invalid request: sessionId=${sessionId}, method=${method}`);
        res.status(400).json({
          jsonrpc: '2.0',
          error: {
            code: -32000,
            message: 'Bad Request: No valid session ID provided'
          },
          id: null
        });
        return;
      }

      // Store authorization header in transport for later use
      // We can't add it to the request body as MCP validates the schema strictly
      if (req.headers.authorization) {
        console.error(`🔐 Authorization header found: ${req.headers.authorization.substring(0, 20)}...`);
        // Store auth header on the transport object so handlers can access it
        (transport as any).authorizationHeader = req.headers.authorization;
      }
      
      // Pass the parsed body from Express to avoid re-parsing
      await transport.handleRequest(nodeReq, nodeRes, req.body);

    } catch (error: any) {
      console.error('❌ Error handling request:', error.message);

      if (!res.headersSent) {
        res.status(500).json({
          jsonrpc: '2.0',
          error: {
            code: -32603,
            message: error.message || 'Internal server error',
          },
          id: null,
        });
      }
    }
  }
  
  /**
   * Start the server
   */
  async start(): Promise<void> {
    // Initialize authentication
    await this.initializeAuth();
    
    // Create and connect transport based on configuration
    if (this.config.transport === 'http') {
      await this.startHttpServer();
    } else {
      await this.startStdioServer();
    }
    
    const displayInfo = () => {
      console.error(`🚀 ${this.config.serverName} started`);
      console.error(`📋 Version: ${this.config.serverVersion}`);
      console.error(`🌐 Transport: ${this.config.transport?.toUpperCase()}`);
      if (this.config.transport === 'http') {
        console.error(`🔗 URL: ${this.config.serverUrl}${this.config.mcpEndpoint}`);
        console.error(`💚 Health: ${this.config.serverUrl}/health`);
      }
      // Show auth status more clearly
      const authStatus = this.authClient 
        ? "ENABLED" 
        : (this.config.jwksUri || this.config.authServerUrl)
          ? "CONFIGURED (C library not available)"
          : "DISABLED";
      console.error(`🔒 Authentication: ${authStatus}`);
      if (this.authClient && this.config.requireAuthOnConnect) {
        console.error(`🔐 Auth on Connect: REQUIRED (must authenticate to initialize)`);
      } else if (this.authClient) {
        console.error(`🔓 Auth on Connect: NOT REQUIRED (only for protected tools)`);
      }
      console.error(`🛠️  Tools registered: ${this.tools.length}`);
      
      this.tools.forEach(tool => {
        const scopes = this.getRequiredScopes(tool.name);
        const authStatus = scopes ? `🔐 Requires: ${scopes}` : "🌍 Public";
        console.error(`   - ${tool.name}: ${authStatus}`);
      });
    };
    
    displayInfo();
    
    // Handle graceful shutdown
    const shutdown = async () => {
      console.error("\n⏹️  Shutting down...");
      if (this.authClient) {
        await this.authClient.destroy();
      }
      
      // Close all transports
      console.error(`🛑 Closing ${this.transports.size} active transports...`);
      for (const transport of this.transports.values()) {
        await transport.close();
      }
      this.transports.clear();
      
      await this.server.close();
      
      // Close HTTP server if running
      const httpServer = (this as any).httpServer;
      if (httpServer) {
        await new Promise<void>((resolve) => {
          httpServer.close(() => resolve());
        });
      }
      
      process.exit(0);
    };
    
    process.on("SIGINT", shutdown);
    process.on("SIGTERM", shutdown);
  }
  
  /**
   * Start stdio transport server
   */
  private async startStdioServer(): Promise<void> {
    const transport = new StdioServerTransport();
    await this.server.connect(transport);
  }
  
  /**
   * Start HTTP server with StreamableHTTPServerTransport
   */
  private async startHttpServer(): Promise<void> {
    this.app = express();
    
    // Configure CORS
    const corsOptions = {
      origin: this.config.corsOrigin,
      credentials: true,
      methods: ['GET', 'POST', 'OPTIONS'],
      allowedHeaders: ['Content-Type', 'Authorization', 'mcp-session-id'],
    };
    this.app.use(cors(corsOptions));
    
    // Parse JSON bodies
    this.app.use(bodyParser.json());
    
    // Health check endpoint
    this.app.get('/health', (_req, res) => {
      res.json({
        status: 'ok',
        server: this.config.serverName,
        version: this.config.serverVersion,
        transport: 'streamable-http',
        authentication: this.authClient ? 'enabled' : 'disabled',
        tools: this.tools.length,
        activeSessions: this.transports.size
      });
    });
    
    // OAuth metadata endpoints (if auth is configured)
    if (this.config.authServerUrl) {
      // Protected resource metadata (RFC 9728)
      this.app.get('/.well-known/oauth-protected-resource', async (_req, res) => {
        try {
          // Extract MCP scopes from configured tools
          const mcpScopes = this.extractScopesFromTools();
          
          // Create comprehensive allowed scopes list matching gopher-auth-sdk-nodejs
          const allowedScopes = [
            // OpenID Connect standard scopes
            "openid",
            "offline_access", 
            "profile",
            "email",
            "address",
            "phone",
            // Keycloak role mapping
            "roles",
            // MCP-specific scopes from tools
            ...mcpScopes,
            // Additional scopes if configured
            ...(this.config.additionalAllowedScopes || []),
          ];
          
          // Remove duplicates
          const uniqueScopes = [...new Set(allowedScopes)];
          
          const metadata = {
            resource: this.config.serverUrl,
            // Point to our OAuth metadata proxy, not directly to Keycloak
            authorization_servers: [this.config.serverUrl],
            scopes_supported: uniqueScopes,
          };
          res.json(metadata);
        } catch (error) {
          // Fallback to configured scopes if error
          const metadata = {
            resource: this.config.serverUrl,
            authorization_servers: [this.config.serverUrl],
            scopes_supported: ['openid', 'profile', 'email', 'offline_access', 'mcp:weather'],
          };
          res.json(metadata);
        }
      });
      
      // Handle OPTIONS for OAuth metadata endpoint
      this.app.options('/.well-known/oauth-authorization-server', (_req, res) => {
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
        res.header('Access-Control-Allow-Headers', 'Content-Type, Authorization');
        res.status(200).send();
      });
      
      // OAuth authorization server metadata
      // This proxies the metadata from the actual auth server
      this.app.get('/.well-known/oauth-authorization-server', async (_req, res) => {
        // Set CORS headers
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
        res.header('Access-Control-Allow-Headers', 'Content-Type, Authorization');
        
        try {
          // Fetch the actual OAuth metadata from Keycloak
          const metadataUrl = `${this.config.authServerUrl}/.well-known/openid-configuration`;
          const response = await fetch(metadataUrl);
          if (!response.ok) {
            throw new Error(`Failed to fetch OAuth metadata: ${response.status}`);
          }
          const metadata = await response.json() as any;
          
          // Rewrite registration endpoint to point to our proxy
          if (metadata.registration_endpoint) {
            // Extract realm from authServerUrl (e.g., http://localhost:8080/realms/gopher-auth)
            const realmMatch = this.config.authServerUrl?.match(/\/realms\/([^/]+)/);
            const realm = realmMatch ? realmMatch[1] : 'gopher-auth';
            
            metadata.registration_endpoint = metadata.registration_endpoint.replace(
              this.config.authServerUrl,
              `${this.config.serverUrl}/realms/${realm}`
            );
          }
          
          // Add client information if available
          if (this.config.clientId) {
            metadata.client_id = this.config.clientId;
          }
          
          // Extract MCP scopes from configured tools
          const mcpScopes = this.extractScopesFromTools();
          
          // Create the same allowed scopes list as in protected resource metadata
          const allowedScopes = [
            // OpenID Connect standard scopes
            "openid",
            "offline_access", 
            "profile",
            "email",
            "address",
            "phone",
            // Keycloak role mapping
            "roles",
            // MCP-specific scopes from tools
            ...mcpScopes,
            // Additional scopes if configured
            ...(this.config.additionalAllowedScopes || []),
          ];
          
          // Filter scopes_supported to only include allowed scopes
          if (metadata.scopes_supported) {
            const uniqueAllowedScopes = [...new Set(allowedScopes)];
            const filteredScopes = metadata.scopes_supported.filter((scope: string) => 
              uniqueAllowedScopes.includes(scope)
            );
            
            console.error(`🔧 Filtered scopes from ${metadata.scopes_supported.length} to ${filteredScopes.length}`);
            metadata.scopes_supported = filteredScopes;
          }
          
          res.json(metadata);
        } catch (error) {
          console.error('Failed to fetch OAuth metadata:', error);
          res.status(500).json({ 
            error: 'Failed to fetch OAuth metadata',
            details: error instanceof Error ? error.message : String(error)
          });
        }
      });
      
      // Handle OPTIONS for client registration endpoint
      this.app.options('/realms/:realm/clients-registrations/openid-connect', (_req, res) => {
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
        res.header('Access-Control-Allow-Headers', 'Content-Type, Authorization');
        res.status(200).send();
      });
      
      // Client Registration proxy endpoint
      this.app.post('/realms/:realm/clients-registrations/openid-connect', async (req, res) => {
        try {
          // Set CORS headers immediately
          res.header('Access-Control-Allow-Origin', '*');
          res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
          res.header('Access-Control-Allow-Headers', 'Content-Type, Authorization');
          
          const keycloakUrl = `${this.config.authServerUrl}/clients-registrations/openid-connect`;
          const registrationRequest = { ...req.body };
          
          console.error('🔄 Proxying client registration to Keycloak');
          console.error(`   URL: ${keycloakUrl}`);
          console.error(`   Request body:`, JSON.stringify(registrationRequest, null, 2));
          console.error(`   Headers:`, req.headers);
          
          // Don't filter scopes - let Keycloak handle what scopes are allowed
          // Just log what was requested
          if (registrationRequest.scope) {
            console.error(`   Requested scope: ${registrationRequest.scope}`);
          }
          
          // Forward to Keycloak
          console.error('   Forwarding to Keycloak...');
          const response = await fetch(keycloakUrl, {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              ...(req.headers['authorization'] ? { 'Authorization': req.headers['authorization'] as string } : {}),
            },
            body: JSON.stringify(registrationRequest),
          });
          
          const responseText = await response.text();
          console.error(`   Keycloak response status: ${response.status}`);
          console.error(`   Keycloak response: ${responseText}`);
          
          // Try to parse as JSON
          let data: any;
          try {
            data = JSON.parse(responseText);
          } catch (e) {
            data = { error: 'Invalid response from Keycloak', details: responseText };
          }
          
          // Return the registration response
          res.status(response.status).json(data);
        } catch (error: any) {
          console.error(`❌ Error proxying client registration:`, error);
          console.error(`   Error stack:`, error.stack);
          res.status(500).json({ 
            error: 'Client registration failed',
            message: error.message,
            details: error.stack
          });
        }
      });
    }
    
    // MCP endpoint - handles both GET and POST
    // Uses StreamableHTTPServerTransport for proper session management
    this.app.all(this.config.mcpEndpoint || '/mcp', async (req, res) => {
      // For OPTIONS requests, just return success
      if (req.method === 'OPTIONS') {
        res.status(200).send();
        return;
      }
      
      // Handle MCP requests with per-session transport
      await this.handleMcpRequest(req, res);
    });
    
    // Start the HTTP server
    const port = this.config.serverPort || 3001;
    const host = 'localhost';
    const httpServer = this.app.listen(port, host, () => {
      console.error(`🌐 HTTP server listening on http://${host}:${port}`);
      console.error(`📡 MCP Endpoint: ${this.config.serverUrl}${this.config.mcpEndpoint} (POST/GET)`);
      if (this.config.authServerUrl) {
        console.error(`🔐 OAuth Metadata: ${this.config.serverUrl}/.well-known/oauth-protected-resource`);
        console.error(`🔑 OAuth Server: ${this.config.serverUrl}/.well-known/oauth-authorization-server`);
      }
    });
    
    // Store server reference for cleanup
    (this as any).httpServer = httpServer;
  }
  
  /**
   * Stop the server
   */
  async stop(): Promise<void> {
    if (this.authClient) {
      await this.authClient.destroy();
    }
    
    // Close all transports
    for (const transport of this.transports.values()) {
      await transport.close();
    }
    this.transports.clear();
    
    // Close HTTP server if running
    const httpServer = (this as any).httpServer;
    if (httpServer) {
      await new Promise<void>((resolve) => {
        httpServer.close(() => resolve());
      });
    }
    
    await this.server.close();
  }
  
  /**
   * Get active session IDs
   */
  getActiveSessions(): string[] {
    return Array.from(this.transports.keys());
  }
}