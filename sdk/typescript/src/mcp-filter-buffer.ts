/**
 * @file filter-buffer.ts
 * @brief TypeScript wrapper for MCP C Filter Buffer API (mcp_c_filter_buffer.h)
 *
 * This module provides TypeScript wrappers for advanced buffer management capabilities
 * including zero-copy operations, scatter-gather I/O, and memory pooling.
 * It uses the existing C++ RAII system through FFI calls.
 */

import { mcpFilterLib } from "./mcp-ffi-bindings";

// ============================================================================
// Buffer Types and Enumerations (matching mcp_c_filter_buffer.h)
// ============================================================================

export enum BufferOwnership {
  NONE = 0, // No ownership (view only)
  SHARED = 1, // Shared ownership (ref counted)
  EXCLUSIVE = 2, // Exclusive ownership
  EXTERNAL = 3, // External ownership (callback)
}

export enum BufferFlags {
  READONLY = 0x01,
  OWNED = 0x02,
  EXTERNAL = 0x04,
  ZERO_COPY = 0x08,
}

// ============================================================================
// Data Structures (matching mcp_c_filter_buffer.h)
// ============================================================================

export interface BufferFragment {
  data: Uint8Array;
  size: number;
  releaseCallback?: (data: Uint8Array, size: number, userData: any) => void;
  userData?: any;
}

export interface BufferReservation {
  data: Uint8Array;
  capacity: number;
  buffer: number;
  reservationId: number;
}

export interface BufferStats {
  totalBytes: number;
  usedBytes: number;
  sliceCount: number;
  fragmentCount: number;
  readOperations: number;
  writeOperations: number;
}

export interface DrainTracker {
  callback: (bytesDrained: number, userData: any) => void;
  userData: any;
}

export interface BufferPoolConfig {
  bufferSize: number;
  maxBuffers: number;
  preallocCount?: number;
  useThreadLocal?: boolean;
  zeroOnAlloc?: boolean;
}

// ============================================================================
// Buffer Creation and Management
// ============================================================================

/**
 * Create a new buffer
 */
export function createBufferOwned(initialCapacity: number, ownership: BufferOwnership): number {
  return mcpFilterLib.mcp_buffer_create_owned(initialCapacity, ownership) as number;
}

/**
 * Create a buffer view (zero-copy reference)
 */
export function createBufferView(data: Uint8Array, length: number): number {
  return mcpFilterLib.mcp_buffer_create_view(data, length) as number;
}

/**
 * Create buffer from external fragment
 */
export function createBufferFromFragment(fragment: BufferFragment): number {
  // Create a proper C struct for the fragment
  // For now, we'll create a simple buffer from the fragment data
  if (fragment.data && fragment.size > 0) {
    const data = new Uint8Array(fragment.data);
    return mcpFilterLib.mcp_filter_buffer_create(
      data,
      fragment.size,
      0 // flags
    ) as number;
  }

  // If no data, create empty buffer
  return mcpFilterLib.mcp_filter_buffer_create(
    new Uint8Array(0),
    0,
    0 // flags
  ) as number;
}

/**
 * Clone a buffer (deep copy)
 */
export function cloneBuffer(buffer: number): number {
  return mcpFilterLib.mcp_buffer_clone(buffer) as number;
}

/**
 * Create copy-on-write buffer
 */
export function createCopyOnWriteBuffer(buffer: number): number {
  return mcpFilterLib.mcp_buffer_create_cow(buffer) as number;
}

// ============================================================================
// Buffer Data Operations
// ============================================================================

/**
 * Add data to buffer
 */
export function addDataToBuffer(buffer: number, data: Uint8Array, length: number): number {
  return mcpFilterLib.mcp_buffer_add(buffer, data, length) as number;
}

/**
 * Add string to buffer
 */
export function addStringToBuffer(buffer: number, str: string): number {
  return mcpFilterLib.mcp_buffer_add_string(buffer, str) as number;
}

/**
 * Add another buffer to buffer
 */
export function addBufferToBuffer(buffer: number, source: number): number {
  return mcpFilterLib.mcp_buffer_add_buffer(buffer, source) as number;
}

/**
 * Add buffer fragment (zero-copy)
 */
export function addFragmentToBuffer(buffer: number, fragment: BufferFragment): number {
  return mcpFilterLib.mcp_buffer_add_fragment(buffer, fragment) as number;
}

/**
 * Prepend data to buffer
 */
export function prependDataToBuffer(buffer: number, data: Uint8Array, length: number): number {
  return mcpFilterLib.mcp_buffer_prepend(buffer, data, length) as number;
}

// ============================================================================
// Buffer Consumption
// ============================================================================

/**
 * Drain bytes from front of buffer
 */
export function drainBuffer(buffer: number, size: number): number {
  return mcpFilterLib.mcp_buffer_drain(buffer, size) as number;
}

/**
 * Move data from one buffer to another
 */
export function moveBufferData(source: number, destination: number, length: number = 0): number {
  return mcpFilterLib.mcp_buffer_move(source, destination, length) as number;
}

/**
 * Set drain tracker for buffer
 */
export function setBufferDrainTracker(buffer: number, tracker: DrainTracker): number {
  return mcpFilterLib.mcp_buffer_set_drain_tracker(buffer, tracker) as number;
}

// ============================================================================
// Buffer Reservation (Zero-Copy Writing)
// ============================================================================

/**
 * Reserve space for writing
 */
export function reserveBufferSpace(
  buffer: number,
  minSize: number,
  reservation: BufferReservation
): number {
  return mcpFilterLib.mcp_buffer_reserve(buffer, minSize, reservation) as number;
}

/**
 * Reserve for vectored I/O
 */
export function reserveBufferIovec(
  buffer: number,
  iovecs: any,
  iovecCount: number,
  reserved: number
): number {
  return mcpFilterLib.mcp_buffer_reserve_iovec(buffer, iovecs, iovecCount, reserved) as number;
}

/**
 * Commit reserved space
 */
export function commitBufferReservation(
  reservation: BufferReservation,
  bytesWritten: number
): number {
  return mcpFilterLib.mcp_buffer_commit_reservation(reservation, bytesWritten) as number;
}

/**
 * Cancel reservation
 */
export function cancelBufferReservation(reservation: BufferReservation): number {
  return mcpFilterLib.mcp_buffer_cancel_reservation(reservation) as number;
}

// ============================================================================
// Buffer Access (Zero-Copy Reading)
// ============================================================================

/**
 * Get contiguous memory view
 */
export function getBufferContiguous(
  buffer: number,
  offset: number,
  length: number,
  data: Uint8Array,
  actualLength: number
): number {
  return mcpFilterLib.mcp_buffer_get_contiguous(
    buffer,
    offset,
    length,
    data,
    actualLength
  ) as number;
}

/**
 * Linearize buffer (ensure contiguous memory)
 */
export function linearizeBuffer(buffer: number, size: number, data: Uint8Array): number {
  return mcpFilterLib.mcp_buffer_linearize(buffer, size, data) as number;
}

/**
 * Peek at buffer data without consuming
 */
export function peekBufferData(
  buffer: number,
  offset: number,
  data: Uint8Array,
  length: number
): number {
  return mcpFilterLib.mcp_buffer_peek(buffer, offset, data, length) as number;
}

// ============================================================================
// Type-Safe I/O Operations
// ============================================================================

/**
 * Write integer with little-endian byte order
 */
export function writeBufferLittleEndianInt(buffer: number, value: number, size: number): number {
  return mcpFilterLib.mcp_buffer_write_le_int(buffer, value, size) as number;
}

/**
 * Write integer with big-endian byte order
 */
export function writeBufferBigEndianInt(buffer: number, value: number, size: number): number {
  return mcpFilterLib.mcp_buffer_write_be_int(buffer, value, size) as number;
}

/**
 * Read integer with little-endian byte order
 */
export function readBufferLittleEndianInt(buffer: number, size: number, value: number): number {
  return mcpFilterLib.mcp_buffer_read_le_int(buffer, size, value) as number;
}

/**
 * Read integer with big-endian byte order
 */
export function readBufferBigEndianInt(buffer: number, size: number, value: number): number {
  return mcpFilterLib.mcp_buffer_read_be_int(buffer, size, value) as number;
}

// ============================================================================
// Buffer Search Operations
// ============================================================================

/**
 * Search for pattern in buffer
 */
export function searchBuffer(
  buffer: number,
  pattern: Uint8Array,
  patternSize: number,
  startPosition: number,
  position: number
): number {
  return mcpFilterLib.mcp_buffer_search(
    buffer,
    pattern,
    patternSize,
    startPosition,
    position
  ) as number;
}

/**
 * Find delimiter in buffer
 */
export function findByteInBuffer(buffer: number, delimiter: number, position: number): number {
  return mcpFilterLib.mcp_buffer_find_byte(buffer, delimiter, position) as number;
}

// ============================================================================
// Buffer Information
// ============================================================================

/**
 * Get buffer length
 */
export function getBufferLength(buffer: number): number {
  return mcpFilterLib.mcp_buffer_length(buffer) as number;
}

/**
 * Get buffer capacity
 */
export function getBufferCapacity(buffer: number): number {
  return mcpFilterLib.mcp_buffer_capacity(buffer) as number;
}

/**
 * Check if buffer is empty
 */
export function isBufferEmpty(buffer: number): boolean {
  return mcpFilterLib.mcp_buffer_is_empty(buffer) as boolean;
}

/**
 * Get buffer statistics
 */
export function getBufferStats(buffer: number, stats: BufferStats): number {
  return mcpFilterLib.mcp_buffer_get_stats(buffer, stats) as number;
}

// ============================================================================
// Buffer Watermarks
// ============================================================================

/**
 * Set buffer watermarks for flow control
 */
export function setBufferWatermarks(
  buffer: number,
  lowWatermark: number,
  highWatermark: number,
  overflowWatermark: number
): number {
  return mcpFilterLib.mcp_buffer_set_watermarks(
    buffer,
    lowWatermark,
    highWatermark,
    overflowWatermark
  ) as number;
}

/**
 * Check if buffer is above high watermark
 */
export function isBufferAboveHighWatermark(buffer: number): boolean {
  return mcpFilterLib.mcp_buffer_above_high_watermark(buffer) as boolean;
}

/**
 * Check if buffer is below low watermark
 */
export function isBufferBelowLowWatermark(buffer: number): boolean {
  return mcpFilterLib.mcp_buffer_below_low_watermark(buffer) as boolean;
}

// ============================================================================
// Advanced Buffer Pool (using existing C++ RAII)
// ============================================================================

/**
 * Create buffer pool with configuration (uses existing C++ RAII)
 */
export function createBufferPoolEx(config: BufferPoolConfig): any {
  return mcpFilterLib.mcp_buffer_pool_create_ex(config);
}

/**
 * Get pool statistics (uses existing C++ RAII)
 */
export function getBufferPoolStats(
  pool: any,
  freeCount: number,
  usedCount: number,
  totalAllocated: number
): number {
  return mcpFilterLib.mcp_buffer_pool_get_stats(
    pool,
    freeCount,
    usedCount,
    totalAllocated
  ) as number;
}

/**
 * Trim pool to reduce memory usage (uses existing C++ RAII)
 */
export function trimBufferPool(pool: any, targetFree: number): number {
  return mcpFilterLib.mcp_buffer_pool_trim(pool, targetFree) as number;
}

// ============================================================================
// Utility Functions for Buffer Management
// ============================================================================

/**
 * Create a buffer from string data
 */
export function createBufferFromString(
  str: string,
  _ownership: BufferOwnership = BufferOwnership.SHARED
): number {
  const data = new TextEncoder().encode(str);
  // Use the C API directly to create buffer with data
  return mcpFilterLib.mcp_filter_buffer_create(
    data,
    data.length,
    0 // flags
  ) as number;
}

/**
 * Read string data from buffer
 */
export function readStringFromBuffer(buffer: number, encoding: string = "utf8"): string {
  // Validate buffer handle
  if (buffer === 0) {
    throw new Error("Invalid buffer handle: 0");
  }

  // Get buffer length using C API
  const length = mcpFilterLib.mcp_buffer_length(buffer) as number;
  if (length === 0) return "";

  // For now, create a simple buffer and use peek to read the data
  // This is not zero-copy but it's safe and works
  const data = new Uint8Array(length);

  // Use buffer peek to safely copy data
  const result = mcpFilterLib.mcp_buffer_peek(
    buffer,
    0, // offset
    data,
    length
  ) as number;

  if (result !== 0) {
    throw new Error("Failed to read buffer data");
  }

  return new TextDecoder(encoding).decode(data);
}

/**
 * Create a buffer from JSON data
 */
export function createBufferFromJson(
  obj: any,
  ownership: BufferOwnership = BufferOwnership.SHARED
): number {
  const jsonStr = JSON.stringify(obj);
  return createBufferFromString(jsonStr, ownership);
}

/**
 * Read JSON data from buffer
 */
export function readJsonFromBuffer(buffer: number): any {
  const jsonStr = readStringFromBuffer(buffer);
  try {
    return JSON.parse(jsonStr);
  } catch (error) {
    throw new Error(`Failed to parse JSON from buffer: ${error}`);
  }
}

/**
 * Create a buffer slice (zero-copy view)
 */
export function createBufferSlice(buffer: number, offset: number, length: number): number {
  const data = new Uint8Array(length);
  const result = getBufferContiguous(buffer, offset, length, data, length);
  if (result !== 0) {
    throw new Error("Failed to create buffer slice");
  }

  return createBufferView(data, length);
}

/**
 * Concatenate multiple buffers
 */
export function concatenateBuffers(buffers: number[]): number {
  if (buffers.length === 0) {
    return createBufferOwned(0, BufferOwnership.SHARED);
  }

  if (buffers.length === 1) {
    const buffer = buffers[0];
    if (buffer !== undefined) {
      return cloneBuffer(buffer);
    }
    return createBufferOwned(0, BufferOwnership.SHARED);
  }

  // Create result buffer with total capacity
  let totalLength = 0;
  for (const buffer of buffers) {
    totalLength += getBufferLength(buffer);
  }

  const result = createBufferOwned(totalLength, BufferOwnership.SHARED);

  // Copy data from all buffers
  for (const buffer of buffers) {
    const length = getBufferLength(buffer);
    if (length > 0) {
      const data = new Uint8Array(length);
      const readResult = getBufferContiguous(buffer, 0, length, data, length);
      if (readResult === 0) {
        addDataToBuffer(result, data, length);
      }
    }
  }

  return result;
}

/**
 * Split buffer at position
 */
export function splitBuffer(buffer: number, position: number): [number, number] {
  const totalLength = getBufferLength(buffer);
  if (position <= 0) {
    return [createBufferOwned(0, BufferOwnership.SHARED), cloneBuffer(buffer)];
  }

  if (position >= totalLength) {
    return [cloneBuffer(buffer), createBufferOwned(0, BufferOwnership.SHARED)];
  }

  const first = createBufferSlice(buffer, 0, position);
  const second = createBufferSlice(buffer, position, totalLength - position);

  return [first, second];
}

/**
 * Compare two buffers
 */
export function compareBuffers(buffer1: number, buffer2: number): number {
  const len1 = getBufferLength(buffer1);
  const len2 = getBufferLength(buffer2);

  if (len1 !== len2) {
    return len1 - len2;
  }

  if (len1 === 0) {
    return 0;
  }

  const data1 = new Uint8Array(len1);
  const data2 = new Uint8Array(len2);

  const result1 = getBufferContiguous(buffer1, 0, len1, data1, len1);
  const result2 = getBufferContiguous(buffer2, 0, len2, data2, len2);

  if (result1 !== 0 || result2 !== 0) {
    throw new Error("Failed to read buffer data for comparison");
  }

  // Compare byte by byte
  for (let i = 0; i < len1; i++) {
    const byte1 = data1[i];
    const byte2 = data2[i];
    if (byte1 !== undefined && byte2 !== undefined && byte1 !== byte2) {
      return byte1 - byte2;
    }
  }

  return 0;
}
