/**
 * Express adapter for Gopher Auth SDK
 * Provides Express-specific integration without coupling SDK to Express
 */

import { Request, Response, NextFunction, Router } from 'express';
import { OAuthHelper } from '../src/oauth-helper.js';

export interface ExpressOAuthConfig {
  oauth: OAuthHelper;
  clientId?: string;
  clientSecret?: string;
  redirectUris?: string[];
  scopes?: string[];
  allowedScopes?: string[];  // Scopes allowed for dynamic registration
  filterInvalidScopes?: boolean;  // Whether to filter invalid scopes
}

/**
 * Creates Express middleware for OAuth authentication
 */
export function createAuthMiddleware(oauth: OAuthHelper) {
  return async (req: Request, res: Response, next: NextFunction): Promise<void> => {
    try {
      // Extract token from multiple sources
      const token = oauth.extractToken(
        req.headers.authorization as string,
        req.query.access_token as string,
        req
      );

      // Validate token
      const result = await oauth.validateToken(token);
      
      if (!result.valid) {
        res.status(result.statusCode || 401)
          .set('WWW-Authenticate', result.wwwAuthenticate)
          .json({
            error: 'Unauthorized',
            message: result.error || 'Authentication required'
          });
        return;
      }

      // Attach auth payload to request
      (req as any).auth = result.payload;
      next();
    } catch (error: any) {
      const result = await oauth.validateToken(undefined);
      res.status(401)
        .set('WWW-Authenticate', result.wwwAuthenticate)
        .json({
          error: 'Unauthorized',
          message: error.message
        });
      return;
    }
  };
}

/**
 * Creates Express router with all required OAuth proxy endpoints
 * These are required for MCP Inspector compatibility
 */
export function createOAuthRouter(config: ExpressOAuthConfig): Router {
  const router = Router();
  const { oauth, clientId, clientSecret, redirectUris, scopes } = config;
  
  const CLIENT_ID = clientId || process.env.GOPHER_CLIENT_ID || '';
  const CLIENT_SECRET = clientSecret || process.env.GOPHER_CLIENT_SECRET || '';
  // Build redirect URIs based on the actual server URL
  const serverUrl = oauth.getServerUrl();
  const REDIRECT_URIS = redirectUris || [
    `${serverUrl}/oauth/callback`,
    // Common MCP Inspector ports
    'http://localhost:6274/oauth/callback',
    'http://localhost:6275/oauth/callback',
    'http://localhost:6276/oauth/callback',
    'http://127.0.0.1:6274/oauth/callback',
    'http://127.0.0.1:6275/oauth/callback',
    'http://127.0.0.1:6276/oauth/callback',
  ];

  // OAuth Discovery endpoint with scope filtering
  router.get('/.well-known/oauth-authorization-server', async (_req, res) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    try {
      const metadata = await oauth.getDiscoveryMetadata();
      
      // Filter scopes_supported to allowed scopes only
      const ALLOWED_SCOPES = config.allowedScopes || scopes || ['openid', 'profile', 'email'];
      if (metadata.scopes_supported) {
        const filteredScopes = [...new Set(
          metadata.scopes_supported.filter((scope: string) => 
            typeof scope === 'string' && ALLOWED_SCOPES.includes(scope)
          )
        )];
        metadata.scopes_supported = filteredScopes;
      }
      
      res.json(metadata);
    } catch (error: any) {
      res.status(500).json({ error: error.message });
    }
  });

  // Dynamic Client Registration endpoint (Keycloak path format)
  router.post('/realms/:realm/clients-registrations/openid-connect', async (req, res) => {
    try {
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL;
      const keycloakUrl = `${authServerUrl}/clients-registrations/openid-connect`;
      const registrationRequest = { ...req.body };
      
      console.log(`🔄 Proxying dynamic client registration to Keycloak`);
      console.log(`📝 Original scope: ${registrationRequest.scope}`);
      
      // Default allowed scopes if not configured
      const ALLOWED_SCOPES = config.allowedScopes || scopes || ['openid', 'profile', 'email'];
      
      // Filter requested scopes
      if (registrationRequest.scope) {
        const requestedScopes = registrationRequest.scope.split(' ');
        const filteredScopes = [...new Set(
          requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
        )];
        
        const removedScopes = requestedScopes.filter((scope: string) => !ALLOWED_SCOPES.includes(scope));
        if (removedScopes.length > 0) {
          console.log(`🗑️  Filtered out invalid scopes: ${removedScopes.join(', ')}`);
        }
        
        registrationRequest.scope = filteredScopes.join(' ');
        console.log(`✅ Filtered scope: ${registrationRequest.scope}`);
      } else {
        // Default to all allowed scopes when no scope provided
        registrationRequest.scope = ALLOWED_SCOPES.join(' ');
        console.log(`✅ Defaulted scope to: ${registrationRequest.scope}`);
      }
      
      // Forward to Keycloak
      const response = await fetch(keycloakUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...(req.headers['authorization'] ? { 'Authorization': req.headers['authorization'] as string } : {}),
        },
        body: JSON.stringify(registrationRequest),
      });
      
      const data = await response.json() as any;
      
      // Deduplicate response scopes
      if (response.ok && data.scope) {
        const responseScopes = data.scope.split(' ');
        const filteredResponseScopes = [...new Set(responseScopes)];
        if (responseScopes.length !== filteredResponseScopes.length) {
          data.scope = filteredResponseScopes.join(' ');
          console.log(`🔧 Deduplicated response scopes`);
        }
      }
      
      // Set CORS headers
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      
      console.log(`✅ Registered client: ${data.client_id}`);
      res.status(response.status).json(data);
    } catch (error: any) {
      console.error(`❌ Error registering client: ${error.message}`);
      res.status(500).json({ error: error.message });
    }
  });
  
  // Legacy registration endpoint (returns pre-configured client for backward compatibility)
  router.post('/register', async (req, res) => {
    // MCP Inspector expects dynamic registration, but we return pre-configured
    if (req.body.client_name === 'MCP Inspector' || req.body.client_name === 'Test Client') {
      // Check if client wants public authentication (no secret)
      const wantsPublic = req.body.token_endpoint_auth_method === 'none';
      
      const client = {
        client_id: CLIENT_ID,
        // Only include secret if not requesting public client
        ...(wantsPublic ? {} : { client_secret: CLIENT_SECRET }),
        redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
        token_endpoint_auth_method: wantsPublic ? 'none' : 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        client_name: req.body.client_name,
        scope: req.body.scope || ['openid', 'profile', 'email', ...(scopes || [])].join(' '),
      };
      return res.status(201).json(client);
    }
    
    // For other clients, could implement actual registration
    return res.status(400).json({ error: 'Registration not supported' });
  });
  
  // Also add /oauth/register endpoint (MCP Inspector may use this)
  router.post('/oauth/register', async (req, res) => {
    console.log('OAuth registration request:', req.body);
    
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    // MCP Inspector expects dynamic registration, but we return pre-configured
    if (req.body.client_name === 'MCP Inspector' || req.body.client_name === 'Test Client') {
      // Check if client wants public authentication (no secret)
      const wantsPublic = req.body.token_endpoint_auth_method === 'none';
      
      const client = {
        client_id: CLIENT_ID,
        // Only include secret if not requesting public client
        ...(wantsPublic ? {} : { client_secret: CLIENT_SECRET }),
        redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
        token_endpoint_auth_method: wantsPublic ? 'none' : 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        client_name: req.body.client_name,
        scope: req.body.scope || ['openid', 'profile', 'email', ...(scopes || [])].join(' '),
      };
      console.log('Returning client config:', {
        client_id: client.client_id,
        auth_method: client.token_endpoint_auth_method,
        has_secret: !!client.client_secret
      });
      return res.status(201).json(client);
    }
    
    // For other clients, could implement actual registration
    return res.status(400).json({ error: 'Registration not supported' });
  });

  // Authorization endpoint (redirect to Keycloak)
  router.get('/authorize', (req, res) => {
    // Ensure client_id is present
    const queryParams = { ...req.query } as Record<string, string>;
    if (!queryParams.client_id) {
      queryParams.client_id = CLIENT_ID;
    }
    
    // For dynamic clients, preserve their redirect_uri exactly as registered
    // Only use default for our pre-configured client
    if (!queryParams.redirect_uri) {
      // Only set default if using our pre-configured client
      if (queryParams.client_id === CLIENT_ID) {
        queryParams.redirect_uri = `${oauth.getServerUrl()}/oauth/callback`;
        console.log('No redirect_uri provided for pre-configured client, using default:', queryParams.redirect_uri);
      }
    } else {
      console.log('Client redirect_uri:', queryParams.redirect_uri);
      // For dynamic clients, DO NOT replace redirect_uri
      // They have registered their own redirect_uri with Keycloak
    }
    
    // Don't modify scopes - let the client decide what scopes to request
    // The scopes should be passed in by the client application
    
    // Force consent screen to appear
    queryParams.prompt = 'consent';
    
    // Log the authorization request details
    console.log('Authorization request:');
    console.log('  Client ID:', queryParams.client_id);
    console.log('  Redirect URI:', queryParams.redirect_uri);
    console.log('  Scopes:', queryParams.scope);
    console.log('  State:', queryParams.state);
    if (queryParams.code_challenge) {
      console.log('  PKCE Challenge:', queryParams.code_challenge);
      console.log('  PKCE Method:', queryParams.code_challenge_method || 'plain');
    }
    
    // Store PKCE verifier if provided
    const codeVerifier = queryParams.code_verifier || 'default-verifier';
    res.cookie('code_verifier', codeVerifier, { 
      httpOnly: true, 
      maxAge: 10 * 60 * 1000 
    });
    
    // Build and redirect to authorization URL
    const authUrl = oauth.buildAuthorizationUrl(queryParams);
    console.log('Redirecting to Keycloak:', authUrl);
    res.redirect(authUrl);
  });

  // Token endpoint (proxy to Keycloak)
  router.post('/token', async (req, res) => {
    try {
      // Extract credentials from Basic auth if present
      let clientIdFromAuth: string | undefined;
      let clientSecretFromAuth: string | undefined;
      
      if (req.headers.authorization?.startsWith('Basic ')) {
        const decoded = Buffer.from(
          req.headers.authorization.substring(6), 
          'base64'
        ).toString();
        [clientIdFromAuth, clientSecretFromAuth] = decoded.split(':');
      }
      
      // Build token request
      const tokenRequest = {
        ...req.body,
        client_id: req.body.client_id || clientIdFromAuth || CLIENT_ID,
        client_secret: req.body.client_secret || clientSecretFromAuth || CLIENT_SECRET,
      };
      
      // For dynamic clients, preserve redirect_uri exactly as sent
      // Dynamic clients have registered their own redirect_uris with Keycloak
      // Only replace for our pre-configured client if needed
      if (tokenRequest.client_id === CLIENT_ID && !tokenRequest.redirect_uri) {
        const serverUrl = oauth.getServerUrl();
        tokenRequest.redirect_uri = `${serverUrl}/oauth/callback`;
        console.log('Added default redirect_uri for pre-configured client');
      }
      
      console.log('Token exchange:', {
        client_id: tokenRequest.client_id,
        redirect_uri: tokenRequest.redirect_uri,
        grant_type: tokenRequest.grant_type
      });
      
      // Exchange token
      const tokenResponse = await oauth.exchangeToken(tokenRequest);
      res.json(tokenResponse);
    } catch (error: any) {
      res.status(400).json({
        error: 'invalid_grant',
        error_description: error.message
      });
    }
  });

  // OAuth callback endpoint (optional - for session-based auth)
  router.get('/callback', async (req, res) => {
    const { code, state, error } = req.query;
    
    if (error) {
      return res.status(400).send(`OAuth Error: ${error}`);
    }
    
    if (!code) {
      return res.status(400).send('No authorization code received');
    }
    
    // Check if we need to redirect back to MCP Inspector
    const mcpInspectorRedirect = req.cookies?.mcp_inspector_redirect;
    if (mcpInspectorRedirect) {
      console.log('Redirecting back to MCP Inspector:', mcpInspectorRedirect);
      res.clearCookie('mcp_inspector_redirect');
      res.clearCookie('mcp_inspector_verifier');
      
      // Redirect to MCP Inspector's callback with the authorization code
      const redirectUrl = new URL(mcpInspectorRedirect);
      redirectUrl.searchParams.set('code', code as string);
      redirectUrl.searchParams.set('state', state as string);
      
      return res.redirect(redirectUrl.toString());
    }
    
    // Otherwise handle our own callback
    const codeVerifier = req.cookies?.code_verifier || 'default-verifier';
    
    try {
      const result = await oauth.handleOAuthCallback(
        code as string,
        state as string,
        codeVerifier,
        res
      );
      
      if (result.success) {
        res.clearCookie('code_verifier');
        
        // Show success page
        res.send(`
          <!DOCTYPE html>
          <html>
          <head>
            <title>Authentication Successful</title>
            <style>
              body { font-family: system-ui; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
              .container { text-align: center; }
              button { padding: 10px 20px; font-size: 16px; cursor: pointer; }
            </style>
          </head>
          <body>
            <div class="container">
              <h1>✅ Authentication Successful</h1>
              <p>You can close this window and return to MCP Inspector.</p>
              <button onclick="window.close()">Close Window</button>
            </div>
            <script>setTimeout(() => window.close(), 3000);</script>
          </body>
          </html>
        `);
      } else {
        res.status(500).send(`Authentication failed: ${result.error}`);
      }
    } catch (error: any) {
      res.status(500).send(`Error: ${error.message}`);
    }
  });

  return router;
}

/**
 * Sets up all required MCP OAuth endpoints on an Express app
 * This is the simplest integration method
 */
export function setupMCPOAuth(
  app: any,
  oauth: OAuthHelper,
  mcpHandler: (req: Request, res: Response) => Promise<void>,
  scopes?: string[]
) {
  // OAuth protected resource metadata (required by MCP spec)
  app.get('/.well-known/oauth-protected-resource', async (_req: Request, res: Response) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    try {
      const metadata = await oauth.generateProtectedResourceMetadata();
      res.json(metadata);
    } catch (error: any) {
      res.status(500).json({ error: error.message });
    }
  });

  // Root-level discovery endpoint for compatibility
  app.get('/.well-known/oauth-authorization-server', async (_req: Request, res: Response) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    try {
      const metadata = await oauth.getDiscoveryMetadata();
      
      // Filter scopes_supported to allowed scopes only
      const ALLOWED_SCOPES = scopes || ['openid', 'profile', 'email'];
      if (metadata.scopes_supported) {
        const filteredScopes = [...new Set(
          metadata.scopes_supported.filter((scope: string) => 
            typeof scope === 'string' && ALLOWED_SCOPES.includes(scope)
          )
        )];
        metadata.scopes_supported = filteredScopes;
      }
      
      res.json(metadata);
    } catch (error: any) {
      res.status(500).json({ error: error.message });
    }
  });
  
  // OpenID Configuration endpoint (MCP Inspector may use this)
  app.get('/.well-known/openid-configuration', async (_req: Request, res: Response) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    try {
      const metadata = await oauth.getDiscoveryMetadata();
      
      // Filter scopes_supported to allowed scopes only
      const ALLOWED_SCOPES = scopes || ['openid', 'profile', 'email'];
      if (metadata.scopes_supported) {
        const filteredScopes = [...new Set(
          metadata.scopes_supported.filter((scope: string) => 
            typeof scope === 'string' && ALLOWED_SCOPES.includes(scope)
          )
        )];
        metadata.scopes_supported = filteredScopes;
      }
      
      res.json(metadata);
    } catch (error: any) {
      res.status(500).json({ error: error.message });
    }
  });

  // OAuth proxy endpoints (required for MCP Inspector)
  app.use('/oauth', createOAuthRouter({ oauth, scopes }));
  
  // Dynamic Client Registration endpoint at root level (Keycloak path format)
  app.post('/realms/:realm/clients-registrations/openid-connect', async (req: Request, res: Response) => {
    try {
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL;
      const keycloakUrl = `${authServerUrl}/clients-registrations/openid-connect`;
      const registrationRequest = { ...req.body };
      
      console.log(`🔄 Proxying dynamic client registration to Keycloak`);
      console.log(`📝 Original scope: ${registrationRequest.scope}`);
      
      // Default allowed scopes if not configured
      const ALLOWED_SCOPES = scopes || ['openid', 'profile', 'email'];
      
      // Filter requested scopes
      if (registrationRequest.scope) {
        const requestedScopes = registrationRequest.scope.split(' ');
        const filteredScopes = [...new Set(
          requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
        )];
        
        const removedScopes = requestedScopes.filter((scope: string) => !ALLOWED_SCOPES.includes(scope));
        if (removedScopes.length > 0) {
          console.log(`🗑️  Filtered out invalid scopes: ${removedScopes.join(', ')}`);
        }
        
        registrationRequest.scope = filteredScopes.join(' ');
        console.log(`✅ Filtered scope: ${registrationRequest.scope}`);
      } else {
        // Default to all allowed scopes when no scope provided
        registrationRequest.scope = ALLOWED_SCOPES.join(' ');
        console.log(`✅ Defaulted scope to: ${registrationRequest.scope}`);
      }
      
      // Forward to Keycloak
      const response = await fetch(keycloakUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...(req.headers['authorization'] ? { 'Authorization': req.headers['authorization'] as string } : {}),
        },
        body: JSON.stringify(registrationRequest),
      });
      
      const data = await response.json() as any;
      
      // Deduplicate response scopes
      if (response.ok && data.scope) {
        const responseScopes = data.scope.split(' ');
        const filteredResponseScopes = [...new Set(responseScopes)];
        if (responseScopes.length !== filteredResponseScopes.length) {
          data.scope = filteredResponseScopes.join(' ');
          console.log(`🔧 Deduplicated response scopes`);
        }
      }
      
      // Set CORS headers
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      
      console.log(`✅ Registered client: ${data.client_id}`);
      res.status(response.status).json(data);
    } catch (error: any) {
      console.error(`❌ Error registering client: ${error.message}`);
      res.status(500).json({ error: error.message });
    }
  });

  // MCP endpoint with authentication
  app.all('/mcp', createAuthMiddleware(oauth), mcpHandler);
  
  // Test endpoints for authentication failure scenarios (development only)
  if (process.env.NODE_ENV !== 'production') {
    app.get('/test/clear-session', (_req: Request, res: Response) => {
      res.clearCookie('mcp_session');
      res.json({ 
        status: 'Session cleared',
        message: 'Next MCP request will fail with 401 Unauthorized',
        instruction: 'Try calling a tool in MCP Inspector now'
      });
    });
    
    app.get('/test/invalid-session', (_req: Request, res: Response) => {
      res.cookie('mcp_session', 'invalid_session_id_12345', {
        httpOnly: true,
        sameSite: 'lax',
        path: '/',
        maxAge: 60 * 1000
      });
      res.json({ 
        status: 'Invalid session set',
        message: 'Next MCP request will fail with invalid token error',
        instruction: 'Try calling a tool in MCP Inspector now'
      });
    });
  }

  // CORS options for all OAuth endpoints
  app.options([
    '/.well-known/oauth-protected-resource',
    '/.well-known/oauth-authorization-server',
    '/.well-known/openid-configuration',
    '/oauth/.well-known/oauth-authorization-server',
    '/oauth/authorize',
    '/oauth/token',
    '/oauth/register',
    '/oauth/callback',
    '/realms/:realm/clients-registrations/openid-connect'
  ], (_req: Request, res: Response) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    res.header('Access-Control-Allow-Credentials', 'true');
    res.sendStatus(200);
  });
}