package com.gopher.mcp.jna.type.filter.buffer

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_buffer_stats_t
 */
@FieldOrder(
    "total_bytes",
    "used_bytes",
    "slice_count",
    "fragment_count",
    "read_operations",
    "write_operations"
)
open class McpBufferStats : Structure {
    @JvmField var total_bytes: Long = 0 // size_t
    @JvmField var used_bytes: Long = 0 // size_t
    @JvmField var slice_count: Long = 0 // size_t
    @JvmField var fragment_count: Long = 0 // size_t
    @JvmField var read_operations: Long = 0 // uint64_t
    @JvmField var write_operations: Long = 0 // uint64_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpBufferStats(), Structure.ByReference
    class ByValue : McpBufferStats(), Structure.ByValue
}