package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_buffer_slice_t
 */
@FieldOrder("data", "size", "flags")
open class McpBufferSlice : Structure {
    @JvmField var data: Pointer? = null
    @JvmField var size: Long = 0 // size_t
    @JvmField var flags: Int = 0 // uint32_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpBufferSlice(), Structure.ByReference
    class ByValue : McpBufferSlice(), Structure.ByValue
}