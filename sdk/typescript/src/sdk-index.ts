/**
 * Gopher Auth SDK for MCP Servers
 */

// Export OAuth helper
export { OAuthHelper } from './oauth-helper.js';

// Export Express adapter
export { 
  ExpressOAuthConfig,
  createAuthMiddleware,
  createOAuthRouter,
  setupMCPOAuth
} from './express-adapter.js';

// Export types
export type { 
  AuthResult,
  OAuthConfig,
  TokenValidationOptions
} from './oauth-helper.js';

export type {
  TokenPayload
} from './auth-types.js';