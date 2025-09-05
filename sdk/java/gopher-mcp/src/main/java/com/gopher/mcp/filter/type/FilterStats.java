package com.gopher.mcp.filter.type;

/**
 * Statistics for a filter.
 */
public class FilterStats {
    private long bytesProcessed;
    private long packetsProcessed;
    private long errors;
    private long processingTimeUs;
    private double throughputMbps;

    /**
     * Default constructor
     */
    public FilterStats() {
        this.bytesProcessed = 0;
        this.packetsProcessed = 0;
        this.errors = 0;
        this.processingTimeUs = 0;
        this.throughputMbps = 0.0;
    }

    /**
     * Constructor with all parameters
     * @param bytesProcessed Number of bytes processed
     * @param packetsProcessed Number of packets processed
     * @param errors Number of errors
     * @param processingTimeUs Processing time in microseconds
     * @param throughputMbps Throughput in Mbps
     */
    public FilterStats(long bytesProcessed, long packetsProcessed, long errors,
                      long processingTimeUs, double throughputMbps) {
        this.bytesProcessed = bytesProcessed;
        this.packetsProcessed = packetsProcessed;
        this.errors = errors;
        this.processingTimeUs = processingTimeUs;
        this.throughputMbps = throughputMbps;
    }

    // Getters and Setters

    public long getBytesProcessed() {
        return bytesProcessed;
    }

    public void setBytesProcessed(long bytesProcessed) {
        this.bytesProcessed = bytesProcessed;
    }

    public long getPacketsProcessed() {
        return packetsProcessed;
    }

    public void setPacketsProcessed(long packetsProcessed) {
        this.packetsProcessed = packetsProcessed;
    }

    public long getErrors() {
        return errors;
    }

    public void setErrors(long errors) {
        this.errors = errors;
    }

    public long getProcessingTimeUs() {
        return processingTimeUs;
    }

    public void setProcessingTimeUs(long processingTimeUs) {
        this.processingTimeUs = processingTimeUs;
    }

    public double getThroughputMbps() {
        return throughputMbps;
    }

    public void setThroughputMbps(double throughputMbps) {
        this.throughputMbps = throughputMbps;
    }
}
