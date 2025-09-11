package com.gopher.mcp.filter.type.buffer

/**
 * Configuration for buffer pool
 */
data class BufferPoolConfig(
    var bufferSize: Long = 0,
    var initialCount: Long = 0,
    var maxCount: Long = 0,
    var growBy: Long = 0,
    var flags: Int = 0,
    var alignment: Int = 0,
    var growthFactor: Float = 0f,
    var allocator: Long? = null
)