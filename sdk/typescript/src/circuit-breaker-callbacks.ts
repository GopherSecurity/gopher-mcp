/**
 * @file circuit-breaker-callbacks.ts
 * @brief Koffi bridge for circuit breaker callbacks.
 */

import * as koffi from 'koffi';
import { mcpFilterLib } from './mcp-ffi-bindings';
import type { CircuitBreakerCallbacks, CircuitStateChangeEvent, CircuitRequestBlockedEvent, CircuitBreakerHealth } from './types/circuit-breaker';
import { CircuitState } from './types/circuit-breaker';

/**
 * Lightweight holder for registered koffi callbacks.
 */
export class CircuitBreakerCallbackHandle {
  private readonly callbacks = new Map<string, koffi.IKoffiRegisteredCallback>();
  private structPtr: any = null;
  private destroyed = false;

  constructor(private readonly jsCallbacks: CircuitBreakerCallbacks) {}

  register(): any {
    if (this.destroyed) {
      throw new Error('CircuitBreakerCallbackHandle has been destroyed');
    }

    const suffix = `${Date.now().toString(36)}_${Math.random().toString(36).slice(2)}`;

    const StateChangeProto = koffi.proto(
      `void circuit_state_change_cb_${suffix}(int32_t, int32_t, const char*, void*)`
    );
    const RequestBlockedProto = koffi.proto(
      `void circuit_request_blocked_cb_${suffix}(const char*, void*)`
    );
    const HealthUpdateProto = koffi.proto(
      `void circuit_health_update_cb_${suffix}(double, uint64_t, void*)`
    );

    const CallbackStruct = koffi.struct(`mcp_circuit_breaker_callbacks_${suffix}`, {
      on_state_change: `circuit_state_change_cb_${suffix} *`,
      on_request_blocked: `circuit_request_blocked_cb_${suffix} *`,
      on_health_update: `circuit_health_update_cb_${suffix} *`,
      user_data: 'void *',
    });

    const registered: Record<string, koffi.IKoffiRegisteredCallback | null> = {
      on_state_change: null,
      on_request_blocked: null,
      on_health_update: null,
    };

    if (this.jsCallbacks.onStateChange) {
      const cb = koffi.register(
        (oldState: number, newState: number, reason: string | null) => {
          try {
            const event: CircuitStateChangeEvent = {
              oldState: oldState as CircuitState,
              newState: newState as CircuitState,
              reason: reason ?? '',
            };
            this.jsCallbacks.onStateChange?.(event);
          } catch (err) {
            this.jsCallbacks.onError?.(err instanceof Error ? err : new Error(String(err)));
          }
        },
        koffi.pointer(StateChangeProto)
      );
      this.callbacks.set('on_state_change', cb);
      registered['on_state_change'] = cb;
    }

    if (this.jsCallbacks.onRequestBlocked) {
      const cb = koffi.register(
        (method: string | null) => {
          try {
            const event: CircuitRequestBlockedEvent = { method: method ?? '<unknown>' };
            this.jsCallbacks.onRequestBlocked?.(event);
          } catch (err) {
            this.jsCallbacks.onError?.(err instanceof Error ? err : new Error(String(err)));
          }
        },
        koffi.pointer(RequestBlockedProto)
      );
      this.callbacks.set('on_request_blocked', cb);
      registered['on_request_blocked'] = cb;
    }

    if (this.jsCallbacks.onHealthUpdate) {
      const cb = koffi.register(
        (successRate: number, latencyMs: bigint) => {
          try {
            const maxSafe = BigInt(Number.MAX_SAFE_INTEGER);
            const boundedLatency = latencyMs > maxSafe ? Number.MAX_SAFE_INTEGER : Number(latencyMs);
            const health: CircuitBreakerHealth = {
              successRate,
              averageLatencyMs: boundedLatency,
            };
            this.jsCallbacks.onHealthUpdate?.(health);
          } catch (err) {
            this.jsCallbacks.onError?.(err instanceof Error ? err : new Error(String(err)));
          }
        },
        koffi.pointer(HealthUpdateProto)
      );
      this.callbacks.set('on_health_update', cb);
      registered['on_health_update'] = cb;
    }

    this.structPtr = koffi.alloc(CallbackStruct, 1);
    koffi.encode(this.structPtr, CallbackStruct, {
      on_state_change: registered['on_state_change'],
      on_request_blocked: registered['on_request_blocked'],
      on_health_update: registered['on_health_update'],
      user_data: null,
    });

    return this.structPtr;
  }

  destroy(): void {
    if (this.destroyed) {
      return;
    }

    for (const cb of this.callbacks.values()) {
      try {
        koffi.unregister(cb);
      } catch (err) {
        // eslint-disable-next-line no-console
        console.error('Failed to unregister circuit breaker callback', err);
      }
    }
    this.callbacks.clear();

    if (this.structPtr) {
      try {
        koffi.free(this.structPtr);
      } catch (err) {
        // eslint-disable-next-line no-console
        console.error('Failed to free circuit breaker callback struct', err);
      }
      this.structPtr = null;
    }

    this.destroyed = true;
  }
}

/**
 * Register callbacks with the native circuit breaker filter.
 *
 * @returns A handle that must be kept alive while callbacks are active, or null if the
 *          runtime does not support callback registration yet.
 */
export function registerCircuitBreakerCallbacks(
  chainHandle: number,
  callbacks: CircuitBreakerCallbacks
): CircuitBreakerCallbackHandle | null {
  const handle = new CircuitBreakerCallbackHandle(callbacks);
  const structPtr = handle.register();

  const result = mcpFilterLib.mcp_filter_chain_set_circuit_breaker_callbacks(BigInt(chainHandle), koffi.as(structPtr, 'void*')) as number;

  if (result === 0) {
    return handle;
  }

  handle.destroy();

  if (result === -3) {
    // Feature not yet supported by the native layer.
    return null;
  }

  throw new Error(`Failed to register circuit breaker callbacks (error code ${result})`);
}

export function unregisterCircuitBreakerCallbacks(
  chainHandle: number,
  callbackHandle: CircuitBreakerCallbackHandle | null
): void {
  if (!callbackHandle) {
    return;
  }

  const result = mcpFilterLib.mcp_filter_chain_clear_circuit_breaker_callbacks(BigInt(chainHandle)) as number;
  if (result !== 0) {
    // eslint-disable-next-line no-console
    console.error(`Failed to unregister circuit breaker callbacks (error code ${result})`);
  }

  callbackHandle.destroy();
}
