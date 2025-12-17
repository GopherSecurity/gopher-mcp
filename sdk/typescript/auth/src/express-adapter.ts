/**
 * Express adapter for Gopher Auth SDK
 * Provides Express-specific integration without coupling SDK to Express
 */

import { Request, Response, NextFunction, Router } from 'express';
import { OAuthHelper } from '../src/oauth-helper.js';
import { McpAuthClient } from '../src/mcp-auth-api.js';

// Store dynamic client credentials for token exchange
const dynamicClientStore = new Map<string, { client_secret: string; timestamp: number }>();

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
  // Initialize auth client for C++ token exchange
  let authClient: McpAuthClient | null = null;
  
  return async (req: Request, res: Response, next: NextFunction): Promise<void> => {
    console.log(`[AUTH MIDDLEWARE] ${req.method} ${req.path} - Starting authentication`);
    
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

      // Attach auth payload and token to request
      (req as any).auth = result.payload;
      (req as any).authToken = token;
      
      // Try token exchange if EXCHANGE_IDPS is configured
      const exchangeIdps = process.env.EXCHANGE_IDPS;
      
      // IMPORTANT: Token exchange MUST use the pre-configured confidential client
      // Dynamic clients registered by MCP Inspector are public clients (no secret)
      // and cannot perform token exchange. This follows the Node.js SDK pattern.
      const clientIdForExchange = process.env.GOPHER_CLIENT_ID;
      const clientSecretForExchange = process.env.GOPHER_CLIENT_SECRET || '';
      
      console.log(`[AUTH MIDDLEWARE] Using pre-configured client for token exchange: ${clientIdForExchange}`);
      
      // Allow token exchange even without client_secret (for public clients)
      if (exchangeIdps && token && clientIdForExchange) {
        const idpList = exchangeIdps.split(',').map(idp => idp.trim()).filter(idp => idp);
        console.log(`[AUTH MIDDLEWARE] Attempting token exchange with client: ${clientIdForExchange}`);
        
        // Store all external tokens in a map
        const externalTokens: Record<string, any> = {};
        
        // Initialize C++ auth client if not already done
        if (!authClient) {
          const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
          console.log(`[C++ AUTH CLIENT] Initializing with auth server: ${authServerUrl}`);
          
          const authConfig = {
            jwksUri: `${authServerUrl}/protocol/openid-connect/certs`,
            issuer: authServerUrl
          };
          authClient = new McpAuthClient(authConfig);
          await authClient.initialize();
          console.log(`[C++ AUTH CLIENT] Initialized successfully`);
        }
        
        // Set up C++ auth client for token exchange
        const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
        const tokenEndpoint = `${authServerUrl}/protocol/openid-connect/token`;
        
        if (clientIdForExchange) {
          console.log(`[C++ AUTH CLIENT] Setting credentials for client: ${clientIdForExchange}`);
          authClient.setClientCredentials(clientIdForExchange, clientSecretForExchange);
          authClient.setTokenEndpoint(tokenEndpoint);
        }
        
        // Exchange tokens for all configured IDPs in parallel using C++ implementation
        const exchangePromises = idpList.map(async (idpAlias) => {
          console.log(`🔄 Attempting EXTERNAL IDP token exchange for: ${idpAlias} (C++ implementation)`);
          
          try {
            if (!authClient) {
              throw new Error('Auth client not initialized');
            }
            
            const exchangeResult = await authClient.exchangeToken(
              token,
              idpAlias
            ) as any;
            
            externalTokens[idpAlias] = {
              access_token: exchangeResult.access_token,
              token_type: exchangeResult.token_type || 'Bearer',
              expires_in: exchangeResult.expires_in
            };
            
            console.log(`✅ EXTERNAL IDP token exchange successful!`);
            console.log(`   IDP: ${idpAlias}`);
            console.log(`   Token Type: ${exchangeResult.token_type || 'Bearer'}`);
            if (exchangeResult.expires_in) {
              console.log(`   Expires In: ${exchangeResult.expires_in}s`);
            }
            console.log(`   Access Token:`);
            console.log(`   ${exchangeResult.access_token}`);
            
            return { idpAlias, success: true };
          } catch (error: any) {
            // Token exchange failed (e.g., user not linked to IDP) - continue anyway
            console.log(`❌ EXTERNAL IDP token exchange failed for: ${idpAlias}`);
            console.log(`   Error: ${error.message}`);
            if (error.message?.includes('not linked')) {
              console.log(`   Note: User account is not linked to ${idpAlias}`);
              console.log(`   Solution: Authenticate through ${idpAlias} when logging in`);
            }
            return { idpAlias, success: false, error: error.message };
          }
        });
        
        // Wait for all exchanges to complete
        const results = await Promise.all(exchangePromises);
        
        // Store all successful external tokens in request
        if (Object.keys(externalTokens).length > 0) {
          (req as any).externalTokens = externalTokens;
          
          // For backward compatibility, also store the first successful token
          const firstSuccessful = Object.keys(externalTokens)[0];
          if (firstSuccessful) {
            (req as any).externalToken = externalTokens[firstSuccessful].access_token;
            (req as any).externalTokenType = externalTokens[firstSuccessful].token_type;
            (req as any).externalIDP = firstSuccessful;
          }
          
          // Log summary
          const successful = results.filter(r => r.success).map(r => r.idpAlias);
          const failed = results.filter(r => !r.success).map(r => r.idpAlias);
          
          if (successful.length > 0) {
            console.log(`✅ Token exchange completed: ${successful.length} succeeded`);
            console.log(`   Successful IDPs: ${successful.join(', ')}`);
          }
          if (failed.length > 0) {
            console.log(`   Failed IDPs: ${failed.join(', ')}`);
          }
        }
      }
      
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
  
  // Initialize auth client for C++ token exchange
  let authClient: McpAuthClient | null = null;
  
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
      
      
      // Default allowed scopes if not configured
      const ALLOWED_SCOPES = config.allowedScopes || scopes || ['openid', 'profile', 'email'];
      
      // Filter requested scopes
      if (registrationRequest.scope) {
        const requestedScopes = registrationRequest.scope.split(' ');
        const filteredScopes = [...new Set(
          requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
        )];
        
        registrationRequest.scope = filteredScopes.join(' ');
      } else {
        // Default to all allowed scopes when no scope provided
        registrationRequest.scope = ALLOWED_SCOPES.join(' ');
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
        data.scope = filteredResponseScopes.join(' ');
      }
      
      // Set CORS headers
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      
      console.log(`✅ Registered client: ${data.client_id}`);
      console.log(`📋 Registration response keys:`, Object.keys(data));
      console.log(`🔐 Client secret present:`, data.client_secret ? 'YES' : 'NO');
      
      // Debug: Check if we're actually going into the storage block
      console.log(`🔍 Checking data.client_id:`, data.client_id, typeof data.client_id);
      
      // Store dynamic client for token exchange
      if (data.client_id) {
        dynamicClientStore.set(data.client_id, {
          client_secret: data.client_secret || '',
          timestamp: Date.now()
        });
        console.log(`✅ Dynamic client registered and stored: ${data.client_id}`);
      }
      
      res.status(response.status).json(data);
    } catch (error: any) {
      console.error(`❌ Error registering client: ${error.message}`);
      res.status(500).json({ error: error.message });
    }
  });
  
  // Dynamic registration endpoint
  router.post('/register', async (req, res) => {
    // Generate a unique dynamic client ID for each registration
    const dynamicClientId = `mcp_dynamic_${Date.now()}_${Math.random().toString(36).substring(7)}`;
    
    // MCP Inspector expects dynamic registration
    if (req.body.client_name === 'MCP Inspector' || req.body.client_name === 'Test Client') {
      // Check if client wants public authentication (no secret)
      const wantsPublic = req.body.token_endpoint_auth_method === 'none';
      
      const client = {
        client_id: dynamicClientId,
        // Only include secret if not requesting public client
        ...(wantsPublic ? {} : { client_secret: CLIENT_SECRET }),
        redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
        token_endpoint_auth_method: wantsPublic ? 'none' : 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        client_name: req.body.client_name,
        scope: req.body.scope || ['openid', 'profile', 'email', ...((scopes || []).filter((s: string) => !['openid', 'profile', 'email'].includes(s)))].join(' '),
      };
      
      // Store dynamic client for token exchange
      dynamicClientStore.set(dynamicClientId, {
        client_secret: wantsPublic ? '' : CLIENT_SECRET,
        timestamp: Date.now()
      });
      
      console.log(`[REGISTRATION] Registered dynamic client: ${dynamicClientId}`);
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
    
    // Generate a unique dynamic client ID for each registration
    const dynamicClientId = `mcp_dynamic_${Date.now()}_${Math.random().toString(36).substring(7)}`;
    
    // MCP Inspector expects dynamic registration
    if (req.body.client_name === 'MCP Inspector' || req.body.client_name === 'Test Client') {
      // Check if client wants public authentication (no secret)
      const wantsPublic = req.body.token_endpoint_auth_method === 'none';
      
      const client = {
        client_id: dynamicClientId,
        // Only include secret if not requesting public client
        ...(wantsPublic ? {} : { client_secret: CLIENT_SECRET }),
        redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
        token_endpoint_auth_method: wantsPublic ? 'none' : 'client_secret_basic',
        grant_types: ['authorization_code', 'refresh_token'],
        response_types: ['code'],
        client_name: req.body.client_name,
        scope: req.body.scope || ['openid', 'profile', 'email', ...((scopes || []).filter((s: string) => !['openid', 'profile', 'email'].includes(s)))].join(' '),
      };
      
      // Store dynamic client for token exchange
      dynamicClientStore.set(dynamicClientId, {
        client_secret: wantsPublic ? '' : CLIENT_SECRET,
        timestamp: Date.now()
      });
      
      console.log(`[OAUTH REGISTRATION] Registered dynamic client: ${dynamicClientId}`);
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
    
    // Log which client is being used for OAuth flow
    const clientId = queryParams.client_id;
    console.log(`[AUTHORIZE] OAuth flow initiated with client: ${clientId}`);
    
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
    
    // Store PKCE verifier and client_id for later use
    const codeVerifier = queryParams.code_verifier || 'default-verifier';
    res.cookie('code_verifier', codeVerifier, { 
      httpOnly: true, 
      maxAge: 10 * 60 * 1000 
    });
    
    // Store the client_id so we know which client this auth flow is for
    res.cookie('auth_client_id', queryParams.client_id, { 
      httpOnly: true, 
      maxAge: 10 * 60 * 1000 
    });
    console.log(`🔖 Storing auth flow client_id in cookie: ${queryParams.client_id}`);
    
    // Build and redirect to authorization URL
    const authUrl = oauth.buildAuthorizationUrl(queryParams);
    console.log('Redirecting to Keycloak:', authUrl);
    res.redirect(authUrl);
  });

  // Track last refresh time per client to prevent refresh loops
  const lastRefreshTime: Map<string, number> = new Map();
  
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
      
      // Log token request
      const clientId = tokenRequest.client_id;
      console.log(`[TOKEN] Token request from client: ${clientId}`);
      
      // Rate limit refresh tokens to prevent loops (minimum 30 seconds between refreshes)
      if (tokenRequest.grant_type === 'refresh_token') {
        const clientKey = `${tokenRequest.client_id}`;
        const lastRefresh = lastRefreshTime.get(clientKey) || 0;
        const timeSinceLastRefresh = Date.now() - lastRefresh;
        
        // More aggressive rate limiting - 30 seconds minimum
        if (timeSinceLastRefresh < 30000) {
          console.error(`🚫 BLOCKING REFRESH LOOP for client ${clientKey}`);
          console.error(`   Time since last refresh: ${timeSinceLastRefresh}ms`);
          console.error(`   Must wait ${30000 - timeSinceLastRefresh}ms before next refresh`);
          
          // Return the same token response structure but with extended expiry
          // This might trick MCP Inspector into not refreshing immediately
          return res.status(200).json({
            access_token: req.body.refresh_token, // Return same token
            token_type: 'Bearer',
            expires_in: 3600, // Claim it's valid for 1 hour instead of 5 minutes
            refresh_token: req.body.refresh_token,
            scope: 'openid profile email gopher:mcp01'
          });
        }
        
        lastRefreshTime.set(clientKey, Date.now());
        console.log(`✅ Allowing refresh for client ${clientKey} (last refresh was ${timeSinceLastRefresh}ms ago)`);
      }
      
      // For dynamic clients, preserve redirect_uri exactly as sent
      // Dynamic clients have registered their own redirect_uris with Keycloak
      // Only replace for our pre-configured client if needed
      if (tokenRequest.client_id === CLIENT_ID && !tokenRequest.redirect_uri) {
        const serverUrl = oauth.getServerUrl();
        tokenRequest.redirect_uri = `${serverUrl}/oauth/callback`;
        console.log('Added default redirect_uri for pre-configured client');
      }
      
      console.log(`[${new Date().toISOString()}] Token endpoint called:`, {
        client_id: tokenRequest.client_id,
        redirect_uri: tokenRequest.redirect_uri,
        grant_type: tokenRequest.grant_type,
        raw_grant_type: req.body.grant_type
      });
      
      // Check if this is a token exchange grant type (RFC 8693)
      if (tokenRequest.grant_type === 'urn:ietf:params:oauth:grant-type:token-exchange' || 
          req.body.grant_type === 'urn:ietf:params:oauth:grant-type:token-exchange') {
        console.log('[TOKEN EXCHANGE] RFC 8693 token exchange detected');
        
        // Validate that we're not processing the initial test token
        const subjectToken = req.body.subject_token;
        if (!subjectToken || subjectToken === 'test-token') {
          return res.status(400).json({
            error: 'invalid_grant',
            error_description: 'Invalid token'
          });
        }
        
        // Forward to the /token-exchange endpoint logic
        const exchangeIdps = process.env.EXCHANGE_IDPS || '';
        const configuredIdps = exchangeIdps ? exchangeIdps.split(',').map(idp => idp.trim()).filter(idp => idp) : [];
        const requested_issuer = req.body.requested_issuer || configuredIdps[0];
        
        if (!requested_issuer) {
          return res.status(400).json({
            error: 'invalid_request',
            error_description: 'requested_issuer parameter is required for token exchange',
            configured_idps: configuredIdps,
            hint: configuredIdps.length > 0 ? `Choose one of: ${configuredIdps.join(', ')}` : 'No IDPs configured in EXCHANGE_IDPS'
          });
        }
        
        // Redirect to /token-exchange endpoint
        req.headers.authorization = `Bearer ${subjectToken}`;
        req.body.requested_issuer = requested_issuer;
        req.body.requested_token_type = req.body.requested_token_type || 'access_token';
        req.body.audience = req.body.audience;
        req.body.scope = req.body.scope;
        
        // Call the token exchange handler directly
        const tokenExchangeHandler = router.stack.find((layer: any) => 
          layer.route?.path === '/token-exchange' && layer.route.methods.post
        )?.route?.stack[0]?.handle;
        
        if (tokenExchangeHandler) {
          return tokenExchangeHandler(req, res, () => {});
        }
        
        return res.status(500).json({
          error: 'server_error',
          error_description: 'Token exchange endpoint not configured'
        });
      }
      
      // Exchange token (standard OAuth flow)
      const tokenResponse = await oauth.exchangeToken(tokenRequest);
      
      // Override expires_in to reduce refresh frequency
      // MCP Inspector seems to be misinterpreting the 300 second expiry
      if (tokenResponse.expires_in && tokenResponse.expires_in < 3600) {
        console.log(`⚠️ Overriding short token expiry from ${tokenResponse.expires_in} to 3600 seconds`);
        tokenResponse.expires_in = 3600; // Set to 1 hour
      }
      
      res.json(tokenResponse);
    } catch (error: any) {
      res.status(400).json({
        error: 'invalid_grant',
        error_description: error.message
      });
    }
  });

  // List available IDPs for token exchange
  router.get('/idps', async (_req, res) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    // Get IDP configuration from EXCHANGE_IDPS
    const exchangeIdps = process.env.EXCHANGE_IDPS || '';
    const idpList = exchangeIdps ? exchangeIdps.split(',').map(idp => idp.trim()).filter(idp => idp) : [];
    
    // Map of known IDP details for documentation
    const knownIDPs: Record<string, { name: string; example_endpoint: string }> = {
      "google": { name: "Google OAuth 2.0", example_endpoint: "https://www.googleapis.com/oauth2/v1/userinfo" },
      "github": { name: "GitHub OAuth", example_endpoint: "https://api.github.com/user" },
      "microsoft": { name: "Microsoft/Azure AD", example_endpoint: "https://graph.microsoft.com/v1.0/me" },
      "facebook": { name: "Facebook Login", example_endpoint: "https://graph.facebook.com/me" },
      "gitlab": { name: "GitLab OAuth", example_endpoint: "https://gitlab.com/api/v4/user" },
      "apple": { name: "Apple Sign In", example_endpoint: "https://appleid.apple.com/auth/userinfo" },
      "linkedin": { name: "LinkedIn OAuth", example_endpoint: "https://api.linkedin.com/v2/me" },
      "twitter": { name: "Twitter/X OAuth", example_endpoint: "https://api.twitter.com/2/users/me" }
    };
    
    // Build IDP information for all configured IDPs
    const configuredIDPs = idpList.map(idpAlias => ({
      alias: idpAlias,
      name: knownIDPs[idpAlias]?.name || `Custom IDP (${idpAlias})`,
      example_endpoint: knownIDPs[idpAlias]?.example_endpoint || "https://your-idp.com/userinfo",
      configured: true
    }));
    
    const idps = {
      description: "Multi-IDP configuration for token exchange",
      configured_idps: configuredIDPs,
      exchange_idps: idpList,
      total_configured: idpList.length,
      configuration: {
        source: "Environment variable: EXCHANGE_IDPS (comma-separated)",
        keycloak: "Each alias must match what's configured in Keycloak Identity Providers",
        requirements: [
          "IDP must be configured in Keycloak with matching alias",
          "'Store Tokens' must be enabled in Keycloak IDP settings",
          "'Stored Tokens Readable' must be enabled in Keycloak IDP settings",
          "Token exchange feature must be enabled in Keycloak (--features=token-exchange)"
        ]
      },
      usage: {
        endpoint: "/oauth/token-exchange",
        method: "POST",
        headers: {
          "Authorization": "Bearer {keycloak_access_token}",
          "Content-Type": "application/json"
        },
        body: {
          "requested_issuer": "{idp_alias} - one of: " + (idpList.length > 0 ? idpList.join(', ') : "(no IDPs configured)"),
          "requested_token_type": "access_token (optional)",
          "audience": "{optional}",
          "scope": "{optional}"
        }
      }
    };
    
    res.json(idps);
  });

  // Token Exchange endpoint (RFC 8693)
  router.post('/token-exchange', async (req, res) => {
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    try {
      // Extract the bearer token from Authorization header
      const authHeader = req.headers.authorization;
      if (!authHeader?.startsWith('Bearer ')) {
        return res.status(401).json({
          error: 'unauthorized',
          error_description: 'Bearer token required for token exchange'
        });
      }
      
      const subjectToken = authHeader.substring(7);
      
      // Extract exchange parameters from request body
      const { requested_issuer, requested_token_type, audience, scope } = req.body;
      
      // Check if requested_issuer is provided
      const exchangeIdps = process.env.EXCHANGE_IDPS || '';
      const configuredIdps = exchangeIdps ? exchangeIdps.split(',').map(idp => idp.trim()).filter(idp => idp) : [];
      
      if (!requested_issuer) {
        return res.status(400).json({
          error: 'invalid_request',
          error_description: 'requested_issuer parameter is required',
          configured_idps: configuredIdps,
          hint: configuredIdps.length > 0 ? `Choose one of: ${configuredIdps.join(', ')}` : 'No IDPs configured in EXCHANGE_IDPS'
        });
      }
      
      const issuerToUse = requested_issuer;
      
      // Setup auth client for C++ token exchange if needed
      if (!authClient) {
        const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
        console.log(`[C++ AUTH INIT] authServerUrl: ${authServerUrl}`);
        
        const authConfig = {
          jwksUri: `${authServerUrl}/protocol/openid-connect/certs`,
          issuer: authServerUrl
        };
        console.log(`[C++ AUTH INIT] Config:`, JSON.stringify(authConfig, null, 2));
        
        try {
          authClient = new McpAuthClient(authConfig);
          console.log(`[C++ AUTH INIT] McpAuthClient created`);
          await authClient.initialize();
          console.log(`[C++ AUTH INIT] Auth client initialized successfully`);
        } catch (initError: any) {
          console.error(`[C++ AUTH INIT] Failed to initialize:`, initError);
          throw initError;
        }
      }
      
      // Extract client ID from the subject token to determine which credentials to use
      let tokenClientId: string | undefined;
      try {
        if (subjectToken) {
          const tokenParts = subjectToken.split('.');
          if (tokenParts.length === 3) {
            const decodedPayload = JSON.parse(Buffer.from(tokenParts[1], 'base64').toString());
            tokenClientId = decodedPayload.azp || decodedPayload.client_id || 
                           (typeof decodedPayload.aud === 'string' ? decodedPayload.aud : decodedPayload.aud?.[0]);
            console.log(`[TOKEN EXCHANGE] Token issued to client: ${tokenClientId}`);
          }
        }
      } catch (e) {
        console.error(`[TOKEN EXCHANGE] Failed to decode token:`, e);
      }
      
      // IMPORTANT: Token exchange MUST use the pre-configured confidential client
      // Token exchange in Keycloak requires a confidential client with a secret.
      // Dynamic clients from MCP Inspector are public clients and cannot do token exchange.
      const clientId = process.env.GOPHER_CLIENT_ID;
      const clientSecret = process.env.GOPHER_CLIENT_SECRET || '';
      
      console.log(`[TOKEN EXCHANGE] Using pre-configured client: ${clientId}`);
      console.log(`[TOKEN EXCHANGE] Token was issued by: ${tokenClientId}`);
      
      if (!clientSecret) {
        console.error(`[TOKEN EXCHANGE] WARNING: No client secret configured - token exchange will fail`);
      }
      
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
      const tokenEndpoint = `${authServerUrl}/protocol/openid-connect/token`;
      
      if (clientId) {
        console.log(`[C++ AUTH] Setting client credentials for: ${clientId}`);
        authClient.setClientCredentials(clientId, clientSecret);
        authClient.setTokenEndpoint(tokenEndpoint);
      }
      
      // Perform token exchange using C++ implementation
      const result = await authClient.exchangeToken(
        subjectToken,
        issuerToUse,
        audience,
        scope
      );
      
      console.log(`✅ Token exchange successful for IDP: ${issuerToUse} (C++ implementation)`);
      console.log(`   Token Type: ${result.token_type || 'Bearer'}`);
      if (result.expires_in) {
        console.log(`   Expires In: ${result.expires_in}s`);
      }
      return res.json(result);
    } catch (error: any) {
      console.error(`❌ Token exchange failed:`, error);
      
      // Handle AuthError from C++ implementation
      if (error.name === 'AuthError') {
        return res.status(400).json({
          error: error.code === -1016 ? 'invalid_request' : 'exchange_failed',
          error_description: error.details || error.message
        });
      }
      
      // Generic error response
      return res.status(500).json({
        error: 'server_error',
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
      
      
      // Default allowed scopes if not configured
      const ALLOWED_SCOPES = scopes || ['openid', 'profile', 'email'];
      
      // Filter requested scopes
      if (registrationRequest.scope) {
        const requestedScopes = registrationRequest.scope.split(' ');
        const filteredScopes = [...new Set(
          requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
        )];
        
        registrationRequest.scope = filteredScopes.join(' ');
      } else {
        // Default to all allowed scopes when no scope provided
        registrationRequest.scope = ALLOWED_SCOPES.join(' ');
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
        data.scope = filteredResponseScopes.join(' ');
      }
      
      // Set CORS headers
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      
      console.log(`✅ Registered client: ${data.client_id}`);
      console.log(`📋 Registration response keys:`, Object.keys(data));
      console.log(`🔐 Client secret present:`, data.client_secret ? 'YES' : 'NO');
      
      // Debug: Check if we're actually going into the storage block
      console.log(`🔍 Checking data.client_id:`, data.client_id, typeof data.client_id);
      
      // Store dynamic client for token exchange
      if (data.client_id) {
        dynamicClientStore.set(data.client_id, {
          client_secret: data.client_secret || '',
          timestamp: Date.now()
        });
        console.log(`✅ Dynamic client registered and stored: ${data.client_id}`);
      }
      
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
    '/oauth/token-exchange',
    '/oauth/idps',
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