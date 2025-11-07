/**
 * @file filter-event-callbacks.ts
 * @brief Koffi bridge for unified chain-level filter event callbacks
 */

import * as koffi from 'koffi';
import { mcpFilterLib } from './mcp-ffi-bindings';
import type {
  FilterEvent,
  FilterEventHandler,
  FilterEventContext,
} from './filter-events';
import { FilterEventType, FilterEventSeverity } from './filter-events';

const EventContextStruct = koffi.struct('mcp_filter_event_context_t', {
  chain_id: 'char*',
  stream_id: 'char*',
  correlation_id: 'char*',
});

/**
 * Lightweight holder for registered koffi callbacks
 */
export class FilterEventCallbackHandle {
  private callback: koffi.IKoffiRegisteredCallback | null = null;
  private destroyed = false;

  constructor(private readonly jsHandler: FilterEventHandler) {}

  register(): koffi.IKoffiRegisteredCallback {
    if (this.destroyed) {
      throw new Error('FilterEventCallbackHandle has been destroyed');
    }

    // Create unique prototype name to avoid conflicts
    const suffix = `${Date.now().toString(36)}_${Math.random().toString(36).slice(2)}`;

    // Define C callback prototype matching mcp_filter_event_callback_t
    // void (*callback)(const char* filter_name,
    //                  const char* filter_instance_id,
    //                  mcp_filter_event_type_t event_type,
    //                  mcp_filter_event_severity_t severity,
    //                  const char* event_data_json,
    //                  const mcp_filter_event_context_t* context,
    //                  int64_t timestamp_ms,
    //                  void* user_data)
    const EventCallbackProto = koffi.proto(
      `void filter_event_callback_${suffix}(` +
        'const char*, ' +      // filter_name
        'const char*, ' +      // filter_instance_id
        'int32_t, ' +          // event_type
        'int32_t, ' +          // severity
        'const char*, ' +      // event_data_json
        'void*, ' +            // context pointer
        'int64_t, ' +          // timestamp_ms
        'void*' +              // user_data
      ')'
    );

    // Register the callback with koffi
    this.callback = koffi.register(
      (
        filterName: string | null,
        filterInstanceId: string | null,
        eventType: number,
        severity: number,
        eventDataJson: string | null,
        contextPtr: any,  // Pointer to context struct
        timestampMs: bigint,
        _userData: unknown,
      ) => {
        try {
          // Parse event data JSON
          let eventData: Record<string, unknown> = {};
          if (eventDataJson) {
            try {
              eventData = JSON.parse(eventDataJson);
            } catch (err) {
              console.error('Failed to parse event data JSON:', err);
            }
          }

          // Extract context from the C struct pointer (not from JSON!)
          let context: FilterEventContext | undefined;
          if (contextPtr && contextPtr !== 0) {
            try {
              const ctx = koffi.decode(contextPtr, EventContextStruct);
              context = {
                chainId: ctx.chain_id || undefined,
                streamId: ctx.stream_id || undefined,
                correlationId: ctx.correlation_id || undefined,
              };
            } catch (err) {
              console.error('Failed to decode context struct:', err);
            }
          }

          // Convert bigint timestamp to number (safely)
          const maxSafe = BigInt(Number.MAX_SAFE_INTEGER);
          const timestamp = timestampMs > maxSafe ? Number.MAX_SAFE_INTEGER : Number(timestampMs);

          // Construct TypeScript event object
          // Use spread operator to only include optional fields if defined (for exactOptionalPropertyTypes)
          const event: FilterEvent = {
            ...(context && { context }),
            filterName: filterName ?? '<unknown>',
            ...(filterInstanceId && { filterInstanceId }),
            eventType: eventType as FilterEventType,
            severity: severity as FilterEventSeverity,
            eventData,
            timestampMs: timestamp,
          };

          // Invoke user's TypeScript handler
          this.jsHandler(event);
        } catch (err) {
          console.error('Error in filter event callback:', err);
        }
      },
      koffi.pointer(EventCallbackProto)
    );

    return this.callback;
  }

  destroy(): void {
    if (this.destroyed) {
      return;
    }

    if (this.callback) {
      try {
        koffi.unregister(this.callback);
      } catch (err) {
        console.error('Failed to unregister filter event callback:', err);
      }
      this.callback = null;
    }

    this.destroyed = true;
  }
}

/**
 * Register chain-level event callback with the native filter chain
 *
 * @param chainHandle Filter chain handle
 * @param handler TypeScript event handler
 * @returns A handle that must be kept alive while the callback is active
 */
export function registerFilterEventCallback(
  chainHandle: number,
  handler: FilterEventHandler
): FilterEventCallbackHandle {
  const handle = new FilterEventCallbackHandle(handler);
  const callback = handle.register();

  // Call mcp_filter_chain_set_event_callback
  const result = mcpFilterLib.mcp_filter_chain_set_event_callback(
    BigInt(chainHandle),
    callback,
    null  // user_data
  ) as number;

  if (result === 0) {
    return handle;
  }

  // Registration failed, clean up
  handle.destroy();

  throw new Error(
    `Failed to register filter event callback (error code ${result})`
  );
}

/**
 * Unregister chain-level event callback
 *
 * @param chainHandle Filter chain handle
 * @param callbackHandle Handle returned from registerFilterEventCallback
 */
export function unregisterFilterEventCallback(
  chainHandle: number,
  callbackHandle: FilterEventCallbackHandle
): void {
  const result = mcpFilterLib.mcp_filter_chain_clear_event_callback(
    BigInt(chainHandle)
  ) as number;

  if (result !== 0) {
    console.error(`Failed to unregister filter event callback (error code ${result})`);
  }

  callbackHandle.destroy();
}
