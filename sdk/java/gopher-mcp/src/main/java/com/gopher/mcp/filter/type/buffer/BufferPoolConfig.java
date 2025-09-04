package com.gopher.mcp.filter.type.buffer;

/**
 * Configuration for buffer pool
 */
public class BufferPoolConfig {
    private long bufferSize;
    private long initialCount;
    private long maxCount;
    private long growBy;
    private int flags;

    /**
     * Default constructor
     */
    public BufferPoolConfig() {
    }

    /**
     * Constructor with all parameters
     * @param bufferSize Size of each buffer
     * @param initialCount Initial buffer count
     * @param maxCount Maximum buffer count
     * @param growBy Number of buffers to grow by
     * @param flags Configuration flags
     */
    public BufferPoolConfig(long bufferSize, long initialCount, long maxCount, long growBy, int flags) {
        this.bufferSize = bufferSize;
        this.initialCount = initialCount;
        this.maxCount = maxCount;
        this.growBy = growBy;
        this.flags = flags;
    }

    // Getters and Setters

    public long getBufferSize() {
        return bufferSize;
    }

    public void setBufferSize(long bufferSize) {
        this.bufferSize = bufferSize;
    }

    public long getInitialCount() {
        return initialCount;
    }

    public void setInitialCount(long initialCount) {
        this.initialCount = initialCount;
    }

    public long getMaxCount() {
        return maxCount;
    }

    public void setMaxCount(long maxCount) {
        this.maxCount = maxCount;
    }

    public long getGrowBy() {
        return growBy;
    }

    public void setGrowBy(long growBy) {
        this.growBy = growBy;
    }

    public int getFlags() {
        return flags;
    }

    public void setFlags(int flags) {
        this.flags = flags;
    }
}
