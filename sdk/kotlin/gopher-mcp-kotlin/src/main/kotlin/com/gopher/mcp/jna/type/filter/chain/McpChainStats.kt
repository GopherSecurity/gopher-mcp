package com.gopher.mcp.jna.type.filter.chain

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_chain_stats_t
 */
@FieldOrder(
    "total_processed",
    "total_errors",
    "total_bypassed",
    "avg_latency_ms",
    "max_latency_ms",
    "throughput_mbps",
    "active_filters"
)
open class McpChainStats : Structure {
    @JvmField var total_processed: Long = 0 // uint64_t
    @JvmField var total_errors: Long = 0 // uint64_t
    @JvmField var total_bypassed: Long = 0 // uint64_t
    @JvmField var avg_latency_ms: Double = 0.0
    @JvmField var max_latency_ms: Double = 0.0
    @JvmField var throughput_mbps: Double = 0.0
    @JvmField var active_filters: Int = 0 // uint32_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpChainStats(), Structure.ByReference
    class ByValue : McpChainStats(), Structure.ByValue
}