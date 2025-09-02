/**
 * @file c-structs.ts
 * @brief C struct conversion utilities for FFI
 *
 * This file provides utilities to convert JavaScript objects to C structs
 * that can be passed to the C++ library functions.
 */

import koffi from "koffi";

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

// Chain configuration struct
export const ChainConfigStruct = koffi.struct("mcp_chain_config", {
  name: "string",
  mode: "int",
  routing: "int",
  max_parallel: "uint32",
  buffer_size: "uint32",
  timeout_ms: "uint32",
  stop_on_error: "int",
});

// Filter node struct
export const FilterNodeStruct = koffi.struct("mcp_filter_node", {
  filter: "uint64",
  name: "string",
  priority: "uint32",
  enabled: "int",
  bypass_on_error: "int",
  config: "void*", // JSON value
});

// Filter condition struct
export const FilterConditionStruct = koffi.struct("mcp_filter_condition", {
  match_type: "int",
  field: "string",
  value: "string",
  target_filter: "uint64",
});

// Filter configuration struct
export const FilterConfigStruct = koffi.struct("mcp_filter_config", {
  name: "string",
  type: "int",
  settings: "void*", // JSON value
  layer: "int",
  memory_pool: "uint64",
});

// Protocol metadata struct
export const ProtocolMetadataStruct = koffi.struct("mcp_protocol_metadata", {
  layer: "int",
  data: koffi.struct("protocol_data", {
    l3: koffi.struct("l3_data", {
      src_ip: "uint32",
      dst_ip: "uint32",
      protocol: "uint8",
      ttl: "uint8",
    }),
    l4: koffi.struct("l4_data", {
      src_port: "uint16",
      dst_port: "uint16",
      protocol: "int",
      sequence_num: "uint32",
    }),
    l5: koffi.struct("l5_data", {
      is_tls: "int",
      alpn: "string",
      sni: "string",
      session_id: "uint32",
    }),
    l7: koffi.struct("l7_data", {
      protocol: "int",
      headers: "void*", // Map
      method: "string",
      path: "string",
      status_code: "uint32",
    }),
  }),
});

// Filter callbacks struct
export const FilterCallbacksStruct = koffi.struct("mcp_filter_callbacks", {
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
export function createProtocolMetadataStruct(metadata: {
  layer?: number;
  data?: any;
}): any {
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
    } else if (
      metadata.layer === ProtocolLayer.APPLICATION &&
      metadata.data.l7
    ) {
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
export function createFilterCallbacksStruct(callbacks: {
  on_data?: any;
  on_write?: any;
  on_new_connection?: any;
  on_high_watermark?: any;
  on_low_watermark?: any;
  on_error?: any;
  user_data?: any;
}): any {
  return {
    on_data: callbacks.on_data || null,
    on_write: callbacks.on_write || null,
    on_new_connection: callbacks.on_new_connection || null,
    on_high_watermark: callbacks.on_high_watermark || null,
    on_low_watermark: callbacks.on_low_watermark || null,
    on_error: callbacks.on_error || null,
    user_data: callbacks.user_data || null,
  };
}

// ============================================================================
// Cleanup Functions
// ============================================================================

/**
 * Free a C struct from memory (no-op for plain JS objects)
 */
export function freeStruct(_struct: any): void {
  // No-op since we're using plain JavaScript objects now
  // The garbage collector will handle cleanup
}
