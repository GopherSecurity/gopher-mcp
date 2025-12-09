/**
 * @file mcp-auth-ffi-bindings.ts
 * @brief FFI bindings for MCP Authentication C API
 *
 * This file provides the low-level FFI bindings that connect TypeScript
 * to the authentication C API functions using koffi.
 */

import * as koffi from "koffi";
import { arch, platform } from "os";
// Library path helper (extracted from mcp-ffi-bindings)
function getLibraryPath(): string {
  const env = process.env;
  // Check for environment variable override first
  if (env.MCP_LIBRARY_PATH) {
    return env.MCP_LIBRARY_PATH;
  }
  
  // Default paths for the auth library
  const possiblePaths = [
    // SDK bundled library - try auth library first
    __dirname + "/../lib/libgopher_mcp_auth.0.1.0.dylib",
    __dirname + "/../../lib/libgopher_mcp_auth.0.1.0.dylib",
    // Fallback to original library name
    __dirname + "/../lib/libgopher_mcp_c.0.1.0.dylib",
    __dirname + "/../../lib/libgopher_mcp_c.0.1.0.dylib",
    // System paths
    "/usr/local/lib/libgopher_mcp_auth.dylib",
    "/usr/local/lib/libgopher_mcp_c.dylib",
    "/opt/homebrew/lib/libgopher_mcp_auth.dylib",
    "/opt/homebrew/lib/libgopher_mcp_c.dylib",
  ];
  
  // Try to find the library
  const fs = require('fs');
  for (const path of possiblePaths) {
    if (fs.existsSync(path)) {
      // Log which library is being used
      if (process.env.MCP_DEBUG) {
        console.log(`[MCP Auth] Loading library from: ${path}`);
        const stats = fs.statSync(path);
        const sizeKB = Math.round(stats.size / 1024);
        console.log(`[MCP Auth] Library size: ${sizeKB} KB`);
      }
      return path;
    }
  }
  
  throw new Error("MCP Auth library not found (libgopher_mcp_auth or libgopher_mcp_c). Set MCP_LIBRARY_PATH environment variable.");
}

/**
 * Authentication error codes matching C API
 */
export const AuthErrorCodes = {
  SUCCESS: 0,
  INVALID_TOKEN: -1000,
  EXPIRED_TOKEN: -1001,
  INVALID_SIGNATURE: -1002,
  INVALID_ISSUER: -1003,
  INVALID_AUDIENCE: -1004,
  INSUFFICIENT_SCOPE: -1005,
  JWKS_FETCH_FAILED: -1006,
  INVALID_KEY: -1007,
  NETWORK_ERROR: -1008,
  INVALID_CONFIG: -1009,
  OUT_OF_MEMORY: -1010,
  INVALID_PARAMETER: -1011,
  NOT_INITIALIZED: -1012,
  INTERNAL_ERROR: -1013
} as const;

/**
 * Validation result structure matching C API
 */
export interface ValidationResultStruct {
  valid: boolean;
  error_code: number;
  error_message: any;  // koffi C string pointer
}

// Type definitions for FFI
const authTypes = {
  // Opaque handles
  mcp_auth_client_t: koffi.pointer('mcp_auth_client_t', koffi.opaque()),
  mcp_auth_token_payload_t: koffi.pointer('mcp_auth_token_payload_t', koffi.opaque()),
  mcp_auth_validation_options_t: koffi.pointer('mcp_auth_validation_options_t', koffi.opaque()),
  mcp_auth_metadata_t: koffi.pointer('mcp_auth_metadata_t', koffi.opaque()),
  
  // Error type
  mcp_auth_error_t: 'int',
  
  // Validation result structure matching C API
  mcp_auth_validation_result_t: koffi.struct('mcp_auth_validation_result_t', {
    valid: 'bool',
    error_code: 'int32',
    error_message: 'char*'  // Pointer to error message
  })
};

/**
 * Auth FFI library wrapper
 */
export class AuthFFILibrary {
  private lib: koffi.IKoffiLib;
  private functions: Record<string, Function> = {};
  private libraryPath: string;
  
  constructor() {
    this.libraryPath = '';  // Initialize first
    try {
      // Try to load the library using the same pattern as main FFI bindings
      this.libraryPath = getLibraryPath();
      if (process.env.MCP_DEBUG) {
        console.log(`Loading auth FFI library from: ${this.libraryPath}`);
      }
      this.lib = koffi.load(this.libraryPath);
      this.bindFunctions();
    } catch (error) {
      console.error(`Failed to load authentication library from ${this.libraryPath}:`, error);
      throw new Error(`Failed to load authentication library: ${error}`);
    }
  }
  
  /**
   * Bind all authentication C API functions
   */
  private bindFunctions(): void {
    try {
      // Library initialization
      this.functions['mcp_auth_init'] = this.lib.func('mcp_auth_init', authTypes.mcp_auth_error_t, []);
      this.functions['mcp_auth_shutdown'] = this.lib.func('mcp_auth_shutdown', authTypes.mcp_auth_error_t, []);
      this.functions['mcp_auth_version'] = this.lib.func('mcp_auth_version', 'const char*', []);
      
      // Client lifecycle
      this.functions['mcp_auth_client_create'] = this.lib.func(
        'mcp_auth_client_create',
        authTypes.mcp_auth_error_t,
        [koffi.out(koffi.pointer(authTypes.mcp_auth_client_t)), 'const char*', 'const char*']
      );
      this.functions['mcp_auth_client_destroy'] = this.lib.func(
        'mcp_auth_client_destroy',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_client_t]
      );
      this.functions['mcp_auth_client_set_option'] = this.lib.func(
        'mcp_auth_client_set_option',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_client_t, 'const char*', 'const char*']
      );
      
      // Validation options
      this.functions['mcp_auth_validation_options_create'] = this.lib.func(
        'mcp_auth_validation_options_create',
        authTypes.mcp_auth_error_t,
        [koffi.out(koffi.pointer(authTypes.mcp_auth_validation_options_t))]
      );
      this.functions['mcp_auth_validation_options_destroy'] = this.lib.func(
        'mcp_auth_validation_options_destroy',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_validation_options_t]
      );
      this.functions['mcp_auth_validation_options_set_scopes'] = this.lib.func(
        'mcp_auth_validation_options_set_scopes',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_validation_options_t, 'const char*']
      );
      this.functions['mcp_auth_validation_options_set_audience'] = this.lib.func(
        'mcp_auth_validation_options_set_audience',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_validation_options_t, 'const char*']
      );
      this.functions['mcp_auth_validation_options_set_clock_skew'] = this.lib.func(
        'mcp_auth_validation_options_set_clock_skew',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_validation_options_t, 'int64']
      );
      
      // Token validation  
      // Use the regular validate_token function with result pointer
      this.functions['mcp_auth_validate_token'] = this.lib.func(
        'mcp_auth_validate_token',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_client_t, 'str', authTypes.mcp_auth_validation_options_t, 
         koffi.out(koffi.pointer(authTypes.mcp_auth_validation_result_t))]  // Result pointer
      );
      this.functions['mcp_auth_extract_payload'] = this.lib.func(
        'mcp_auth_extract_payload',
        authTypes.mcp_auth_error_t,
        ['const char*', koffi.out(koffi.pointer(authTypes.mcp_auth_token_payload_t))]
      );
      
      // Token payload access
      this.functions['mcp_auth_payload_get_subject'] = this.lib.func(
        'mcp_auth_payload_get_subject',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t, koffi.out(koffi.pointer('char*'))]
      );
      this.functions['mcp_auth_payload_get_issuer'] = this.lib.func(
        'mcp_auth_payload_get_issuer',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t, koffi.out(koffi.pointer('char*'))]
      );
      this.functions['mcp_auth_payload_get_audience'] = this.lib.func(
        'mcp_auth_payload_get_audience',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t, koffi.out(koffi.pointer('char*'))]
      );
      this.functions['mcp_auth_payload_get_scopes'] = this.lib.func(
        'mcp_auth_payload_get_scopes',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t, koffi.out(koffi.pointer('char*'))]
      );
      this.functions['mcp_auth_payload_get_expiration'] = this.lib.func(
        'mcp_auth_payload_get_expiration',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t, koffi.out(koffi.pointer('int64'))]
      );
      this.functions['mcp_auth_payload_get_claim'] = this.lib.func(
        'mcp_auth_payload_get_claim',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t, 'const char*', koffi.out(koffi.pointer('char*'))]
      );
      this.functions['mcp_auth_payload_destroy'] = this.lib.func(
        'mcp_auth_payload_destroy',
        authTypes.mcp_auth_error_t,
        [authTypes.mcp_auth_token_payload_t]
      );
      
      // WWW-Authenticate header generation
      this.functions['mcp_auth_generate_www_authenticate'] = this.lib.func(
        'mcp_auth_generate_www_authenticate',
        authTypes.mcp_auth_error_t,
        ['const char*', 'const char*', 'const char*', koffi.out(koffi.pointer('char*'))]
      );
      
      // Memory management
      this.functions['mcp_auth_free_string'] = this.lib.func(
        'mcp_auth_free_string',
        'void',
        ['char*']
      );
      this.functions['mcp_auth_get_last_error'] = this.lib.func(
        'mcp_auth_get_last_error',
        'const char*',
        []
      );
      this.functions['mcp_auth_clear_error'] = this.lib.func(
        'mcp_auth_clear_error',
        'void',
        []
      );
      
      // Utility functions
      this.functions['mcp_auth_validate_scopes'] = this.lib.func(
        'mcp_auth_validate_scopes',
        'bool',
        ['const char*', 'const char*']
      );
      this.functions['mcp_auth_error_to_string'] = this.lib.func(
        'mcp_auth_error_to_string',
        'const char*',
        [authTypes.mcp_auth_error_t]
      );

      // OAuth metadata generation functions - commented out until available in C++
      // These functions are not yet exported from the C++ library
      // this.functions['mcp_auth_generate_protected_resource_metadata'] = this.lib.func(
      //   'mcp_auth_generate_protected_resource_metadata',
      //   authTypes.mcp_auth_error_t,
      //   ['const char*', 'const char*', koffi.out(koffi.pointer('char*'))]
      // );

      // this.functions['mcp_auth_proxy_discovery_metadata'] = this.lib.func(
      //   'mcp_auth_proxy_discovery_metadata',
      //   authTypes.mcp_auth_error_t,
      //   [authTypes.mcp_auth_client_t, 'const char*', 'const char*', 'const char*', koffi.out(koffi.pointer('char*'))]
      // );

      // this.functions['mcp_auth_proxy_client_registration'] = this.lib.func(
      //   'mcp_auth_proxy_client_registration',
      //   authTypes.mcp_auth_error_t,
      //   [authTypes.mcp_auth_client_t, 'const char*', 'const char*', 'const char*', 'const char*', koffi.out(koffi.pointer('char*'))]
      // );
      
    } catch (error) {
      throw new Error(`Failed to bind authentication functions: ${error}`);
    }
  }
  
  /**
   * Get a bound function by name
   */
  getFunction(name: string): Function {
    const fn = this.functions[name];
    if (!fn) {
      throw new Error(`Function ${name} not found in authentication library`);
    }
    return fn;
  }

  /**
   * Check if a function exists
   */
  hasFunction(name: string): boolean {
    return !!this.functions[name];
  }
  
  /**
   * Check if library is loaded
   */
  isLoaded(): boolean {
    return this.lib !== undefined && Object.keys(this.functions).length > 0;
  }
  
  /**
   * Get library path
   */
  getLibraryPath(): string {
    return this.libraryPath;
  }
  
  /**
   * Initialize authentication library
   */
  init(): number {
    const fn = this.functions['mcp_auth_init'];
    if (!fn) return AuthErrorCodes.INVALID_PARAMETER;
    return fn();
  }
  
  /**
   * Shutdown authentication library
   */
  shutdown(): number {
    const fn = this.functions['mcp_auth_shutdown'];
    if (!fn) return AuthErrorCodes.INVALID_PARAMETER;
    return fn();
  }
  
  /**
   * Get version string
   */
  version(): string {
    const fn = this.functions['mcp_auth_version'];
    if (!fn) return 'unknown';
    return fn();
  }
  
  /**
   * Get last error message
   */
  getLastError(): string {
    const fn = this.functions['mcp_auth_get_last_error'];
    if (!fn) return 'Unknown error';
    return fn();
  }
  
  /**
   * Clear last error
   */
  clearError(): void {
    const fn = this.functions['mcp_auth_clear_error'];
    if (fn) fn();
  }
  
  /**
   * Convert error code to string
   */
  errorToString(code: number): string {
    const fn = this.functions['mcp_auth_error_to_string'];
    if (!fn) return `Error code: ${code}`;
    return fn(code);
  }
  
  /**
   * Free string allocated by library
   */
  freeString(str: any): void {
    if (str) {
      const fn = this.functions['mcp_auth_free_string'];
      if (fn) fn(str);
    }
  }
  
  /**
   * Get the validation result struct type for allocation
   */
  getValidationResultStruct() {
    return authTypes.mcp_auth_validation_result_t;
  }
}

// Singleton instance
let authFFIInstance: AuthFFILibrary | null = null;

/**
 * Get or create auth FFI library instance
 */
export function getAuthFFI(): AuthFFILibrary {
  if (!authFFIInstance) {
    authFFIInstance = new AuthFFILibrary();
  }
  return authFFIInstance;
}

/**
 * Check if auth functions are available
 */
export function hasAuthSupport(): boolean {
  try {
    const ffi = getAuthFFI();
    return ffi.isLoaded();
  } catch (error) {
    console.error('Failed to load auth FFI:', error);
    return false;
  }
}

// Export types for use in higher-level API
export type AuthClient = any;  // Opaque handle
export type TokenPayload = any;  // Opaque handle
export type ValidationOptions = any;  // Opaque handle