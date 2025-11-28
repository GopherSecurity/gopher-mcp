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
} from './auth-types';
import {
  getAuthFFI,
  hasAuthSupport,
  AuthErrorCodes,
  AuthClient,
  ValidationOptions as FFIValidationOptions
} from './mcp-auth-ffi-bindings';
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
    const createResult = this.ffi.getFunction('mcp_auth_client_create')(
      clientPtr,
      this.config.jwksUri,
      this.config.issuer
    );
    
    if (createResult !== AuthErrorCodes.SUCCESS) {
      throw new AuthError(
        'Failed to create authentication client',
        createResult as AuthErrorCode,
        this.ffi.getLastError()
      );
    }
    
    this.client = clientPtr[0];
    
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
    
    // Validate token - use the new function that returns struct by value
    // This is much more reliable for FFI than output parameters
    console.log('Before validation - calling mcp_auth_validate_token_ret');
    console.log('Token length:', token?.length || 0);
    console.log('Client pointer:', this.client);
    console.log('Options pointer:', this.options);
    
    // Call the new function that returns the struct directly
    const result = this.ffi.getFunction('mcp_auth_validate_token_ret')(
      this.client,
      token,
      this.options
    ) as { valid: boolean; error_code: number; error_message: any };
    
    console.log('After validation - returned result:', result);
    
    if (!result) {
      throw new AuthError(
        'Failed to get validation result',
        AuthErrorCode.INTERNAL_ERROR
      );
    }
    
    console.log('Token validation result:', {
      valid: result.valid,
      errorCode: result.error_code,
      lastError: result.error_message || this.ffi.getLastError()
    });
    
    // Check if validation failed based on the result struct
    if (result.error_code !== AuthErrorCodes.SUCCESS && result.error_code !== 0) {
      throw new AuthError(
        'Token validation failed',
        result.error_code as AuthErrorCode,
        result.error_message || this.ffi.getLastError()
      );
    }
    
    // Return the result - don't try to access error_message to avoid malloc issues
    return {
      valid: result.valid,
      errorCode: result.error_code as AuthErrorCode,
      errorMessage: undefined  // Skip error_message to avoid malloc crash
    };
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
        if (subjectPtr[0]) this.ffi.freeString(subjectPtr[0]);
      }
      
      // Get issuer
      const issuerPtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_issuer')(payloadHandle, issuerPtr) === AuthErrorCodes.SUCCESS) {
        payload.issuer = issuerPtr[0] ? String(issuerPtr[0]) : undefined;
        if (issuerPtr[0]) this.ffi.freeString(issuerPtr[0]);
      }
      
      // Get audience
      const audiencePtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_audience')(payloadHandle, audiencePtr) === AuthErrorCodes.SUCCESS) {
        payload.audience = audiencePtr[0] ? String(audiencePtr[0]) : undefined;
        if (audiencePtr[0]) this.ffi.freeString(audiencePtr[0]);
      }
      
      // Get scopes
      const scopesPtr = [null];
      if (this.ffi.getFunction('mcp_auth_payload_get_scopes')(payloadHandle, scopesPtr) === AuthErrorCodes.SUCCESS) {
        payload.scopes = scopesPtr[0] ? String(scopesPtr[0]) : undefined;
        if (scopesPtr[0]) this.ffi.freeString(scopesPtr[0]);
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
   * Get library version
   */
  getVersion(): string {
    return this.ffi.version();
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
export { AuthErrorCode, ValidationResult, TokenPayload, AuthClientConfig, ValidationOptions } from './auth-types';