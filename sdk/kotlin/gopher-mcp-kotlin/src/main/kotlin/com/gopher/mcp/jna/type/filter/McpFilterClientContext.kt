package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_filter_client_context_t
 */
@FieldOrder("client", "request_filters", "response_filters")
open class McpFilterClientContext : Structure {
    @JvmField var client: Pointer? = null // mcp_client_t
    @JvmField var request_filters: Long = 0 // mcp_filter_chain_t (uint64_t)
    @JvmField var response_filters: Long = 0 // mcp_filter_chain_t (uint64_t)

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpFilterClientContext(), Structure.ByReference
    class ByValue : McpFilterClientContext(), Structure.ByValue
}