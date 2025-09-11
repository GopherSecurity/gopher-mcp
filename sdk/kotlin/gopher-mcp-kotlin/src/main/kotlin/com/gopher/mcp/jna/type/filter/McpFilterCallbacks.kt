package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_callbacks_t
 */
@FieldOrder("on_data", "on_write", "on_event", "on_metadata", "on_trailers", "user_data")
open class McpFilterCallbacks : Structure {
    @JvmField var on_data: Pointer? = null
    @JvmField var on_write: Pointer? = null
    @JvmField var on_event: Pointer? = null
    @JvmField var on_metadata: Pointer? = null
    @JvmField var on_trailers: Pointer? = null
    @JvmField var user_data: Pointer? = null

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterCallbacks(), Structure.ByReference
    class ByValue : McpFilterCallbacks(), Structure.ByValue
}