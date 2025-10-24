/**
 * @file c-structs.ts
 * @brief C struct conversion utilities for FFI
 *
 * This file provides utilities to convert JavaScript objects to C structs
 * that can be passed to the C++ library functions.
 */

import * as koffi from "koffi";

// ============================================================================
// C Struct Definitions
// ============================================================================

// Define constants directly to avoid circular imports
const ChainExecutionMode = {
  SEQUENTIAL: 0,
  PARALLEL: 1,
  CONDITIONAL: 2,
  PIPELINE: 3,
} as const;

const RoutingStrategy = {
  ROUND_ROBIN: 0,
  LEAST_LOADED: 1,
  HASH_BASED: 2,
  PRIORITY: 3,
  CUSTOM: 99,
} as const;

const MatchCondition = {
  ALL: 0,
  ANY: 1,
  NONE: 2,
  CUSTOM: 99,
} as const;

const ProtocolLayer = {
  NETWORK: 3,
  TRANSPORT: 4,
  SESSION: 5,
  PRESENTATION: 6,
  APPLICATION: 7,
} as const;

const TransportProtocol = {
  TCP: 0,
  UDP: 1,
  QUIC: 2,
  SCTP: 3,
} as const;

const AppProtocol = {
  HTTP: 0,
  HTTPS: 1,
  HTTP2: 2,
  HTTP3: 3,
  GRPC: 4,
  WEBSOCKET: 5,
  JSONRPC: 6,
  CUSTOM: 99,
} as const;

// ============================================================================
// C Struct Types
// ============================================================================

// Chain configuration struct - use unique name to avoid conflicts
const uniqueId = Math.random().toString(36).substring(2, 15);
export const ChainConfigStruct = koffi.struct(`mcp_chain_config_ts_${uniqueId}`, {
  name: "string",
  mode: "int",
  routing: "int",
  max_parallel: "uint32",
  buffer_size: "uint32",
  timeout_ms: "uint32",
  stop_on_error: "int",
});

// Filter node struct - use unique name to avoid conflicts
export const FilterNodeStruct = koffi.struct(`mcp_filter_node_ts_${uniqueId}`, {
  filter: "uint64",
  name: "string",
  priority: "uint32",
  enabled: "int",
  bypass_on_error: "int",
  config: "void*", // JSON value
});

// Filter condition struct
export const FilterConditionStruct = koffi.struct(`mcp_filter_condition_${uniqueId}`, {
  match_type: "int",
  field: "string",
  value: "string",
  target_filter: "uint64",
});

// Assembler filter configuration struct (matches mcp_c_filter_chain.h)
export const FilterConfigStruct = koffi.struct(`mcp_filter_config_${uniqueId}`, {
  type: "string",
  name: "string",
  config: "void*", // JSON value
  enabled: "int",
  enabled_when: "void*", // JSON value
});

// Filter chain configuration struct for assembler
export const FilterChainConfigStruct = koffi.struct(`mcp_filter_chain_config_${uniqueId}`, {
  name: "string",
  transport_type: "string",
  filters: "void*", // Pointer to filter config array
  filter_count: "size_t",
});

// Validation result struct
export const ValidationResultStruct = koffi.struct(`mcp_chain_validation_result_${uniqueId}`, {
  valid: "int",
  error_count: "size_t",
  errors: "char**",
  warning_count: "size_t",
  warnings: "char**",
});

// Assembly result struct
export const AssemblyResultStruct = koffi.struct(`mcp_chain_assembly_result_${uniqueId}`, {
  success: "int",
  chain: "uint64",
  error_message: "string",
  created_filter_count: "size_t",
  created_filters: "char**",
  warning_count: "size_t",
  warnings: "char**",
});

// Protocol metadata struct
export const ProtocolMetadataStruct = koffi.struct(`mcp_protocol_metadata_${uniqueId}`, {
  layer: "int",
  data: koffi.struct(`protocol_data_${uniqueId}`, {
    l3: koffi.struct(`l3_data_${uniqueId}`, {
      src_ip: "uint32",
      dst_ip: "uint32",
      protocol: "uint8",
      ttl: "uint8",
    }),
    l4: koffi.struct(`l4_data_${uniqueId}`, {
      src_port: "uint16",
      dst_port: "uint16",
      protocol: "int",
      sequence_num: "uint32",
    }),
    l5: koffi.struct(`l5_data_${uniqueId}`, {
      is_tls: "int",
      alpn: "string",
      sni: "string",
      session_id: "uint32",
    }),
    l7: koffi.struct(`l7_data_${uniqueId}`, {
      protocol: "int",
      headers: "void*", // Map
      method: "string",
      path: "string",
      status_code: "uint32",
    }),
  }),
});

// Filter callbacks struct - use unique name to avoid conflicts
export const FilterCallbacksStruct = koffi.struct(`mcp_filter_callbacks_ts_${uniqueId}`, {
  on_data: "void*",
  on_write: "void*",
  on_new_connection: "void*",
  on_high_watermark: "void*",
  on_low_watermark: "void*",
  on_error: "void*",
  user_data: "void*",
});

// Alternative struct for JavaScript object casting
export const FilterCallbacksJSStruct = koffi.struct(`mcp_filter_callbacks_js_${uniqueId}`, {
  on_data: "void*",
  on_write: "void*",
  on_new_connection: "void*",
  on_high_watermark: "void*",
  on_low_watermark: "void*",
  on_error: "void*",
  user_data: "void*",
});

// ============================================================================
// Conversion Functions
// ============================================================================

/**
 * Convert JavaScript ChainConfig to C struct
 */
export function createChainConfigStruct(config: {
  name?: string;
  mode?: number;
  routing?: number;
  max_parallel?: number;
  buffer_size?: number;
  timeout_ms?: number;
  stop_on_error?: boolean;
}): any {
  // Create struct with initial values
  const struct = {
    name: config.name || "default-chain",
    mode: config.mode || ChainExecutionMode.SEQUENTIAL,
    routing: config.routing || RoutingStrategy.ROUND_ROBIN,
    max_parallel: config.max_parallel || 4,
    buffer_size: config.buffer_size || 8192,
    timeout_ms: config.timeout_ms || 5000,
    stop_on_error: config.stop_on_error ? 1 : 0,
  };

  return struct;
}

/**
 * Convert JavaScript FilterNode to C struct
 */
export function createFilterNodeStruct(node: {
  filter: number;
  name?: string;
  priority?: number;
  enabled?: boolean;
  bypass_on_error?: boolean;
  config?: any;
}): any {
  return {
    filter: node.filter,
    name: node.name || "unnamed-filter",
    priority: node.priority || 0,
    enabled: node.enabled !== false ? 1 : 0,
    bypass_on_error: node.bypass_on_error ? 1 : 0,
    config: null, // TODO: Convert JS object to JSON value
  };
}

/**
 * Convert JavaScript FilterCondition to C struct
 */
export function createFilterConditionStruct(condition: {
  match_type?: number;
  field?: string;
  value?: string;
  target_filter?: number;
}): any {
  return {
    match_type: condition.match_type || MatchCondition.ALL,
    field: condition.field || "",
    value: condition.value || "",
    target_filter: condition.target_filter || 0,
  };
}

/**
 * Convert JavaScript FilterConfig to C struct
 */
export function createFilterConfigStruct(config: {
  name?: string;
  type: number;
  settings?: any;
  layer?: number;
  memory_pool?: number;
}): any {
  return {
    name: config.name || "unnamed-filter",
    type: config.type,
    settings: null, // TODO: Convert JS object to JSON value
    layer: config.layer || ProtocolLayer.APPLICATION,
    memory_pool: config.memory_pool || 0,
  };
}

/**
 * Convert JavaScript ProtocolMetadata to C struct
 */
export function createProtocolMetadataStruct(metadata: { layer?: number; data?: any }): any {
  const result: any = {
    layer: metadata.layer || ProtocolLayer.APPLICATION,
    data: {},
  };

  // Initialize the union data
  if (metadata.data) {
    if (metadata.layer === ProtocolLayer.NETWORK && metadata.data.l3) {
      result.data.l3 = {
        src_ip: metadata.data.l3.src_ip || 0,
        dst_ip: metadata.data.l3.dst_ip || 0,
        protocol: metadata.data.l3.protocol || 0,
        ttl: metadata.data.l3.ttl || 64,
      };
    } else if (metadata.layer === ProtocolLayer.TRANSPORT && metadata.data.l4) {
      result.data.l4 = {
        src_port: metadata.data.l4.src_port || 0,
        dst_port: metadata.data.l4.dst_port || 0,
        protocol: metadata.data.l4.protocol || TransportProtocol.TCP,
        sequence_num: metadata.data.l4.sequence_num || 0,
      };
    } else if (metadata.layer === ProtocolLayer.SESSION && metadata.data.l5) {
      result.data.l5 = {
        is_tls: metadata.data.l5.is_tls ? 1 : 0,
        alpn: metadata.data.l5.alpn || "",
        sni: metadata.data.l5.sni || "",
        session_id: metadata.data.l5.session_id || 0,
      };
    } else if (metadata.layer === ProtocolLayer.APPLICATION && metadata.data.l7) {
      result.data.l7 = {
        protocol: metadata.data.l7.protocol || AppProtocol.HTTP,
        headers: null, // TODO: Convert JS object to Map
        method: metadata.data.l7.method || "",
        path: metadata.data.l7.path || "",
        status_code: metadata.data.l7.status_code || 0,
      };
    }
  }

  return result;
}

/**
 * Convert JavaScript FilterCallbacks to C struct
 */
export function createFilterCallbacksStruct(callbacks: any): any {
  // Store callbacks to prevent garbage collection
  if (!globalCallbackStore) {
    globalCallbackStore = new Set();
  }

  // Define callback prototypes using koffi.proto() with unique names
  const structId = Date.now().toString(36) + Math.random().toString(36).substring(2);
  const DataCallback = koffi.proto(
    `int DataCallback_${structId}(void *buffer, bool endStream, void *user_data)`
  );
  const WriteCallback = koffi.proto(
    `int WriteCallback_${structId}(void *buffer, bool endStream, void *user_data)`
  );
  const ConnCallback = koffi.proto(`int ConnCallback_${structId}(int state, void *user_data)`);
  const MarkCallback = koffi.proto(`void MarkCallback_${structId}(void *user_data)`);
  const ErrorCallback = koffi.proto(
    `void ErrorCallback_${structId}(uint64_t filter, int code, const char *msg, void *user_data)`
  );

  // Create a struct with function pointer types
  const CallbackStruct = koffi.struct(`mcp_filter_callbacks_${structId}`, {
    on_data: `DataCallback_${structId} *`,
    on_write: `WriteCallback_${structId} *`,
    on_new_connection: `ConnCallback_${structId} *`,
    on_high_watermark: `MarkCallback_${structId} *`,
    on_low_watermark: `MarkCallback_${structId} *`,
    on_error: `ErrorCallback_${structId} *`,
    user_data: "void *",
  });

  // Create registered callbacks for non-null callbacks
  const registeredCallbacks: any[] = [];

  const onDataPtr = callbacks.on_data
    ? koffi.register(callbacks.on_data, koffi.pointer(DataCallback))
    : null;
  if (onDataPtr) registeredCallbacks.push(onDataPtr);

  const onWritePtr = callbacks.on_write
    ? koffi.register(callbacks.on_write, koffi.pointer(WriteCallback))
    : null;
  if (onWritePtr) registeredCallbacks.push(onWritePtr);

  const onNewConnectionPtr = callbacks.on_new_connection
    ? koffi.register(callbacks.on_new_connection, koffi.pointer(ConnCallback))
    : null;
  if (onNewConnectionPtr) registeredCallbacks.push(onNewConnectionPtr);

  const onHighWatermarkPtr = callbacks.on_high_watermark
    ? koffi.register(callbacks.on_high_watermark, koffi.pointer(MarkCallback))
    : null;
  if (onHighWatermarkPtr) registeredCallbacks.push(onHighWatermarkPtr);

  const onLowWatermarkPtr = callbacks.on_low_watermark
    ? koffi.register(callbacks.on_low_watermark, koffi.pointer(MarkCallback))
    : null;
  if (onLowWatermarkPtr) registeredCallbacks.push(onLowWatermarkPtr);

  const onErrorPtr = callbacks.on_error
    ? koffi.register(callbacks.on_error, koffi.pointer(ErrorCallback))
    : null;
  if (onErrorPtr) registeredCallbacks.push(onErrorPtr);

  // Allocate the struct
  const callbackStruct = koffi.alloc(CallbackStruct, 1);

  // Use koffi.encode() to write the struct contents into the allocated memory
  // This bypasses the non-extensible object issue by writing directly to C memory
  koffi.encode(callbackStruct, CallbackStruct, {
    on_data: onDataPtr,
    on_write: onWritePtr,
    on_new_connection: onNewConnectionPtr,
    on_high_watermark: onHighWatermarkPtr,
    on_low_watermark: onLowWatermarkPtr,
    on_error: onErrorPtr,
    user_data: callbacks.user_data || null,
  });

  // Store the struct and callbacks to prevent GC
  globalCallbackStore.add(callbackStruct);
  globalCallbackStore.add(registeredCallbacks);

  return callbackStruct;
}

// Global callback store to prevent garbage collection
let globalCallbackStore: Set<any> | null = null;

// ============================================================================
// Cleanup Functions
// ============================================================================

/**
 * Free a C struct from memory
 */
export function freeStruct(struct: any): void {
  if (struct && typeof struct === "object" && struct.ptr) {
    // Free koffi-allocated memory
    koffi.free(struct);
  }
}
