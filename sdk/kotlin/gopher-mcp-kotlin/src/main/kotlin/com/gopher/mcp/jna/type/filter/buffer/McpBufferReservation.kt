package com.gopher.mcp.jna.type.filter.buffer

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure mapping for mcp_buffer_reservation_t
 */
@FieldOrder("data", "capacity", "buffer", "reservation_id")
open class McpBufferReservation : Structure {
    @JvmField var data: Pointer? = null
    @JvmField var capacity: Long = 0 // size_t
    @JvmField var buffer: Long = 0 // mcp_buffer_handle_t
    @JvmField var reservation_id: Long = 0 // uint64_t

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpBufferReservation(), Structure.ByReference
    class ByValue : McpBufferReservation(), Structure.ByValue
}