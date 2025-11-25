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

// Export FFI bindings for advanced users
export {
  getAuthFFI,
  hasAuthSupport,
  AuthErrorCodes
} from './mcp-auth-ffi-bindings';