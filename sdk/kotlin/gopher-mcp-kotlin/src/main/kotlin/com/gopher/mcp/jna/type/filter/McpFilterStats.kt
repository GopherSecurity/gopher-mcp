package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_stats_t
 */
@FieldOrder(
    "bytes_processed",
    "packets_processed",
    "errors",
    "processing_time_us",
    "throughput_mbps"
)
open class McpFilterStats : Structure {
    @JvmField var bytes_processed: Long = 0
    @JvmField var packets_processed: Long = 0
    @JvmField var errors: Long = 0
    @JvmField var processing_time_us: Long = 0
    @JvmField var throughput_mbps: Double = 0.0

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterStats(), Structure.ByReference
    class ByValue : McpFilterStats(), Structure.ByValue
}