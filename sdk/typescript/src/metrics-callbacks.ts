/**
 * @file metrics-callbacks.ts
 * @brief Koffi bridge for metrics filter callbacks.
 */

import * as koffi from 'koffi';
import { mcpFilterLib } from './mcp-ffi-bindings';
import type { MetricsCallbacks, MetricsSnapshot, MetricsThresholdEvent } from './types/metrics';

function clampBigIntToNumber(value: bigint): number {
  const maxSafe = BigInt(Number.MAX_SAFE_INTEGER);
  if (value > maxSafe) {
    return Number.MAX_SAFE_INTEGER;
  }
  if (value < BigInt(Number.MIN_SAFE_INTEGER)) {
    return Number.MIN_SAFE_INTEGER;
  }
  return Number(value);
}

function normalizeNumber(value: unknown): number {
  if (value === null || value === undefined) {
    return 0;
  }
  if (typeof value === 'number') {
    return value;
  }
  if (typeof value === 'bigint') {
    return clampBigIntToNumber(value);
  }
  return Number(value ?? 0);
}

function toMetricsSnapshot(raw: Record<string, unknown>): MetricsSnapshot {
  return {
    bytesReceived: normalizeNumber(raw.bytes_received),
    bytesSent: normalizeNumber(raw.bytes_sent),
    messagesReceived: normalizeNumber(raw.messages_received),
    messagesSent: normalizeNumber(raw.messages_sent),
    requestsReceived: normalizeNumber(raw.requests_received),
    requestsSent: normalizeNumber(raw.requests_sent),
    responsesReceived: normalizeNumber(raw.responses_received),
    responsesSent: normalizeNumber(raw.responses_sent),
    notificationsReceived: normalizeNumber(raw.notifications_received),
    notificationsSent: normalizeNumber(raw.notifications_sent),
    errorsReceived: normalizeNumber(raw.errors_received),
    errorsSent: normalizeNumber(raw.errors_sent),
    protocolErrors: normalizeNumber(raw.protocol_errors),
    totalLatencyMs: normalizeNumber(raw.total_latency_ms),
    minLatencyMs: normalizeNumber(raw.min_latency_ms),
    maxLatencyMs: normalizeNumber(raw.max_latency_ms),
    latencySamples: normalizeNumber(raw.latency_samples),
    currentReceiveRateBps: normalizeNumber(raw.current_receive_rate_bps),
    currentSendRateBps: normalizeNumber(raw.current_send_rate_bps),
    peakReceiveRateBps: normalizeNumber(raw.peak_receive_rate_bps),
    peakSendRateBps: normalizeNumber(raw.peak_send_rate_bps),
    connectionUptimeMs: normalizeNumber(raw.connection_uptime_ms),
    idleTimeMs: normalizeNumber(raw.idle_time_ms),
  };
}

/**
 * Lightweight holder for registered Koffi callbacks.
 */
export class MetricsCallbackHandle {
  private readonly callbacks = new Map<string, koffi.IKoffiRegisteredCallback>();
  private structPtr: any = null;
  private destroyed = false;

  constructor(private readonly jsCallbacks: MetricsCallbacks) {}

  register(): any {
    if (this.destroyed) {
      throw new Error('MetricsCallbackHandle has been destroyed');
    }

    const suffix = `${Date.now().toString(36)}_${Math.random().toString(36).slice(2)}`;

    const MetricsUpdateProto = koffi.proto(
      `void metrics_update_cb_${suffix}(void*, void*)`
    );
    const MetricsThresholdProto = koffi.proto(
      `void metrics_threshold_cb_${suffix}(const char*, uint64_t, uint64_t, void*)`
    );

    const MetricsStruct = koffi.struct(`mcp_connection_metrics_${suffix}`, {
      bytes_received: 'uint64_t',
      bytes_sent: 'uint64_t',
      messages_received: 'uint64_t',
      messages_sent: 'uint64_t',
      requests_received: 'uint64_t',
      requests_sent: 'uint64_t',
      responses_received: 'uint64_t',
      responses_sent: 'uint64_t',
      notifications_received: 'uint64_t',
      notifications_sent: 'uint64_t',
      errors_received: 'uint64_t',
      errors_sent: 'uint64_t',
      protocol_errors: 'uint64_t',
      total_latency_ms: 'uint64_t',
      min_latency_ms: 'uint64_t',
      max_latency_ms: 'uint64_t',
      latency_samples: 'uint64_t',
      current_receive_rate_bps: 'double',
      current_send_rate_bps: 'double',
      peak_receive_rate_bps: 'double',
      peak_send_rate_bps: 'double',
      connection_uptime_ms: 'uint64_t',
      idle_time_ms: 'uint64_t',
    });

    const CallbackStruct = koffi.struct(`mcp_metrics_callbacks_${suffix}`, {
      on_metrics_update: `metrics_update_cb_${suffix} *`,
      on_threshold_exceeded: `metrics_threshold_cb_${suffix} *`,
      user_data: 'void *',
    });

    const registered: Record<string, koffi.IKoffiRegisteredCallback | null> = {
      on_metrics_update: null,
      on_threshold_exceeded: null,
    };

    if (this.jsCallbacks.onMetricsUpdate) {
      const cb = koffi.register(
        (metricsPtr: Buffer, _userData: Buffer | null) => {
          try {
            if (!metricsPtr) {
              throw new Error('Received null metrics pointer');
            }
            const decoded = koffi.decode(metricsPtr, MetricsStruct) as Record<string, unknown>;
            this.jsCallbacks.onMetricsUpdate?.(toMetricsSnapshot(decoded));
          } catch (err) {
            this.jsCallbacks.onError?.(err instanceof Error ? err : new Error(String(err)));
          }
        },
        koffi.pointer(MetricsUpdateProto)
      );
      this.callbacks.set('on_metrics_update', cb);
      registered.on_metrics_update = cb;
    }

    if (this.jsCallbacks.onThresholdExceeded) {
      const cb = koffi.register(
        (metricName: string | null, value: bigint, threshold: bigint, _userData: Buffer | null) => {
          try {
            const event: MetricsThresholdEvent = {
              metric: metricName ?? '<unknown>',
              value: clampBigIntToNumber(value),
              threshold: clampBigIntToNumber(threshold),
            };
            this.jsCallbacks.onThresholdExceeded?.(event);
          } catch (err) {
            this.jsCallbacks.onError?.(err instanceof Error ? err : new Error(String(err)));
          }
        },
        koffi.pointer(MetricsThresholdProto)
      );
      this.callbacks.set('on_threshold_exceeded', cb);
      registered.on_threshold_exceeded = cb;
    }

    this.structPtr = koffi.alloc(CallbackStruct, 1);
    koffi.encode(this.structPtr, CallbackStruct, {
      on_metrics_update: registered.on_metrics_update,
      on_threshold_exceeded: registered.on_threshold_exceeded,
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
        console.error('Failed to unregister metrics callback', err);
      }
    }
    this.callbacks.clear();

    if (this.structPtr) {
      try {
        koffi.free(this.structPtr);
      } catch (err) {
        // eslint-disable-next-line no-console
        console.error('Failed to free metrics callback struct', err);
      }
      this.structPtr = null;
    }

    this.destroyed = true;
  }
}

export function registerMetricsCallbacks(
  chainHandle: number,
  callbacks: MetricsCallbacks
): MetricsCallbackHandle | null {
  const handle = new MetricsCallbackHandle(callbacks);
  const structPtr = handle.register();

  const result = mcpFilterLib.mcp_filter_chain_set_metrics_callbacks(
    BigInt(chainHandle),
    koffi.as(structPtr, 'void*')
  ) as number;

  if (result === 0) {
    return handle;
  }

  handle.destroy();

  if (result === -2) {
    // Metrics filter not available on this chain yet.
    return null;
  }

  throw new Error(`Failed to register metrics callbacks (error code ${result})`);
}

export function unregisterMetricsCallbacks(
  chainHandle: number,
  callbackHandle: MetricsCallbackHandle | null
): void {
  if (!callbackHandle) {
    return;
  }

  const result = mcpFilterLib.mcp_filter_chain_clear_metrics_callbacks(
    BigInt(chainHandle)
  ) as number;

  if (result !== 0) {
    // eslint-disable-next-line no-console
    console.error(`Failed to unregister metrics callbacks (error code ${result})`);
  }

  callbackHandle.destroy();
}
