/**
 * @file filter-types.ts
 * @brief TypeScript type definitions for MCP Filter API types
 *
 * This file provides TypeScript interfaces and types that map 1:1
 * with the filter API types defined in mcp_filter_api.h
 */

import {
  McpBool,
  McpBufferHandle,
  McpConnectionState,
  McpJsonValue,
  McpMap,
  McpMemoryPool,
  McpResult,
} from "./mcp-types";

// ============================================================================
// Filter Types and Enumerations
// ============================================================================

/**
 * Filter handle (opaque)
 */
export type McpFilter = number;

/**
 * Filter chain handle (opaque)
 */
export type McpFilterChain = number;

/**
 * Filter manager handle (opaque)
 */
export type McpFilterManager = number;

/**
 * Filter factory handle (opaque)
 */
export type McpFilterFactory = number;

/**
 * Filter chain builder handle (opaque)
 */
export type McpFilterChainBuilder = number;

/**
 * Filter status for processing control
 */
export enum McpFilterStatus {
  CONTINUE = 0, // Continue filter chain processing
  STOP_ITERATION = 1, // Stop filter chain processing
}

/**
 * Filter position in chain
 */
export enum McpFilterPosition {
  FIRST = 0,
  LAST = 1,
  BEFORE = 2, // Requires reference filter
  AFTER = 3, // Requires reference filter
}

/**
 * Protocol layers (OSI model)
 */
export enum McpProtocolLayer {
  LAYER_3_NETWORK = 3, // IP level
  LAYER_4_TRANSPORT = 4, // TCP/UDP
  LAYER_5_SESSION = 5, // Session management
  LAYER_6_PRESENTATION = 6, // Encoding/Encryption
  LAYER_7_APPLICATION = 7, // HTTP/gRPC/WebSocket
}

/**
 * Transport protocols for L4
 */
export enum McpTransportProtocol {
  TCP = 0,
  UDP = 1,
  QUIC = 2,
  SCTP = 3,
}

/**
 * Application protocols for L7
 */
export enum McpAppProtocol {
  HTTP = 0,
  HTTPS = 1,
  HTTP2 = 2,
  HTTP3 = 3,
  GRPC = 4,
  WEBSOCKET = 5,
  JSONRPC = 6,
  CUSTOM = 99,
}

/**
 * Built-in filter types
 */
export enum McpBuiltinFilterType {
  // Network filters
  TCP_PROXY = 0,
  UDP_PROXY = 1,

  // HTTP filters
  HTTP_CODEC = 10,
  HTTP_ROUTER = 11,
  HTTP_COMPRESSION = 12,

  // Security filters
  TLS_TERMINATION = 20,
  AUTHENTICATION = 21,
  AUTHORIZATION = 22,

  // Observability
  ACCESS_LOG = 30,
  METRICS = 31,
  TRACING = 32,

  // Traffic management
  RATE_LIMIT = 40,
  CIRCUIT_BREAKER = 41,
  RETRY = 42,
  LOAD_BALANCER = 43,

  // Custom filter
  CUSTOM = 100,
}

/**
 * Filter error codes
 */
export enum McpFilterError {
  NONE = 0,
  INVALID_CONFIG = -1000,
  INITIALIZATION_FAILED = -1001,
  BUFFER_OVERFLOW = -1002,
  PROTOCOL_VIOLATION = -1003,
  UPSTREAM_TIMEOUT = -1004,
  CIRCUIT_OPEN = -1005,
  RESOURCE_EXHAUSTED = -1006,
  INVALID_STATE = -1007,
}

/**
 * Buffer flags
 */
export enum McpBufferFlag {
  READONLY = 0x01,
  OWNED = 0x02,
  EXTERNAL = 0x04,
  ZERO_COPY = 0x08,
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Filter configuration
 */
export interface McpFilterConfig {
  name: string; // Filter name
  type: McpBuiltinFilterType; // Filter type
  settings: McpJsonValue; // JSON configuration
  layer: McpProtocolLayer; // OSI layer
  memoryPool: McpMemoryPool | null; // Optional memory pool
}

/**
 * Buffer slice for zero-copy access
 */
export interface McpBufferSlice {
  data: Buffer; // Direct pointer to buffer memory
  length: number; // Length of this slice
  flags: number; // Buffer flags
}

/**
 * Protocol metadata for different layers
 */
export interface McpProtocolMetadata {
  layer: McpProtocolLayer;

  // L3 - Network layer
  l3?: {
    srcIp: number; // Source IP address
    dstIp: number; // Destination IP address
    protocol: number; // IP protocol
    ttl: number; // Time to live
  };

  // L4 - Transport layer
  l4?: {
    srcPort: number; // Source port
    dstPort: number; // Destination port
    protocol: McpTransportProtocol; // Transport protocol
    sequenceNum: number; // Sequence number
  };

  // L5 - Session layer
  l5?: {
    isTls: McpBool; // Is TLS connection
    alpn: string | null; // ALPN protocol
    sni: string | null; // Server Name Indication
    sessionId: number; // Session ID
  };

  // L7 - Application layer
  l7?: {
    protocol: McpAppProtocol; // Application protocol
    headers: McpMap; // HTTP headers
    method: string | null; // HTTP method
    path: string | null; // HTTP path
    statusCode: number; // HTTP status code
  };
}

// ============================================================================
// Callback Types
// ============================================================================

/**
 * Filter data callback (onData from ReadFilter)
 */
export type McpFilterDataCallback = (
  buffer: McpBufferHandle,
  endStream: McpBool,
  userData: any
) => McpFilterStatus;

/**
 * Filter write callback (onWrite from WriteFilter)
 */
export type McpFilterWriteCallback = (
  buffer: McpBufferHandle,
  endStream: McpBool,
  userData: any
) => McpFilterStatus;

/**
 * Connection event callback
 */
export type McpFilterEventCallback = (
  state: McpConnectionState,
  userData: any
) => McpFilterStatus;

/**
 * Watermark callbacks
 */
export type McpFilterWatermarkCallback = (
  filter: McpFilter,
  userData: any
) => void;

/**
 * Error callback
 */
export type McpFilterErrorCallback = (
  filter: McpFilter,
  error: McpFilterError,
  message: string,
  userData: any
) => void;

/**
 * Completion callback for async operations
 */
export type McpFilterCompletionCallback = (
  result: McpResult,
  userData: any
) => void;

/**
 * Post completion callback
 */
export type McpPostCompletionCallback = (
  result: McpResult,
  userData: any
) => void;

/**
 * Request callback for server
 */
export type McpFilterRequestCallback = (
  responseBuffer: McpBufferHandle,
  result: McpResult,
  userData: any
) => void;

/**
 * Filter callbacks structure
 */
export interface McpFilterCallbacks {
  // Data callbacks (executed in dispatcher thread)
  onData?: McpFilterDataCallback;
  onWrite?: McpFilterWriteCallback;
  onNewConnection?: McpFilterEventCallback;

  // Watermark callbacks
  onHighWatermark?: McpFilterWatermarkCallback;
  onLowWatermark?: McpFilterWatermarkCallback;

  // Error handling
  onError?: McpFilterErrorCallback;

  userData?: any;
}

// ============================================================================
// Client/Server Integration Types
// ============================================================================

/**
 * Client context for filtered operations
 */
export interface McpFilterClientContext {
  client: McpClient;
  requestFilters: McpFilterChain;
  responseFilters: McpFilterChain;
}

/**
 * Server context for filtered operations
 */
export interface McpFilterServerContext {
  server: McpServer;
  requestFilters: McpFilterChain;
  responseFilters: McpFilterChain;
}

// ============================================================================
// Resource Management Types
// ============================================================================

/**
 * Filter resource guard for RAII
 */
export type McpFilterResourceGuard = number;

/**
 * Buffer pool handle
 */
export type McpBufferPool = number;

// ============================================================================
// Statistics and Monitoring Types
// ============================================================================

/**
 * Filter statistics
 */
export interface McpFilterStats {
  bytesProcessed: number;
  packetsProcessed: number;
  errors: number;
  processingTimeUs: number;
  throughputMbps: number;
}

// ============================================================================
// Type Guards and Utilities
// ============================================================================

/**
 * Check if a filter status indicates continue
 */
export function shouldContinue(status: McpFilterStatus): boolean {
  return status === McpFilterStatus.CONTINUE;
}

/**
 * Check if a filter status indicates stop
 */
export function shouldStop(status: McpFilterStatus): boolean {
  return status === McpFilterStatus.STOP_ITERATION;
}

/**
 * Create a filter status from boolean
 */
export function createFilterStatus(
  continueProcessing: boolean
): McpFilterStatus {
  return continueProcessing
    ? McpFilterStatus.CONTINUE
    : McpFilterStatus.STOP_ITERATION;
}

/**
 * Check if buffer has specific flag
 */
export function hasBufferFlag(flags: number, flag: McpBufferFlag): boolean {
  return (flags & flag) !== 0;
}

/**
 * Add buffer flag
 */
export function addBufferFlag(flags: number, flag: McpBufferFlag): number {
  return flags | flag;
}

/**
 * Remove buffer flag
 */
export function removeBufferFlag(flags: number, flag: McpBufferFlag): number {
  return flags & ~flag;
}

// Placeholder types for now (will be defined in other files)
export type McpClient = number;
export type McpServer = number;
