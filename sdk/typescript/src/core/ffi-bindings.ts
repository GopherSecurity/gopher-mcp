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
      // Try dynamic library first, then static
      const staticLib = path.join(__dirname, "../../../build/libgopher-mcp.a");
      const dynamicLib = path.join(
        __dirname,
        "../../build/libgopher-mcp.dylib"
      );

      if (fs.existsSync(dynamicLib)) {
        return dynamicLib;
      } else if (fs.existsSync(staticLib)) {
        return staticLib;
      }
      return dynamicLib; // Default to dynamic for path generation
    } else if (arch() === "arm64") {
      const staticLib = path.join(__dirname, "../../../build/libgopher-mcp.a");
      const dynamicLib = path.join(
        __dirname,
        "../../build/libgopher-mcp.dylib"
      );

      if (fs.existsSync(dynamicLib)) {
        return dynamicLib;
      } else if (fs.existsSync(staticLib)) {
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
    // Library loading failed - this should not happen in production
    throw new Error("Failed to load MCP Filter library");
  } else {
    // Load dynamic library
    const library = koffi.load(libPath);
    console.log("MCP Filter library loaded successfully");

    // Explicitly bind the functions we need
    mcpFilterLib = {
      // Core MCP Functions
      mcp_init: library.func("mcp_init", "int", ["void*"]),
      mcp_shutdown: library.func("mcp_shutdown", "void", []),
      mcp_is_initialized: library.func("mcp_is_initialized", "int", []),
      mcp_get_version: library.func("mcp_get_version", "string", []),

      // Memory Management
      mcp_memory_pool_create: library.func("mcp_memory_pool_create", "int", [
        "uint64",
      ]),
      mcp_memory_pool_destroy: library.func("mcp_memory_pool_destroy", "void", [
        "int",
      ]),
      mcp_memory_pool_alloc: library.func("mcp_memory_pool_alloc", "void*", [
        "int",
        "uint64",
      ]),

      // Dispatcher Functions
      mcp_dispatcher_create: library.func("mcp_dispatcher_create", "int", []),
      mcp_dispatcher_destroy: library.func("mcp_dispatcher_destroy", "void", [
        "int",
      ]),

      // Filter Functions
      mcp_filter_create: library.func("mcp_filter_create", "int", [
        "int",
        "void*",
      ]),
      mcp_filter_create_builtin: library.func(
        "mcp_filter_create_builtin",
        "int",
        ["int", "uint32", "int"]
      ),
      mcp_filter_release: library.func("mcp_filter_release", "void", ["int"]),
      mcp_filter_process_data: library.func("mcp_filter_process_data", "int", [
        "int",
        "void*",
        "uint64",
      ]),

      // Buffer Functions
      mcp_filter_buffer_create: library.func(
        "mcp_filter_buffer_create",
        "int",
        ["void*", "uint64", "uint32"]
      ),
      mcp_filter_buffer_release: library.func(
        "mcp_filter_buffer_release",
        "void",
        ["int"]
      ),
      mcp_filter_buffer_get_data: library.func(
        "mcp_filter_buffer_get_data",
        "int",
        ["int", "void**", "void**"]
      ),
      mcp_filter_buffer_set_data: library.func(
        "mcp_filter_buffer_set_data",
        "int",
        ["int", "void*", "uint64"]
      ),

      // JSON Functions
      mcp_json_create_object: library.func("mcp_json_create_object", "int", []),
      mcp_json_destroy: library.func("mcp_json_destroy", "void", ["int"]),
      mcp_json_stringify: library.func("mcp_json_stringify", "string", ["int"]),

      // Utility Functions
      mcp_get_error_string: library.func("mcp_get_error_string", "string", [
        "int",
      ]),
    };
  }
} catch (error) {
  console.warn(`Failed to load MCP Filter library: ${error}`);
  throw new Error(`Failed to load MCP Filter library: ${error}`);
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
  // Get the size of the struct
  const structSize = koffi.sizeof(structType);

  // Create a buffer to hold the struct data
  const buffer = Buffer.alloc(structSize);

  // For the McpFilterConfig struct, we need to handle:
  // - char* (string pointer) - we'll write the string data and store offset
  // - uint32 (4 bytes)
  // - void* (pointer) - we'll write as 8 bytes (64-bit pointer)

  let offset = 0;

  // Handle each field based on the struct definition
  if ("name" in data) {
    // For char*, we need to handle this carefully
    // In a real implementation, we'd need to allocate string memory
    // For now, we'll write a placeholder pointer
    buffer.writeBigUInt64LE(BigInt(0x12345678), offset);
    offset += 8;
  }

  if ("type" in data) {
    buffer.writeUInt32LE(data["type"], offset);
    offset += 4;
  }

  if ("settings" in data) {
    // void* - write as 8-byte pointer
    buffer.writeBigUInt64LE(BigInt(data["settings"] || 0), offset);
    offset += 8;
  }

  if ("layer" in data) {
    buffer.writeUInt32LE(data["layer"], offset);
    offset += 4;
  }

  if ("memoryPool" in data) {
    // void* - write as 8-byte pointer
    buffer.writeBigUInt64LE(BigInt(data["memoryPool"] || 0), offset);
    offset += 8;
  }

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
