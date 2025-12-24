/**
 * Express adapter for Gopher Auth SDK with Enhanced Auto-Refresh Features
 * Provides Express-specific integration without coupling SDK to Express
 * 
 * Enhanced Features:
 * - Automatic token refresh with session management
 * - Token expiration tracking and proactive refresh
 * - Session-based authentication state
 * - Debug endpoints for session information
 */

import { Request, Response, NextFunction, Router } from 'express';
import { OAuthHelper } from '../src/oauth-helper.js';
import { McpAuthClient } from '../src/mcp-auth-api.js';
import { 
  EnhancedSessionManager, 
  initializeEnhancedSessionManager,
  getEnhancedSessionManager 
} from './session-manager.js';
import { initializeKeycloakAdmin } from './keycloak-admin.js';

// Store dynamic client credentials for token exchange
const dynamicClientStore = new Map<string, { client_secret: string; timestamp: number; mappedTo?: string }>();

export interface ExpressOAuthConfig {
  oauth: OAuthHelper;
  clientId?: string;
  clientSecret?: string;
  redirectUris?: string[];
  scopes?: string[];
  allowedScopes?: string[];  // Scopes allowed for dynamic registration
  filterInvalidScopes?: boolean;  // Whether to filter invalid scopes
}

export interface EnhancedExpressOAuthConfig extends ExpressOAuthConfig {
  enableAutoRefresh?: boolean; // Enable automatic token refresh
  refreshBufferSeconds?: number; // Seconds before expiry to refresh
}

/**
 * Creates Express middleware for OAuth authentication
 */
export function createAuthMiddleware(oauth: OAuthHelper) {
  // Initialize auth client for C++ token exchange
  let authClient: McpAuthClient | null = null;
  
  return async (req: Request, res: Response, next: NextFunction): Promise<void> => {
    console.log(`[OAuth] 🔐 Middleware: ${req.method} ${req.path}`);
    
    try {
      // Extract token from multiple sources
      const token = oauth.extractToken(
        req.headers.authorization as string,
        req.query.access_token as string,
        req
      );
      
      console.log(`[OAuth] 🎫 Token: ${token ? `Yes (${token.substring(0, 20)}...)` : 'No'}`);

      // Validate token
      console.log(`[OAuth] 🔍 Validating token...`);
      const result = await oauth.validateToken(token);
      
      if (!result.valid) {
        console.log(`[OAuth] ❌ Token validation FAILED: ${result.error}`);
        const wwwAuth = result.wwwAuthenticate;
        console.log(`[OAuth] 📤 WWW-Authenticate: ${wwwAuth?.substring(0, 100) || 'none'}...`);
        
        res.status(result.statusCode || 401)
          .set('WWW-Authenticate', wwwAuth)
          .json({
            error: 'Unauthorized',
            message: result.error || 'Authentication required'
          });
        return;
      }
      
      console.log(`[OAuth] ✅ Token valid! sub=${result.payload?.subject}, iss=${result.payload?.issuer}`);
      console.log(`[OAuth]    Scopes: ${result.payload?.scopes || 'none'}`);

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
 * Creates Enhanced Express middleware with automatic token re-exchange
 */
export function createEnhancedAuthMiddleware(
  oauth: OAuthHelper,
  config: Partial<EnhancedExpressOAuthConfig> & { oauth?: OAuthHelper } = {}
) {
  // Ensure oauth is set in config
  config.oauth = config.oauth || oauth;
  // Initialize auth client for C++ token exchange
  let authClient: McpAuthClient | null = null;
  let sessionManager: EnhancedSessionManager | null = null;
  
  return async (req: Request, res: Response, next: NextFunction): Promise<void> => {
    console.log(`[ENHANCED AUTH] ${req.method} ${req.path} - Starting authentication`);
    
    try {
      // Initialize auth client if needed
      if (!authClient && config.enableAutoRefresh !== false) {
        const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
        console.log(`[ENHANCED AUTH] Initializing auth client: ${authServerUrl}`);
        
        const authConfig = {
          jwksUri: `${authServerUrl}/protocol/openid-connect/certs`,
          issuer: authServerUrl
        };
        authClient = new McpAuthClient(authConfig);
        await authClient.initialize();
        
        // Configure for token exchange
        const clientId = process.env.GOPHER_CLIENT_ID;
        const clientSecret = process.env.GOPHER_CLIENT_SECRET || '';
        const tokenEndpoint = `${authServerUrl}/protocol/openid-connect/token`;
        
        if (clientId) {
          authClient.setClientCredentials(clientId, clientSecret);
          authClient.setTokenEndpoint(tokenEndpoint);
        }
        
        // Initialize session manager with auth client
        sessionManager = initializeEnhancedSessionManager(authClient);
        console.log(`[ENHANCED AUTH] Session manager initialized with auto-refresh`);
      }
      
      // Check for existing session first
      let sessionId: string | undefined;
      let tokenFromSession: string | undefined;
      let needsNewSession = true;
      
      if (sessionManager) {
        sessionId = sessionManager.extractSessionId(req) || undefined;
        
        if (sessionId) {
          console.log(`[ENHANCED AUTH] Found session: ${sessionId.substring(0, 8)}...`);
          
          // Try to get token from session (with auto-refresh)
          const sessionToken = await sessionManager.getTokenFromSession(sessionId);
          
          if (sessionToken) {
            tokenFromSession = sessionToken.token;
            needsNewSession = false;
            
            if (sessionToken.needsRefresh) {
              console.log(`[ENHANCED AUTH] Token needs refresh but continuing with expired token`);
            } else {
              console.log(`[ENHANCED AUTH] Using valid token from session`);
            }
          }
        }
      }
      
      // Extract token from request if no session token
      const token = tokenFromSession || oauth.extractToken(
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

      // Attach auth payload and tokens to request
      (req as any).auth = result.payload;
      (req as any).authToken = token;
      (req as any).kcToken = token; // Store KC token for re-exchange
      
      // Handle token exchange if configured
      const exchangeIdps = process.env.EXCHANGE_IDPS;
      
      if (exchangeIdps && token && authClient) {
        const idpList = exchangeIdps.split(',').map(idp => idp.trim()).filter(idp => idp);
        console.log(`[ENHANCED AUTH] Attempting token exchange for IDPs: ${idpList.join(', ')}`);
        
        // Store all external tokens
        const externalTokens: Record<string, any> = {};
        
        // Exchange tokens for all configured IDPs
        const exchangePromises = idpList.map(async (idpAlias) => {
          console.log(`🔄 Exchanging token for IDP: ${idpAlias}`);
          
          try {
            const exchangeResult = await authClient!.exchangeToken(
              token,
              idpAlias,
              result.payload?.audience,
              result.payload?.scopes
            );
            
            externalTokens[idpAlias] = {
              access_token: exchangeResult.access_token,
              token_type: exchangeResult.token_type || 'Bearer',
              expires_in: exchangeResult.expires_in
            };
            
            // Store in session if we have a session manager
            if (sessionManager) {
              // Create new session if needed
              if (needsNewSession) {
                sessionId = sessionManager.generateSessionId();
                needsNewSession = false;
                console.log(`[ENHANCED AUTH] Created new session: ${sessionId.substring(0, 8)}...`);
              }
              
              // Store tokens with expiration tracking
              sessionManager.storeTokens(
                sessionId!,
                exchangeResult.access_token,
                token, // KC token for re-exchange
                idpAlias,
                exchangeResult.expires_in,
                result.payload
              );
              
              // Set session cookie if it's a new session
              if (!sessionManager.extractSessionId(req)) {
                sessionManager.setSessionCookie(res, sessionId!, exchangeResult.expires_in || 3600);
              }
            }
            
            console.log(`✅ Token exchange successful for ${idpAlias}`);
            console.log(`   Expires in: ${exchangeResult.expires_in}s`);
            
            return { idpAlias, success: true };
          } catch (error: any) {
            console.log(`❌ Token exchange failed for ${idpAlias}: ${error.message}`);
            return { idpAlias, success: false, error: error.message };
          }
        });
        
        // Wait for all exchanges
        const results = await Promise.all(exchangePromises);
        
        // Attach external tokens to request
        if (Object.keys(externalTokens).length > 0) {
          (req as any).externalTokens = externalTokens;
          (req as any).sessionId = sessionId;
          
          // For backward compatibility, store first token
          const firstIdp = Object.keys(externalTokens)[0];
          if (firstIdp) {
            (req as any).externalToken = externalTokens[firstIdp].access_token;
            (req as any).externalTokenType = externalTokens[firstIdp].token_type;
            (req as any).externalIDP = firstIdp;
            (req as any).exchangedToken = externalTokens[firstIdp]; // For compatibility
          }
          
          // Log summary
          const successful = results.filter(r => r.success).map(r => r.idpAlias);
          const failed = results.filter(r => !r.success).map(r => r.idpAlias);
          
          console.log(`[ENHANCED AUTH] Token exchange summary:`);
          if (successful.length > 0) {
            console.log(`   ✅ Successful: ${successful.join(', ')}`);
          }
          if (failed.length > 0) {
            console.log(`   ❌ Failed: ${failed.join(', ')}`);
          }
        }
      }
      
      next();
    } catch (error: any) {
      console.error(`[ENHANCED AUTH] Error: ${error.message}`);
      
      const result = await oauth.validateToken(undefined);
      res.status(401)
        .set('WWW-Authenticate', result.wwwAuthenticate)
        .json({
          error: 'Unauthorized',
          message: error.message
        });
    }
  };
}

/**
 * Middleware to refresh tokens if needed during request processing
 * Use this for long-running operations that might outlive token validity
 */
export function createTokenRefreshMiddleware() {
  const sessionManager = getEnhancedSessionManager();
  
  return async (req: Request, res: Response, next: NextFunction): Promise<void> => {
    const sessionId = (req as any).sessionId || sessionManager.extractSessionId(req);
    
    if (!sessionId) {
      return next();
    }
    
    // Check if tokens need refresh
    const sessionInfo = sessionManager.getSessionInfo(sessionId);
    if (!sessionInfo) {
      return next();
    }
    
    // If token expires in less than 60 seconds, try to refresh
    if (sessionInfo.externalTokenExpiry.remainingSecs < 60) {
      console.log(`[TOKEN REFRESH] Token expiring soon, attempting refresh...`);
      
      const refreshed = await sessionManager.getTokenFromSession(sessionId, true);
      
      if (refreshed && !refreshed.needsRefresh) {
        // Update request with new token
        const externalTokens = (req as any).externalTokens || {};
        const idpAlias = sessionInfo.idpAlias;
        
        if (externalTokens[idpAlias]) {
          externalTokens[idpAlias].access_token = refreshed.token;
          console.log(`[TOKEN REFRESH] ✅ Token refreshed for ${idpAlias}`);
        }
      }
    }
    
    next();
  };
}

/**
 * Endpoint to get session info (for debugging)
 */
export function createSessionInfoEndpoint() {
  const router = Router();
  const sessionManager = getEnhancedSessionManager();
  
  router.get('/session/info', (req: Request, res: Response) => {
    const sessionId = (req as any).sessionId || sessionManager.extractSessionId(req);
    
    if (!sessionId) {
      return res.status(404).json({ error: 'No session found' });
    }
    
    const info = sessionManager.getSessionInfo(sessionId);
    if (!info) {
      return res.status(404).json({ error: 'Session not found' });
    }
    
    res.json(info);
  });
  
  router.get('/session/all', (_req: Request, res: Response) => {
    const sessions = sessionManager.getActiveSessions();
    res.json({
      count: sessions.length,
      sessions
    });
  });
  
  return router;
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
      const serverUrl = oauth.getServerUrl();
      
      // Rewrite issuer to match MCP server URL (required for OAuth 2.0 compliance)
      // Claude.ai validates that issuer matches the URL it fetched from
      if (metadata.issuer) {
        console.log(`🔧 Rewriting issuer from ${metadata.issuer} to ${serverUrl}`);
        metadata.issuer = serverUrl;
      }
      
      // Extract realm from authServerUrl
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
      const realmMatch = authServerUrl.match(/\/realms\/([^/]+)/);
      const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
      
      // Rewrite token_endpoint to point to our proxy (required for CORS)
      if (metadata.token_endpoint) {
        metadata.token_endpoint = `${serverUrl}/realms/${realm}/protocol/openid-connect/token`;
        console.log(`🔧 Rewrote token_endpoint to: ${metadata.token_endpoint}`);
      }
      
      // Rewrite authorization_endpoint to point to our proxy
      if (metadata.authorization_endpoint) {
        metadata.authorization_endpoint = `${serverUrl}/realms/${realm}/protocol/openid-connect/auth`;
        console.log(`🔧 Rewrote authorization_endpoint to: ${metadata.authorization_endpoint}`);
      }
      
      // Rewrite registration endpoint to point to our proxy
      if (metadata.registration_endpoint) {
        metadata.registration_endpoint = `${serverUrl}/realms/${realm}/clients-registrations/openid-connect`;
        console.log(`🔧 Rewrote registration_endpoint to: ${metadata.registration_endpoint}`);
      }
      
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
      
      // Ensure "none" is in token_endpoint_auth_methods_supported for public clients (Claude.ai)
      if (metadata.token_endpoint_auth_methods_supported && !metadata.token_endpoint_auth_methods_supported.includes('none')) {
        metadata.token_endpoint_auth_methods_supported.push('none');
        console.log(`🔧 Added "none" to token_endpoint_auth_methods_supported for public clients`);
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
      
      // Force public client registration for MCP
      // MCP clients (Claude.ai, mcp-remote, MCP Inspector) are browser-based
      // and cannot safely store a client_secret, even if they request one
      if (registrationRequest.token_endpoint_auth_method && registrationRequest.token_endpoint_auth_method !== 'none') {
        console.log(`⚠️  Client requested ${registrationRequest.token_endpoint_auth_method}, overriding to 'none' (MCP requires public clients)`);
      }
      registrationRequest.token_endpoint_auth_method = 'none';
      console.log(`🔧 Using token_endpoint_auth_method: none (public client)`);
      
      // Filter requested scopes
      if (registrationRequest.scope) {
        const requestedScopes = registrationRequest.scope.split(' ');
        const filteredScopes = [...new Set(
          requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
        )];
        
        // If all scopes were filtered out, default to allowed scopes
        if (filteredScopes.length === 0) {
          registrationRequest.scope = ALLOWED_SCOPES.join(' ');
          console.log(`⚠️  All requested scopes filtered out, defaulting to: ${registrationRequest.scope}`);
        } else {
          registrationRequest.scope = filteredScopes.join(' ');
          console.log(`✅ Filtered scope: ${registrationRequest.scope}`);
        }
      } else {
        // Default to all allowed scopes when no scope provided
        registrationRequest.scope = ALLOWED_SCOPES.join(' ');
        console.log(`ℹ️  No scope provided, defaulting to: ${registrationRequest.scope}`);
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
        
        // Automatically enable token exchange permissions if client has a secret
        if (data.client_secret && process.env.ENABLE_AUTO_TOKEN_EXCHANGE_PERMISSION === 'true') {
          try {
            console.log(`🔧 Attempting to enable token exchange permission for ${data.client_id}...`);
            const adminClient = initializeKeycloakAdmin();
            await adminClient.enableTokenExchangePermission(data.client_id);
            console.log(`✅ Token exchange permission enabled for ${data.client_id}`);
          } catch (error: any) {
            console.error(`⚠️ Failed to enable token exchange permission: ${error.message}`);
            console.error(`   This is non-fatal - client registration succeeded`);
          }
        }
      }
      
      res.status(response.status).json(data);
    } catch (error: any) {
      console.error(`❌ Error registering client: ${error.message}`);
      res.status(500).json({ error: error.message });
    }
  });
  
  // Dynamic registration endpoint - actually register with Keycloak
  router.post('/register', async (req, res) => {
    console.log('Registration request:', req.body);
    
    // Define allowed scopes for filtering
    const ALLOWED_SCOPES = config.allowedScopes || scopes || ['openid', 'profile', 'email', 'offline_access', 'gopher:mcp01'];
    
    try {
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
      const realmMatch = authServerUrl.match(/\/realms\/([^/]+)/);
      const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
      
      // Keycloak's dynamic registration endpoint
      const registrationUrl = `${authServerUrl}/clients-registrations/openid-connect`;
      
      console.log(`🔄 Attempting dynamic registration with Keycloak: ${registrationUrl}`);
      
      // Prepare registration request
      const registrationRequest = {
        client_name: req.body.client_name || 'MCP Dynamic Client',
        redirect_uris: req.body.redirect_uris || [`${oauth.getServerUrl()}/oauth/callback`],
        grant_types: req.body.grant_types || ['authorization_code', 'refresh_token'],
        response_types: req.body.response_types || ['code'],
        token_endpoint_auth_method: req.body.token_endpoint_auth_method || 'client_secret_post',
        application_type: req.body.application_type || 'web',
        require_auth_time: false,
        // Request specific scopes - Keycloak may still override with its defaults
        // Include gopher:mcp01 for MCP access
        scope: req.body.scope || 'openid profile email offline_access'
      };
      
      console.log('Registration payload:', JSON.stringify(registrationRequest, null, 2));
      
      // Make registration request to Keycloak
      const fetch = (await import('node-fetch')).default;
      const response = await fetch(registrationUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Accept': 'application/json'
        },
        body: JSON.stringify(registrationRequest)
      });
      
      const responseText = await response.text();
      
      if (response.ok) {
        const client = JSON.parse(responseText);
        console.log(`✅ Client dynamically registered with Keycloak!`);
        console.log(`   Client ID: ${client.client_id}`);
        console.log(`   Client Secret: ${client.client_secret ? '[REDACTED]' : 'None (public client)'}`);
        
        // Store the dynamically registered client for reference
        if (client.client_id) {
          dynamicClientStore.set(client.client_id, {
            client_secret: client.client_secret || '',
            timestamp: Date.now()
          });
        }
        
        return res.status(201).json(client);
      } else {
        console.log(`❌ Keycloak registration failed: ${response.status}`);
        console.log(`   Response: ${responseText}`);
        
        // If registration is not allowed, fall back to pre-configured client
        if (response.status === 403 || response.status === 401 || responseText.includes('not permitted')) {
          console.log('⚠️ Dynamic registration not permitted, checking for pre-configured client...');
          
          if (CLIENT_ID && CLIENT_SECRET) {
            console.log('✅ Using pre-configured client as fallback');
            
            // Filter scopes for fallback client too
            let clientScope = 'openid profile email';
            if (req.body.scope) {
              const requestedScopes = req.body.scope.split(' ');
              const filteredScopes = [...new Set(
                requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
              )];
              
              if (filteredScopes.length > 0) {
                clientScope = filteredScopes.join(' ');
              }
            }
            
            const client = {
              client_id: CLIENT_ID,
              client_secret: CLIENT_SECRET,
              redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
              token_endpoint_auth_method: 'client_secret_post',
              grant_types: ['authorization_code', 'refresh_token'],
              response_types: ['code'],
              client_name: req.body.client_name,
              scope: clientScope
            };
            return res.status(201).json(client);
          }
        }
        
        return res.status(response.status).json({ 
          error: 'Registration failed',
          message: responseText,
          fallback: 'Configure GOPHER_CLIENT_ID and GOPHER_CLIENT_SECRET to use pre-configured client'
        });
      }
    } catch (error: any) {
      console.error('Registration error:', error.message);
      
      // Fall back to pre-configured client if available
      if (CLIENT_ID && CLIENT_SECRET) {
        console.log('⚠️ Using pre-configured client due to registration error');
        
        // Filter scopes for fallback client
        let clientScope = 'openid profile email';
        if (req.body.scope) {
          const requestedScopes = req.body.scope.split(' ');
          const filteredScopes = [...new Set(
            requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
          )];
          
          if (filteredScopes.length > 0) {
            clientScope = filteredScopes.join(' ');
          }
        }
        
        const client = {
          client_id: CLIENT_ID,
          client_secret: CLIENT_SECRET,
          redirect_uris: req.body.redirect_uris || REDIRECT_URIS,
          token_endpoint_auth_method: 'client_secret_post',
          grant_types: ['authorization_code', 'refresh_token'],
          response_types: ['code'],
          client_name: req.body.client_name,
          scope: clientScope
        };
        return res.status(201).json(client);
      }
      
      return res.status(500).json({ 
        error: 'Registration failed',
        message: error.message
      });
    }
  });
  
  // Also add /oauth/register endpoint (MCP Inspector may use this) - delegate to main /register
  router.post('/oauth/register', async (req, res, next) => {
    console.log('OAuth registration request (forwarding to /register):', req.body.client_name);
    
    // Add CORS headers
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    
    // Simply forward the request to the /register handler
    req.url = '/register';
    next();
  });

  // Authorization endpoint with auto-registration for unknown clients
  router.get('/authorize', async (req, res) => {
    // Ensure client_id is present
    const queryParams = { ...req.query } as Record<string, string>;
    if (!queryParams.client_id) {
      queryParams.client_id = CLIENT_ID;
    }
    
    const clientId = queryParams.client_id;
    const redirectUri = queryParams.redirect_uri || '';
    
    console.log(`[AUTHORIZE] OAuth flow initiated with client: ${clientId}`);
    console.log(`[AUTHORIZE] Redirect URI: ${redirectUri}`);
    
    // Check if this is Claude based on redirect URI
    const isClaudeRedirect = redirectUri.includes('claude.ai/api/mcp/auth_callback') || 
                            redirectUri.includes('claude.com/api/mcp/auth_callback');
    
    // Check if client exists in our dynamic store
    const isKnownClient = dynamicClientStore.has(clientId) || clientId === CLIENT_ID;
    
    // Auto-register unknown clients from Claude
    if (!isKnownClient && isClaudeRedirect) {
      console.log(`🤖 Unknown client ${clientId} from Claude - auto-registering...`);
      
      try {
        const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL;
        // Extract realm from authServerUrl or use default
        const realmMatch = authServerUrl ? authServerUrl.match(/\/realms\/([^/]+)/) : null;
        const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
        const keycloakUrl = authServerUrl?.includes('/realms/') 
          ? `${authServerUrl}/clients-registrations/openid-connect`
          : `${authServerUrl}/realms/${realm}/clients-registrations/openid-connect`;
        
        const registrationRequest = {
          // Don't include client_id - Keycloak will generate one
          client_name: `Claude Desktop (${clientId})`,
          redirect_uris: [
            'https://claude.ai/api/mcp/auth_callback',
            'https://claude.com/api/mcp/auth_callback'
          ],
          grant_types: ['authorization_code', 'refresh_token'],
          response_types: ['code'],
          token_endpoint_auth_method: 'none', // Public client for Claude
          scope: queryParams.scope || 'openid profile email'
        };
        
        console.log(`📝 Registering client with redirect URIs:`, registrationRequest.redirect_uris);
        
        const response = await fetch(keycloakUrl, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify(registrationRequest),
        });
        
        const data = await response.json() as any;
        
        if (response.ok && data.client_id) {
          // Store the registered client
          dynamicClientStore.set(data.client_id, {
            client_secret: data.client_secret || '',
            timestamp: Date.now()
          });
          
          // Also map the requested client_id to the generated one
          dynamicClientStore.set(clientId, {
            client_secret: data.client_secret || '',
            timestamp: Date.now(),
            mappedTo: data.client_id
          });
          
          console.log(`✅ Auto-registered client: ${data.client_id} (requested: ${clientId})`);
          
          // Use the dynamically registered client for authorization
          // This ensures the redirect_uri is valid
          queryParams.client_id = data.client_id;
          console.log(`🔄 Using dynamic client for authorization: ${data.client_id}`);
        } else {
          console.log(`⚠️ Registration failed, continuing with original client_id`);
          console.log(`   Response:`, data);
        }
      } catch (error: any) {
        console.error(`❌ Auto-registration error: ${error.message}`);
        // Continue with original client_id even if registration fails
      }
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
      let tokenRequest = {
        ...req.body,
        client_id: req.body.client_id || clientIdFromAuth || CLIENT_ID,
        client_secret: req.body.client_secret || clientSecretFromAuth || CLIENT_SECRET,
      };
      
      // Handle client mapping for dynamic clients
      const originalClientId = tokenRequest.client_id;
      const clientEntry = dynamicClientStore.get(originalClientId);
      
      if (tokenRequest.grant_type === 'authorization_code') {
        // Check if this is the original fake client_id with a mapping
        if (clientEntry && clientEntry.mappedTo) {
          // Map the fake client_id to the real dynamic client
          console.log(`🔄 Mapping client_id: ${originalClientId} -> ${clientEntry.mappedTo}`);
          tokenRequest.client_id = clientEntry.mappedTo;
          // Dynamic clients are public, remove any secret
          delete tokenRequest.client_secret;
        } else if (dynamicClientStore.has(originalClientId)) {
          // This is already the dynamic client_id
          console.log(`🔓 Using dynamic client: ${originalClientId}`);
          // Dynamic clients are public, remove any secret
          delete tokenRequest.client_secret;
        }
        
        // Log PKCE parameters if present
        if (tokenRequest.code_verifier) {
          console.log(`✅ PKCE code_verifier present for public client`);
        }
      } else if (tokenRequest.grant_type === 'refresh_token') {
        // For refresh tokens, use configured credentials
        const configuredClientId = process.env.GOPHER_CLIENT_ID || CLIENT_ID;
        const configuredClientSecret = process.env.GOPHER_CLIENT_SECRET || CLIENT_SECRET;
        
        if (configuredClientId && configuredClientSecret) {
          console.log(`🔐 refresh_token: using configured client ${configuredClientId}`);
          tokenRequest.client_id = configuredClientId;
          tokenRequest.client_secret = configuredClientSecret;
        }
      }
      
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
      const serverUrl = oauth.getServerUrl();
      
      // Rewrite issuer to match MCP server URL (required for OAuth 2.0 compliance)
      if (metadata.issuer) {
        metadata.issuer = serverUrl;
      }
      
      // Extract realm from authServerUrl
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
      const realmMatch = authServerUrl.match(/\/realms\/([^/]+)/);
      const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
      
      // Rewrite token_endpoint to point to our proxy using realm-based paths
      if (metadata.token_endpoint) {
        metadata.token_endpoint = `${serverUrl}/realms/${realm}/protocol/openid-connect/token`;
      }
      
      // Rewrite authorization_endpoint to point to our proxy using realm-based paths
      if (metadata.authorization_endpoint) {
        metadata.authorization_endpoint = `${serverUrl}/realms/${realm}/protocol/openid-connect/auth`;
      }
      
      // Rewrite registration endpoint to point to our proxy
      if (metadata.registration_endpoint) {
        metadata.registration_endpoint = `${serverUrl}/realms/${realm}/clients-registrations/openid-connect`;
      }
      
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
      
      // Ensure "none" is in token_endpoint_auth_methods_supported for public clients
      if (metadata.token_endpoint_auth_methods_supported && !metadata.token_endpoint_auth_methods_supported.includes('none')) {
        metadata.token_endpoint_auth_methods_supported.push('none');
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
      const serverUrl = oauth.getServerUrl();
      
      // Rewrite issuer to match MCP server URL (required for OAuth 2.0 compliance)
      if (metadata.issuer) {
        metadata.issuer = serverUrl;
      }
      
      // Extract realm from authServerUrl
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
      const realmMatch = authServerUrl.match(/\/realms\/([^/]+)/);
      const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
      
      // Rewrite token_endpoint to point to our proxy using realm-based paths
      if (metadata.token_endpoint) {
        metadata.token_endpoint = `${serverUrl}/realms/${realm}/protocol/openid-connect/token`;
      }
      
      // Rewrite authorization_endpoint to point to our proxy using realm-based paths
      if (metadata.authorization_endpoint) {
        metadata.authorization_endpoint = `${serverUrl}/realms/${realm}/protocol/openid-connect/auth`;
      }
      
      // Rewrite registration endpoint to point to our proxy
      if (metadata.registration_endpoint) {
        metadata.registration_endpoint = `${serverUrl}/realms/${realm}/clients-registrations/openid-connect`;
      }
      
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
      
      // Ensure "none" is in token_endpoint_auth_methods_supported for public clients
      if (metadata.token_endpoint_auth_methods_supported && !metadata.token_endpoint_auth_methods_supported.includes('none')) {
        metadata.token_endpoint_auth_methods_supported.push('none');
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
      
      // Force public client registration for MCP
      if (registrationRequest.token_endpoint_auth_method && registrationRequest.token_endpoint_auth_method !== 'none') {
        console.log(`⚠️  Client requested ${registrationRequest.token_endpoint_auth_method}, overriding to 'none' (MCP requires public clients)`);
      }
      registrationRequest.token_endpoint_auth_method = 'none';
      console.log(`🔧 Using token_endpoint_auth_method: none (public client)`);
      
      // Filter requested scopes
      if (registrationRequest.scope) {
        const requestedScopes = registrationRequest.scope.split(' ');
        const filteredScopes = [...new Set(
          requestedScopes.filter((scope: string) => ALLOWED_SCOPES.includes(scope))
        )];
        
        // If all scopes were filtered out, default to allowed scopes
        if (filteredScopes.length === 0) {
          registrationRequest.scope = ALLOWED_SCOPES.join(' ');
          console.log(`⚠️  All requested scopes filtered out, defaulting to: ${registrationRequest.scope}`);
        } else {
          registrationRequest.scope = filteredScopes.join(' ');
          console.log(`✅ Filtered scope: ${registrationRequest.scope}`);
        }
      } else {
        // Default to all allowed scopes when no scope provided
        registrationRequest.scope = ALLOWED_SCOPES.join(' ');
        console.log(`ℹ️  No scope provided, defaulting to: ${registrationRequest.scope}`);
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
        
        // Automatically enable token exchange permissions if client has a secret
        if (data.client_secret && process.env.ENABLE_AUTO_TOKEN_EXCHANGE_PERMISSION === 'true') {
          try {
            console.log(`🔧 Attempting to enable token exchange permission for ${data.client_id}...`);
            const adminClient = initializeKeycloakAdmin();
            await adminClient.enableTokenExchangePermission(data.client_id);
            console.log(`✅ Token exchange permission enabled for ${data.client_id}`);
          } catch (error: any) {
            console.error(`⚠️ Failed to enable token exchange permission: ${error.message}`);
            console.error(`   This is non-fatal - client registration succeeded`);
          }
        }
      }
      
      res.status(response.status).json(data);
    } catch (error: any) {
      console.error(`❌ Error registering client: ${error.message}`);
      res.status(500).json({ error: error.message });
    }
  });

  // ================================================================
  // Token Endpoint Proxy (required for CORS) - Critical for Claude
  // ================================================================
  app.post('/realms/:realm/protocol/openid-connect/token', async (req: Request, res: Response) => {
    try {
      const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
      const keycloakUrl = `${authServerUrl}/protocol/openid-connect/token`;

      console.log(`🔄 Proxying token request to Keycloak: ${keycloakUrl}`);
      
      // Parse the request body
      let requestParams: Record<string, string>;
      if (typeof req.body === 'string' && req.body.length > 0) {
        // Body is already a string (raw body)
        requestParams = Object.fromEntries(new URLSearchParams(req.body));
      } else if (req.body && typeof req.body === 'object' && Object.keys(req.body).length > 0) {
        // Body is an object (parsed by urlencoded middleware)
        requestParams = req.body as Record<string, string>;
      } else {
        // Fallback: body might be empty, return error
        console.log(`❌ Request body is empty or invalid`);
        res.status(400).json({ error: 'invalid_request', error_description: 'Empty request body' });
        return;
      }

      // Handle client mapping for dynamic clients
      const originalClientId = requestParams.client_id;
      const clientEntry = dynamicClientStore.get(originalClientId);
      
      if (requestParams.grant_type === 'authorization_code') {
        // Check if this is the original fake client_id with a mapping
        if (clientEntry && clientEntry.mappedTo) {
          // Map the fake client_id to the real dynamic client
          console.log(`🔄 Mapping client_id: ${originalClientId} -> ${clientEntry.mappedTo}`);
          requestParams.client_id = clientEntry.mappedTo;
          // Dynamic clients are public, remove any secret
          delete requestParams.client_secret;
        } else if (dynamicClientStore.has(originalClientId)) {
          // This is already the dynamic client_id
          console.log(`🔓 Using dynamic client: ${originalClientId}`);
          // Dynamic clients are public, remove any secret
          delete requestParams.client_secret;
        }
        
        // Log PKCE parameters if present
        if (requestParams.code_verifier) {
          console.log(`✅ PKCE code_verifier present for public client`);
        }
      } else if (requestParams.grant_type === 'refresh_token') {
        // For refresh tokens, use configured credentials
        const configuredClientId = process.env.GOPHER_CLIENT_ID;
        const configuredClientSecret = process.env.GOPHER_CLIENT_SECRET;
        
        if (configuredClientId && configuredClientSecret) {
          console.log(`🔐 refresh_token: using configured client ${configuredClientId}`);
          requestParams.client_id = configuredClientId;
          requestParams.client_secret = configuredClientSecret;
        }
      }
      
      // Build the body for Keycloak
      const bodyToSend = new URLSearchParams(requestParams).toString();

      // Forward the request to Keycloak
      const response = await fetch(keycloakUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: bodyToSend,
      });

      const data = await response.json();

      if (response.ok) {
        console.log(`✅ Token exchange successful`);
      } else {
        console.log(`❌ Token exchange failed: ${JSON.stringify(data)}`);
      }

      // Forward the response with same status and CORS headers
      res.header('Access-Control-Allow-Origin', '*');
      res.header('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.header('Access-Control-Allow-Headers', '*');
      res.status(response.status).json(data);
    } catch (error: any) {
      console.error(`❌ Error proxying token request: ${error.message}`);
      res.status(500).json({ error: error.message });
    }
  });

  // ================================================================
  // Authorization Endpoint with Auto-Registration - Critical for Claude
  // ================================================================
  app.get('/realms/:realm/protocol/openid-connect/auth', async (req: Request, res: Response) => {
    const queryParams = req.query as Record<string, string>;
    const clientId = queryParams.client_id;
    const redirectUri = queryParams.redirect_uri || '';
    
    console.log(`[AUTHORIZE-REALM] OAuth flow initiated with client: ${clientId}`);
    console.log(`[AUTHORIZE-REALM] Redirect URI: ${redirectUri}`);
    
    // Check if this is Claude based on redirect URI
    const isClaudeRedirect = redirectUri.includes('claude.ai/api/mcp/auth_callback') || 
                            redirectUri.includes('claude.com/api/mcp/auth_callback');
    
    // Check if client exists in our dynamic store
    const isKnownClient = dynamicClientStore.has(clientId) || 
                         clientId === process.env.GOPHER_CLIENT_ID;
    
    // Auto-register unknown clients from Claude
    if (!isKnownClient && isClaudeRedirect && clientId) {
      console.log(`🤖 Unknown client ${clientId} from Claude - auto-registering...`);
      
      try {
        const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
        // Extract realm from authServerUrl or use default
        const realmMatch = authServerUrl ? authServerUrl.match(/\/realms\/([^/]+)/) : null;
        const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
        const keycloakUrl = authServerUrl.includes('/realms/') 
          ? `${authServerUrl}/clients-registrations/openid-connect`
          : `${authServerUrl}/realms/${realm}/clients-registrations/openid-connect`;
        
        const registrationRequest = {
          // Don't include client_id - Keycloak will generate one
          client_name: `Claude Desktop (${clientId})`,
          redirect_uris: [
            'https://claude.ai/api/mcp/auth_callback',
            'https://claude.com/api/mcp/auth_callback'
          ],
          grant_types: ['authorization_code', 'refresh_token'],
          response_types: ['code'],
          token_endpoint_auth_method: 'none', // Public client for Claude
          scope: queryParams.scope || 'openid profile email'
        };
        
        console.log(`📝 Registering client with redirect URIs:`, registrationRequest.redirect_uris);
        
        const response = await fetch(keycloakUrl, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify(registrationRequest),
        });
        
        const data = await response.json() as any;
        
        if (response.ok && data.client_id) {
          // Store the registered client
          dynamicClientStore.set(data.client_id, {
            client_secret: data.client_secret || '',
            timestamp: Date.now()
          });
          
          // Also map the requested client_id to the generated one
          dynamicClientStore.set(clientId, {
            client_secret: data.client_secret || '',
            timestamp: Date.now(),
            mappedTo: data.client_id
          });
          
          console.log(`✅ Auto-registered client: ${data.client_id} (requested: ${clientId})`);
          
          // Use the dynamically registered client for authorization
          // This ensures the redirect_uri is valid
          queryParams.client_id = data.client_id;
          console.log(`🔄 Using dynamic client for authorization: ${data.client_id}`);
        } else {
          console.log(`⚠️ Registration failed, continuing with original client_id`);
          console.log(`   Response:`, data);
        }
      } catch (error: any) {
        console.error(`❌ Auto-registration error: ${error.message}`);
        // Continue with original client_id even if registration fails
      }
    }
    
    // Build the final Keycloak authorization URL
    const authServerUrl = process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '';
    const keycloakAuthUrl = `${authServerUrl}/protocol/openid-connect/auth`;
    const queryString = new URLSearchParams(queryParams).toString();
    const redirectUrl = queryString ? `${keycloakAuthUrl}?${queryString}` : keycloakAuthUrl;

    console.log(`🔄 Redirecting authorization request to Keycloak: ${redirectUrl}`);

    // Redirect to Keycloak (this is a user-facing redirect, not a proxy)
    res.redirect(redirectUrl);
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
    '/realms/:realm/clients-registrations/openid-connect',
    '/realms/:realm/protocol/openid-connect/token',
    '/realms/:realm/protocol/openid-connect/auth'
  ], (_req: Request, res: Response) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', '*');
    res.header('Access-Control-Allow-Credentials', 'true');
    res.sendStatus(200);
  });
}

/**
 * Export the enhanced session manager for direct use
 */
export { 
  EnhancedSessionManager,
  initializeEnhancedSessionManager,
  getEnhancedSessionManager 
};