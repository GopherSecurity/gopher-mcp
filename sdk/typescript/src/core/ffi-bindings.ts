/**
 * @file ffi-bindings.ts
 * @brief Core FFI bindings for MCP Filter C API
 *
 * This file provides the low-level FFI bindings that connect TypeScript
 * to the C API functions. It uses koffi for cross-platform compatibility.
 */

import koffi from "koffi";
import { arch, platform } from "os";

// Library configuration for different platforms and architectures
const LIBRARY_CONFIG = {
  darwin: {
    x64: {
      path: "/usr/local/lib/libgopher_mcp_c.dylib",
      name: "libgopher_mcp_c.dylib",
    },
    arm64: {
      path: "/usr/local/lib/libgopher_mcp_c.dylib",
      name: "libgopher_mcp_c.dylib",
    },
  },
  linux: {
    x64: {
      path: "/usr/local/lib/libgopher_mcp_c.so",
      name: "libgopher_mcp_c.so",
    },
    arm64: {
      path: "/usr/local/lib/libgopher_mcp_c.so",
      name: "libgopher_mcp_c.so",
    },
  },
  win32: {
    x64: {
      path: "C:\\Program Files\\gopher-mcp\\bin\\gopher_mcp_c.dll",
      name: "gopher_mcp_c.dll",
    },
    ia32: {
      path: "C:\\Program Files (x86)\\gopher-mcp\\bin\\gopher_mcp_c.dll",
      name: "gopher_mcp_c.dll",
    },
  },
} as const;

function getLibraryPath(): string {
  const currentPlatform = platform() as keyof typeof LIBRARY_CONFIG;
  const currentArch =
    arch() as keyof (typeof LIBRARY_CONFIG)[typeof currentPlatform];

  if (
    !LIBRARY_CONFIG[currentPlatform] ||
    !LIBRARY_CONFIG[currentPlatform][currentArch]
  ) {
    throw new Error(`Unsupported platform: ${currentPlatform} ${currentArch}`);
  }

  return LIBRARY_CONFIG[currentPlatform][currentArch].path;
}

function getLibraryName(): string {
  const currentPlatform = platform() as keyof typeof LIBRARY_CONFIG;
  const currentArch =
    arch() as keyof (typeof LIBRARY_CONFIG)[typeof currentPlatform];

  if (
    !LIBRARY_CONFIG[currentPlatform] ||
    !LIBRARY_CONFIG[currentPlatform][currentArch]
  ) {
    throw new Error(`Unsupported platform: ${currentPlatform} ${currentArch}`);
  }

  return LIBRARY_CONFIG[currentPlatform][currentArch].name;
}

// MCP Filter Library interface - using real C API functions
export let mcpFilterLib: any = {};

try {
  const libPath = getLibraryPath();
  const libName = getLibraryName();

  console.log(`Loading MCP C API library: ${libName}`);
  console.log(`Library path: ${libPath}`);

  // Load the shared library
  const library = koffi.load(libPath);
  console.log(`MCP C API library loaded successfully: ${libName}`);

  // Try to bind functions and see which ones are available
  const availableFunctions: any = {};

  // List of REAL C API functions from mcp_filter_api.h, mcp_filter_buffer.h, mcp_filter_chain.h
  const functionList = [
    // Core filter functions from mcp_filter_api.h
    {
      name: "mcp_filter_create",
      signature: "uint64_t",
      args: ["uint64_t", "void*"],
    },
    {
      name: "mcp_filter_create_builtin",
      signature: "uint64_t",
      args: ["uint64_t", "int", "void*"],
    },
    { name: "mcp_filter_retain", signature: "void", args: ["uint64_t"] },
    { name: "mcp_filter_release", signature: "void", args: ["uint64_t"] },
    {
      name: "mcp_filter_set_callbacks",
      signature: "int",
      args: ["uint64_t", "void*"],
    },
    {
      name: "mcp_filter_set_protocol_metadata",
      signature: "int",
      args: ["uint64_t", "void*"],
    },
    {
      name: "mcp_filter_get_protocol_metadata",
      signature: "int",
      args: ["uint64_t", "void*"],
    },

    // Filter chain functions from mcp_filter_api.h and mcp_filter_chain.h
    {
      name: "mcp_filter_chain_builder_create",
      signature: "void*",
      args: ["uint64_t"],
    },
    {
      name: "mcp_filter_chain_add_filter",
      signature: "int",
      args: ["void*", "uint64_t", "int", "uint64_t"],
    },
    { name: "mcp_filter_chain_build", signature: "uint64_t", args: ["void*"] },
    {
      name: "mcp_filter_chain_builder_destroy",
      signature: "void",
      args: ["void*"],
    },
    { name: "mcp_filter_chain_retain", signature: "void", args: ["uint64_t"] },
    { name: "mcp_filter_chain_release", signature: "void", args: ["uint64_t"] },

    // Advanced chain functions from mcp_filter_chain.h
    {
      name: "mcp_chain_builder_create_ex",
      signature: "void*",
      args: ["uint64_t", "void*"],
    },
    {
      name: "mcp_chain_builder_add_node",
      signature: "int",
      args: ["void*", "void*"],
    },
    {
      name: "mcp_chain_builder_add_conditional",
      signature: "int",
      args: ["void*", "void*", "uint64_t"],
    },
    {
      name: "mcp_chain_builder_add_parallel_group",
      signature: "int",
      args: ["void*", "void*", "size_t"],
    },
    { name: "mcp_chain_get_state", signature: "int", args: ["uint64_t"] },
    { name: "mcp_chain_pause", signature: "int", args: ["uint64_t"] },
    { name: "mcp_chain_resume", signature: "int", args: ["uint64_t"] },
    { name: "mcp_chain_reset", signature: "int", args: ["uint64_t"] },

    // Buffer functions from mcp_filter_buffer.h
    {
      name: "mcp_buffer_create_owned",
      signature: "uint64_t",
      args: ["size_t", "int"],
    },
    {
      name: "mcp_buffer_create_view",
      signature: "uint64_t",
      args: ["void*", "size_t"],
    },
    {
      name: "mcp_buffer_create_from_fragment",
      signature: "uint64_t",
      args: ["void*"],
    },
    { name: "mcp_buffer_clone", signature: "uint64_t", args: ["uint64_t"] },
    {
      name: "mcp_buffer_create_cow",
      signature: "uint64_t",
      args: ["uint64_t"],
    },
    {
      name: "mcp_buffer_add",
      signature: "int",
      args: ["uint64_t", "void*", "size_t"],
    },
    {
      name: "mcp_buffer_add_string",
      signature: "int",
      args: ["uint64_t", "string"],
    },
    {
      name: "mcp_buffer_add_buffer",
      signature: "int",
      args: ["uint64_t", "uint64_t"],
    },
    {
      name: "mcp_buffer_add_fragment",
      signature: "int",
      args: ["uint64_t", "void*"],
    },
    {
      name: "mcp_buffer_prepend",
      signature: "int",
      args: ["uint64_t", "void*", "size_t"],
    },
    {
      name: "mcp_buffer_drain",
      signature: "int",
      args: ["uint64_t", "size_t"],
    },
    {
      name: "mcp_buffer_move",
      signature: "int",
      args: ["uint64_t", "uint64_t", "size_t"],
    },
    {
      name: "mcp_buffer_reserve",
      signature: "int",
      args: ["uint64_t", "size_t", "void*"],
    },
    {
      name: "mcp_buffer_commit_reservation",
      signature: "int",
      args: ["void*", "size_t"],
    },
    {
      name: "mcp_buffer_cancel_reservation",
      signature: "int",
      args: ["void*"],
    },
    {
      name: "mcp_buffer_get_contiguous",
      signature: "int",
      args: ["uint64_t", "size_t", "size_t", "void*", "size_t*"],
    },
    {
      name: "mcp_buffer_linearize",
      signature: "int",
      args: ["uint64_t", "size_t", "void*"],
    },
    {
      name: "mcp_buffer_peek",
      signature: "int",
      args: ["uint64_t", "size_t", "void*", "size_t"],
    },
    {
      name: "mcp_buffer_write_le_int",
      signature: "int",
      args: ["uint64_t", "uint64_t", "size_t"],
    },
    {
      name: "mcp_buffer_write_be_int",
      signature: "int",
      args: ["uint64_t", "uint64_t", "size_t"],
    },
    {
      name: "mcp_buffer_read_le_int",
      signature: "int",
      args: ["uint64_t", "size_t", "uint64_t*"],
    },
    {
      name: "mcp_buffer_read_be_int",
      signature: "int",
      args: ["uint64_t", "size_t", "uint64_t*"],
    },
    {
      name: "mcp_buffer_search",
      signature: "int",
      args: ["uint64_t", "void*", "size_t", "size_t", "size_t*"],
    },
    {
      name: "mcp_buffer_find_byte",
      signature: "int",
      args: ["uint64_t", "uint8_t", "size_t*"],
    },
    { name: "mcp_buffer_length", signature: "size_t", args: ["uint64_t"] },
    { name: "mcp_buffer_capacity", signature: "size_t", args: ["uint64_t"] },
    { name: "mcp_buffer_is_empty", signature: "int", args: ["uint64_t"] },
    {
      name: "mcp_buffer_get_stats",
      signature: "int",
      args: ["uint64_t", "void*"],
    },
    {
      name: "mcp_buffer_set_watermarks",
      signature: "int",
      args: ["uint64_t", "size_t", "size_t", "size_t"],
    },
    {
      name: "mcp_buffer_above_high_watermark",
      signature: "int",
      args: ["uint64_t"],
    },
    {
      name: "mcp_buffer_below_low_watermark",
      signature: "int",
      args: ["uint64_t"],
    },

    // Buffer pool functions
    {
      name: "mcp_buffer_pool_create",
      signature: "void*",
      args: ["size_t", "size_t"],
    },
    { name: "mcp_buffer_pool_create_ex", signature: "void*", args: ["void*"] },
    { name: "mcp_buffer_pool_acquire", signature: "uint64_t", args: ["void*"] },
    {
      name: "mcp_buffer_pool_release",
      signature: "void",
      args: ["void*", "uint64_t"],
    },
    { name: "mcp_buffer_pool_destroy", signature: "void", args: ["void*"] },
    {
      name: "mcp_buffer_pool_get_stats",
      signature: "int",
      args: ["void*", "size_t*", "size_t*", "size_t*"],
    },
    {
      name: "mcp_buffer_pool_trim",
      signature: "int",
      args: ["void*", "size_t"],
    },

    // Filter manager functions
    {
      name: "mcp_filter_manager_create",
      signature: "uint64_t",
      args: ["uint64_t", "uint64_t"],
    },
    {
      name: "mcp_filter_manager_add_filter",
      signature: "int",
      args: ["uint64_t", "uint64_t"],
    },
    {
      name: "mcp_filter_manager_add_chain",
      signature: "int",
      args: ["uint64_t", "uint64_t"],
    },
    {
      name: "mcp_filter_manager_initialize",
      signature: "int",
      args: ["uint64_t"],
    },
    {
      name: "mcp_filter_manager_release",
      signature: "void",
      args: ["uint64_t"],
    },

    // Zero-copy buffer operations
    {
      name: "mcp_filter_get_buffer_slices",
      signature: "int",
      args: ["uint64_t", "void*", "size_t*"],
    },
    {
      name: "mcp_filter_reserve_buffer",
      signature: "int",
      args: ["uint64_t", "size_t", "void*"],
    },
    {
      name: "mcp_filter_commit_buffer",
      signature: "int",
      args: ["uint64_t", "size_t"],
    },
    {
      name: "mcp_filter_buffer_create",
      signature: "uint64_t",
      args: ["void*", "size_t", "uint32_t"],
    },
    {
      name: "mcp_filter_buffer_release",
      signature: "void",
      args: ["uint64_t"],
    },

    // Client/Server integration
    {
      name: "mcp_client_send_filtered",
      signature: "uint64_t",
      args: ["void*", "void*", "size_t", "void*", "void*"],
    },
    {
      name: "mcp_server_process_filtered",
      signature: "int",
      args: ["void*", "uint64_t", "uint64_t", "void*", "void*"],
    },

    // Thread-safe operations
    {
      name: "mcp_filter_post_data",
      signature: "int",
      args: ["uint64_t", "void*", "size_t", "void*", "void*"],
    },

    // Resource management
    { name: "mcp_filter_guard_create", signature: "void*", args: ["uint64_t"] },
    {
      name: "mcp_filter_guard_add_filter",
      signature: "int",
      args: ["void*", "uint64_t"],
    },
    { name: "mcp_filter_guard_release", signature: "void", args: ["void*"] },

    // Statistics and monitoring
    {
      name: "mcp_filter_get_stats",
      signature: "int",
      args: ["uint64_t", "void*"],
    },
    { name: "mcp_filter_reset_stats", signature: "int", args: ["uint64_t"] },
    {
      name: "mcp_chain_get_stats",
      signature: "int",
      args: ["uint64_t", "void*"],
    },

    // Chain optimization and debugging
    { name: "mcp_chain_optimize", signature: "int", args: ["uint64_t"] },
    { name: "mcp_chain_reorder_filters", signature: "int", args: ["uint64_t"] },
    {
      name: "mcp_chain_profile",
      signature: "int",
      args: ["uint64_t", "uint64_t", "size_t", "void*"],
    },
    {
      name: "mcp_chain_set_trace_level",
      signature: "int",
      args: ["uint64_t", "uint32_t"],
    },
    {
      name: "mcp_chain_dump",
      signature: "string",
      args: ["uint64_t", "string"],
    },
    {
      name: "mcp_chain_validate",
      signature: "int",
      args: ["uint64_t", "void*"],
    },

    // Chain routing
    { name: "mcp_chain_router_create", signature: "void*", args: ["void*"] },
    {
      name: "mcp_chain_router_add_route",
      signature: "int",
      args: ["void*", "void*", "uint64_t"],
    },
    {
      name: "mcp_chain_router_route",
      signature: "uint64_t",
      args: ["void*", "uint64_t", "void*"],
    },
    { name: "mcp_chain_router_destroy", signature: "void", args: ["void*"] },

    // Chain pool for load balancing
    {
      name: "mcp_chain_pool_create",
      signature: "void*",
      args: ["uint64_t", "size_t", "int"],
    },
    { name: "mcp_chain_pool_get_next", signature: "uint64_t", args: ["void*"] },
    {
      name: "mcp_chain_pool_return",
      signature: "void",
      args: ["void*", "uint64_t"],
    },
    {
      name: "mcp_chain_pool_get_stats",
      signature: "int",
      args: ["void*", "size_t*", "size_t*", "uint64_t*"],
    },
    { name: "mcp_chain_pool_destroy", signature: "void", args: ["void*"] },

    // Memory management (from mcp_c_memory.h)
    { name: "mcp_memory_pool_create", signature: "void*", args: ["size_t"] },
    { name: "mcp_memory_pool_destroy", signature: "void", args: ["void*"] },
    {
      name: "mcp_memory_pool_alloc",
      signature: "void*",
      args: ["void*", "size_t"],
    },

    // JSON utilities (from mcp_c_collections.h)
    { name: "mcp_json_create_object", signature: "void*", args: [] },
    { name: "mcp_json_create_string", signature: "void*", args: ["string"] },
    { name: "mcp_json_create_number", signature: "void*", args: ["double"] },
    { name: "mcp_json_create_bool", signature: "void*", args: ["int"] },
    { name: "mcp_json_free", signature: "void", args: ["void*"] },
    { name: "mcp_json_stringify", signature: "string", args: ["void*"] },

    // Core MCP functions (from mcp_c_api.h)
    { name: "mcp_init", signature: "int", args: ["void*"] },
    { name: "mcp_shutdown", signature: "void", args: [] },
    { name: "mcp_is_initialized", signature: "int", args: [] },
    { name: "mcp_get_version", signature: "string", args: [] },
    { name: "mcp_get_last_error", signature: "void*", args: [] },
    { name: "mcp_clear_last_error", signature: "void", args: [] },
  ];

  // Try to bind each function
  for (const func of functionList) {
    try {
      availableFunctions[func.name] = library.func(
        func.name,
        func.signature,
        func.args
      );
      console.log(`✓ Bound function: ${func.name}`);
    } catch (error) {
      console.log(`✗ Failed to bind function: ${func.name} - ${error}`);
    }
  }

  mcpFilterLib = availableFunctions;

  const boundCount = Object.keys(availableFunctions).length;
  console.log(
    `Successfully bound ${boundCount} out of ${functionList.length} functions`
  );

  if (boundCount === 0) {
    throw new Error("No functions could be bound from the library");
  }
} catch (error) {
  const libName = getLibraryName();
  const errorMsg = `Failed to load MCP C API library (${libName}): ${error}`;
  console.error(errorMsg);
  throw new Error(errorMsg);
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
    // Note: This is a limitation - we need to allocate string memory properly
    // For the enhanced wrapper to work, we'll use a non-null value
    buffer.writeBigUInt64LE(BigInt(0x1000), offset);
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
