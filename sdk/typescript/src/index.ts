/**
 * @file index.ts
 * @brief Main entry point for MCP Filter SDK
 *
 * This module exports the core MCP filter infrastructure including:
 * - Filter API (filter lifecycle, chain management, basic buffer operations)
 * - Filter Chain (advanced chain composition, routing, optimization)
 * - Filter Buffer (zero-copy operations, scatter-gather I/O, memory pooling)
 * - FFI bindings for C++ integration
 */

// Core Filter API (mcp_c_filter_api.h)
export * from "./filter-api";

// Advanced Filter Chain Management (mcp_c_filter_chain.h)
export * from "./filter-chain";

// Advanced Buffer Operations (mcp_c_filter_buffer.h)
export * from "./filter-buffer";

// Filter Manager for JSONRPCMessage processing
export * from "./filter-manager";

// FFI bindings for C++ integration
export * from "./ffi-bindings";

// C struct conversion utilities (selective exports to avoid conflicts)
export {
  createChainConfigStruct,
  createFilterCallbacksStruct,
  createFilterConditionStruct,
  createFilterConfigStruct,
  createFilterNodeStruct,
  createProtocolMetadataStruct,
  freeStruct,
} from "./c-structs";

// Type definitions
export * from "./types";
