/**
 * @file filter-types.ts
 * @brief Type definitions for FFI Filter Chain implementation
 *
 * This module provides TypeScript type definitions for the Koffi-based
 * FFI bridge to the C++ filter chain API, supporting Hybrid SDK.
 */

// Re-export canonical configuration types from mcp-filter-chain.ts
export type {
  CanonicalConfig,
  ListenerConfig,
  FilterChain as FilterChainConfig,
  FilterSpec,
  Address,
  SocketAddress,
} from './mcp-filter-chain';

/**
 * Filter operation result codes (matching mcp_c_types.h)
 */
export enum FilterResultCode {
  SUCCESS = 0,
  ERROR = -1,
  INVALID_CONFIG = -2,
  PROCESSING_ERROR = -3,
  OUT_OF_MEMORY = -4,
  NOT_INITIALIZED = -5,
}

/**
 * Filter decisions for message processing
 */
export enum FilterDecision {
  ALLOW = 0,      // Continue processing
  DENY = 1,       // Reject the message
  DELAY = 2,      // Delay processing
  QUEUE = 3,      // Queue for later
  TRANSFORM = 4,  // Message was transformed
}

/**
 * Message structure for filter processing (DEPRECATED)
 *
 * @deprecated This interface is no longer used. FilterChain.processIncoming() and
 * processOutgoing() accept message objects directly (e.g., JSONRPCMessage) and handle
 * JSON serialization internally. Do NOT wrap messages with { messageJson: ... }.
 *
 * Correct usage:
 * ```typescript
 * await chain.processIncoming(message);  // ✅ Pass message object directly
 * ```
 *
 * Incorrect usage:
 * ```typescript
 * await chain.processIncoming({ messageJson: JSON.stringify(message) });  // ❌ Double serialization
 * ```
 */
export interface FilterMessage {
  messageJson: string;
}

/**
 * Filter processing result
 */
export interface FilterResult {
  decision: FilterDecision;
  transformedMessage?: string;
  reason?: string;
  delayMs?: number;
  metadata?: Record<string, any>;
}

/**
 * Filter metrics (per-filter statistics)
 */
export interface FilterMetrics {
  [filterName: string]: {
    requests_total?: number;
    requests_allowed?: number;
    requests_denied?: number;
    requests_delayed?: number;
    current_rate?: number;
    tokens_available?: number;
    state?: string;  // e.g., "OPEN", "CLOSED", "HALF_OPEN" for circuit breaker
    failure_count?: number;
    success_count?: number;
    avg_latency_ms?: number;
    p95_latency_ms?: number;
    p99_latency_ms?: number;
    [key: string]: any;  // Allow additional filter-specific metrics
  };
}

/**
 * Chain statistics (overall chain metrics)
 */
export interface ChainStats {
  total_processed: number;
  total_errors: number;
  total_bypassed: number;
  avg_latency_ms: number;
  max_latency_ms: number;
  throughput_mbps: number;
  active_filters: number;
}

/**
 * Validation diagnostics
 */
export interface ValidationResult {
  valid: boolean;
  errors: string[];
  warnings: string[];
}

/**
 * Assembly diagnostics
 */
export interface AssemblyResult {
  success: boolean;
  chain?: number;  // Filter chain handle if successful
  errorMessage?: string;
  createdFilters: string[];
  warnings: string[];
}
