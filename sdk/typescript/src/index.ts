/**
 * @file index.ts
 * @brief Main export file for MCP Filter SDK
 *
 * This file provides the main entry point for the MCP Filter SDK,
 * exporting all public classes, types, and utilities.
 */

// ============================================================================
// Core SDK
// ============================================================================

export {
  default as McpFilterSdk,
  McpFilterSdk as SDK,
} from "./core/mcp-filter-sdk";
export type {
  McpSdkConfig,
  McpSdkEvents,
  McpSdkStats,
} from "./core/mcp-filter-sdk";

// ============================================================================
// FFI Bindings
// ============================================================================

export {
  McpBufferSliceStruct,
  McpFilterCallbacksStruct,
  McpFilterConfigStruct,
  McpFilterStatsStruct,
  McpProtocolMetadataStruct,
  allocateCString,
  createStruct,
  freeCString,
  fromCBool,
  fromCString,
  getNullPtr,
  isNullPtr,
  mcpFilterLib,
  readStruct,
  toCBool,
  toCString,
} from "./core/ffi-bindings";

// ============================================================================
// Type Definitions
// ============================================================================

export * from "./types";

// ============================================================================
// Version Information
// ============================================================================

export const VERSION = "1.0.0";
export const SDK_NAME = "MCP Filter SDK";
export const SDK_DESCRIPTION = "TypeScript SDK for MCP Filter C API";

// ============================================================================
// Default Export
// ============================================================================

export { McpFilterSdk as default } from "./core/mcp-filter-sdk";
