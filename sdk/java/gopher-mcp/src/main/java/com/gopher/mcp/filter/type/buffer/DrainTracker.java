package com.gopher.mcp.filter.type.buffer;

/**
 * Drain tracker for buffer monitoring
 */
public class DrainTracker {
    private long bytesDrained;
    private long totalBytes;
    private Object userData;

    /**
     * Default constructor
     */
    public DrainTracker() {
    }

    /**
     * Constructor with parameters
     * @param bytesDrained Bytes already drained
     * @param totalBytes Total bytes to drain
     */
    public DrainTracker(long bytesDrained, long totalBytes) {
        this.bytesDrained = bytesDrained;
        this.totalBytes = totalBytes;
    }

    // Getters and Setters

    public long getBytesDrained() {
        return bytesDrained;
    }

    public void setBytesDrained(long bytesDrained) {
        this.bytesDrained = bytesDrained;
    }

    public long getTotalBytes() {
        return totalBytes;
    }

    public void setTotalBytes(long totalBytes) {
        this.totalBytes = totalBytes;
    }

    public Object getUserData() {
        return userData;
    }

    public void setUserData(Object userData) {
        this.userData = userData;
    }
}
