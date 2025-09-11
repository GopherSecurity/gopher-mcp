package com.gopher.mcp.filter.type.buffer

/** Statistics for a buffer. */
class BufferStats {

    var totalBytes: Long = 0
    var usedBytes: Long = 0
    var sliceCount: Long = 0
    var fragmentCount: Long = 0
    var readOperations: Long = 0
    var writeOperations: Long = 0

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param totalBytes Total bytes in buffer
     * @param usedBytes Used bytes in buffer
     * @param sliceCount Number of slices
     * @param fragmentCount Number of fragments
     * @param readOperations Number of read operations
     * @param writeOperations Number of write operations
     */
    constructor(
        totalBytes: Long,
        usedBytes: Long,
        sliceCount: Long,
        fragmentCount: Long,
        readOperations: Long,
        writeOperations: Long
    ) {
        this.totalBytes = totalBytes
        this.usedBytes = usedBytes
        this.sliceCount = sliceCount
        this.fragmentCount = fragmentCount
        this.readOperations = readOperations
        this.writeOperations = writeOperations
    }

    /**
     * Calculate the percentage of buffer used
     *
     * @return Percentage used (0-100)
     */
    fun getUsagePercentage(): Double {
        return if (totalBytes > 0) usedBytes.toDouble() / totalBytes * 100 else 0.0
    }
}