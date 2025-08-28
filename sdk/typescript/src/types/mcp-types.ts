/**
 * @file mcp-types.ts
 * @brief TypeScript type definitions for MCP C API types
 *
 * This file provides TypeScript interfaces and types that map 1:1
 * with the C API types defined in mcp_c_types.h
 */

// ============================================================================
// Basic Types
// ============================================================================

/**
 * FFI-safe boolean type (guaranteed 1 byte)
 */
export type McpBool = number; // 0 = false, 1 = true

/**
 * Result codes for all API operations
 */
export enum McpResult {
  OK = 0,
  ERROR_INVALID_ARGUMENT = -1,
  ERROR_NULL_POINTER = -2,
  ERROR_OUT_OF_MEMORY = -3,
  ERROR_NOT_FOUND = -4,
  ERROR_ALREADY_EXISTS = -5,
  ERROR_PERMISSION_DENIED = -6,
  ERROR_IO_ERROR = -7,
  ERROR_TIMEOUT = -8,
  ERROR_CANCELLED = -9,
  ERROR_NOT_IMPLEMENTED = -10,
  ERROR_INVALID_STATE = -11,
  ERROR_BUFFER_TOO_SMALL = -12,
  ERROR_PROTOCOL_ERROR = -13,
  ERROR_CONNECTION_FAILED = -14,
  ERROR_CONNECTION_CLOSED = -15,
  ERROR_ALREADY_INITIALIZED = -16,
  ERROR_NOT_INITIALIZED = -17,
  ERROR_RESOURCE_EXHAUSTED = -18,
  ERROR_INVALID_FORMAT = -19,
  ERROR_CLEANUP_FAILED = -20,
  ERROR_RESOURCE_LIMIT = -21,
  ERROR_NO_MEMORY = -22,
  ERROR_INITIALIZATION_FAILED = -23,
  ERROR_UNKNOWN = -999,
}

/**
 * Type identifiers for runtime type checking
 */
export enum McpTypeId {
  INVALID = 0,
  BOOL = 1,
  INT8 = 2,
  UINT8 = 3,
  INT16 = 4,
  UINT16 = 5,
  INT32 = 6,
  UINT32 = 7,
  INT64 = 8,
  UINT64 = 9,
  FLOAT32 = 10,
  FLOAT64 = 11,
  STRING = 12,
  BYTES = 13,
  LIST = 14,
  MAP = 15,
  STRUCT = 16,
  UNION = 17,
  ENUM = 18,
  OPTIONAL = 19,
  ARRAY = 20,
  POINTER = 21,
  REFERENCE = 22,
  FUNCTION = 23,
  CUSTOM = 100,
}

/**
 * Connection state enumeration
 */
export enum McpConnectionState {
  IDLE = 0,
  CONNECTING = 1,
  CONNECTED = 2,
  CLOSING = 3,
  DISCONNECTED = 4,
  ERROR = 5,
}

/**
 * Request ID type
 */
export type McpRequestId = number;

/**
 * Progress token type
 */
export type McpProgressToken = number;

// ============================================================================
// Collection Types
// ============================================================================

/**
 * List handle (opaque)
 */
export type McpList = number;

/**
 * Map handle (opaque)
 */
export type McpMap = number;

/**
 * JSON value handle (opaque)
 */
export type McpJsonValue = number;

/**
 * Buffer handle (opaque)
 */
export type McpBufferHandle = number;

// ============================================================================
// Memory Management Types
// ============================================================================

/**
 * Memory pool handle (opaque)
 */
export type McpMemoryPool = number;

/**
 * Allocator function type
 */
export type McpAllocatorFn = (size: number) => number;

/**
 * Deallocator function type
 */
export type McpDeallocatorFn = (ptr: number) => void;

/**
 * Reallocator function type
 */
export type McpReallocatorFn = (ptr: number, newSize: number) => number;

/**
 * Allocator structure
 */
export interface McpAllocator {
  alloc: McpAllocatorFn;
  dealloc: McpDeallocatorFn;
  realloc: McpReallocatorFn;
  userData: number;
}

// ============================================================================
// Error Information Types
// ============================================================================

/**
 * Error severity levels
 */
export enum McpErrorSeverity {
  INFO = 0,
  WARNING = 1,
  ERROR = 2,
  FATAL = 3,
}

/**
 * Error information structure
 */
export interface McpErrorInfo {
  code: McpResult;
  severity: McpErrorSeverity;
  message: string;
  function: string;
  file: string;
  line: number;
  timestamp: number;
  context: McpJsonValue;
}

// ============================================================================
// Utility Types
// ============================================================================

/**
 * Size type (platform-dependent)
 */
export type McpSize = number;

/**
 * Pointer type (platform-dependent)
 */
export type McpPointer = number;

/**
 * Handle type (opaque resource identifier)
 */
export type McpHandle = number;

/**
 * Callback function type for generic operations
 */
export type McpCallback<T = void> = (result: McpResult, data?: T) => void;

/**
 * Generic result wrapper
 */
export interface McpResultWrapper<T> {
  result: McpResult;
  data: T | undefined;
  error: string | undefined;
}

// ============================================================================
// Type Guards
// ============================================================================

/**
 * Check if a result indicates success
 */
export function isSuccess(result: McpResult): boolean {
  return result === McpResult.OK;
}

/**
 * Check if a result indicates an error
 */
export function isError(result: McpResult): boolean {
  return result !== McpResult.OK;
}

/**
 * Convert McpBool to JavaScript boolean
 */
export function toBoolean(value: McpBool): boolean {
  return value !== 0;
}

/**
 * Convert JavaScript boolean to McpBool
 */
export function fromBoolean(value: boolean): McpBool {
  return value ? 1 : 0;
}

/**
 * Create a result wrapper from a result code
 */
export function createResult<T>(
  result: McpResult,
  data?: T,
  error?: string
): McpResultWrapper<T> {
  return { result, data: data || undefined, error: error || undefined };
}

/**
 * Create a success result wrapper
 */
export function createSuccess<T>(data: T): McpResultWrapper<T> {
  return { result: McpResult.OK, data, error: undefined };
}

/**
 * Create an error result wrapper
 */
export function createError<T>(
  result: McpResult,
  error: string
): McpResultWrapper<T> {
  return { result, data: undefined, error };
}
