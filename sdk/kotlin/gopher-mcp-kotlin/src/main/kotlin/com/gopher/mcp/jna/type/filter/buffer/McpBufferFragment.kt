package com.gopher.mcp.jna.type.filter.buffer

import com.sun.jna.Callback
import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_buffer_fragment_t
 */
@FieldOrder("data", "size", "release_callback", "user_data")
open class McpBufferFragment : Structure {
    @JvmField var data: Pointer? = null
    @JvmField var size: Long = 0 // size_t
    @JvmField var release_callback: ReleaseCallback? = null
    @JvmField var user_data: Pointer? = null

    interface ReleaseCallback : Callback {
        fun invoke(data: Pointer?, size: Long, user_data: Pointer?)
    }

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpBufferFragment(), Structure.ByReference
    class ByValue : McpBufferFragment(), Structure.ByValue
}