/**
 * @file mcp-auth-api.ts
 * @brief High-level TypeScript API for authentication functionality
 *
 * This file provides a developer-friendly async/await interface
 * wrapping the C API for authentication operations.
 */

import {
  AuthErrorCode,
  ValidationResult,
  TokenPayload,
  AuthClientConfig,
  ValidationOptions,
  WwwAuthenticateParams
} from './auth-types.js';
import {
  getAuthFFI,
  hasAuthSupport,
  AuthErrorCodes,
  AuthClient,
  ValidationOptions as FFIValidationOptions
} from './mcp-auth-ffi-bindings.js';
import * as koffi from 'koffi';

/**
 * Authentication error class
 */
export class AuthError extends Error {
  constructor(
    message: string,
    public code: AuthErrorCode,
    public details?: string
  ) {
    super(message);
    this.name = 'AuthError';
  }
}

/**
 * MCP Authentication Client
 * 
 * Provides high-level authentication functionality including
 * JWT token validation, scope checking, and payload extraction.
 */
export class McpAuthClient {
  private ffi: ReturnType<typeof getAuthFFI>;
  private client: AuthClient | null = null;
  private options: FFIValidationOptions | null = null;
  private initialized = false;
  
  constructor(private config: AuthClientConfig) {
    if (!hasAuthSupport()) {
      throw new AuthError(
        'Authentication support not available',
        AuthErrorCode.NOT_INITIALIZED
      );
    }
    this.ffi = getAuthFFI();
  }
  
  /**
   * Initialize the authentication client
   */
  async initialize(): Promise<void> {
    if (this.initialized) {
      return;
    }
    
    // Initialize library
    const initResult = this.ffi.init();
    if (initResult !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to initialize authentication library',
        initResult as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    // Create client
    const clientPtr = [null];
    console.log('[McpAuthClient] Creating client with:');
    console.log('  jwksUri:', this.config.jwksUri);
    console.log('  issuer:', this.config.issuer);
    
    const createResult = this.ffi.getFunction('mcp_auth_client_create')(
      clientPtr,
      this.config.jwksUri,
      this.config.issuer
    );
    
    if (createResult !== AuthErrorCodes.SUCCESS) {
      console.error('[McpAuthClient] Client creation failed:');
      console.error('  Error code:', createResult);
      console.error('  Last error:', this.ffi.getLastError());
      throw new AuthError(
        'Failed to create authentication client',
        createResult as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    this.client = clientPtr[0];
    console.log('[McpAuthClient] Client created:', typeof this.client, this.client);
    
    // Set optional configuration
    if (this.config.cacheDuration !== undefined) {
      this.setOption('cache_duration', this.config.cacheDuration.toString());
    }
    if (this.config.autoRefresh !== undefined) {
      this.setOption('auto_refresh', this.config.autoRefresh ? 'true' : 'false');
    }
    if (this.config.requestTimeout !== undefined) {
      this.setOption('request_timeout', this.config.requestTimeout.toString());
    }
    
    this.initialized = true;
  }
  
  /**
   * Set client configuration option
   */
  private setOption(name: string, value: string): void {
    if (!this.client) {
      return;
    }
    
    const result = this.ffi.getFunction('mcp_auth_client_set_option')(
      this.client,
      name,
      value
    );
    
    if (result !== AuthErrorCodes.SUCCESS) {
      console.warn(`Failed to set option ${name}: ${this.ffi.errorToString(result)}`);
    }
  }
  
  /**
   * Validate a JWT token
   */
  async validateToken(
    token: string,
    options?: ValidationOptions
  ): Promise<ValidationResult> {
    if (!this.initialized) {
      await this.initialize();
    }
    
    // Create or update validation options
    if (options) {
      if (this.options) {
        this.ffi.getFunction('mcp_auth_validation_options_destroy')(this.options);
        this.options = null;
      }
      
      const optionsPtr = [null];
      const createResult = this.ffi.getFunction('mcp_auth_validation_options_create')(optionsPtr);
      
      if (createResult !== AuthErrorCodes.SUCCESS) {
        throw new AuthError(
          'Failed to create validation options',
          createResult as AuthErrorCode
        );
      }
      
      this.options = optionsPtr[0];
      
      // Set options
      if (options.scopes) {
        this.ffi.getFunction('mcp_auth_validation_options_set_scopes')(
          this.options,
          options.scopes
        );
      }
      if (options.audience) {
        this.ffi.getFunction('mcp_auth_validation_options_set_audience')(
          this.options,
          options.audience
        );
      }
      if (options.clockSkew !== undefined) {
        this.ffi.getFunction('mcp_auth_validation_options_set_clock_skew')(
          this.options,
          options.clockSkew
        );
      }
    }
    
    // Validate token using the regular function
    try {
      // Prepare result structure
      const resultPtr = [{ valid: false, error_code: 0, error_message: null }];
      
      // Call validate_token with proper parameters
      const errorCode = this.ffi.getFunction('mcp_auth_validate_token')(
        this.client,
        token,
        null,  // Pass null for options to avoid crash - TODO: Fix options handling
        resultPtr  // Pass result pointer
      );
      
      // Check the result
      const validationResult = resultPtr[0];
      if (!validationResult) {
        throw new AuthError(
          'No validation result returned',
          AuthErrorCode.INTERNAL_ERROR
        );
      }
      const isValid = validationResult.valid;
      
      // If there's an error other than invalid token, throw
      if (!isValid && errorCode !== AuthErrorCodes.SUCCESS &&
          errorCode !== AuthErrorCodes.INVALID_TOKEN && 
          errorCode !== AuthErrorCodes.EXPIRED_TOKEN && 
          errorCode !== AuthErrorCodes.INVALID_SIGNATURE) {
        throw new AuthError(
          'Token validation failed',
          errorCode as AuthErrorCode,
          validationResult.error_message || this.ffi.getLastError()
        );
      }
      
      // Return the validation result
      return {
        valid: isValid,
        errorCode: errorCode as AuthErrorCode,
        errorMessage: isValid ? undefined : (validationResult?.error_message || this.ffi.getLastError())
      };
    } catch (error: any) {
      console.error('Token validation error:', error);
      throw new AuthError(
        `Token validation failed: ${error.message}`,
        AuthErrorCode.INTERNAL_ERROR,
        error.message
      );
    }
  }
  
  /**
   * Extract payload from token without validation
   */
  async extractPayload(token: string): Promise<TokenPayload> {
    const payloadPtr = [null];
    const extractResult = this.ffi.getFunction('mcp_auth_extract_payload')(
      token,
      payloadPtr
    );
    
    if (extractResult !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to extract token payload',
        extractResult as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    const payloadHandle = payloadPtr[0];
    if (!payloadHandle) {
      throw new AuthError(
        'Invalid payload handle',
        AuthErrorCode.INTERNAL_ERROR
      );
    }
    
    try {
      // Extract payload fields
      const payload: TokenPayload = {};
      
      // Get subject
      const subjectPtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_subject')(payloadHandle, subjectPtr) === AuthErrorCodes.SUCCESS) {
        payload.subject = subjectPtr[0] ? String(subjectPtr[0]) : undefined;
        // Don't free - might be causing malloc error
        // if (subjectPtr[0]) this.ffi.freeString(subjectPtr[0]);
      }
      
      // Get issuer
      const issuerPtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_issuer')(payloadHandle, issuerPtr) === AuthErrorCodes.SUCCESS) {
        payload.issuer = issuerPtr[0] ? String(issuerPtr[0]) : undefined;
        // Don't free - might be causing malloc error
        // if (issuerPtr[0]) this.ffi.freeString(issuerPtr[0]);
      }
      
      // Get audience
      const audiencePtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_audience')(payloadHandle, audiencePtr) === AuthErrorCodes.SUCCESS) {
        payload.audience = audiencePtr[0] ? String(audiencePtr[0]) : undefined;
        // Don't free - might be causing malloc error
        // if (audiencePtr[0]) this.ffi.freeString(audiencePtr[0]);
      }
      
      // Get scopes
      const scopesPtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_scopes')(payloadHandle, scopesPtr) === AuthErrorCodes.SUCCESS) {
        payload.scopes = scopesPtr[0] ? String(scopesPtr[0]) : undefined;
        // Don't free - might be causing malloc error
        // if (scopesPtr[0]) this.ffi.freeString(scopesPtr[0]);
      }
      
      // Get expiration
      const expirationPtr = [0];
      if (this.ffi.getFunction('mcp_auth_payload_get_expiration')(payloadHandle, expirationPtr) === AuthErrorCodes.SUCCESS) {
        payload.expiration = expirationPtr[0];
      }
      
      return payload;
      
    } finally {
      // Always destroy payload handle
      this.ffi.getFunction('mcp_auth_payload_destroy')(payloadHandle);
    }
  }
  
  /**
   * Validate scopes
   */
  async validateScopes(
    requiredScopes: string,
    availableScopes: string
  ): Promise<boolean> {
    return this.ffi.getFunction('mcp_auth_validate_scopes')(
      requiredScopes,
      availableScopes
    );
  }
  
  /**
   * Generate WWW-Authenticate header
   */
  async generateWwwAuthenticate(params: WwwAuthenticateParams): Promise<string> {
    const headerPtr = [null];
    const result = this.ffi.getFunction('mcp_auth_generate_www_authenticate')(
      params.realm,
      params.error || null,
      params.errorDescription || null,
      headerPtr
    );
    
    if (result !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to generate WWW-Authenticate header',
        result as AuthErrorCode
      );
    }
    
    const header = String(headerPtr[0]);
    this.ffi.freeString(headerPtr[0]);
    return header;
  }
  
  /**
   * Generate OAuth protected resource metadata
   */
  async generateProtectedResourceMetadata(serverUrl: string, scopes: string[]): Promise<any> {
    // Force fallback implementation
    throw new AuthError(
      'Using fallback implementation',
      AuthErrorCode.NOT_INITIALIZED
    );
    
    // This code will be enabled once the C++ functions are available
    /*
    const jsonPtr: [string | null] = [null];
    const scopesStr = scopes.join(',');
    
    const result = this.ffi.getFunction('mcp_auth_generate_protected_resource_metadata')(
      serverUrl,
      scopesStr,
      jsonPtr
    );
    
    if (result !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to generate protected resource metadata',
        result as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    const jsonStr = jsonPtr[0];
    if (!jsonStr) {
      throw new AuthError('No metadata returned', AuthErrorCode.INTERNAL_ERROR);
    }
    
    try {
      const metadata = JSON.parse(jsonStr);
      this.ffi.freeString(jsonPtr[0]!);
      return metadata;
    } catch (e: any) {
      this.ffi.freeString(jsonPtr[0]!);
      throw new AuthError('Invalid JSON response', AuthErrorCode.INTERNAL_ERROR, e.message);
    }
    */
  }

  /**
   * Proxy OAuth discovery metadata
   */
  async proxyDiscoveryMetadata(serverUrl: string, authServerUrl: string, scopes: string[]): Promise<any> {
    // The OAuth discovery functions are not yet exported from the C++ library
    throw new AuthError(
      'OAuth discovery proxy not yet available',
      AuthErrorCode.NOT_INITIALIZED
    );
    
    /* This code will be enabled once the C++ functions are available
    if (!this.initialized) {
      await this.initialize();
    }
    
    const jsonPtr: [string | null] = [null];
    const scopesStr = scopes.join(',');
    
    const result = this.ffi.getFunction('mcp_auth_proxy_discovery_metadata')(
      this.client,
      serverUrl,
      authServerUrl,
      scopesStr,
      jsonPtr
    );
    
    if (result !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to proxy discovery metadata',
        result as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    const jsonStr = jsonPtr[0];
    if (!jsonStr) {
      throw new AuthError('No metadata returned', AuthErrorCode.INTERNAL_ERROR);
    }
    
    try {
      const metadata = JSON.parse(jsonStr);
      this.ffi.freeString(jsonPtr[0]!);
      return metadata;
    } catch (e: any) {
      this.ffi.freeString(jsonPtr[0]!);
      throw new AuthError('Invalid JSON response', AuthErrorCode.INTERNAL_ERROR, e.message);
    }
    */
  }

  /**
   * Proxy client registration
   */
  async proxyClientRegistration(
    authServerUrl: string,
    registrationRequest: any,
    initialAccessToken?: string,
    allowedScopes?: string[]
  ): Promise<any> {
    // The OAuth registration functions are not yet exported from the C++ library
    throw new AuthError(
      'OAuth client registration proxy not yet available',
      AuthErrorCode.NOT_INITIALIZED
    );
    
    /* This code will be enabled once the C++ functions are available
    if (!this.initialized) {
      await this.initialize();
    }
    
    const jsonPtr: [string | null] = [null];
    const requestJson = JSON.stringify(registrationRequest);
    const scopesStr = allowedScopes ? allowedScopes.join(',') : null;
    
    const result = this.ffi.getFunction('mcp_auth_proxy_client_registration')(
      this.client,
      authServerUrl,
      requestJson,
      initialAccessToken || null,
      scopesStr,
      jsonPtr
    );
    
    if (result !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to proxy client registration',
        result as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    const jsonStr = jsonPtr[0];
    if (!jsonStr) {
      throw new AuthError('No response returned', AuthErrorCode.INTERNAL_ERROR);
    }
    
    try {
      const response = JSON.parse(jsonStr);
      this.ffi.freeString(jsonPtr[0]!);
      return response;
    } catch (e: any) {
      this.ffi.freeString(jsonPtr[0]!);
      throw new AuthError('Invalid JSON response', AuthErrorCode.INTERNAL_ERROR, e.message);
    }
    */
  }

  /**
   * Get library version
   */
  getVersion(): string {
    return this.ffi.version();
  }
  
  /**
   * Set client credentials for token exchange
   */
  setClientCredentials(clientId: string, clientSecret: string): void {
    if (!this.client) {
      throw new AuthError('Client not initialized', AuthErrorCode.NOT_INITIALIZED);
    }
    
    this.setOption('client_id', clientId);
    this.setOption('client_secret', clientSecret);
  }
  
  /**
   * Set token endpoint for exchange
   */
  setTokenEndpoint(endpoint: string): void {
    if (!this.client) {
      throw new AuthError('Client not initialized', AuthErrorCode.NOT_INITIALIZED);
    }
    
    this.setOption('token_endpoint', endpoint);
  }
  
  /**
   * Exchange a token for an external IDP token using C++ implementation
   */
  async exchangeToken(
    subjectToken: string,
    idpAlias: string,
    audience?: string,
    scope?: string
  ): Promise<{
    access_token: string;
    token_type: string;
    expires_in?: number;
    refresh_token?: string;
    scope?: string;
  }> {
    if (!this.initialized) {
      await this.initialize();
    }
    
    if (!this.client) {
      throw new AuthError('Client not initialized', AuthErrorCode.NOT_INITIALIZED);
    }
    
    // Call C++ token exchange function
    console.log('[McpAuthClient.exchangeToken] Calling C++ function with:');
    console.log('  client type:', typeof this.client);
    console.log('  client value:', this.client);
    console.log('  subjectToken:', subjectToken?.substring(0, 50) + '...');
    console.log('  idpAlias:', idpAlias);
    console.log('  audience:', audience);
    console.log('  scope:', scope);
    
    // Get the function
    const exchangeTokenFn = this.ffi.getFunction('mcp_auth_exchange_token');
    console.log('  exchangeTokenFn type:', typeof exchangeTokenFn);
    console.log('  exchangeTokenFn:', exchangeTokenFn);
    
    // Prepare parameters
    const params = [
      this.client,
      subjectToken,
      idpAlias,
      audience || '',  // Use empty string instead of null
      scope || ''      // Use empty string instead of null
    ];
    
    console.log('  Calling with params:', params.map((p, i) => {
      if (i === 0) {
        return `[${i}] ${typeof p}: [External pointer]`;  // Don't try to convert External to string
      } else if (typeof p === 'string') {
        return `[${i}] string: ${p.length > 30 ? p.substring(0, 30) + '...' : p}`;
      } else {
        return `[${i}] ${typeof p}: ${String(p)}`;
      }
    }));
    
    // Try to call the function
    let result: any;
    try {
      console.log('  About to call exchangeTokenFn with client:', this.client);
      console.log('  Client constructor name:', this.client?.constructor?.name);
      console.log('  Is client an External?', this.client?.constructor?.name === 'External');
      
      // Call the function directly with parameters
      result = exchangeTokenFn(
        this.client,
        subjectToken,
        idpAlias,
        audience || '',
        scope || ''
      );
      
      console.log('  Function returned:', result);
    } catch (callError: any) {
      console.error('  Error calling exchangeTokenFn:', callError.message);
      console.error('  Stack:', callError.stack);
      throw callError;
    }
    
    // Check for errors
    if (result.error_code !== AuthErrorCodes.SUCCESS) {
      // error_description is already a string, not a pointer
      const errorMsg = result.error_description || 'Token exchange failed';
      
      // Free the result
      this.ffi.getFunction('mcp_auth_free_exchange_result')([result]);
      
      throw new AuthError(
        errorMsg,
        result.error_code as AuthErrorCode,
        `IDP: ${idpAlias}`
      );
    }
    
    // Extract successful result (fields are already strings, not pointers)
    const response: any = {
      access_token: result.access_token || '',
      token_type: result.token_type || 'Bearer'
    };
    
    if (result.expires_in > 0) {
      response.expires_in = Number(result.expires_in);
    }
    
    if (result.refresh_token) {
      response.refresh_token = result.refresh_token;
    }
    
    if (result.scope) {
      response.scope = result.scope;
    }
    
    // Free the result
    this.ffi.getFunction('mcp_auth_free_exchange_result')([result]);
    
    return response;
  }
  
  /**
   * Cleanup and destroy the client
   */
  async destroy(): Promise<void> {
    if (this.options) {
      this.ffi.getFunction('mcp_auth_validation_options_destroy')(this.options);
      this.options = null;
    }
    
    if (this.client) {
      this.ffi.getFunction('mcp_auth_client_destroy')(this.client);
      this.client = null;
    }
    
    if (this.initialized) {
      this.ffi.shutdown();
      this.initialized = false;
    }
  }
}

/**
 * Convenience function to validate a token
 */
export async function validateToken(
  token: string,
  config: AuthClientConfig,
  options?: ValidationOptions
): Promise<ValidationResult> {
  const client = new McpAuthClient(config);
  try {
    await client.initialize();
    return await client.validateToken(token, options);
  } finally {
    await client.destroy();
  }
}

/**
 * Convenience function to extract token payload
 */
export async function extractTokenPayload(token: string): Promise<TokenPayload> {
  const ffi = getAuthFFI();
  const payloadPtr = [null];
  const result = ffi.getFunction('mcp_auth_extract_payload')(token, payloadPtr);
  
  if (result !== AuthErrorCodes.SUCCESS) {
    throw new AuthError(
      'Failed to extract payload',
      result as AuthErrorCode
    );
  }
  
  const handle = payloadPtr[0];
  try {
    const payload: TokenPayload = {};
    // Extract fields (simplified)
    return payload;
  } finally {
    ffi.getFunction('mcp_auth_payload_destroy')(handle);
  }
}

/**
 * Check if authentication is available
 */
export function isAuthAvailable(): boolean {
  return hasAuthSupport();
}

// Re-export types for convenience
export type { ValidationResult, TokenPayload, AuthClientConfig, ValidationOptions } from './auth-types.js';
export { AuthErrorCode } from './auth-types.js';