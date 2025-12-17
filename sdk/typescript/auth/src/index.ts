/**
 * @file index.ts
 * @brief Main entry point for MCP OAuth SDK
 *
 * This module exports the OAuth authentication infrastructure
 */

// Authentication API
export * from "./auth";

// OAuth Helper (framework-agnostic)
export { OAuthHelper } from "./oauth-helper";
export type { OAuthConfig, TokenValidationOptions, AuthResult } from "./oauth-helper";

// Session management
export * from "./session-manager";

// Express adapter for easy integration
export * from "./express-adapter";