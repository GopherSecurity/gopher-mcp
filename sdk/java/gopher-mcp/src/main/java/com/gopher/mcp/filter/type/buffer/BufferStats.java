package com.gopher.mcp.filter.type.buffer;

/**
 * Statistics for a buffer.
 */
public class BufferStats {

    private long totalBytes;
    private long usedBytes;
    private long sliceCount;
    private long fragmentCount;
    private long readOperations;
    private long writeOperations;

    /**
     * Default constructor
     */
    public BufferStats() {
    }

    /**
     * Constructor with all parameters
     * @param totalBytes Total bytes in buffer
     * @param usedBytes Used bytes in buffer
     * @param sliceCount Number of slices
     * @param fragmentCount Number of fragments
     * @param readOperations Number of read operations
     * @param writeOperations Number of write operations
     */
    public BufferStats(long totalBytes, long usedBytes, long sliceCount,
                      long fragmentCount, long readOperations, long writeOperations) {
        this.totalBytes = totalBytes;
        this.usedBytes = usedBytes;
        this.sliceCount = sliceCount;
        this.fragmentCount = fragmentCount;
        this.readOperations = readOperations;
        this.writeOperations = writeOperations;
    }

    // Getters and Setters

    public long getTotalBytes() {
        return totalBytes;
    }

    public void setTotalBytes(long totalBytes) {
        this.totalBytes = totalBytes;
    }

    public long getUsedBytes() {
        return usedBytes;
    }

    public void setUsedBytes(long usedBytes) {
        this.usedBytes = usedBytes;
    }

    public long getSliceCount() {
        return sliceCount;
    }

    public void setSliceCount(long sliceCount) {
        this.sliceCount = sliceCount;
    }

    public long getFragmentCount() {
        return fragmentCount;
    }

    public void setFragmentCount(long fragmentCount) {
        this.fragmentCount = fragmentCount;
    }

    public long getReadOperations() {
        return readOperations;
    }

    public void setReadOperations(long readOperations) {
        this.readOperations = readOperations;
    }

    public long getWriteOperations() {
        return writeOperations;
    }

    public void setWriteOperations(long writeOperations) {
        this.writeOperations = writeOperations;
    }

    /**
     * Calculate the percentage of buffer used
     * @return Percentage used (0-100)
     */
    public double getUsagePercentage() {
        return totalBytes > 0 ? (double) usedBytes / totalBytes * 100 : 0;
    }
}
