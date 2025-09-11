package com.gopher.mcp.filter.type.chain

/**
 * Statistics for a chain pool.
 */
data class ChainPoolStats(
    var active: Int = 0,
    var idle: Int = 0,
    var totalProcessed: Long = 0
)