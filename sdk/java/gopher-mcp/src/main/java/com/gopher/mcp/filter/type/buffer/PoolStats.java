package com.gopher.mcp.filter.type.buffer;

/**
 * Statistics for buffer pool
 */
public class PoolStats {
    private long freeCount;
    private long usedCount;
    private long totalAllocated;

    /**
     * Default constructor
     */
    public PoolStats() {
    }

    /**
     * Constructor with all parameters
     * @param freeCount Number of free buffers
     * @param usedCount Number of used buffers
     * @param totalAllocated Total bytes allocated
     */
    public PoolStats(long freeCount, long usedCount, long totalAllocated) {
        this.freeCount = freeCount;
        this.usedCount = usedCount;
        this.totalAllocated = totalAllocated;
    }

    // Getters and Setters

    public long getFreeCount() {
        return freeCount;
    }

    public void setFreeCount(long freeCount) {
        this.freeCount = freeCount;
    }

    public long getUsedCount() {
        return usedCount;
    }

    public void setUsedCount(long usedCount) {
        this.usedCount = usedCount;
    }

    public long getTotalAllocated() {
        return totalAllocated;
    }

    public void setTotalAllocated(long totalAllocated) {
        this.totalAllocated = totalAllocated;
    }
}
