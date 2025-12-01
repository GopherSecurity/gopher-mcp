/**
 * @file auth.ts
 * @brief Authentication module exports for subpath import
 *
 * This file allows importing authentication functionality via
 * @mcp/filter-sdk/auth
 */

// Export all authentication types
export * from './auth-types';

// Export high-level API
export {
  McpAuthClient,
  AuthError,
  validateToken,
  extractTokenPayload,
  isAuthAvailable
} from './mcp-auth-api';

// Export OAuth helper (framework-agnostic)
export {
  OAuthHelper,
  type OAuthConfig,
  type TokenValidationOptions,
  type AuthResult
} from './oauth-helper';


// Export FFI bindings for advanced users
export {
  getAuthFFI,
  hasAuthSupport,
  AuthErrorCodes
} from './mcp-auth-ffi-bindings';

// Export utility functions
export function errorCodeToString(code: number): string {
  const errorMessages = new Map<number, string>([
    [0, 'Success'],
    [-1000, 'Invalid token'],
    [-1001, 'Token expired'],
    [-1002, 'Invalid signature'],
    [-1003, 'Invalid issuer'],
    [-1004, 'Invalid audience'],
    [-1005, 'Insufficient scope'],
    [-1006, 'JWKS fetch failed'],
    [-1007, 'Key not found'],
    [-1008, 'Internal error'],
    [-1009, 'Not initialized'],
    [-1010, 'Invalid argument']
  ]);
  
  return errorMessages.get(code) || `Unknown error (${code})`;
}

export function isSuccess(code: number): boolean {
  return code === 0;
}

export function isAuthError(code: number): boolean {
  return code < 0 && code >= -1010;
}