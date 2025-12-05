/**
 * Express adapter for Gopher Auth SDK
 * Provides Express-specific integration without coupling SDK to Express
 */

import { Request, Response, NextFunction, Router } from 'express';
import { OAuthHelper } from '@mcp/filter-sdk/auth';

export interface ExpressOAuthConfig {
  oauth: OAuthHelper;
  clientId?: string;
  clientSecret?: string;
  redirectUris?: string[];
}

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
        req
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
 * Creates Express router with all required OAuth proxy endpoints
 * These are required for MCP Inspector compatibility
 */
export function createOAuthRouter(config: ExpressOAuthConfig): Router {
  const router = Router();
  const { oauth, clientId, clientSecret, redirectUris } = config;
  
  const CLIENT_ID = clientId || process.env.GOPHER_CLIENT_ID!;
  const CLIENT_SECRET = clientSecret || process.env.GOPHER_CLIENT_SECRET!;
  const REDIRECT_URIS = redirectUris || [
    'http://localhost:3000/oauth/callback',
    'http://localhost:3001/oauth/callback',
    'http://localhost:6274/oauth/callback',
    'http://localhost:6275/oauth/callback',
    'http://localhost:6276/oauth/callback',
    'http://127.0.0.1:6274/oauth/callback',
    'http://127.0.0.1:6275/oauth/callback',
    'http://127.0.0.1:6276/oauth/callback',
  ];

  // OAuth Discovery endpoint
  router.get('/.well-known/oauth-authorization-server', async (_req, res) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    try {
      const metadata = await oauth.getDiscoveryMetadata();
      res.json(metadata);
    } catch (error: any) {
      res.status(500).json({ error: error.message });
    }
  });

  // Client Registration endpoint (returns pre-configured client)
  router.post('/register', async (req, res) => {
    // MCP Inspector expects dynamic registration, but we return pre-configured
    if (req.body.client_name === 'MCP Inspector' || req.body.client_name === 'Test Client') {
      const client = {
        client_id: CLIENT_ID,
        client_secret: CLIENT_SECRET,
        redirect_uris: REDIRECT_URIS,
        token_endpoint_auth_method: 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        client_name: req.body.client_name,
        scope: req.body.scope || 'openid profile email mcp:weather',
      };
      return res.status(201).json(client);
    }
    
    // For other clients, could implement actual registration
    res.status(400).json({ error: 'Registration not supported' });
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
      const client = {
        client_id: CLIENT_ID,
        client_secret: CLIENT_SECRET,
        redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
        token_endpoint_auth_method: 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        client_name: req.body.client_name,
        scope: req.body.scope || 'openid profile email mcp:weather',
      };
      console.log('Returning pre-configured client:', client);
      return res.status(201).json(client);
    }
    
    // For other clients, could implement actual registration
    res.status(400).json({ error: 'Registration not supported' });
  });

  // Authorization endpoint (redirect to Keycloak)
  router.get('/authorize', (req, res) => {
    // Ensure client_id is present
    const queryParams = { ...req.query } as Record<string, string>;
    if (!queryParams.client_id) {
      queryParams.client_id = CLIENT_ID;
    }
    
    // Ensure mcp:weather scope is included
    if (!queryParams.scope) {
      queryParams.scope = 'openid profile email mcp:weather';
    } else if (!queryParams.scope.includes('mcp:weather')) {
      queryParams.scope = queryParams.scope + ' mcp:weather';
    }
    
    // Force consent screen to appear
    queryParams.prompt = 'consent';
    
    // Log the redirect_uri being requested
    console.log('Authorization request redirect_uri:', queryParams.redirect_uri);
    console.log('Configured client_id:', queryParams.client_id || CLIENT_ID);
    console.log('Requested scopes:', queryParams.scope);
    console.log('Forcing consent with prompt=consent');
    
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
  mcpHandler: (req: Request, res: Response) => Promise<void>
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
      res.json(metadata);
    } catch (error: any) {
      res.status(500).json({ error: error.message });
    }
  });

  // OAuth proxy endpoints (required for MCP Inspector)
  app.use('/oauth', createOAuthRouter({ oauth }));

  // MCP endpoint with authentication
  app.all('/mcp', createAuthMiddleware(oauth), mcpHandler);

  // CORS options for all OAuth endpoints
  app.options([
    '/.well-known/oauth-protected-resource',
    '/.well-known/oauth-authorization-server',
    '/.well-known/openid-configuration',
    '/oauth/.well-known/oauth-authorization-server',
    '/oauth/authorize',
    '/oauth/token',
    '/oauth/register',
    '/oauth/callback'
  ], (_req: Request, res: Response) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    res.header('Access-Control-Allow-Credentials', 'true');
    res.sendStatus(200);
  });
}