package com.gopher.mcp.filter.type.buffer

import java.nio.ByteBuffer

/**
 * Buffer reservation for zero-copy writing
 */
data class BufferReservation(
    var buffer: Long = 0,
    var data: ByteBuffer? = null,
    var capacity: Long = 0,
    var reservationId: Long = 0
)