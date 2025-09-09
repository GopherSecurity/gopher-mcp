package com.gopher.mcp.example.filter;

import com.gopher.mcp.filter.McpFilter;
import com.gopher.mcp.example.filter.chain.FilterChainBuilder;
import com.gopher.mcp.example.filter.transport.FilteredClientTransport;
import com.gopher.mcp.example.filter.utils.FilterConfiguration;
import com.gopher.mcp.filter.type.FilterType;
import com.gopher.mcp.transport.CustomClientTransport;
import io.modelcontextprotocol.spec.McpSchema.JSONRPCMessage;
import io.modelcontextprotocol.spec.McpSchema.JSONRPCRequest;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Comprehensive example demonstrating the MCP Filter.
 *
 * This example shows:
 * - Creating a filtered MCP client with various filter configurations
 * - Building different types of filter chains (sequential, parallel, conditional)
 * - Dynamic filter management
 * - Performance monitoring and statistics
 * - Proper resource cleanup
 *
 * @author Gopher MCP SDK
 * @since 1.0.0
 */
public class FilteredMcpExample {
    private static final Logger LOGGER = LoggerFactory.getLogger(FilteredMcpExample.class);

    public static void main(String[] args) {
        LOGGER.info("=== MCP Filter Example ===\n");

        // Example 1: Basic filtered client with configuration
        basicFilteredClient();

        // Example 2: Advanced chain building
        advancedChainBuilding();

        // Example 3: Dynamic filter management
        dynamicFilterManagement();

        // Example 4: Performance monitoring
        performanceMonitoring();

        LOGGER.info("\n=== Example completed successfully ===");
    }

    /**
     * Example 1: Basic filtered client with standard configuration
     */
    private static void basicFilteredClient() {
        LOGGER.info("\n--- Example 1: Basic Filtered Client ---");

        // Create queues for communication
        BlockingQueue<JSONRPCMessage> inboundQueue = new LinkedBlockingQueue<>();
        BlockingQueue<JSONRPCMessage> outboundQueue = new LinkedBlockingQueue<>();

        // Create base transport
        CustomClientTransport baseTransport = new CustomClientTransport(inboundQueue, outboundQueue);

        // Configure filters
        FilterConfiguration config = FilterConfiguration.builder()
            .withCompression(FilterConfiguration.CompressionLevel.HIGH)
            .withEncryption(FilterConfiguration.EncryptionAlgorithm.AES256)
            .withRateLimit(100, TimeUnit.SECONDS)
            .withMetrics(true)
            .withMonitoring(true, 5000) // Monitor every 5 seconds
            .withBufferPool(50, 65536) // 50 buffers of 64KB
            .withZeroCopy(true)
            .withAutoOptimization(true)
            .build();

        // Create filtered transport
        try (FilteredClientTransport transport = new FilteredClientTransport(baseTransport, config)) {

            // Note: In a real application, you would create an MCP client here
            // using the filtered transport. For this example, we're demonstrating
            // the filter integration without requiring the full MCP client setup.
            LOGGER.info("Filtered transport created and ready for MCP client");

            // Simulate some operations
            Thread.sleep(2000);

            // Get statistics
            var stats = transport.getChainStatistics();
            LOGGER.info("Chain statistics: {}", stats);

            var filterStats = transport.getFilterStatistics();
            filterStats.forEach((name, stat) ->
                LOGGER.info("Filter '{}' stats: processed={}, errors={}",
                    name, stat.getBytesProcessed(), stat.getErrors()));

        } catch (Exception e) {
            LOGGER.error("Example 1 failed", e);
        }
    }

    /**
     * Example 2: Advanced chain building with different execution modes
     */
    private static void advancedChainBuilding() {
        LOGGER.info("\n--- Example 2: Advanced Chain Building ---");

        McpFilter filter = new McpFilter();
        long dispatcher = 0x1234567890ABCDEFL; // Mock dispatcher

        try {
            // Sequential chain example
            LOGGER.info("Building sequential chain...");
            long compressionFilter = filter.createBuiltin(dispatcher, FilterType.HTTP_COMPRESSION.getValue(), null);
            //long encryptionFilter = filter.createBuiltin(dispatcher, FilterType.ENCRYPTION.getValue(), null);
            long loggingFilter = filter.createBuiltin(dispatcher, FilterType.ACCESS_LOG.getValue(), null);

            long sequentialChain = FilterChainBuilder.sequential()
                .withName("request-processing")
                .addFilter(compressionFilter)
                //.addFilter(encryptionFilter)
                .addFilter(loggingFilter)
                .stopOnError(true)
                .withOptimization(true)
                .build(dispatcher);

            LOGGER.info("Sequential chain created: {}", sequentialChain);

            // Parallel chain example
            LOGGER.info("\nBuilding parallel chain...");
            long metricsFilter = filter.createBuiltin(dispatcher, FilterType.METRICS.getValue(), null);
            long tracingFilter = filter.createBuiltin(dispatcher, FilterType.TRACING.getValue(), null);
            //long auditFilter = filter.createBuiltin(dispatcher, FilterType.AUDIT_LOG.getValue(), null);

            long parallelChain = FilterChainBuilder.parallel()
                .withName("observability")
                .addParallelGroup(metricsFilter, tracingFilter/*, auditFilter*/)
                .withMaxConcurrency(3)
                .build(dispatcher);

            LOGGER.info("Parallel chain created: {}", parallelChain);

            // Conditional chain example
            LOGGER.info("\nBuilding conditional chain...");
            long httpChain = filter.chainBuilderCreate(dispatcher);
            long tcpChain = filter.chainBuilderCreate(dispatcher);
            long defaultChain = filter.chainBuilderCreate(dispatcher);

            // Build the chains
            httpChain = filter.chainBuild(httpChain);
            tcpChain = filter.chainBuild(tcpChain);
            defaultChain = filter.chainBuild(defaultChain);

            long conditionalChain = FilterChainBuilder.conditional()
                .withName("protocol-router")
                .when(data -> isHttpRequest(data), httpChain)
                .when(data -> isTcpRequest(data), tcpChain)
                .otherwise(defaultChain)
                .build(dispatcher);

            LOGGER.info("Conditional chain created: {}", conditionalChain);

            // Pipeline example
            LOGGER.info("\nBuilding complex pipeline...");
            long pipeline = FilterChainBuilder.pipeline()
                .withName("data-pipeline")
                .addStage("ingestion", sequentialChain)
                .branch("validation", data -> needsValidation(data), parallelChain)
                .addStage("processing", conditionalChain)
                .merge("aggregation", "validation", "processing")
                .withBackpressure(true, 100)
                .build(dispatcher);

            LOGGER.info("Pipeline created: {}", pipeline);

            // Cleanup
            filter.chainRelease(sequentialChain);
            filter.chainRelease(parallelChain);
            filter.chainRelease(conditionalChain);
            filter.chainRelease(pipeline);

        } catch (Exception e) {
            LOGGER.error("Example 2 failed", e);
        } finally {
            filter.close();
        }
    }

    /**
     * Example 3: Dynamic filter management
     */
    private static void dynamicFilterManagement() {
        LOGGER.info("\n--- Example 3: Dynamic Filter Management ---");

        BlockingQueue<JSONRPCMessage> inboundQueue = new LinkedBlockingQueue<>();
        BlockingQueue<JSONRPCMessage> outboundQueue = new LinkedBlockingQueue<>();
        CustomClientTransport baseTransport = new CustomClientTransport(inboundQueue, outboundQueue);

        // Start with minimal configuration
        FilterConfiguration config = FilterConfiguration.builder()
            .withMetrics(true)
            .withMonitoring(true, 1000)
            .build();

        try (FilteredClientTransport transport = new FilteredClientTransport(baseTransport, config)) {

            LOGGER.info("Initial configuration: metrics only");

            // Dynamically add compression
            LOGGER.info("Adding compression filter...");
            transport.addFilter("dynamic_compression", FilterType.HTTP_COMPRESSION,
                Map.of("level", 6));

            Thread.sleep(1000);

            // Dynamically add rate limiting
            LOGGER.info("Adding rate limit filter...");
            transport.addFilter("dynamic_rate_limit", FilterType.RATE_LIMIT,
                Map.of("requests", 50, "window", "1s"));

            Thread.sleep(1000);

            // Update filter configuration
            LOGGER.info("Updating rate limit configuration...");
            transport.updateFilterConfig("dynamic_rate_limit",
                Map.of("requests", 100, "window", "1s"));

            // Pause a filter
            LOGGER.info("Pausing compression filter...");
            transport.pauseFilter("dynamic_compression");

            Thread.sleep(1000);

            // Resume the filter
            LOGGER.info("Resuming compression filter...");
            transport.resumeFilter("dynamic_compression");

            // Optimize chains
            LOGGER.info("Optimizing filter chains...");
            transport.optimizeChains();

            // Validate chains
            LOGGER.info("Validating filter chains...");
            transport.validateChains();

            // Remove a filter
            LOGGER.info("Removing rate limit filter...");
            transport.removeFilter("dynamic_rate_limit");

            // Get final statistics
            var stats = transport.getFilterStatistics();
            LOGGER.info("Final filter count: {}", stats.size());

        } catch (Exception e) {
            LOGGER.error("Example 3 failed", e);
        }
    }

    /**
     * Example 4: Performance monitoring and alerting
     */
    private static void performanceMonitoring() {
        LOGGER.info("\n--- Example 4: Performance Monitoring ---");

        BlockingQueue<JSONRPCMessage> inboundQueue = new LinkedBlockingQueue<>();
        BlockingQueue<JSONRPCMessage> outboundQueue = new LinkedBlockingQueue<>();
        CustomClientTransport baseTransport = new CustomClientTransport(inboundQueue, outboundQueue);

        // Configure with alerting enabled
        FilterConfiguration config = FilterConfiguration.builder()
            .withMetrics(true)
            .withMonitoring(true, 1000) // Monitor every second
            .withAlerting(true)
            .build();

        try (FilteredClientTransport transport = new FilteredClientTransport(baseTransport, config)) {

            // Simulate message processing
            LOGGER.info("Simulating message processing...");

            for (int i = 0; i < 10; i++) {
                // Create a test message
                JSONRPCRequest request = new JSONRPCRequest(
                    "2.0",                     // jsonrpc version
                    String.valueOf(i),         // id
                    "test/method",             // method
                    Map.of("index", i)         // params
                );

                // Send message (will be filtered)
                final int messageId = i;
                transport.sendMessage(request)
                    .subscribe(
                        v -> LOGGER.debug("Message {} sent", messageId),
                        e -> LOGGER.error("Failed to send message {}", messageId, e)
                    );

                Thread.sleep(100);
            }

            // Wait for processing
            Thread.sleep(2000);

            // Get performance statistics
            var chainStats = transport.getChainStatistics();
            LOGGER.info("\nPerformance Summary:");
            LOGGER.info("  Total messages: {}", chainStats.getTotalProcessed());
            LOGGER.info("  Total errors: {}", chainStats.getTotalErrors());
            LOGGER.info("  Average latency: {}ms", String.format("%.2f", chainStats.getAvgLatencyMs()));
            LOGGER.info("  Throughput: {} Mbps", String.format("%.1f", chainStats.getThroughputMbps()));
            LOGGER.info("  Error rate: {}%", String.format("%.2f", chainStats.getErrorRate()));

            // Get filter-specific statistics
            LOGGER.info("\nFilter Statistics:");
            var filterStats = transport.getFilterStatistics();
            filterStats.forEach((name, stats) -> {
                LOGGER.info("  {}: {} bytes processed, {} errors",
                    name, stats.getBytesProcessed(), stats.getErrors());
            });

        } catch (Exception e) {
            LOGGER.error("Example 4 failed", e);
        }
    }

    // Helper methods for conditional routing
    private static boolean isHttpRequest(byte[] data) {
        if (data == null || data.length < 4) return false;
        String start = new String(data, 0, Math.min(data.length, 4));
        return start.startsWith("GET ") || start.startsWith("POST") ||
               start.startsWith("PUT ") || start.startsWith("HTTP");
    }

    private static boolean isTcpRequest(byte[] data) {
        // Simple heuristic - check if it's not HTTP
        return !isHttpRequest(data);
    }

    private static boolean needsValidation(byte[] data) {
        // Example validation logic
        return data != null && data.length > 1024;
    }
}
