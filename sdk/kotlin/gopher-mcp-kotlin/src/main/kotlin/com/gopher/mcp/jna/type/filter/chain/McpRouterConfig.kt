package com.gopher.mcp.jna.type.filter.chain

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_router_config_t
 */
@FieldOrder("strategy", "hash_seed", "route_table", "custom_router_data")
open class McpRouterConfig : Structure {
    @JvmField var strategy: Int = 0 // mcp_routing_strategy_t
    @JvmField var hash_seed: Int = 0 // uint32_t
    @JvmField var route_table: Pointer? = null // mcp_map_t
    @JvmField var custom_router_data: Pointer? = null

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpRouterConfig(), Structure.ByReference
    class ByValue : McpRouterConfig(), Structure.ByValue
}