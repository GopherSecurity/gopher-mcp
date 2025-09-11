package com.gopher.mcp.filter.type.buffer

import java.nio.ByteBuffer

/**
 * Contiguous memory data from buffer
 */
data class ContiguousData(
    var data: ByteBuffer? = null,
    var length: Long = 0
)