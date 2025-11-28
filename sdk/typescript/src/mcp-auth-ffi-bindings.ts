/**
 * @file mcp-auth-ffi-bindings.ts
 * @brief FFI bindings for MCP Authentication C API
 *
 * This file provides the low-level FFI bindings that connect TypeScript
 * to the authentication C API functions using koffi.
 */

import * as koffi from "koffi";
import { arch, platform } from "os";
import { getLibraryPath } from "./mcp-ffi-bindings";

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
  
  // Validation result structure - simplified without pointer field
  // This avoids memory management issues with FFI
  mcp_auth_validation_result_simple_t: koffi.struct('mcp_auth_validation_result_simple_t', {
    valid: 'bool',
    error_code: 'int32'  // Just these two fields, no pointer
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
      console.log(`Loading auth FFI library from: ${this.libraryPath}`);
      this.lib = koffi.load(this.libraryPath);
      console.log(`Auth FFI library loaded successfully`);
      this.bindFunctions();
      console.log(`Auth FFI functions bound successfully`);
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
      // The C function fills the struct via pointer
      // Use pointer without koffi.out to avoid automatic memory management
      // Use the simplified function that returns struct without pointer fields
      // This completely avoids memory management issues
      this.functions['mcp_auth_validate_token_simple'] = this.lib.func(
        'mcp_auth_validate_token_simple',
        authTypes.mcp_auth_validation_result_simple_t,  // Returns simplified struct
        [authTypes.mcp_auth_client_t, 'str', authTypes.mcp_auth_validation_options_t]
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
    return authTypes.mcp_auth_validation_result_simple_t;
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