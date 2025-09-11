package com.gopher.mcp.jna.type.filter.chain

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_condition_t
 */
@FieldOrder("match_type", "field", "value", "target_filter")
open class McpFilterCondition : Structure {
    @JvmField var match_type: Int = 0 // mcp_match_condition_t
    @JvmField var field: String? = null
    @JvmField var value: String? = null
    @JvmField var target_filter: Pointer? = null // mcp_filter_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterCondition(), Structure.ByReference
    class ByValue : McpFilterCondition(), Structure.ByValue
}