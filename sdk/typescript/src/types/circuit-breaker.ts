/**
 * @file circuit-breaker.ts
 * @brief Circuit breaker filter type definitions
 *
 * This module provides TypeScript type definitions for circuit breaker
 * filter configuration and state management.
 */

/**
 * Circuit breaker state
 */
export enum CircuitBreakerState {
  /**  Circuit is operating normally */
  CLOSED = 'CLOSED',
  /** Circuit has opened due to failures */
  OPEN = 'OPEN',
  /** Circuit is testing if service has recovered */
  HALF_OPEN = 'HALF_OPEN',
}

/**
 * Circuit breaker configuration
 */
export interface CircuitBreakerConfig {
  /** Name of the circuit breaker instance */
  name?: string;

  /** Number of consecutive failures before opening circuit */
  failureThreshold?: number;

  /** Error rate threshold (0.0-1.0) to open circuit */
  errorRateThreshold?: number;

  /** Minimum number of requests before error rate is considered */
  minRequests?: number;

  /** How long circuit stays open before trying half-open state (ms) */
  timeoutMs?: number;

  /** Sliding window duration in milliseconds */
  windowSizeMs?: number;

  /** Maximum requests allowed in half-open state */
  halfOpenMaxRequests?: number;

  /** Successful half-open requests required to close the circuit */
  halfOpenSuccessThreshold?: number;

  /** Count timeouts as failures */
  trackTimeouts?: boolean;

  /** Count generic errors as failures */
  trackErrors?: boolean;

  /** Count 4xx responses as failures */
  track4xxAsErrors?: boolean;

  /** @deprecated use failureThreshold */
  consecutive_5xx?: number;
  /** @deprecated use errorRateThreshold */
  error_rate_threshold?: number;
  /** @deprecated use windowSizeMs */
  interval_ms?: number;
  /** @deprecated use timeoutMs */
  ejection_time_ms?: number;
  /** @deprecated not supported */
  max_ejection_percent?: number;
  /** @deprecated use minRequests */
  min_request_count?: number;
  /** @deprecated use halfOpenSuccessThreshold */
  success_threshold?: number;
  /** @deprecated use halfOpenMaxRequests */
  half_open_max_requests?: number;
}

/**
 * Circuit breaker statistics
 */
export interface CircuitBreakerStats {
  /** Current state of the circuit */
  state: CircuitBreakerState;

  /** Total number of requests */
  total_requests: number;

  /** Number of successful requests */
  successful_requests: number;

  /** Number of failed requests */
  failed_requests: number;

  /** Current error rate (0.0-1.0) */
  error_rate: number;

  /** Number of times circuit has opened */
  trip_count: number;

  /** Timestamp when circuit last changed state */
  last_state_change_ms: number;

  /** Time remaining until circuit can transition (ms) */
  time_until_retry_ms?: number;
}

/**
 * Circuit breaker event data
 */
export interface CircuitBreakerEventData {
  /** Previous state */
  old_state?: CircuitBreakerState;

  /** New state */
  new_state?: CircuitBreakerState;

  /** Current error rate */
  error_rate?: number;

  /** Number of consecutive failures */
  consecutive_failures?: number;

  /** Reason for state change */
  reason?: string;

  /** Current statistics */
  stats?: Partial<CircuitBreakerStats>;
}
