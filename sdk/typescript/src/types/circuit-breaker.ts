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
  consecutive_5xx?: number;

  /** Error rate threshold (0.0-1.0) to open circuit */
  error_rate_threshold?: number;

  /** Time window in milliseconds for error rate calculation */
  interval_ms?: number;

  /** How long circuit stays open before trying half-open state (ms) */
  ejection_time_ms?: number;

  /** Maximum percentage of hosts that can be ejected */
  max_ejection_percent?: number;

  /** Minimum number of requests before circuit can open */
  min_request_count?: number;

  /** Number of successful requests needed to close from half-open */
  success_threshold?: number;

  /** Maximum requests allowed in half-open state */
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
