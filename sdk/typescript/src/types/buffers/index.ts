/**
 * @file buffer-types.ts
 * @brief TypeScript type definitions for MCP Filter Buffer API types
 *
 * This file provides TypeScript interfaces and types that map 1:1
 * with the buffer API types defined in mcp_filter_buffer.h
 */

import { McpBool, McpBufferHandle } from "../mcp-types";

// ============================================================================
// Buffer Types and Enumerations
// ============================================================================

/**
 * Buffer ownership model
 */
export enum McpBufferOwnership {
  NONE = 0, // No ownership (view only)
  SHARED = 1, // Shared ownership (ref counted)
  EXCLUSIVE = 2, // Exclusive ownership
  EXTERNAL = 3, // External ownership (callback)
}

/**
 * Buffer fragment for external memory
 */
export interface McpBufferFragment {
  data: Buffer; // Data pointer
  size: number; // Data size
  releaseCallback: (data: Buffer, size: number, userData: any) => void;
  userData: any;
}

/**
 * Buffer reservation for writing
 */
export interface McpBufferReservation {
  data: Buffer; // Data pointer
  capacity: number; // Reserved capacity
  buffer: McpBufferHandle; // Associated buffer
  reservationId: number; // Unique reservation ID
}

/**
 * Buffer statistics
 */
export interface McpBufferStats {
  totalBytes: number; // Total buffer size
  usedBytes: number; // Bytes currently used
  sliceCount: number; // Number of slices
  fragmentCount: number; // Number of fragments
  readOperations: number; // Read operations count
  writeOperations: number; // Write operations count
}

/**
 * Drain tracker for monitoring buffer consumption
 */
export type McpDrainTrackerCallback = (bytesDrained: number, userData: any) => void;

/**
 * Drain tracker structure
 */
export interface McpDrainTracker {
  callback: McpDrainTrackerCallback;
  userData: any;
}

// ============================================================================
// Advanced Buffer Pool Types
// ============================================================================

/**
 * Buffer pool configuration
 */
export interface McpBufferPoolConfig {
  bufferSize: number; // Size of each buffer
  maxBuffers: number; // Maximum buffers in pool
  preallocCount: number; // Number to preallocate
  useThreadLocal: McpBool; // Use thread-local caching
  zeroOnAlloc: McpBool; // Zero memory on allocation
}

// ============================================================================
// Buffer Watermark Types
// ============================================================================

/**
 * Buffer watermark configuration
 */
export interface McpBufferWatermarks {
  lowWatermark: number; // Low watermark bytes
  highWatermark: number; // High watermark bytes
  overflowWatermark: number; // Overflow watermark bytes
}

// ============================================================================
// Buffer Search Types
// ============================================================================

/**
 * Buffer search result
 */
export interface McpBufferSearchResult {
  found: boolean; // Whether pattern was found
  position: number; // Position where found
  length: number; // Length of match
}

/**
 * Buffer delimiter search result
 */
export interface McpBufferDelimiterResult {
  found: boolean; // Whether delimiter was found
  position: number; // Position where found
  data: Buffer; // Data up to delimiter
}

// ============================================================================
// Buffer I/O Types
// ============================================================================

/**
 * Buffer read operation result
 */
export interface McpBufferReadResult {
  bytesRead: number; // Number of bytes read
  data: Buffer; // Read data
  endOfBuffer: boolean; // Whether end of buffer reached
}

/**
 * Buffer write operation result
 */
export interface McpBufferWriteResult {
  bytesWritten: number; // Number of bytes written
  bufferFull: boolean; // Whether buffer is full
  remainingCapacity: number; // Remaining capacity
}

/**
 * Buffer copy operation result
 */
export interface McpBufferCopyResult {
  bytesCopied: number; // Number of bytes copied
  sourceExhausted: boolean; // Whether source buffer exhausted
  destinationFull: boolean; // Whether destination buffer full
}

// ============================================================================
// Buffer Type-Safe I/O Types
// ============================================================================

/**
 * Integer size for type-safe I/O
 */
export enum McpIntSize {
  BYTE = 1,
  SHORT = 2,
  INT = 4,
  LONG = 8,
}

/**
 * Endianness for type-safe I/O
 */
export enum McpEndianness {
  LITTLE = 0,
  BIG = 1,
}

/**
 * Type-safe integer read result
 */
export interface McpIntReadResult {
  value: number; // Read integer value
  bytesRead: number; // Number of bytes read
  success: boolean; // Whether read was successful
}

/**
 * Type-safe integer write result
 */
export interface McpIntWriteResult {
  bytesWritten: number; // Number of bytes written
  success: boolean; // Whether write was successful
}

// ============================================================================
// Buffer Utility Types
// ============================================================================

/**
 * Buffer comparison result
 */
export interface McpBufferComparisonResult {
  equal: boolean; // Whether buffers are equal
  differenceAt: number; // Position of first difference
  leftValue: number; // Value in left buffer
  rightValue: number; // Value in right buffer
}

/**
 * Buffer range specification
 */
export interface McpBufferRange {
  start: number; // Start position
  length: number; // Length of range
  end: number; // End position (computed)
}

/**
 * Buffer slice specification
 */
export interface McpBufferSliceSpec {
  start: number; // Start position
  length: number; // Length of slice
  step: number; // Step size (for strided access)
}

// ============================================================================
// Buffer Event Types
// ============================================================================

/**
 * Buffer event types
 */
export enum McpBufferEventType {
  CREATED = 0,
  DESTROYED = 1,
  RESIZED = 2,
  DATA_ADDED = 3,
  DATA_REMOVED = 4,
  WATERMARK_REACHED = 5,
  ERROR = 6,
}

/**
 * Buffer event data
 */
export interface McpBufferEvent {
  type: McpBufferEventType; // Event type
  buffer: McpBufferHandle; // Affected buffer
  timestamp: number; // Event timestamp
  data?: any; // Event-specific data
}

/**
 * Buffer event callback
 */
export type McpBufferEventCallback = (event: McpBufferEvent) => void;

// ============================================================================
// Type Guards and Utilities
// ============================================================================

/**
 * Check if buffer has specific ownership
 */
export function hasOwnership(ownership: McpBufferOwnership, type: McpBufferOwnership): boolean {
  return ownership === type;
}

/**
 * Check if buffer is view-only
 */
export function isViewOnly(ownership: McpBufferOwnership): boolean {
  return ownership === McpBufferOwnership.NONE;
}

/**
 * Check if buffer is shared
 */
export function isShared(ownership: McpBufferOwnership): boolean {
  return ownership === McpBufferOwnership.SHARED;
}

/**
 * Check if buffer is exclusive
 */
export function isExclusive(ownership: McpBufferOwnership): boolean {
  return ownership === McpBufferOwnership.EXCLUSIVE;
}

/**
 * Check if buffer is external
 */
export function isExternal(ownership: McpBufferOwnership): boolean {
  return ownership === McpBufferOwnership.EXTERNAL;
}

/**
 * Create buffer range from start and length
 */
export function createBufferRange(start: number, length: number): McpBufferRange {
  return {
    start,
    length,
    end: start + length,
  };
}

/**
 * Check if buffer range is valid
 */
export function isValidBufferRange(range: McpBufferRange): boolean {
  return range.start >= 0 && range.length > 0 && range.end > range.start;
}

/**
 * Check if position is within buffer range
 */
export function isInBufferRange(position: number, range: McpBufferRange): boolean {
  return position >= range.start && position < range.end;
}

/**
 * Create buffer slice specification
 */
export function createBufferSliceSpec(
  start: number,
  length: number,
  step: number = 1
): McpBufferSliceSpec {
  return { start, length, step };
}

/**
 * Check if buffer slice specification is valid
 */
export function isValidBufferSliceSpec(spec: McpBufferSliceSpec): boolean {
  return spec.start >= 0 && spec.length > 0 && spec.step > 0;
}
