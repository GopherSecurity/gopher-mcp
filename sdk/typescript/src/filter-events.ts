/**
 * @file filter-events.ts
 * @brief TypeScript type definitions for unified chain-level filter events
 */

/**
 * Filter event types
 *
 * Matches the C API mcp_filter_event_type_t enum values.
 */
export enum FilterEventType {
  // Circuit breaker events
  CIRCUIT_STATE_CHANGE = 0,
  CIRCUIT_REQUEST_BLOCKED = 1,
  CIRCUIT_HEALTH_UPDATE = 2,

  // Rate limiter events
  RATE_LIMIT_EXCEEDED = 3,
  RATE_LIMIT_RESET = 4,
  RATE_LIMIT_SAMPLE = 5,
  RATE_LIMIT_WINDOW_RESET = 6,
  RATE_LIMIT_CONFIGURATION_SYNCED = 7,

  // Metrics events
  METRIC_UPDATE = 50,
  METRIC_FLUSH = 51,

  // Request logger events
  REQUEST_LOGGED = 60,
  RESPONSE_LOGGED = 61,

  // Protocol events
  PROTOCOL_ERROR = 70,
  PROTOCOL_UPGRADE = 71,

  // Generic filter events
  FILTER_INITIALIZED = 80,
  FILTER_DESTROYED = 81,
  FILTER_ERROR = 100,
  FILTER_WARNING = 101,
  FILTER_INFO = 102,
}

/**
 * Event severity levels
 */
export enum FilterEventSeverity {
  TRACE = 0,
  DEBUG = 1,
  INFO = 2,
  WARN = 3,
  ERROR = 4,
  CRITICAL = 5,
}

/**
 * Event context metadata
 */
export interface FilterEventContext {
  /** Chain identifier (unique per chain instance) */
  chainId?: string;

  /** Stream or session identifier */
  streamId?: string;

  /** Correlation identifier for tracing requests */
  correlationId?: string;
}

/**
 * Unified filter event
 *
 * Complete event structure emitted by filters and delivered to chain-level
 * observers.
 */
export interface FilterEvent {
  /** Event context (chain ID, stream ID, correlation ID) */
  context?: FilterEventContext;

  /** Filter name (e.g., "circuit_breaker", "rate_limiter") */
  filterName: string;

  /** Filter instance ID (unique if multiple instances) */
  filterInstanceId?: string;

  /** Event type classification */
  eventType: FilterEventType;

  /** Event severity level */
  severity: FilterEventSeverity;

  /** Event-specific structured data (filter-defined schema) */
  eventData: Record<string, unknown>;

  /** Event timestamp in milliseconds since epoch */
  timestampMs: number;
}

/**
 * Filter event handler callback type
 */
export type FilterEventHandler = (event: FilterEvent) => void;

/**
 * Get string representation of event type
 */
export function filterEventTypeToString(type: FilterEventType): string {
  return FilterEventType[type] ?? 'UNKNOWN';
}

/**
 * Get string representation of event severity
 */
export function filterEventSeverityToString(severity: FilterEventSeverity): string {
  return FilterEventSeverity[severity] ?? 'UNKNOWN';
}
