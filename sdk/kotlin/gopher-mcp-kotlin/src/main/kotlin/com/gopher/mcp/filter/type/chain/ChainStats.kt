package com.gopher.mcp.filter.type.chain

/** Statistics for a filter chain. */
class ChainStats {

    var totalProcessed: Long = 0
    var totalErrors: Long = 0
    var totalBypassed: Long = 0
    var avgLatencyMs: Double = 0.0
    var maxLatencyMs: Double = 0.0
    var throughputMbps: Double = 0.0
    var activeFilters: Int = 0

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param totalProcessed Total requests processed
     * @param totalErrors Total errors encountered
     * @param totalBypassed Total requests bypassed
     * @param avgLatencyMs Average latency in milliseconds
     * @param maxLatencyMs Maximum latency in milliseconds
     * @param throughputMbps Throughput in Mbps
     * @param activeFilters Number of active filters
     */
    constructor(
        totalProcessed: Long,
        totalErrors: Long,
        totalBypassed: Long,
        avgLatencyMs: Double,
        maxLatencyMs: Double,
        throughputMbps: Double,
        activeFilters: Int
    ) {
        this.totalProcessed = totalProcessed
        this.totalErrors = totalErrors
        this.totalBypassed = totalBypassed
        this.avgLatencyMs = avgLatencyMs
        this.maxLatencyMs = maxLatencyMs
        this.throughputMbps = throughputMbps
        this.activeFilters = activeFilters
    }

    /**
     * Calculate error rate
     *
     * @return Error rate as percentage (0-100)
     */
    fun getErrorRate(): Double {
        val total = totalProcessed + totalErrors
        return if (total > 0) totalErrors.toDouble() / total * 100 else 0.0
    }
}