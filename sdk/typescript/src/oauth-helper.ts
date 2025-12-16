/**
 * @file oauth-helper.ts
 * @brief Generic OAuth authentication helper for MCP servers
 * 
 * Provides OAuth functionality without framework dependency
 */

import { McpAuthClient } from './mcp-auth-api';
import type { 
  AuthClientConfig, 
  ValidationOptions, 
  TokenPayload,
  TokenExchangeOptions,
  TokenExchangeResult 
} from './auth-types';
import { TokenExchangeError } from './auth-types';
import { 
  extractSessionId, 
  getTokenFromSession, 
  storeTokenInSession, 
  setSessionCookie,
  generateSessionId 
} from './session-manager';

/**
 * OAuth configuration options
 */
export interface OAuthConfig extends Partial<AuthClientConfig> {
  /** OAuth server URL */
  serverUrl: string;
  
  /** Token audience */
  tokenAudience?: string;
  
  /** Allowed scopes for the resource */
  allowedScopes?: string[];
}

/**
 * Token validation options
 */
export interface TokenValidationOptions {
  /** Expected audience(s) */
  audience?: string | string[];
  
  /** Required scopes */
  requiredScopes?: string[];
}

/**
 * OAuth authentication result
 */
export interface AuthResult {
  /** Whether authentication succeeded */
  valid: boolean;
  
  /** Token payload if valid */
  payload?: TokenPayload;
  
  /** Error message if invalid */
  error?: string;
  
  /** HTTP status code */
  statusCode?: number;
  
  /** WWW-Authenticate header value */
  wwwAuthenticate?: string;
}

/**
 * Generic OAuth authentication helper
 * Provides OAuth functionality without framework dependency
 */
export class OAuthHelper {
  private authClient: McpAuthClient;
  private config: AuthClientConfig;
  private serverUrl: string;
  private tokenIssuer: string;
  private tokenAudience?: string;
  private allowedScopes: string[];
  
  constructor(config: OAuthConfig) {
    const env = process.env;
    const authServerUrl = config.serverUrl || env['GOPHER_AUTH_SERVER_URL'] || env['OAUTH_SERVER_URL'] || '';
    
    this.serverUrl = config.serverUrl;
    this.tokenIssuer = config.issuer || env['TOKEN_ISSUER'] || authServerUrl;
    this.tokenAudience = config.tokenAudience || env['TOKEN_AUDIENCE'];
    this.allowedScopes = config.allowedScopes || ['openid', 'profile', 'email'];
    
    
    this.config = {
      jwksUri: config.jwksUri || env['JWKS_URI'] || `${authServerUrl}/protocol/openid-connect/certs`,
      issuer: this.tokenIssuer,
      cacheDuration: config.cacheDuration || parseInt(env['JWKS_CACHE_DURATION'] || '3600'),
      autoRefresh: config.autoRefresh ?? (env['JWKS_AUTO_REFRESH'] === 'true'),
      requestTimeout: config.requestTimeout || parseInt(env['REQUEST_TIMEOUT'] || '10'),
    };
    
    this.authClient = new McpAuthClient(this.config);
  }
  
  /**
   * Generate OAuth protected resource metadata (RFC 9728)
   */
  async generateProtectedResourceMetadata(): Promise<any> {
    try {
      // Try to use C++ implementation if available
      const result = await this.authClient.generateProtectedResourceMetadata(this.serverUrl, this.allowedScopes);
      console.log('Using C++ metadata:', result);
      return result;
    } catch (error: any) {
      // Fallback implementation until C++ functions are available
      // Point authorization_servers to our proxy so MCP Inspector uses our endpoints
      // This avoids CORS issues with direct Keycloak access
      console.log('Using fallback metadata, serverUrl:', this.serverUrl);
      const metadata = {
        resource: this.serverUrl,
        authorization_servers: [this.serverUrl],  // Use our proxy, not Keycloak directly
        scopes_supported: this.allowedScopes,
        bearer_methods_supported: ['header', 'query'],
      };
      console.log('Returning metadata:', metadata);
      return metadata;
    }
  }
  
  /**
   * Get OAuth discovery metadata
   */
  async getDiscoveryMetadata(): Promise<any> {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    
    try {
      // Try to use C++ implementation if available
      return await this.authClient.proxyDiscoveryMetadata(
        this.serverUrl,
        authServerUrl!,
        this.allowedScopes
      );
    } catch (mcpError) {
      // Fallback implementation until C++ functions are available
      try {
        // Fetch discovery metadata from auth server
        const response = await fetch(`${authServerUrl}/.well-known/openid-configuration`);
        const metadata = await response.json() as any;
        
        // Extract realm from authServerUrl
        const realmMatch = authServerUrl ? authServerUrl.match(/\/realms\/([^/]+)/) : null;
        const realm = realmMatch ? realmMatch[1] : 'gopher-mcp-auth';
        
        // Update ALL endpoints to use our proxy to ensure MCP Inspector doesn't bypass us
        return {
          ...metadata,
          issuer: this.serverUrl, // Override issuer to our proxy
          authorization_endpoint: `${this.serverUrl}/oauth/authorize`,
          token_endpoint: `${this.serverUrl}/oauth/token`,
          userinfo_endpoint: `${this.serverUrl}/oauth/userinfo`,
          jwks_uri: `${this.serverUrl}/oauth/jwks`,
          // Point to dynamic registration endpoint in Keycloak path format
          registration_endpoint: `${this.serverUrl}/realms/${realm}/clients-registrations/openid-connect`,
          introspection_endpoint: `${this.serverUrl}/oauth/introspect`,
          revocation_endpoint: `${this.serverUrl}/oauth/revoke`,
          scopes_supported: this.allowedScopes,
          // Ensure we support public clients
          token_endpoint_auth_methods_supported: [
            ...(metadata.token_endpoint_auth_methods_supported || []),
            'none'
          ].filter((v, i, a) => a.indexOf(v) === i) // Remove duplicates
        };
      } catch (error: any) {
        throw new Error(`Failed to fetch discovery metadata: ${error.message}`);
      }
    }
  }
  
  /**
   * Handle client registration
   */
  async registerClient(
    registrationRequest: any,
    initialAccessToken?: string
  ): Promise<any> {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    
    try {
      // Try to use C++ implementation if available
      return await this.authClient.proxyClientRegistration(
        authServerUrl!,
        registrationRequest,
        initialAccessToken,
        this.allowedScopes
      );
    } catch (mcpError) {
      // Fallback implementation until C++ functions are available
      const realm = process.env.KEYCLOAK_REALM || 'gopher-auth';
      
      try {
        // Add default scopes to registration request
        const request = {
          ...registrationRequest,
          scope: registrationRequest.scope || this.allowedScopes.join(' '),
        };
        
        const headers: Record<string, string> = {
          'Content-Type': 'application/json',
        };
        
        if (initialAccessToken) {
          headers['Authorization'] = `Bearer ${initialAccessToken}`;
        }
        
        console.log(`Registering client with Keycloak at: ${authServerUrl}/realms/${realm}/clients-registrations/openid-connect`);
        const response = await fetch(
          `${authServerUrl}/realms/${realm}/clients-registrations/openid-connect`,
          {
            method: 'POST',
            headers,
            body: JSON.stringify(request),
          }
        );
        
        const responseText = await response.text();
        console.log(`Registration response status: ${response.status}, body: ${responseText}`);
        
        if (!response.ok) {
          throw new Error(`HTTP ${response.status} ${response.statusText}`);
        }
        
        const data = JSON.parse(responseText);
        return data;
      } catch (error: any) {
        throw new Error(`Failed to register client: ${error.message}`);
      }
    }
  }
  
  /**
   * Build authorization redirect URL
   */
  buildAuthorizationUrl(queryParams: Record<string, string>): string {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    const params = new URLSearchParams(queryParams);
    return `${authServerUrl}/protocol/openid-connect/auth?${params.toString()}`;
  }
  
  /**
   * Exchange authorization code for token
   */
  async exchangeToken(tokenRequest: any): Promise<any> {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    const tokenUrl = `${authServerUrl}/protocol/openid-connect/token`;
    
    // Check if this looks like a public client (no secret provided)
    const isPublicClient = !tokenRequest.client_secret || tokenRequest.client_secret === '';
    
    // If it's a public client OR specifically mcp-inspector-public
    if (isPublicClient || tokenRequest.client_id === 'mcp-inspector-public') {
      console.log(`   Handling as public client: ${tokenRequest.client_id}`);
      // Remove any client_secret that might be present
      delete tokenRequest.client_secret;
      // Ensure we're not using client credentials in the body
      delete tokenRequest.client_assertion_type;
      delete tokenRequest.client_assertion;
    }
    
    console.log(`Token exchange request:`, {
      grant_type: tokenRequest.grant_type,
      client_id: tokenRequest.client_id,
      redirect_uri: tokenRequest.redirect_uri,
      code: tokenRequest.code ? 'present' : 'missing',
      code_verifier: tokenRequest.code_verifier ? 'present' : 'missing'
    });
    
    // For public clients, include client_id in the body (not in Basic auth)
    const headers: Record<string, string> = {
      'Content-Type': 'application/x-www-form-urlencoded',
    };
    
    // Don't add Authorization header for public clients
    if (tokenRequest.client_id !== 'mcp-inspector-public' && tokenRequest.client_secret) {
      // For confidential clients, use Basic auth
      const credentials = Buffer.from(`${tokenRequest.client_id}:${tokenRequest.client_secret}`).toString('base64');
      headers['Authorization'] = `Basic ${credentials}`;
      // Remove from body since we're using Basic auth
      const { client_secret, ...bodyWithoutSecret } = tokenRequest;
      tokenRequest = bodyWithoutSecret;
    }
    
    const response = await fetch(tokenUrl, {
      method: 'POST',
      headers,
      body: new URLSearchParams(tokenRequest).toString(),
    });
    
    const data = await response.json() as any;
    
    if (!response.ok) {
      console.error(`Token exchange failed:`, data);
      throw new Error(data.error_description || data.error || 'Token exchange failed');
    }

    console.log(`Token exchange authServerUrl: ${authServerUrl}`);
    console.log(`Token exchange successful, token type: ${data.token_type}, expires_in: ${data.expires_in}`);
    return data;
  }
  
  /**
   * Validate a token
   */
  async validateToken(
    token: string | undefined,
    options?: TokenValidationOptions
  ): Promise<AuthResult> {
    if (!token) {
      return {
        valid: false,
        error: 'No token provided',
        statusCode: 401,
        wwwAuthenticate: this.getWWWAuthenticateHeader(),
      };
    }
    
    try {
      const validationOptions: ValidationOptions = {
        audience: typeof options?.audience === 'string' 
          ? options.audience 
          : Array.isArray(options?.audience) 
            ? options.audience[0] 
            : this.tokenAudience,
        scopes: options?.requiredScopes?.join(' '),
      };
      
      const result = await this.authClient.validateToken(token, validationOptions);
      
      if (!result.valid) {
        // Normalize error message for consistency with tests
        const errorMessage = 'Token validation failed';
        return {
          valid: false,
          error: errorMessage,
          statusCode: 401,
          wwwAuthenticate: this.getWWWAuthenticateHeader({
            error: 'invalid_token',
            errorDescription: errorMessage,
          }),
        };
      }
      
      const payload = await this.authClient.extractPayload(token);
      
      return {
        valid: true,
        payload,
        statusCode: 200,
      };
    } catch (error: any) {
      return {
        valid: false,
        error: error.message,
        statusCode: 401,
        wwwAuthenticate: this.getWWWAuthenticateHeader({
          error: 'invalid_token',
          errorDescription: error.message,
        }),
      };
    }
  }
  
  /**
   * Extract token from Authorization header or query parameter
   * Session support is needed for MCP Inspector which doesn't handle tokens properly
   */
  extractToken(authHeader?: string, queryToken?: string, req?: any): string | undefined {
    // First try Authorization header
    if (authHeader?.startsWith('Bearer ')) {
      return authHeader.substring(7).trim();
    }
    
    // Then try query parameter
    if (queryToken) {
      return queryToken;
    }
    
    // Session cookie support for MCP Inspector
    // We need this because MCP Inspector doesn't properly send the token after OAuth
    if (req) {
      const sessionId = extractSessionId(req);
      if (sessionId) {
        const token = getTokenFromSession(sessionId);
        if (token) {
          console.log(`🍪 Using token from session ${sessionId.substring(0, 8)}... (MCP Inspector workaround)`);
          return token;
        }
      }
    }
    
    return undefined;
  }
  
  /**
   * Generate WWW-Authenticate header for 401 responses
   */
  private getWWWAuthenticateHeader(options?: {
    error?: string;
    errorDescription?: string;
  }): string {
    // Start with Bearer scheme
    let header = 'Bearer';
    
    // Always include resource_metadata first (required by MCP spec)
    header += ` resource_metadata="${this.serverUrl}/.well-known/oauth-protected-resource"`;
    
    // Include scopes if available (only include MCP scopes for clarity)
    const mcpScopes = this.allowedScopes.filter(s => s.startsWith('mcp:'));
    if (mcpScopes.length > 0) {
      header += `, scope="${mcpScopes.join(' ')}"`;
    }
    
    // Add error details if present
    if (options?.error) {
      header += `, error="${options.error}"`;
    }
    
    if (options?.errorDescription) {
      header += `, error_description="${options.errorDescription}"`;
    }
    
    return header;
  }
  
  /**
   * Handle OAuth callback and store token in session
   * This is for MCP Inspector support - it doesn't complete OAuth flow
   */
  async handleOAuthCallback(code: string, state: string, codeVerifier: string, res: any): Promise<{
    success: boolean;
    sessionId?: string;
    token?: string;
    error?: string;
  }> {
    try {
      // Use the configured client from environment (if available)
      const clientId = process.env.GOPHER_CLIENT_ID;
      const clientSecret = process.env.GOPHER_CLIENT_SECRET;
      
      if (!clientId || !clientSecret) {
        throw new Error('OAuth callback requires GOPHER_CLIENT_ID and GOPHER_CLIENT_SECRET in environment');
      }
      
      // Exchange code for token
      const tokenResponse = await this.exchangeToken({
        grant_type: 'authorization_code',
        code,
        client_id: clientId,
        client_secret: clientSecret,
        redirect_uri: `${this.serverUrl}/oauth/callback`,
        code_verifier: codeVerifier
      });
      
      if (!tokenResponse.access_token) {
        throw new Error('No access token in response');
      }
      
      // Validate the token to get payload
      const validationResult = await this.validateToken(tokenResponse.access_token);
      
      // Generate session and store token
      const sessionId = generateSessionId();
      storeTokenInSession(
        sessionId,
        tokenResponse.access_token,
        tokenResponse.expires_in || 3600,
        validationResult.payload
      );
      
      // Set session cookie
      if (res) {
        setSessionCookie(res, sessionId, tokenResponse.expires_in || 3600);
      }
      
      console.log(`✅ OAuth callback successful, session created: ${sessionId.substring(0, 8)}...`);
      
      return {
        success: true,
        sessionId,
        token: tokenResponse.access_token
      };
    } catch (error: any) {
      console.error('OAuth callback error:', error);
      return {
        success: false,
        error: error.message
      };
    }
  }
  
  /**
   * Handle OAuth callback with specific client credentials
   * Use this when you have a confidential client with a secret
   */
  async handleOAuthCallbackWithClient(
    code: string, 
    state: string, 
    codeVerifier: string,
    clientId: string,
    clientSecret: string,
    res: any
  ): Promise<{
    success: boolean;
    sessionId?: string;
    token?: string;
    error?: string;
  }> {
    try {
      // Exchange code for token with specific client
      const tokenResponse = await this.exchangeToken({
        grant_type: 'authorization_code',
        code,
        client_id: clientId,
        client_secret: clientSecret,
        redirect_uri: `${this.serverUrl}/oauth/callback`,
        code_verifier: codeVerifier
      });
      
      if (!tokenResponse.access_token) {
        throw new Error('No access token in response');
      }
      
      // Validate the token to get payload
      const validationResult = await this.validateToken(tokenResponse.access_token);
      
      // Generate session and store token
      const sessionId = generateSessionId();
      storeTokenInSession(
        sessionId,
        tokenResponse.access_token,
        tokenResponse.expires_in || 3600,
        validationResult.payload
      );
      
      // Set session cookie
      if (res) {
        setSessionCookie(res, sessionId, tokenResponse.expires_in || 3600);
      }
      
      console.log(`✅ OAuth callback successful with client ${clientId}, session created: ${sessionId.substring(0, 8)}...`);
      
      return {
        success: true,
        sessionId,
        token: tokenResponse.access_token
      };
    } catch (error: any) {
      console.error('OAuth callback error:', error);
      return {
        success: false,
        error: error.message
      };
    }
  }
  
  /**
   * Get the auth client instance
   */
  getAuthClient(): McpAuthClient {
    return this.authClient;
  }
  
  /**
   * Get the server URL
   */
  getServerUrl(): string {
    return this.serverUrl;
  }
  
  
  /**
   * Exchange a Keycloak access token for an external IDP token (RFC 8693)
   *
   * This is useful when users authenticate via a federated IDP through Keycloak,
   * and you need the original IDP's access token to call that IDP's APIs.
   *
   * Prerequisites:
   * - Token Exchange feature must be enabled in Keycloak (features=preview or features=token-exchange)
   * - The IDP must have "Store Tokens" and "Stored Tokens Readable" enabled
   * - Client and IDP must have token-exchange permissions configured
   *
   * @param options - Token exchange options
   * @returns The exchanged token from the external IDP
   * @throws TokenExchangeError if the exchange fails
   *
   * @example
   * ```typescript
   * const result = await oauth.exchangeTokenForExternalIDP({
   *   subjectToken: keycloakAccessToken,
   *   requestedIssuer: 'google', // IDP alias in Keycloak
   * });
   *
   * // Use result.access_token to call Google APIs
   * ```
   */
  async exchangeTokenForExternalIDP(options: TokenExchangeOptions): Promise<TokenExchangeResult> {
    // Get requested IDP
    const requestedIssuer = options.requestedIssuer;
    
    if (!requestedIssuer) {
      throw new TokenExchangeError('invalid_request', 'IDP alias (requested_issuer) is required');
    }
    
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    if (!authServerUrl) {
      throw new TokenExchangeError('config_error', 'Auth server URL not configured');
    }

    const clientId = process.env.GOPHER_CLIENT_ID;
    const clientSecret = process.env.GOPHER_CLIENT_SECRET;
    
    if (!clientId || !clientSecret) {
      throw new TokenExchangeError('config_error', 'Client ID and secret are required for token exchange');
    }

    const tokenEndpoint = `${authServerUrl}/protocol/openid-connect/token`;

    // Build the token exchange request body
    const params = new URLSearchParams();
    params.append('grant_type', 'urn:ietf:params:oauth:grant-type:token-exchange');
    params.append('client_id', clientId);
    params.append('client_secret', clientSecret);
    params.append('subject_token', options.subjectToken);
    params.append('subject_token_type', 'urn:ietf:params:oauth:token-type:access_token');
    params.append('requested_issuer', requestedIssuer!);

    // Optional: requested token type
    if (options.requestedTokenType) {
      const tokenTypeMap: Record<string, string> = {
        'access_token': 'urn:ietf:params:oauth:token-type:access_token',
        'refresh_token': 'urn:ietf:params:oauth:token-type:refresh_token',
        'id_token': 'urn:ietf:params:oauth:token-type:id_token',
      };
      const tokenType = tokenTypeMap[options.requestedTokenType];
      if (tokenType) {
        params.append('requested_token_type', tokenType);
      }
    }

    // Optional: audience
    if (options.audience) {
      params.append('audience', options.audience);
    }

    // Optional: scope
    if (options.scope) {
      params.append('scope', options.scope);
    }

    try {
      console.log(`🔄 Exchanging token for external IDP: ${requestedIssuer}`);
      
      const response = await fetch(tokenEndpoint, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: params.toString(),
      });

      const data = await response.json() as {
        access_token?: string;
        token_type?: string;
        expires_in?: number;
        issued_token_type?: string;
        refresh_token?: string;
        scope?: string;
        error?: string;
        error_description?: string;
      };

      if (!response.ok) {
        throw new TokenExchangeError(
          data.error || 'unknown_error',
          data.error_description
        );
      }

      if (!data.access_token) {
        throw new TokenExchangeError('invalid_response', 'No access token in response');
      }

      console.log(`✅ Token exchange successful, got ${data.token_type} token for ${requestedIssuer}`);

      return {
        access_token: data.access_token,
        token_type: data.token_type || 'Bearer',
        expires_in: data.expires_in,
        issued_token_type: data.issued_token_type || 'urn:ietf:params:oauth:token-type:access_token',
        refresh_token: data.refresh_token,
        scope: data.scope,
      };
    } catch (error) {
      if (error instanceof TokenExchangeError) {
        throw error;
      }
      throw new TokenExchangeError(
        'exchange_failed',
        error instanceof Error ? error.message : 'Unknown error during token exchange'
      );
    }
  }

  /**
   * Get the token endpoint URL for this auth server
   * Useful for debugging or direct API calls
   */
  getTokenEndpoint(): string {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'] || '';
    return `${authServerUrl}/protocol/openid-connect/token`;
  }
  
  /**
   * Cleanup resources
   */
  async destroy(): Promise<void> {
    await this.authClient.destroy();
  }
}