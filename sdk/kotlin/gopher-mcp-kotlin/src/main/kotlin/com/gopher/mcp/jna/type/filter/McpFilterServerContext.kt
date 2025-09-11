package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_server_context_t
 */
@FieldOrder("server", "request_filters", "response_filters")
open class McpFilterServerContext : Structure {
    @JvmField var server: Pointer? = null // mcp_server_t
    @JvmField var request_filters: Long = 0 // mcp_filter_chain_t (uint64_t)
    @JvmField var response_filters: Long = 0 // mcp_filter_chain_t (uint64_t)

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterServerContext(), Structure.ByReference
    class ByValue : McpFilterServerContext(), Structure.ByValue
}