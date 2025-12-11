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
export * from "./mcp-filter-api";

// Advanced Filter Chain Management (mcp_c_filter_chain.h)
export * from "./mcp-filter-chain";

// Advanced Buffer Operations (mcp_c_filter_buffer.h)
export * from "./mcp-filter-buffer";

// Filter Manager for JSONRPCMessage processing
export * from "./mcp-filter-manager";

// FFI bindings for C++ integration
export * from "./mcp-ffi-bindings";

// C struct conversion utilities (selective exports to avoid conflicts)
export {
  createChainConfigStruct,
  createFilterCallbacksStruct,
  createFilterConditionStruct,
  createFilterConfigStruct,
  createFilterNodeStruct,
  createProtocolMetadataStruct,
  freeStruct,
} from "./mcp-c-structs";

// Type definitions
export * from "./types";

// FilterChain FFI wrapper for Hybrid SDK
export { FilterChain } from "./filter-chain-ffi";

// Metrics callbacks bridge
export {
  registerMetricsCallbacks,
  unregisterMetricsCallbacks,
} from "./metrics-callbacks";
export type {
  MetricsCallbacks,
  MetricsSnapshot,
  MetricsThresholdEvent,
} from "./types/metrics";

// Filter event types and callbacks bridge
export {
  FilterEventType,
  FilterEventSeverity,
  filterEventTypeToString,
  filterEventSeverityToString,
} from "./filter-events";
export type {
  FilterEvent,
  FilterEventContext,
  FilterEventHandler,
} from "./filter-events";
export {
  registerFilterEventCallback,
  unregisterFilterEventCallback,
  FilterEventCallbackHandle,
} from "./filter-event-callbacks";

// Filter types and enums for Hybrid SDK (avoid re-exporting conflicting types)
export {
  FilterResultCode,
  FilterDecision,
  FilterMessage,
  FilterResult,
  FilterMetrics,
} from "./filter-types";
export type { CanonicalConfig } from "./filter-types";

// Hybrid SDK + Gopher Filters
export { GopherFilteredTransport, FilterDeniedError } from "./gopher-filtered-transport";
export type { GopherFilteredTransportConfig } from "./gopher-filtered-transport";
export { MessageQueue } from "./message-queue";

// Authentication API
export * as auth from "./auth";
