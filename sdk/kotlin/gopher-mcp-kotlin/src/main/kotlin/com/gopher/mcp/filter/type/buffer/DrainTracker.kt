package com.gopher.mcp.filter.type.buffer

/**
 * Drain tracker for buffer monitoring
 */
data class DrainTracker(
    var bytesDrained: Long = 0,
    var totalBytes: Long = 0,
    var userData: Any? = null
) {
    /**
     * Constructor with parameters
     *
     * @param bytesDrained Bytes already drained
     * @param totalBytes Total bytes to drain
     */
    constructor(bytesDrained: Long, totalBytes: Long) : this() {
        this.bytesDrained = bytesDrained
        this.totalBytes = totalBytes
    }
}