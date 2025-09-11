package com.gopher.mcp.filter.type.buffer

/**
 * Statistics for buffer pool
 */
data class PoolStats(
    var totalBuffers: Long = 0,
    var availableBuffers: Long = 0,
    var inUseBuffers: Long = 0,
    var totalAllocations: Long = 0,
    var totalReleases: Long = 0,
    var failedAllocations: Long = 0,
    var currentMemoryUsage: Long = 0,
    var peakMemoryUsage: Long = 0,
    // Legacy fields for compatibility
    var freeCount: Long = 0,
    var usedCount: Long = 0,
    var totalAllocated: Long = 0
)