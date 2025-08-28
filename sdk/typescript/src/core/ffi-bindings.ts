/**
 * @file ffi-bindings.ts
 * @brief Core FFI bindings for MCP Filter C API
 *
 * This file provides the low-level FFI bindings that connect TypeScript
 * to the C API functions. It uses koffi for cross-platform compatibility.
 */

import fs from "fs";
import koffi from "koffi";
import { arch, platform } from "os";
import path from "path";

// ============================================================================
// Platform Detection
// ============================================================================

const isWindows = platform() === "win32";
const isMac = platform() === "darwin";
const isLinux = platform() === "linux";

// ============================================================================
// Library Path Configuration
// ============================================================================

function getLibraryPath(): string {
  if (isMac) {
    // macOS - support both static and dynamic libraries
    if (arch() === "x64") {
      // Try static library first, then dynamic
      const staticLib = path.join(__dirname, "../../../build/libgopher-mcp.a");
      const dynamicLib = path.join(
        __dirname,
        "../../../build/libgopher-mcp.dylib"
      );

      if (fs.existsSync(staticLib)) {
        return staticLib;
      } else if (fs.existsSync(dynamicLib)) {
        return dynamicLib;
      }
      return dynamicLib; // Default to dynamic for path generation
    } else if (arch() === "arm64") {
      const staticLib = path.join(__dirname, "../../../build/libgopher-mcp.a");
      const dynamicLib = path.join(
        __dirname,
        "../../../build/libgopher-mcp.dylib"
      );

      if (fs.existsSync(staticLib)) {
        return staticLib;
      } else if (fs.existsSync(dynamicLib)) {
        return staticLib;
      }
      return dynamicLib;
    }
  } else if (isLinux) {
    // Linux - support both static and dynamic libraries
    if (arch() === "x64") {
      const staticLib = path.join(__dirname, "../../../build/libgopher-mcp.a");
      const dynamicLib = path.join(
        __dirname,
        "../../../build/libgopher-mcp.so"
      );

      if (fs.existsSync(staticLib)) {
        return staticLib;
      } else if (fs.existsSync(dynamicLib)) {
        return staticLib;
      }
      return dynamicLib;
    } else if (arch() === "arm64") {
      const staticLib = path.join(__dirname, "../../../build/libgopher-mcp.a");
      const dynamicLib = path.join(
        __dirname,
        "../../../build/libgopher-mcp.so"
      );

      if (fs.existsSync(staticLib)) {
        return staticLib;
      } else if (fs.existsSync(dynamicLib)) {
        return staticLib;
      }
      return dynamicLib;
    }
  } else if (isWindows) {
    // Windows - support both static and dynamic libraries
    if (arch() === "x64") {
      const staticLib = path.join(__dirname, "../../../build/gopher-mcp.lib");
      const dynamicLib = path.join(__dirname, "../../../build/gopher-mcp.dll");

      if (fs.existsSync(staticLib)) {
        return staticLib;
      } else if (fs.existsSync(dynamicLib)) {
        return staticLib;
      }
      return dynamicLib;
    } else if (arch() === "ia32") {
      const staticLib = path.join(__dirname, "../../../build/gopher-mcp.lib");
      const dynamicLib = path.join(__dirname, "../../../build/gopher-mcp.dll");

      if (fs.existsSync(staticLib)) {
        return staticLib;
      } else if (fs.existsSync(dynamicLib)) {
        return staticLib;
      }
      return dynamicLib;
    }
  }

  throw new Error(`Unsupported platform: ${platform()} ${arch()}`);
}

// ============================================================================
// C Struct Definitions
// ============================================================================

/**
 * MCP Filter Configuration C struct
 */
export const McpFilterConfigStruct = koffi.struct("McpFilterConfig", {
  name: "char*",
  type: "uint32",
  settings: "void*",
  layer: "uint32",
  memoryPool: "void*",
});

/**
 * MCP Buffer Slice C struct
 */
export const McpBufferSliceStruct = koffi.struct("McpBufferSlice", {
  data: "void*",
  length: "size_t",
  flags: "uint32",
});

/**
 * MCP Protocol Metadata C struct
 */
export const McpProtocolMetadataStruct = koffi.struct("McpProtocolMetadata", {
  layer: "uint32",
  data: "void*",
});

/**
 * MCP Filter Callbacks C struct
 */
export const McpFilterCallbacksStruct = koffi.struct("McpFilterCallbacks", {
  onData: "void*",
  onWrite: "void*",
  onNewConnection: "void*",
  onHighWatermark: "void*",
  onLowWatermark: "void*",
  onError: "void*",
  userData: "void*",
});

/**
 * MCP Filter Stats C struct
 */
export const McpFilterStatsStruct = koffi.struct("McpFilterStats", {
  bytesProcessed: "uint64",
  packetsProcessed: "uint64",
  errors: "uint32",
  processingTimeUs: "uint64",
  throughputMbps: "double",
});

// ============================================================================
// Library Loading
// ============================================================================

let mcpFilterLib: any = null;

try {
  const libPath = getLibraryPath();
  console.log(`Loading MCP Filter library from: ${libPath}`);

  // Check if this is a static library
  if (libPath.endsWith(".a") || libPath.endsWith(".lib")) {
    console.warn(
      "Static library detected. Static libraries cannot be directly loaded by FFI."
    );
    console.warn(
      "Please build a dynamic wrapper library or use the mock implementation."
    );
    console.warn("Falling back to mock implementation for development");

    // Fallback to mock implementation
    mcpFilterLib = createMockLibrary();
  } else {
    // Load dynamic library
    mcpFilterLib = koffi.load(libPath);
    console.log("MCP Filter library loaded successfully");
  }
} catch (error) {
  console.warn(`Failed to load MCP Filter library: ${error}`);
  console.warn("Falling back to mock implementation for development");

  // Fallback to mock implementation
  mcpFilterLib = createMockLibrary();
}

// ============================================================================
// Mock Library Fallback
// ============================================================================

function createMockLibrary(): any {
  console.log("Creating mock MCP Filter library for development");

  return {
    // Core MCP Functions
    mcp_init: (_allocator: any) => 0, // MCP_OK
    mcp_shutdown: () => {},
    mcp_is_initialized: () => 1,
    mcp_get_version: () => "1.0.0-mock",

    // Memory Management
    mcp_memory_pool_create: (_size: number) =>
      Math.floor(Math.random() * 1000) + 1,
    mcp_memory_pool_destroy: (_pool: number) => {},
    mcp_memory_pool_alloc: (_pool: number, size: number) => Buffer.alloc(size),

    // Dispatcher Functions
    mcp_dispatcher_create: () => Math.floor(Math.random() * 1000) + 1,
    mcp_dispatcher_destroy: (_dispatcher: number) => {},

    // Filter Functions
    mcp_filter_create: (_dispatcher: number, _config: any) =>
      Math.floor(Math.random() * 1000) + 1,
    mcp_filter_create_builtin: (
      _dispatcher: number,
      _type: number,
      _config: number
    ) => Math.floor(Math.random() * 1000) + 1,
    mcp_filter_release: (_filter: number) => {},

    // Buffer Functions
    mcp_buffer_create: (_size: number, _flags: number) =>
      Math.floor(Math.random() * 1000) + 1,
    mcp_filter_buffer_create: (_data: any, _size: number, _flags: number) =>
      Math.floor(Math.random() * 1000) + 1,
    mcp_buffer_destroy: (_buffer: number) => {},
    mcp_filter_buffer_release: (_buffer: number) => {},
    mcp_buffer_get_data: (_buffer: number, _data: any, _size: any) => 0,
    mcp_buffer_set_data: (_buffer: number, _data: any, _size: number) => 0,

    // JSON Functions
    mcp_json_create_object: () => Math.floor(Math.random() * 1000) + 1,
    mcp_json_destroy: (_json: number) => {},
    mcp_json_stringify: (_json: number) => "{}",

    // Filter Chain Functions
    mcp_filter_chain_create: (_name: string) =>
      Math.floor(Math.random() * 1000) + 1,
    mcp_filter_chain_destroy: (_chain: number) => {},
    mcp_filter_chain_add_filter: (
      _builder: any,
      _filter: number,
      _position: number,
      _reference: number
    ) => 0,

    // Buffer Pool Functions
    mcp_buffer_pool_create: (_config: any) =>
      Math.floor(Math.random() * 1000) + 1,
    mcp_buffer_pool_destroy: (_pool: number) => {},
    mcp_buffer_pool_alloc: (_pool: number) =>
      Math.floor(Math.random() * 1000) + 1,

    // Resource Guard Functions
    mcp_filter_guard_create: () => Math.floor(Math.random() * 1000) + 1,
    mcp_filter_guard_destroy: (_guard: any) => {},
    mcp_filter_guard_add_filter: (_guard: any, _filter: number) => 0,

    // Thread-Safe Operations
    mcp_filter_post_data: (
      _filter: number,
      _data: any,
      _length: number,
      _callback: any,
      _userData: any
    ) => 0,

    // Utility Functions
    mcp_get_error_string: (_result: number) => "Mock error message",
  };
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Convert JavaScript string to C string (null-terminated)
 */
export function toCString(str: string): Buffer {
  return Buffer.from(str + "\0", "utf8");
}

/**
 * Convert C string (null-terminated) to JavaScript string
 */
export function fromCString(ptr: Buffer): string {
  if (!ptr || ptr.length === 0) return "";
  return ptr.toString("utf8").replace(/\0+$/, "");
}

/**
 * Create a C struct from JavaScript object
 */
export function createStruct<T extends Record<string, any>>(
  structType: any,
  data: T
): Buffer {
  const buffer = Buffer.alloc(structType.size);
  const struct = structType.alloc();

  // Copy data to struct
  Object.keys(data).forEach((key) => {
    if (structType.fields.includes(key)) {
      (struct as any)[key] = data[key];
    }
  });

  return buffer;
}

/**
 * Read C struct into JavaScript object
 */
export function readStruct<T>(structType: any, ptr: Buffer): T {
  if (!ptr || ptr.length === 0) {
    throw new Error("Cannot read from null or empty buffer");
  }

  const struct = structType.alloc();
  const result: any = {};

  // Copy struct data to result object
  structType.fields.forEach((field: string) => {
    result[field] = struct[field];
  });

  return result as T;
}

/**
 * Free C string memory
 */
export function freeCString(_ptr: Buffer): void {
  // In koffi, memory management is handled automatically
  // This function is kept for API compatibility
}

/**
 * Check if pointer is null
 */
export function isNullPtr(ptr: any): boolean {
  return ptr === null || ptr === undefined || ptr === 0;
}

/**
 * Get null pointer
 */
export function getNullPtr(): number {
  return 0;
}

/**
 * Convert boolean to C boolean
 */
export function toCBool(value: boolean): number {
  return value ? 1 : 0;
}

/**
 * Convert C boolean to JavaScript boolean
 */
export function fromCBool(value: number): boolean {
  return value !== 0;
}

/**
 * Allocate C string
 */
export function allocateCString(str: string): Buffer {
  return Buffer.from(str + "\0", "utf8");
}

// ============================================================================
// Export Library
// ============================================================================

export { mcpFilterLib };
