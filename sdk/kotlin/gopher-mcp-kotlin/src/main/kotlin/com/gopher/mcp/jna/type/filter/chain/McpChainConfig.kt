package com.gopher.mcp.jna.type.filter.chain

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_chain_config_t
 */
@FieldOrder(
    "name",
    "mode",
    "routing",
    "max_parallel",
    "buffer_size",
    "timeout_ms",
    "stop_on_error"
)
open class McpChainConfig : Structure {
    @JvmField var name: String? = null
    @JvmField var mode: Int = 0 // mcp_chain_execution_mode_t
    @JvmField var routing: Int = 0 // mcp_routing_strategy_t
    @JvmField var max_parallel: Int = 0 // uint32_t
    @JvmField var buffer_size: Int = 0 // uint32_t
    @JvmField var timeout_ms: Int = 0 // uint32_t
    @JvmField var stop_on_error: Byte = 0 // mcp_bool_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpChainConfig(), Structure.ByReference
    class ByValue : McpChainConfig(), Structure.ByValue
}