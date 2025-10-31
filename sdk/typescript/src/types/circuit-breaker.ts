/**
 * @file circuit-breaker.ts
 * @brief Minimal TypeScript types matching the circuit breaker C API callbacks.
 */

/**
 * Circuit breaker state enumeration (matches mcp_circuit_state_t).
 */
export enum CircuitState {
  CLOSED = 0,
  OPEN = 1,
  HALF_OPEN = 2,
}

/**
 * Event emitted when the circuit breaker transitions between states.
 */
export interface CircuitStateChangeEvent {
  oldState: CircuitState;
  newState: CircuitState;
  reason: string;
}

/**
 * Event emitted when a request is blocked because the circuit is open.
 */
export interface CircuitRequestBlockedEvent {
  method: string;
}

/**
 * Periodic health metrics reported by the circuit breaker.
 */
export interface CircuitBreakerHealth {
  successRate: number;
  averageLatencyMs: number;
}

/**
 * Callback bundle that can be registered with the circuit breaker filter.
 * All callbacks are optional.
 */
export interface CircuitBreakerCallbacks {
  onStateChange?: (event: CircuitStateChangeEvent) => void;
  onRequestBlocked?: (event: CircuitRequestBlockedEvent) => void;
  onHealthUpdate?: (health: CircuitBreakerHealth) => void;
  onError?: (error: Error) => void;
}

/**
 * Utility: is the provided value a valid circuit state?
 */
export function isCircuitState(value: unknown): value is CircuitState {
  return value === CircuitState.CLOSED ||
         value === CircuitState.OPEN ||
         value === CircuitState.HALF_OPEN;
}

/**
 * Utility: convert a circuit state to a human-readable string.
 */
export function circuitStateToString(state: CircuitState): string {
  switch (state) {
    case CircuitState.CLOSED:
      return 'CLOSED';
    case CircuitState.OPEN:
      return 'OPEN';
    case CircuitState.HALF_OPEN:
      return 'HALF_OPEN';
    default:
      return `UNKNOWN(${state})`;
  }
}
