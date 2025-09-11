package com.gopher.mcp.filter.type

/**
 * Statistics for a filter
 */
class FilterStats {
    var bytesProcessed: Long = 0
    var packetsProcessed: Long = 0
    var errors: Long = 0
    var processingTimeUs: Long = 0
    var throughputMbps: Double = 0.0
    
    // Additional fields from original Kotlin version
    var totalProcessed: Long = 0
    var totalErrors: Long = 0
    var avgLatencyMs: Double = 0.0
    var maxLatencyMs: Double = 0.0
    var minLatencyMs: Double = 0.0
    var lastErrorCode: Int = 0
    var lastErrorMessage: String? = null
    var state: FilterState = FilterState.IDLE
    var uptime: Long = 0

    /** Default constructor */
    constructor()

    /**
     * Constructor matching Java version
     */
    constructor(
        bytesProcessed: Long,
        packetsProcessed: Long,
        errors: Long,
        processingTimeUs: Long,
        throughputMbps: Double
    ) {
        this.bytesProcessed = bytesProcessed
        this.packetsProcessed = packetsProcessed
        this.errors = errors
        this.processingTimeUs = processingTimeUs
        this.throughputMbps = throughputMbps
        this.totalErrors = errors
    }

    /**
     * Calculate error rate as a percentage
     */
    val errorRate: Double
        get() = if (totalProcessed > 0) {
            (totalErrors.toDouble() / totalProcessed) * 100
        } else {
            0.0
        }

    /**
     * Calculate throughput in MB/s
     */
    fun getThroughputMbps(durationSeconds: Double): Double {
        return if (durationSeconds > 0) {
            (bytesProcessed / (1024.0 * 1024.0)) / durationSeconds
        } else {
            0.0
        }
    }

    /**
     * Reset all statistics
     */
    fun reset() {
        bytesProcessed = 0
        packetsProcessed = 0
        errors = 0
        processingTimeUs = 0
        throughputMbps = 0.0
        totalProcessed = 0
        totalErrors = 0
        avgLatencyMs = 0.0
        maxLatencyMs = 0.0
        minLatencyMs = 0.0
        lastErrorCode = 0
        lastErrorMessage = null
    }

    /**
     * Merge statistics from another instance
     */
    fun merge(other: FilterStats) {
        bytesProcessed += other.bytesProcessed
        packetsProcessed += other.packetsProcessed
        errors += other.errors
        processingTimeUs += other.processingTimeUs
        totalProcessed += other.totalProcessed
        totalErrors += other.totalErrors
        
        // Recalculate average latency
        if (totalProcessed > 0) {
            avgLatencyMs = ((avgLatencyMs * (totalProcessed - other.totalProcessed)) + 
                           (other.avgLatencyMs * other.totalProcessed)) / totalProcessed
        }
        
        maxLatencyMs = maxOf(maxLatencyMs, other.maxLatencyMs)
        minLatencyMs = if (minLatencyMs == 0.0) other.minLatencyMs else minOf(minLatencyMs, other.minLatencyMs)
        
        if (other.lastErrorCode != 0) {
            lastErrorCode = other.lastErrorCode
            lastErrorMessage = other.lastErrorMessage
        }
    }
}

/**
 * Filter state enumeration
 */
enum class FilterState {
    IDLE,
    ACTIVE,
    PAUSED,
    ERROR,
    STOPPED
}