package com.gopher.mcp.jna.type.filter.chain

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_node_t
 */
@FieldOrder("filter", "name", "priority", "enabled", "bypass_on_error", "config")
open class McpFilterNode : Structure {
    @JvmField var filter: Pointer? = null // mcp_filter_t
    @JvmField var name: String? = null
    @JvmField var priority: Int = 0 // uint32_t
    @JvmField var enabled: Byte = 0 // mcp_bool_t
    @JvmField var bypass_on_error: Byte = 0 // mcp_bool_t
    @JvmField var config: Pointer? = null // mcp_json_value_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterNode(), Structure.ByReference
    class ByValue : McpFilterNode(), Structure.ByValue
}