/**
 * @file oauth-helper.ts
 * @brief Generic OAuth authentication helper for MCP servers
 * 
 * Provides OAuth functionality without framework dependency
 */

import { McpAuthClient } from './mcp-auth-api';
import type { AuthClientConfig, ValidationOptions, TokenPayload } from './auth-types';

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
    return this.authClient.generateProtectedResourceMetadata(this.serverUrl, this.allowedScopes);
  }
  
  /**
   * Get OAuth discovery metadata
   */
  async getDiscoveryMetadata(): Promise<any> {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    return this.authClient.proxyDiscoveryMetadata(
      this.serverUrl,
      authServerUrl!,
      this.allowedScopes
    );
  }
  
  /**
   * Handle client registration
   */
  async registerClient(
    registrationRequest: any,
    initialAccessToken?: string
  ): Promise<any> {
    const authServerUrl = this.tokenIssuer || process.env['GOPHER_AUTH_SERVER_URL'];
    return this.authClient.proxyClientRegistration(
      authServerUrl!,
      registrationRequest,
      initialAccessToken,
      this.allowedScopes
    );
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
    
    const response = await fetch(tokenUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: new URLSearchParams(tokenRequest).toString(),
    });
    
    const data = await response.json() as any;
    
    if (!response.ok) {
      throw new Error(data.error_description || data.error || 'Token exchange failed');
    }
    
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
        return {
          valid: false,
          error: result.errorMessage || 'Invalid token',
          statusCode: 401,
          wwwAuthenticate: this.getWWWAuthenticateHeader({
            error: 'invalid_token',
            errorDescription: result.errorMessage,
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
   */
  extractToken(authHeader?: string, queryToken?: string): string | undefined {
    if (authHeader?.startsWith('Bearer ')) {
      return authHeader.substring(7).trim();
    }
    return queryToken;
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
    
    header += `, resource_metadata="${this.serverUrl}/.well-known/oauth-protected-resource"`;
    
    return header;
  }
  
  /**
   * Get the auth client instance
   */
  getAuthClient(): McpAuthClient {
    return this.authClient;
  }
  
  /**
   * Cleanup resources
   */
  async destroy(): Promise<void> {
    await this.authClient.destroy();
  }
}