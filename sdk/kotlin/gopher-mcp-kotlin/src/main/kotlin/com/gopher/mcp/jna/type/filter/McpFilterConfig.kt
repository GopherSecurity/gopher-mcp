package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_config_t
 */
@FieldOrder("name", "filter_type", "config_json", "layer")
open class McpFilterConfig : Structure {
    @JvmField var name: String? = null
    @JvmField var filter_type: Int = 0 // mcp_filter_type_t
    @JvmField var config_json: Pointer? = null // mcp_json_value_t
    @JvmField var layer: Int = 0 // mcp_filter_layer_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterConfig(), Structure.ByReference
    class ByValue : McpFilterConfig(), Structure.ByValue
}