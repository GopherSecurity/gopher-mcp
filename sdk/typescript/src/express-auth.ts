/**
 * @file express-auth.ts
 * @brief Express-style authentication APIs for MCP servers
 * 
 * Provides registerOAuthRoutes and expressMiddleware APIs similar to gopher-auth-sdk-nodejs
 * while maintaining compatibility with the existing AuthenticatedMcpServer pattern
 */

import { Request, Response, NextFunction, RequestHandler, Express } from 'express';
import { McpAuthClient } from './mcp-auth-api';
import type { AuthClientConfig, ValidationOptions, TokenPayload } from './auth-types';

/**
 * Options for Express OAuth middleware
 */
export interface ExpressMiddlewareOptions {
  /** Expected audience(s) for tokens */
  audience?: string | string[];
  
  /** Paths that don't require authentication (e.g., ['.well-known']) */
  publicPaths?: string[];
  
  /** MCP methods that don't require authentication (e.g., ['initialize']) */
  publicMethods?: string[];
  
  /** Tool-specific scope requirements (tool name -> required scopes) */
  toolScopes?: Record<string, string[]>;
}

/**
 * Options for OAuth proxy routes
 */
export interface OAuthProxyOptions {
  /** MCP server URL (e.g., "http://localhost:3001") */
  serverUrl: string;
  
  /** Allowed scopes for client registration */
  allowedScopes?: string[];
}

/**
 * MCP Express Authentication
 * Provides Express-style APIs similar to gopher-auth-sdk-nodejs
 */
export class McpExpressAuth {
  private authClient: McpAuthClient;
  private config: AuthClientConfig;
  private tokenIssuer: string;
  private tokenAudience?: string;
  
  constructor(config?: Partial<AuthClientConfig> & { tokenAudience?: string }) {
    // Use environment variables if config not provided
    const env = process.env;
    const authServerUrl = env['GOPHER_AUTH_SERVER_URL'] || env['OAUTH_SERVER_URL'] || '';
    
    this.tokenIssuer = config?.issuer || env['TOKEN_ISSUER'] || authServerUrl;
    this.tokenAudience = config?.tokenAudience || env['TOKEN_AUDIENCE'];
    
    this.config = {
      jwksUri: config?.jwksUri || env['JWKS_URI'] || `${authServerUrl}/protocol/openid-connect/certs`,
      issuer: this.tokenIssuer,
      cacheDuration: config?.cacheDuration || parseInt(env['JWKS_CACHE_DURATION'] || '3600'),
      autoRefresh: config?.autoRefresh ?? (env['JWKS_AUTO_REFRESH'] === 'true'),
      requestTimeout: config?.requestTimeout || parseInt(env['REQUEST_TIMEOUT'] || '10'),
    };
    
    this.authClient = new McpAuthClient(this.config);
  }
  
  /**
   * Register OAuth proxy routes on Express app
   * This handles OAuth discovery, metadata, and client registration
   * 
   * @param app Express application
   * @param options OAuth proxy options
   */
  registerOAuthRoutes(app: Express, options: OAuthProxyOptions): void {
    const { serverUrl, allowedScopes = ['openid'] } = options;
    
    // Protected Resource Metadata (RFC 9728)
    app.get('/.well-known/oauth-protected-resource', (_req: Request, res: Response) => {
      res.json({
        resource: serverUrl,
        authorization_servers: [`${serverUrl}/oauth`],
        scopes_supported: allowedScopes,
        bearer_methods_supported: ['header', 'query'],
        resource_documentation: `${serverUrl}/docs`,
      });
    });
    
    // OAuth Authorization Server Metadata proxy (RFC 8414)
    app.get('/.well-known/oauth-authorization-server', async (_req: Request, res: Response) => {
      try {
        const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
        const discoveryUrl = `${authServerUrl}/.well-known/openid-configuration`;
        
        // Fetch from auth server
        const response = await fetch(discoveryUrl);
        const data = await response.json() as any;
        
        // Override endpoints to use our proxy
        data.issuer = `${serverUrl}/oauth`;
        data.authorization_endpoint = `${serverUrl}/oauth/authorize`;
        data.token_endpoint = `${serverUrl}/oauth/token`;
        data.registration_endpoint = `${serverUrl}/realms/gopher-auth/clients-registrations/openid-connect`;
        
        // Keep other endpoints from Keycloak
        data.jwks_uri = data.jwks_uri || `${authServerUrl}/protocol/openid-connect/certs`;
        data.userinfo_endpoint = data.userinfo_endpoint || `${authServerUrl}/protocol/openid-connect/userinfo`;
        data.end_session_endpoint = data.end_session_endpoint || `${authServerUrl}/protocol/openid-connect/logout`;
        
        // Filter scopes if needed
        if (data.scopes_supported) {
          data.scopes_supported = data.scopes_supported.filter(
            (scope: string) => allowedScopes.includes(scope)
          );
        }
        
        // Set CORS headers
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
        res.header('Access-Control-Allow-Headers', '*');
        
        res.json(data);
      } catch (error: any) {
        res.status(500).json({ error: error.message });
      }
    });
    
    // OAuth discovery endpoint at /oauth/.well-known/oauth-authorization-server
    app.get('/oauth/.well-known/oauth-authorization-server', async (_req: Request, res: Response) => {
      try {
        const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
        const discoveryUrl = `${authServerUrl}/.well-known/openid-configuration`;
        
        // Fetch from auth server
        const response = await fetch(discoveryUrl);
        const data = await response.json() as any;
        
        // Override endpoints to use our proxy
        data.issuer = `${serverUrl}/oauth`;
        data.authorization_endpoint = `${serverUrl}/oauth/authorize`;
        data.token_endpoint = `${serverUrl}/oauth/token`;
        data.registration_endpoint = `${serverUrl}/realms/gopher-auth/clients-registrations/openid-connect`;
        
        // Keep other endpoints from Keycloak
        data.jwks_uri = data.jwks_uri || `${authServerUrl}/protocol/openid-connect/certs`;
        data.userinfo_endpoint = data.userinfo_endpoint || `${authServerUrl}/protocol/openid-connect/userinfo`;
        data.end_session_endpoint = data.end_session_endpoint || `${authServerUrl}/protocol/openid-connect/logout`;
        
        // Filter scopes if needed
        if (data.scopes_supported) {
          data.scopes_supported = data.scopes_supported.filter(
            (scope: string) => allowedScopes.includes(scope)
          );
        }
        
        // Set CORS headers
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
        res.header('Access-Control-Allow-Headers', '*');
        
        res.json(data);
      } catch (error: any) {
        res.status(500).json({ error: error.message });
      }
    });
    
    // Client registration proxy
    app.post('/realms/:realm/clients-registrations/openid-connect', async (req: Request, res: Response) => {
      try {
        const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
        const registrationUrl = `${authServerUrl}/clients-registrations/openid-connect`;
        
        const registrationRequest = { ...req.body };
        
        // Filter requested scopes
        if (registrationRequest.scope) {
          const requestedScopes = registrationRequest.scope.split(' ');
          const filteredScopes = requestedScopes.filter(
            (scope: string) => allowedScopes.includes(scope)
          );
          registrationRequest.scope = filteredScopes.join(' ');
        }
        
        // Forward to auth server
        const response = await fetch(registrationUrl, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            ...(req.headers['authorization'] ? { 'Authorization': req.headers['authorization'] as string } : {}),
          },
          body: JSON.stringify(registrationRequest),
        });
        
        const data = await response.json() as any;
        
        // Set CORS headers
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
        res.header('Access-Control-Allow-Headers', '*');
        
        res.status(response.status).json(data);
      } catch (error: any) {
        res.status(500).json({ error: error.message });
      }
    });
    
    // OAuth Authorization endpoint - redirects to Keycloak login
    app.get('/oauth/authorize', async (req: Request, res: Response) => {
      const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
      
      // Build authorization URL with all query params
      const params = new URLSearchParams(req.query as any);
      const authorizationUrl = `${authServerUrl}/protocol/openid-connect/auth?${params.toString()}`;
      
      console.log(`🔐 OAuth authorize redirect to: ${authorizationUrl}`);
      
      // Redirect to Keycloak login page
      res.redirect(authorizationUrl);
    });
    
    // OAuth Token endpoint - proxies to Keycloak token endpoint
    app.post('/oauth/token', async (req: Request, res: Response) => {
      try {
        const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
        const tokenUrl = `${authServerUrl}/protocol/openid-connect/token`;
        
        // Parse the body - could be JSON or form-encoded
        let tokenRequest: any;
        const contentType = req.headers['content-type'] || '';
        
        if (contentType.includes('application/json')) {
          tokenRequest = req.body;
        } else if (contentType.includes('application/x-www-form-urlencoded')) {
          // Body-parser should have parsed this already
          tokenRequest = req.body;
        } else {
          // Try to parse raw body as form data
          const rawBody = req.body;
          if (typeof rawBody === 'string') {
            tokenRequest = Object.fromEntries(new URLSearchParams(rawBody));
          } else {
            tokenRequest = rawBody || {};
          }
        }
        
        console.log(`🔑 OAuth token exchange for grant_type: ${tokenRequest.grant_type || 'unknown'}`);
        
        // Forward to Keycloak token endpoint
        const response = await fetch(tokenUrl, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
            ...(req.headers['authorization'] ? { 'Authorization': req.headers['authorization'] as string } : {}),
          },
          body: new URLSearchParams(tokenRequest).toString(),
        });
        
        const data = await response.json() as any;
        
        // Set CORS headers
        res.header('Access-Control-Allow-Origin', '*');
        res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
        res.header('Access-Control-Allow-Headers', '*');
        
        console.log(`🔑 Token response status: ${response.status}`);
        if (!response.ok) {
          console.error('Token error response:', data);
        } else if (data.access_token) {
          console.log(`✅ Token issued successfully for grant_type: ${tokenRequest.grant_type}`);
          console.log(`   Token preview: ${data.access_token.substring(0, 20)}...`);
        }
        
        res.status(response.status).json(data);
      } catch (error: any) {
        console.error('Token exchange error:', error);
        res.status(500).json({ error: 'token_exchange_failed', error_description: error.message });
      }
    });
    
    // Handle OPTIONS for CORS
    app.options('/.well-known/oauth-protected-resource', (_req: Request, res: Response) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(200).send();
    });
    
    app.options('/.well-known/oauth-authorization-server', (_req: Request, res: Response) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(200).send();
    });
    
    app.options('/oauth/.well-known/oauth-authorization-server', (_req: Request, res: Response) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(200).send();
    });
    
    app.options('/oauth/authorize', (_req: Request, res: Response) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(200).send();
    });
    
    app.options('/oauth/token', (_req: Request, res: Response) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(200).send();
    });
    
    app.options('/realms/:realm/clients-registrations/openid-connect', (_req: Request, res: Response) => {
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(200).send();
    });
  }
  
  /**
   * Create Express middleware for OAuth token validation
   * 
   * @param options Middleware options
   * @returns Express middleware handler
   */
  expressMiddleware(options: ExpressMiddlewareOptions = {}): RequestHandler {
    return async (req: Request, res: Response, next: NextFunction) => {
      try {
        // Check if path is public
        if (options.publicPaths?.some(path => req.path.includes(path))) {
          console.log(`✅ Public path: ${req.path}`);
          return next();
        }
        
        console.log(`🔐 Auth check for: ${req.method} ${req.path} [method: ${req.body?.method || 'N/A'}]`);
        console.log(`   Headers: Authorization=${req.headers['authorization'] ? 'present' : 'missing'}, Cookie=${req.headers['cookie'] ? 'present' : 'missing'}`);
        
        // Extract token from Authorization header or query parameter
        const authHeader = req.headers['authorization'];
        const queryToken = (req.query as any)?.access_token;
        const token = authHeader?.split('Bearer ')[1]?.trim() || queryToken;
        
        // GET requests (SSE) - need authentication but no method-level checks
        if (req.method === 'GET') {
          if (!token) {
            console.log(`❌ No token provided for GET ${req.path}`);
            return res.status(401)
              .set('WWW-Authenticate', this.getWWWAuthenticateHeader())
              .json({
                error: 'Unauthorized',
                message: 'Authentication required for SSE connection',
                help: 'Add an Authorization header with a Bearer token. Run ./get-token-password.sh to obtain one.',
                documentation: 'See mcp-inspector-setup.md for detailed instructions'
              });
          }
          
          // Validate token
          const result = await this.authClient.validateToken(token, {
            audience: typeof options.audience === 'string' ? options.audience : 
                     Array.isArray(options.audience) ? options.audience[0] : undefined,
          });
          
          if (!result.valid) {
            return res.status(401)
              .set('WWW-Authenticate', this.getWWWAuthenticateHeader())
              .json({
                error: 'Unauthorized',
                message: result.errorMessage || 'Invalid token'
              });
          }
          
          // Extract and attach auth payload
          console.log('Validation successful, extracting payload...');
          const payload = await this.authClient.extractPayload(token);
          console.log('Payload extracted successfully:', payload);
          (req as any).auth = payload;
          return next();
        }
        
        // POST requests - check method-level permissions
        const method = req.body?.method;
        
        // Check if method is public
        if (options.publicMethods?.includes(method)) {
          console.log(`✅ Public method: ${method}`);
          return next();
        }
        
        if (!token) {
          console.log(`❌ No token provided for POST ${req.path} method: ${method}`);
          return res.status(401)
            .set('WWW-Authenticate', this.getWWWAuthenticateHeader())
            .json({
              error: 'Unauthorized',
              message: 'Authentication required',
              help: 'To connect with MCP Inspector, you need to add an Authorization header with a Bearer token. Run ./get-token-password.sh to obtain a token.',
              documentation: 'See mcp-inspector-setup.md for detailed instructions'
            });
        }
        
        // Determine required scopes for this tool
        let requiredScopes: string[] | undefined;
        if (method === 'tools/call') {
          const toolName = req.body?.params?.name;
          requiredScopes = options.toolScopes?.[toolName];
        }
        
        // Validate token with scopes
        const result = await this.authClient.validateToken(token, {
          audience: typeof options.audience === 'string' ? options.audience : 
                   Array.isArray(options.audience) ? options.audience[0] : undefined,
          scopes: requiredScopes?.join(' '),
        });
        
        if (!result.valid) {
          const wwwAuth = this.getWWWAuthenticateHeader({
            error: 'invalid_token',
            errorDescription: result.errorMessage,
          });
          
          return res.status(401)
            .set('WWW-Authenticate', wwwAuth)
            .json({
              error: 'Unauthorized',
              message: result.errorMessage || 'Invalid token'
            });
        }
        
        // Extract and attach auth payload to request
        const payload = await this.authClient.extractPayload(token);
        (req as any).auth = payload;
        console.log(`✅ Authenticated: ${payload?.subject || 'unknown'} for method: ${method}`);
        next();
        
      } catch (error: any) {
        const wwwAuth = this.getWWWAuthenticateHeader({
          error: 'invalid_token',
          errorDescription: error.message,
        });
        
        return res.status(401)
          .set('WWW-Authenticate', wwwAuth)
          .json({
            error: 'Unauthorized',
            message: error.message
          });
      }
    };
  }
  
  /**
   * Generate WWW-Authenticate header for 401 responses
   */
  private getWWWAuthenticateHeader(options?: {
    error?: string;
    errorDescription?: string;
  }): string {
    let header = 'Bearer realm="OAuth"';
    
    if (options?.error) {
      header += `, error="${options.error}"`;
    }
    
    if (options?.errorDescription) {
      header += `, error_description="${options.errorDescription}"`;
    }
    
    const serverUrl = process.env['SERVER_URL'] || 'http://localhost:3001';
    header += `, resource_metadata="${serverUrl}/.well-known/oauth-protected-resource"`;
    
    return header;
  }
  
  /**
   * Get the auth client instance
   */
  getAuthClient(): McpAuthClient {
    return this.authClient;
  }
}