package com.gopher.mcp.example.filter.utils;

import java.util.concurrent.TimeUnit;

/**
 * Configuration for the filtered transport system. Provides a fluent builder API for configuring
 * various filter options.
 *
 * @author Gopher MCP SDK
 * @since 1.0.0
 */
public class FilterConfiguration {

  // Compression settings
  private boolean compressionEnabled = false;
  private CompressionLevel compressionLevel = CompressionLevel.MEDIUM;

  // Encryption settings
  private boolean encryptionEnabled = false;
  private EncryptionAlgorithm encryptionAlgorithm = EncryptionAlgorithm.AES256;

  // Rate limiting settings
  private boolean rateLimitEnabled = false;
  private int rateLimitRequests = 100;
  private TimeUnit rateLimitTimeUnit = TimeUnit.SECONDS;

  // Metrics settings
  private boolean metricsEnabled = false;
  private long metricsInterval = 60000; // 1 minute

  // Buffer settings
  private int bufferPoolSize = 100;
  private int bufferSize = 65536; // 64KB
  private boolean zeroCopyEnabled = true;

  // Performance settings
  private int maxParallelFilters = 4;
  private long filterTimeout = 5000; // 5 seconds
  private boolean autoOptimizationEnabled = true;

  // Monitoring settings
  private boolean monitoringEnabled = true;
  private long monitoringInterval = 10000; // 10 seconds
  private boolean alertingEnabled = false;

  private FilterConfiguration() {}

  public static Builder builder() {
    return new Builder();
  }

  // Getters
  public boolean isCompressionEnabled() {
    return compressionEnabled;
  }

  public CompressionLevel getCompressionLevel() {
    return compressionLevel;
  }

  public boolean isEncryptionEnabled() {
    return encryptionEnabled;
  }

  public EncryptionAlgorithm getEncryptionAlgorithm() {
    return encryptionAlgorithm;
  }

  public boolean isRateLimitEnabled() {
    return rateLimitEnabled;
  }

  public int getRateLimitRequests() {
    return rateLimitRequests;
  }

  public TimeUnit getRateLimitTimeUnit() {
    return rateLimitTimeUnit;
  }

  public boolean isMetricsEnabled() {
    return metricsEnabled;
  }

  public long getMetricsInterval() {
    return metricsInterval;
  }

  public int getBufferPoolSize() {
    return bufferPoolSize;
  }

  public int getBufferSize() {
    return bufferSize;
  }

  public boolean isZeroCopyEnabled() {
    return zeroCopyEnabled;
  }

  public int getMaxParallelFilters() {
    return maxParallelFilters;
  }

  public long getFilterTimeout() {
    return filterTimeout;
  }

  public boolean isAutoOptimizationEnabled() {
    return autoOptimizationEnabled;
  }

  public boolean isMonitoringEnabled() {
    return monitoringEnabled;
  }

  public long getMonitoringInterval() {
    return monitoringInterval;
  }

  public boolean isAlertingEnabled() {
    return alertingEnabled;
  }

  @Override
  public String toString() {
    return "FilterConfiguration{"
        + "compression="
        + compressionEnabled
        + ", encryption="
        + encryptionEnabled
        + ", rateLimit="
        + rateLimitEnabled
        + ", metrics="
        + metricsEnabled
        + ", bufferPoolSize="
        + bufferPoolSize
        + ", zeroCopy="
        + zeroCopyEnabled
        + ", monitoring="
        + monitoringEnabled
        + '}';
  }

  /** Compression level options */
  public enum CompressionLevel {
    LOW(1),
    MEDIUM(5),
    HIGH(9);

    private final int level;

    CompressionLevel(int level) {
      this.level = level;
    }

    public int getLevel() {
      return level;
    }
  }

  /** Encryption algorithm options */
  public enum EncryptionAlgorithm {
    AES128("AES/CBC/PKCS5Padding", 128),
    AES256("AES/CBC/PKCS5Padding", 256),
    CHACHA20("ChaCha20-Poly1305", 256);

    private final String algorithm;
    private final int keySize;

    EncryptionAlgorithm(String algorithm, int keySize) {
      this.algorithm = algorithm;
      this.keySize = keySize;
    }

    public String getAlgorithm() {
      return algorithm;
    }

    public int getKeySize() {
      return keySize;
    }
  }

  /** Builder for FilterConfiguration */
  public static class Builder {
    private final FilterConfiguration config = new FilterConfiguration();

    /** Enables compression with the specified level. */
    public Builder withCompression(CompressionLevel level) {
      config.compressionEnabled = true;
      config.compressionLevel = level;
      return this;
    }

    /** Enables encryption with the specified algorithm. */
    public Builder withEncryption(EncryptionAlgorithm algorithm) {
      config.encryptionEnabled = true;
      config.encryptionAlgorithm = algorithm;
      return this;
    }

    /** Enables rate limiting with the specified limits. */
    public Builder withRateLimit(int requests, TimeUnit timeUnit) {
      config.rateLimitEnabled = true;
      config.rateLimitRequests = requests;
      config.rateLimitTimeUnit = timeUnit;
      return this;
    }

    /** Enables metrics collection. */
    public Builder withMetrics(boolean enabled) {
      config.metricsEnabled = enabled;
      return this;
    }

    /** Sets the metrics collection interval. */
    public Builder withMetricsInterval(long intervalMs) {
      config.metricsInterval = intervalMs;
      return this;
    }

    /** Configures buffer pool settings. */
    public Builder withBufferPool(int poolSize, int bufferSize) {
      config.bufferPoolSize = poolSize;
      config.bufferSize = bufferSize;
      return this;
    }

    /** Enables or disables zero-copy operations. */
    public Builder withZeroCopy(boolean enabled) {
      config.zeroCopyEnabled = enabled;
      return this;
    }

    /** Sets the maximum number of filters that can run in parallel. */
    public Builder withMaxParallelFilters(int max) {
      config.maxParallelFilters = max;
      return this;
    }

    /** Sets the filter execution timeout. */
    public Builder withFilterTimeout(long timeoutMs) {
      config.filterTimeout = timeoutMs;
      return this;
    }

    /** Enables automatic chain optimization. */
    public Builder withAutoOptimization(boolean enabled) {
      config.autoOptimizationEnabled = enabled;
      return this;
    }

    /** Configures monitoring settings. */
    public Builder withMonitoring(boolean enabled, long intervalMs) {
      config.monitoringEnabled = enabled;
      config.monitoringInterval = intervalMs;
      return this;
    }

    /** Enables alerting for performance issues. */
    public Builder withAlerting(boolean enabled) {
      config.alertingEnabled = enabled;
      return this;
    }

    /** Builds the configuration. */
    public FilterConfiguration build() {
      // Validate configuration
      if (config.bufferPoolSize <= 0) {
        throw new IllegalArgumentException("Buffer pool size must be positive");
      }
      if (config.bufferSize <= 0) {
        throw new IllegalArgumentException("Buffer size must be positive");
      }
      if (config.maxParallelFilters <= 0) {
        throw new IllegalArgumentException("Max parallel filters must be positive");
      }
      if (config.filterTimeout <= 0) {
        throw new IllegalArgumentException("Filter timeout must be positive");
      }

      return config;
    }
  }
}
