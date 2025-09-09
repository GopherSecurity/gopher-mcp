package com.gopher.mcp.filter.type.chain;

/** Statistics for a filter chain. */
public class ChainStats {

  private long totalProcessed;
  private long totalErrors;
  private long totalBypassed;
  private double avgLatencyMs;
  private double maxLatencyMs;
  private double throughputMbps;
  private int activeFilters;

  /** Default constructor */
  public ChainStats() {}

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
  public ChainStats(
      long totalProcessed,
      long totalErrors,
      long totalBypassed,
      double avgLatencyMs,
      double maxLatencyMs,
      double throughputMbps,
      int activeFilters) {
    this.totalProcessed = totalProcessed;
    this.totalErrors = totalErrors;
    this.totalBypassed = totalBypassed;
    this.avgLatencyMs = avgLatencyMs;
    this.maxLatencyMs = maxLatencyMs;
    this.throughputMbps = throughputMbps;
    this.activeFilters = activeFilters;
  }

  // Getters and Setters

  public long getTotalProcessed() {
    return totalProcessed;
  }

  public void setTotalProcessed(long totalProcessed) {
    this.totalProcessed = totalProcessed;
  }

  public long getTotalErrors() {
    return totalErrors;
  }

  public void setTotalErrors(long totalErrors) {
    this.totalErrors = totalErrors;
  }

  public long getTotalBypassed() {
    return totalBypassed;
  }

  public void setTotalBypassed(long totalBypassed) {
    this.totalBypassed = totalBypassed;
  }

  public double getAvgLatencyMs() {
    return avgLatencyMs;
  }

  public void setAvgLatencyMs(double avgLatencyMs) {
    this.avgLatencyMs = avgLatencyMs;
  }

  public double getMaxLatencyMs() {
    return maxLatencyMs;
  }

  public void setMaxLatencyMs(double maxLatencyMs) {
    this.maxLatencyMs = maxLatencyMs;
  }

  public double getThroughputMbps() {
    return throughputMbps;
  }

  public void setThroughputMbps(double throughputMbps) {
    this.throughputMbps = throughputMbps;
  }

  public int getActiveFilters() {
    return activeFilters;
  }

  public void setActiveFilters(int activeFilters) {
    this.activeFilters = activeFilters;
  }

  /**
   * Calculate error rate
   *
   * @return Error rate as percentage (0-100)
   */
  public double getErrorRate() {
    long total = totalProcessed + totalErrors;
    return total > 0 ? (double) totalErrors / total * 100 : 0;
  }
}
