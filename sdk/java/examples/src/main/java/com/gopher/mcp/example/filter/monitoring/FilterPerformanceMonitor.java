package com.gopher.mcp.example.filter.monitoring;

import com.gopher.mcp.example.filter.utils.FilterConfiguration;
import com.gopher.mcp.filter.type.chain.ChainStats;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;
import java.util.Queue;
import java.util.LinkedList;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.LongAdder;

/**
 * Monitors filter performance and provides real-time metrics collection,
 * performance degradation detection, auto-scaling, and alert generation.
 *
 * <p>Features:
 * <ul>
 *   <li>Real-time metrics collection for latency, throughput, and errors</li>
 *   <li>Sliding window statistics for accurate performance tracking</li>
 *   <li>Automatic performance degradation detection</li>
 *   <li>Alert generation for performance issues</li>
 *   <li>Auto-scaling recommendations based on throughput</li>
 *   <li>Comprehensive logging and reporting</li>
 * </ul>
 *
 * @author Gopher MCP SDK
 * @since 1.0.0
 */
public class FilterPerformanceMonitor implements AutoCloseable {
    private static final Logger LOGGER = LoggerFactory.getLogger(FilterPerformanceMonitor.class);

    // Configuration
    private final FilterConfiguration config;
    private final long monitoringInterval;
    private final boolean alertingEnabled;

    // Metrics collectors
    private final Map<String, FilterMetrics> filterMetrics = new ConcurrentHashMap<>();
    private final Map<Long, MessageTracker> messageTrackers = new ConcurrentHashMap<>();

    // Global metrics
    private final LongAdder totalMessages = new LongAdder();
    private final LongAdder totalErrors = new LongAdder();
    private final LongAdder totalBytes = new LongAdder();

    // Latency tracking (in nanoseconds)
    private final LatencyTracker overallLatency = new LatencyTracker();
    private final Map<String, LatencyTracker> filterLatencies = new ConcurrentHashMap<>();

    // Throughput tracking
    private final ThroughputTracker throughput = new ThroughputTracker();

    // Performance thresholds
    private volatile long latencyThreshold = 10_000_000; // 10ms in nanoseconds
    private volatile long throughputThreshold = 1000; // messages per second
    private volatile double errorRateThreshold = 0.01; // 1% error rate

    // Monitoring state
    private final ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(2);
    private volatile boolean running = false;
    private ScheduledFuture<?> monitoringTask;
    private ScheduledFuture<?> cleanupTask;

    // Alert handling
    private final Queue<PerformanceAlert> alertQueue = new ConcurrentLinkedQueue<>();
    private final BlockingQueue<PerformanceAlert> pendingAlerts = new LinkedBlockingQueue<>();

    /**
     * Creates a new performance monitor with the specified configuration.
     *
     * @param config The filter configuration
     */
    public FilterPerformanceMonitor(FilterConfiguration config) {
        this.config = config;
        this.monitoringInterval = config.getMonitoringInterval();
        this.alertingEnabled = config.isAlertingEnabled();
    }

    /**
     * Starts performance monitoring.
     */
    public void start() {
        if (running) {
            return;
        }

        running = true;

        // Schedule periodic monitoring
        monitoringTask = scheduler.scheduleAtFixedRate(
            this::collectMetrics,
            monitoringInterval,
            monitoringInterval,
            TimeUnit.MILLISECONDS
        );

        // Schedule cleanup of old message trackers
        cleanupTask = scheduler.scheduleAtFixedRate(
            this::cleanupOldTrackers,
            60000, // 1 minute
            60000,
            TimeUnit.MILLISECONDS
        );

        LOGGER.info("Performance monitoring started with interval {}ms", monitoringInterval);
    }

    /**
     * Stops performance monitoring.
     */
    public void stop() {
        running = false;

        if (monitoringTask != null) {
            monitoringTask.cancel(false);
        }
        if (cleanupTask != null) {
            cleanupTask.cancel(false);
        }

        LOGGER.info("Performance monitoring stopped");
    }

    /**
     * Records that a message was sent.
     *
     * @param messageId The message ID
     */
    public void recordMessageSent(long messageId) {
        MessageTracker tracker = new MessageTracker(messageId, System.nanoTime());
        messageTrackers.put(messageId, tracker);
        totalMessages.increment();
    }

    /**
     * Records that a message completed processing.
     *
     * @param messageId The message ID
     */
    public void recordMessageComplete(long messageId) {
        MessageTracker tracker = messageTrackers.remove(messageId);
        if (tracker != null) {
            long duration = System.nanoTime() - tracker.startTime;
            overallLatency.record(duration);
            throughput.recordMessage();

            // Check for performance degradation
            if (duration > latencyThreshold) {
                generateAlert(AlertType.HIGH_LATENCY,
                    String.format("Message %d took %dms (threshold: %dms)",
                        messageId, duration / 1_000_000, latencyThreshold / 1_000_000));
            }
        }
    }

    /**
     * Records an error for a message.
     *
     * @param messageId The message ID
     * @param error The error that occurred
     */
    public void recordError(long messageId, Throwable error) {
        messageTrackers.remove(messageId);
        totalErrors.increment();

        // Check error rate
        double errorRate = getErrorRate();
        if (errorRate > errorRateThreshold) {
            generateAlert(AlertType.HIGH_ERROR_RATE,
                String.format("Error rate %.2f%% exceeds threshold %.2f%%",
                    errorRate * 100, errorRateThreshold * 100));
        }
    }

    /**
     * Records filter processing latency.
     *
     * @param filterName The filter name
     * @param latencyNanos The latency in nanoseconds
     */
    public void recordFilterLatency(String filterName, long latencyNanos) {
        filterLatencies.computeIfAbsent(filterName, k -> new LatencyTracker())
            .record(latencyNanos);

        filterMetrics.computeIfAbsent(filterName, k -> new FilterMetrics())
            .recordLatency(latencyNanos);
    }

    /**
     * Records bytes processed.
     *
     * @param bytes The number of bytes processed
     */
    public void recordBytesProcessed(long bytes) {
        totalBytes.add(bytes);
        throughput.recordBytes(bytes);
    }

    /**
     * Gets the current error rate.
     *
     * @return The error rate as a percentage (0.0 to 1.0)
     */
    public double getErrorRate() {
        long total = totalMessages.sum();
        long errors = totalErrors.sum();
        return total > 0 ? (double) errors / total : 0.0;
    }

    /**
     * Gets the average latency in milliseconds.
     *
     * @return The average latency
     */
    public double getAverageLatency() {
        return overallLatency.getAverage() / 1_000_000.0; // Convert to ms
    }

    /**
     * Gets the current throughput in messages per second.
     *
     * @return The throughput
     */
    public double getThroughput() {
        return throughput.getMessagesPerSecond();
    }

    /**
     * Gets chain statistics.
     *
     * @return The chain statistics
     */
    public ChainStats getChainStatistics() {
        ChainStats stats = new ChainStats();
        stats.setTotalProcessed(totalMessages.sum());
        stats.setTotalErrors(totalErrors.sum());
        stats.setAvgLatencyMs(getAverageLatency());
        stats.setThroughputMbps(getThroughput());
        // Error rate is calculated automatically in getErrorRate() method
        return stats;
    }

    /**
     * Collects periodic metrics.
     */
    private void collectMetrics() {
        try {
            // Calculate current metrics
            double currentLatency = getAverageLatency();
            double currentThroughput = getThroughput();
            double currentErrorRate = getErrorRate();

            // Log metrics
            LOGGER.debug("Performance metrics - Latency: {:.2f}ms, Throughput: {:.1f}msg/s, Errors: {:.2f}%",
                currentLatency, currentThroughput, currentErrorRate * 100);

            // Check for performance degradation
            checkPerformanceDegradation(currentLatency, currentThroughput, currentErrorRate);

            // Auto-scaling recommendations
            if (config.isAutoOptimizationEnabled()) {
                generateScalingRecommendations(currentThroughput);
            }

            // Process pending alerts
            if (alertingEnabled) {
                processAlerts();
            }

        } catch (Exception e) {
            LOGGER.error("Error collecting metrics", e);
        }
    }

    /**
     * Checks for performance degradation.
     */
    private void checkPerformanceDegradation(double latency, double throughput, double errorRate) {
        // Check latency degradation
        if (latency > latencyThreshold / 1_000_000.0) {
            generateAlert(AlertType.LATENCY_DEGRADATION,
                String.format("Average latency %.2fms exceeds threshold %.2fms",
                    latency, latencyThreshold / 1_000_000.0));
        }

        // Check throughput degradation
        if (throughput < throughputThreshold && totalMessages.sum() > 100) {
            generateAlert(AlertType.THROUGHPUT_DEGRADATION,
                String.format("Throughput %.1f msg/s below threshold %.1f msg/s",
                    throughput, (double) throughputThreshold));
        }

        // Check error rate
        if (errorRate > errorRateThreshold) {
            generateAlert(AlertType.HIGH_ERROR_RATE,
                String.format("Error rate %.2f%% exceeds threshold %.2f%%",
                    errorRate * 100, errorRateThreshold * 100));
        }
    }

    /**
     * Generates scaling recommendations based on throughput.
     */
    private void generateScalingRecommendations(double currentThroughput) {
        // Calculate optimal scaling based on throughput
        if (currentThroughput > throughputThreshold * 1.5) {
            LOGGER.info("Recommendation: Scale up - current throughput {:.1f} msg/s is high",
                currentThroughput);
        } else if (currentThroughput < throughputThreshold * 0.3 && totalMessages.sum() > 1000) {
            LOGGER.info("Recommendation: Scale down - current throughput {:.1f} msg/s is low",
                currentThroughput);
        }
    }

    /**
     * Generates a performance alert.
     */
    private void generateAlert(AlertType type, String message) {
        if (!alertingEnabled) {
            return;
        }

        PerformanceAlert alert = new PerformanceAlert(type, message, System.currentTimeMillis());
        alertQueue.offer(alert);
        pendingAlerts.offer(alert);

        // Log alert
        switch (type.getSeverity()) {
            case ERROR:
                LOGGER.error("ALERT [{}]: {}", type, message);
                break;
            case WARNING:
                LOGGER.warn("ALERT [{}]: {}", type, message);
                break;
            case INFO:
                LOGGER.info("ALERT [{}]: {}", type, message);
                break;
        }
    }

    /**
     * Processes pending alerts.
     */
    private void processAlerts() {
        PerformanceAlert alert;
        while ((alert = pendingAlerts.poll()) != null) {
            // Here you would send alerts to external systems
            // For now, just log them
            LOGGER.info("Processing alert: {}", alert);
        }
    }

    /**
     * Cleans up old message trackers to prevent memory leaks.
     */
    private void cleanupOldTrackers() {
        long cutoff = System.nanoTime() - TimeUnit.MINUTES.toNanos(5);
        messageTrackers.values().removeIf(tracker -> tracker.startTime < cutoff);
    }

    /**
     * Sets performance thresholds.
     */
    public void setThresholds(long latencyMs, long throughputMps, double errorRate) {
        this.latencyThreshold = latencyMs * 1_000_000; // Convert to nanos
        this.throughputThreshold = throughputMps;
        this.errorRateThreshold = errorRate;

        LOGGER.info("Updated thresholds - Latency: {}ms, Throughput: {} msg/s, Error rate: {:.2f}%",
            latencyMs, throughputMps, errorRate * 100);
    }

    /**
     * Gets recent alerts.
     *
     * @param limit Maximum number of alerts to return
     * @return List of recent alerts
     */
    public Queue<PerformanceAlert> getRecentAlerts(int limit) {
        Queue<PerformanceAlert> recent = new LinkedList<>();
        int count = 0;
        for (PerformanceAlert alert : alertQueue) {
            if (count++ >= limit) break;
            recent.offer(alert);
        }
        return recent;
    }

    @Override
    public void close() {
        stop();
        scheduler.shutdown();
        try {
            if (!scheduler.awaitTermination(5, TimeUnit.SECONDS)) {
                scheduler.shutdownNow();
            }
        } catch (InterruptedException e) {
            scheduler.shutdownNow();
            Thread.currentThread().interrupt();
        }
    }

    /**
     * Tracks message processing.
     */
    private static class MessageTracker {
        final long messageId;
        final long startTime;

        MessageTracker(long messageId, long startTime) {
            this.messageId = messageId;
            this.startTime = startTime;
        }
    }

    /**
     * Tracks latency with sliding window.
     */
    private static class LatencyTracker {
        private final Queue<Long> window = new ConcurrentLinkedQueue<>();
        private final LongAdder sum = new LongAdder();
        private final AtomicLong count = new AtomicLong();
        private static final int WINDOW_SIZE = 1000;

        void record(long latency) {
            window.offer(latency);
            sum.add(latency);
            count.incrementAndGet();

            // Maintain window size
            while (window.size() > WINDOW_SIZE) {
                Long old = window.poll();
                if (old != null) {
                    sum.add(-old);
                    count.decrementAndGet();
                }
            }
        }

        double getAverage() {
            long c = count.get();
            return c > 0 ? (double) sum.sum() / c : 0.0;
        }
    }

    /**
     * Tracks throughput with time windows.
     */
    private static class ThroughputTracker {
        private final Queue<TimedCount> messageWindows = new ConcurrentLinkedQueue<>();
        private final Queue<TimedCount> byteWindows = new ConcurrentLinkedQueue<>();
        private static final long WINDOW_DURATION = 10000; // 10 seconds

        void recordMessage() {
            long now = System.currentTimeMillis();
            cleanOldWindows(now);
            messageWindows.offer(new TimedCount(now, 1));
        }

        void recordBytes(long bytes) {
            long now = System.currentTimeMillis();
            cleanOldWindows(now);
            byteWindows.offer(new TimedCount(now, bytes));
        }

        double getMessagesPerSecond() {
            long now = System.currentTimeMillis();
            cleanOldWindows(now);

            long totalMessages = messageWindows.stream()
                .mapToLong(w -> w.count)
                .sum();

            return totalMessages / (WINDOW_DURATION / 1000.0);
        }

        double getBytesPerSecond() {
            long now = System.currentTimeMillis();
            cleanOldWindows(now);

            long totalBytes = byteWindows.stream()
                .mapToLong(w -> w.count)
                .sum();

            return totalBytes / (WINDOW_DURATION / 1000.0);
        }

        private void cleanOldWindows(long now) {
            long cutoff = now - WINDOW_DURATION;
            messageWindows.removeIf(w -> w.timestamp < cutoff);
            byteWindows.removeIf(w -> w.timestamp < cutoff);
        }

        private static class TimedCount {
            final long timestamp;
            final long count;

            TimedCount(long timestamp, long count) {
                this.timestamp = timestamp;
                this.count = count;
            }
        }
    }

    /**
     * Filter-specific metrics.
     */
    private static class FilterMetrics {
        private final LongAdder invocations = new LongAdder();
        private final LongAdder errors = new LongAdder();
        private final LatencyTracker latency = new LatencyTracker();

        void recordLatency(long nanos) {
            invocations.increment();
            latency.record(nanos);
        }

        void recordError() {
            errors.increment();
        }

        long getInvocations() { return invocations.sum(); }
        long getErrors() { return errors.sum(); }
        double getAverageLatency() { return latency.getAverage(); }
    }

    /**
     * Performance alert types.
     */
    public enum AlertType {
        HIGH_LATENCY(Severity.WARNING),
        LATENCY_DEGRADATION(Severity.WARNING),
        THROUGHPUT_DEGRADATION(Severity.WARNING),
        HIGH_ERROR_RATE(Severity.ERROR),
        MEMORY_PRESSURE(Severity.WARNING),
        FILTER_FAILURE(Severity.ERROR);

        private final Severity severity;

        AlertType(Severity severity) {
            this.severity = severity;
        }

        public Severity getSeverity() {
            return severity;
        }
    }

    /**
     * Alert severity levels.
     */
    public enum Severity {
        INFO, WARNING, ERROR
    }

    /**
     * Performance alert.
     */
    public static class PerformanceAlert {
        private final AlertType type;
        private final String message;
        private final long timestamp;

        PerformanceAlert(AlertType type, String message, long timestamp) {
            this.type = type;
            this.message = message;
            this.timestamp = timestamp;
        }

        public AlertType getType() { return type; }
        public String getMessage() { return message; }
        public long getTimestamp() { return timestamp; }

        @Override
        public String toString() {
            return String.format("[%s] %s at %d", type, message, timestamp);
        }
    }
}
