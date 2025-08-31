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
  allocateCString,
  createStruct,
  freeCString,
  fromCBool,
  fromCString,
  getNullPtr,
  isNullPtr,
  // Core FFI bindings
  mcpFilterLib,
  readStruct,
  toCBool,
  toCString,
} from "./core/ffi-bindings";

// ============================================================================
// Protocol Filters
// ============================================================================

// HTTP Filter
export {
  HttpFilter,
  HttpFilterType,
  HttpMethod,
  HttpStatus,
} from "./protocols/http-filter";
export type {
  HttpFilterCallbacks,
  HttpFilterConfig,
  HttpHeaders,
  HttpRequest,
  HttpResponse,
} from "./protocols/http-filter";

// TCP Proxy Filter
export {
  TcpConnectionState,
  TcpFilterType,
  TcpProxyFilter,
} from "./protocols/tcp-proxy-filter";
export type {
  TcpConnection,
  TcpProxyCallbacks,
  TcpProxyConfig,
} from "./protocols/tcp-proxy-filter";

// ============================================================================
// Advanced Buffer Management
// ============================================================================

export {
  AdvancedBuffer,
  AdvancedBufferPool,
  BufferFlags,
  BufferOwnership,
} from "./buffers/advanced-buffer";
export type {
  BufferFragment,
  BufferReservation,
  DrainTracker,
} from "./buffers/advanced-buffer";

// ============================================================================
// Filter Chains
// ============================================================================

export {
  ChainExecutionMode,
  ChainState,
  FilterChain,
  MatchCondition,
  RoutingStrategy,
} from "./chains/filter-chain";
export type {
  ChainConfig,
  ChainEventCallback,
  ChainStats,
  FilterCondition,
  FilterMatchCallback,
  FilterNode,
  RouterConfig,
  RoutingFunction,
} from "./chains/filter-chain";

// ============================================================================
// Type Definitions
// ============================================================================

export * from "./types";

// ============================================================================
// RAII Resource Management
// ============================================================================

export * from "./core/raii";

// ============================================================================
// Version Information
// ============================================================================

export const VERSION = "1.0.0";
export const SDK_NAME = "MCP Filter SDK";
export const SDK_DESCRIPTION =
  "TypeScript SDK for MCP Filter C API with Advanced Buffer Management, Filter Chains, and RAII Resource Management";

// ============================================================================
// Default Export
// ============================================================================

export { McpFilterSdk as default } from "./core/mcp-filter-sdk";
