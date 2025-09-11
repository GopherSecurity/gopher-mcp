package com.gopher.mcp.jna.type.filter.buffer

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_buffer_pool_config_t
 */
@FieldOrder("buffer_size", "max_buffers", "prealloc_count", "use_thread_local", "zero_on_alloc")
open class McpBufferPoolConfig : Structure {
    @JvmField var buffer_size: Long = 0 // size_t
    @JvmField var max_buffers: Long = 0 // size_t
    @JvmField var prealloc_count: Long = 0 // size_t
    @JvmField var use_thread_local: Byte = 0 // mcp_bool_t
    @JvmField var zero_on_alloc: Byte = 0 // mcp_bool_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpBufferPoolConfig(), Structure.ByReference
    class ByValue : McpBufferPoolConfig(), Structure.ByValue
}