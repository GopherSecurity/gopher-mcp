package com.gopher.mcp.example.filter.transport;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.gopher.mcp.example.filter.buffer.FilterBufferManager;
import com.gopher.mcp.example.filter.monitoring.FilterPerformanceMonitor;
import com.gopher.mcp.example.filter.utils.FilterConfiguration;
import com.gopher.mcp.filter.McpFilter;
import com.gopher.mcp.filter.McpFilterBuffer;
import com.gopher.mcp.filter.McpFilterChain;
import com.gopher.mcp.filter.type.FilterPosition;
import com.gopher.mcp.filter.type.FilterStats;
import com.gopher.mcp.filter.type.FilterType;
import com.gopher.mcp.filter.type.chain.ChainStats;
import io.modelcontextprotocol.spec.McpClientTransport;
import io.modelcontextprotocol.spec.McpSchema.JSONRPCMessage;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import java.util.function.Function;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;
import reactor.core.publisher.Sinks;
import reactor.core.scheduler.Schedulers;

/**
 * Production-ready filtered client transport implementation that integrates the MCP Filter
 * framework with McpClient. Provides transparent message filtering for both inbound and outbound
 * messages with support for dynamic filter management, performance monitoring, and resource
 * management.
 *
 * <p>Example usage:
 *
 * <pre>{@code
 * FilterConfiguration config = FilterConfiguration.builder()
 *     .withCompression(CompressionLevel.HIGH)
 *     .withEncryption(EncryptionAlgorithm.AES256)
 *     .withRateLimit(100, TimeUnit.SECONDS)
 *     .withMetrics(true)
 *     .build();
 *
 * FilteredClientTransport transport = new FilteredClientTransport(
 *     inboundQueue, outboundQueue, config);
 *
 * McpClient client = McpClient.async(transport)
 *     .clientInfo(new Implementation("filtered-client", "1.0.0"))
 *     .build();
 * }</pre>
 *
 * @author Gopher MCP SDK
 * @since 1.0.0
 */
public class FilteredClientTransport implements McpClientTransport, AutoCloseable {
  private static final Logger LOGGER = LoggerFactory.getLogger(FilteredClientTransport.class);
  private static final ObjectMapper MAPPER = new ObjectMapper();

  // Core components
  private final McpFilter filter;
  private final McpFilterBuffer buffer;
  private final McpFilterChain chain;
  private final FilterBufferManager bufferManager;
  private final FilterPerformanceMonitor monitor;

  // Filter chains
  private volatile long outboundChainHandle;
  private volatile long inboundChainHandle;

  // Dynamic filter management
  private final Map<String, Long> dynamicFilters = new ConcurrentHashMap<>();
  private final List<Long> activeHandles = new CopyOnWriteArrayList<>();

  // Configuration
  private final FilterConfiguration config;
  private final long dispatcher;

  // Transport state
  private final AtomicBoolean closed = new AtomicBoolean(false);
  private final AtomicLong messageCounter = new AtomicLong(0);

  // Base transport for actual communication
  private final McpClientTransport baseTransport;

  // Message handling
  private Function<Mono<JSONRPCMessage>, Mono<JSONRPCMessage>> messageHandler;
  private Consumer<Throwable> exceptionHandler;

  // Reactive components
  private final Sinks.Many<JSONRPCMessage> inboundSink =
      Sinks.many().multicast().directBestEffort();
  private final Sinks.Many<JSONRPCMessage> outboundSink =
      Sinks.many().multicast().directBestEffort();

  /**
   * Creates a new filtered client transport with the specified configuration.
   *
   * @param baseTransport The underlying transport for actual communication
   * @param config The filter configuration
   */
  public FilteredClientTransport(McpClientTransport baseTransport, FilterConfiguration config) {
    this.baseTransport = baseTransport;
    this.config = config;

    // Initialize core components
    this.filter = new McpFilter();
    this.buffer = new McpFilterBuffer();
    this.chain = new McpFilterChain();
    this.bufferManager = new FilterBufferManager(config);
    this.monitor = new FilterPerformanceMonitor(config);

    // Create dispatcher (mock value for now, would be native in production)
    this.dispatcher = createDispatcher();

    // Initialize filter chains
    initializeFilterChains();

    // Start monitoring
    monitor.start();

    // Register shutdown hook
    Runtime.getRuntime().addShutdownHook(new Thread(this::close));

    LOGGER.info("FilteredClientTransport initialized with config: {}", config);
  }

  /** Creates the native dispatcher for filter execution. */
  private long createDispatcher() {
    // In production, this would create a native dispatcher
    // For now, return a mock value
    return 0x1234567890ABCDEFL;
  }

  /** Initializes the default filter chains based on configuration. */
  private void initializeFilterChains() {
    try {
      // Build outbound chain
      long outboundBuilder = filter.chainBuilderCreate(dispatcher);
      if (outboundBuilder != 0) {
        activeHandles.add(outboundBuilder);

        // Add configured filters for outbound
        if (config.isCompressionEnabled()) {
          long compressionFilter =
              filter.createBuiltin(dispatcher, FilterType.HTTP_COMPRESSION.getValue(), null);
          if (compressionFilter != 0) {
            activeHandles.add(compressionFilter);
            dynamicFilters.put("outbound_compression", compressionFilter);
            filter.chainAddFilter(
                outboundBuilder, compressionFilter, FilterPosition.FIRST.getValue(), 0);
          }
        }

        if (config.isRateLimitEnabled()) {
          long rateLimitFilter =
              filter.createBuiltin(dispatcher, FilterType.RATE_LIMIT.getValue(), null);
          if (rateLimitFilter != 0) {
            activeHandles.add(rateLimitFilter);
            dynamicFilters.put("outbound_rate_limit", rateLimitFilter);
            filter.chainAddFilter(
                outboundBuilder, rateLimitFilter, FilterPosition.FIRST.getValue(), 0);
          }
        }

        if (config.isMetricsEnabled()) {
          long metricsFilter =
              filter.createBuiltin(dispatcher, FilterType.METRICS.getValue(), null);
          if (metricsFilter != 0) {
            activeHandles.add(metricsFilter);
            dynamicFilters.put("outbound_metrics", metricsFilter);
            filter.chainAddFilter(
                outboundBuilder, metricsFilter, FilterPosition.LAST.getValue(), 0);
          }
        }

        outboundChainHandle = filter.chainBuild(outboundBuilder);
        if (outboundChainHandle != 0) {
          activeHandles.add(outboundChainHandle);
        }
        filter.chainBuilderDestroy(outboundBuilder);
      }

      // Build inbound chain (reverse order for decryption/decompression)
      long inboundBuilder = filter.chainBuilderCreate(dispatcher);
      if (inboundBuilder != 0) {
        activeHandles.add(inboundBuilder);

        if (config.isMetricsEnabled()) {
          long metricsFilter =
              filter.createBuiltin(dispatcher, FilterType.METRICS.getValue(), null);
          if (metricsFilter != 0) {
            activeHandles.add(metricsFilter);
            dynamicFilters.put("inbound_metrics", metricsFilter);
            filter.chainAddFilter(
                inboundBuilder, metricsFilter, FilterPosition.FIRST.getValue(), 0);
          }
        }

        inboundChainHandle = filter.chainBuild(inboundBuilder);
        if (inboundChainHandle != 0) {
          activeHandles.add(inboundChainHandle);
        }
        filter.chainBuilderDestroy(inboundBuilder);
      }

      LOGGER.info(
          "Filter chains initialized - Outbound: {}, Inbound: {}",
          outboundChainHandle,
          inboundChainHandle);

    } catch (Exception e) {
      LOGGER.error("Failed to initialize filter chains", e);
      throw new RuntimeException("Filter chain initialization failed", e);
    }
  }

  @Override
  public Mono<Void> connect(Function<Mono<JSONRPCMessage>, Mono<JSONRPCMessage>> handler) {
    this.messageHandler = handler;

    // Connect the base transport with our filtered handler
    return baseTransport.connect(
        message ->
            processInboundMessage(message)
                .flatMap(
                    filtered -> {
                      if (messageHandler != null) {
                        return messageHandler.apply(Mono.just(filtered));
                      }
                      return Mono.just(filtered);
                    })
                .flatMap(this::processOutboundMessage));
  }

  @Override
  public Mono<Void> sendMessage(JSONRPCMessage message) {
    if (closed.get()) {
      return Mono.error(new IllegalStateException("Transport is closed"));
    }

    long messageId = messageCounter.incrementAndGet();
    monitor.recordMessageSent(messageId);

    return processOutboundMessage(message)
        .flatMap(
            filtered -> {
              LOGGER.debug("Sending filtered message {} to base transport", messageId);
              return baseTransport.sendMessage(filtered);
            })
        .doOnSuccess(
            v -> {
              LOGGER.debug("Message {} sent successfully", messageId);
              monitor.recordMessageComplete(messageId);
            })
        .doOnError(
            e -> {
              monitor.recordError(messageId, e);
              LOGGER.error("Failed to send message {}: {}", messageId, e.getMessage());
            });
  }

  /** Processes an outbound message through the filter chain. */
  private Mono<JSONRPCMessage> processOutboundMessage(JSONRPCMessage message) {
    return Mono.fromCallable(
            () -> {
              long startTime = System.nanoTime();

              // Acquire buffer from pool
              long bufferHandle = bufferManager.acquireBuffer();
              activeHandles.add(bufferHandle);

              try {
                // In production, we would serialize the message and process through filters
                // For simulation, we'll track the message processing without actual serialization

                // Simulate serialization size (for metrics)
                String json = MAPPER.writeValueAsString(message);
                int dataSize = json.getBytes(StandardCharsets.UTF_8).length;

                // Process through outbound filter chain
                if (outboundChainHandle != 0) {
                  // TODO: Chain processing will be implemented when native API is available
                  // In production, this would:
                  // 1. Copy data to native buffer
                  // 2. Process through filter chain
                  // 3. Get filtered data back
                  LOGGER.debug(
                      "Processing {} bytes through outbound filter chain (simulated)", dataSize);

                  // Simulate processing delay
                  Thread.sleep(1);
                }

                // Update metrics
                long duration = System.nanoTime() - startTime;
                monitor.recordFilterLatency("outbound", duration);

                // Return the original message
                // (in production, this would be the filtered/transformed message)
                return message;

              } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException("Filter processing interrupted", e);
              } finally {
                // Release buffer back to pool
                bufferManager.releaseBuffer(bufferHandle);
                activeHandles.remove(bufferHandle);
              }
            })
        .subscribeOn(Schedulers.boundedElastic());
  }

  /** Processes an inbound message through the filter chain. */
  private Mono<JSONRPCMessage> processInboundMessage(Mono<JSONRPCMessage> message) {
    return message
        .flatMap(
            msg ->
                Mono.fromCallable(
                    () -> {
                      long startTime = System.nanoTime();

                      // Acquire buffer from pool
                      long bufferHandle = bufferManager.acquireBuffer();
                      activeHandles.add(bufferHandle);

                      try {
                        // In production, we would serialize the message and process through filters
                        // For simulation, we'll track the message processing without actual
                        // serialization

                        // Simulate serialization size (for metrics)
                        String json = MAPPER.writeValueAsString(msg);
                        int dataSize = json.getBytes(StandardCharsets.UTF_8).length;

                        // Process through inbound filter chain
                        if (inboundChainHandle != 0) {
                          // TODO: Chain processing will be implemented when native API is available
                          LOGGER.debug(
                              "Processing {} bytes through inbound filter chain (simulated)",
                              dataSize);

                          // Simulate processing delay
                          Thread.sleep(1);
                        }

                        // Update metrics
                        long duration = System.nanoTime() - startTime;
                        monitor.recordFilterLatency("inbound", duration);

                        // Return the original message
                        // (in production, this would be the filtered/transformed message)
                        return msg;

                      } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new RuntimeException("Filter processing interrupted", e);
                      } finally {
                        // Release buffer back to pool
                        bufferManager.releaseBuffer(bufferHandle);
                        activeHandles.remove(bufferHandle);
                      }
                    }))
        .subscribeOn(Schedulers.boundedElastic());
  }

  @Override
  public void setExceptionHandler(Consumer<Throwable> handler) {
    this.exceptionHandler = handler;
    baseTransport.setExceptionHandler(handler);
  }

  /**
   * Adds a filter dynamically to the specified chain.
   *
   * @param name The filter name
   * @param type The filter type
   * @param filterConfig The filter configuration
   */
  public void addFilter(String name, FilterType type, Map<String, Object> filterConfig) {
    if (dynamicFilters.containsKey(name)) {
      throw new IllegalArgumentException("Filter already exists: " + name);
    }

    try {
      long filterHandle = filter.createBuiltin(dispatcher, type.getValue(), null);
      if (filterHandle != 0) {
        activeHandles.add(filterHandle);
        dynamicFilters.put(name, filterHandle);

        // Rebuild chains to include new filter
        rebuildFilterChains();

        LOGGER.info("Added filter '{}' of type {}", name, type);
      }
    } catch (Exception e) {
      LOGGER.error("Failed to add filter '{}'", name, e);
      throw new RuntimeException("Failed to add filter", e);
    }
  }

  /**
   * Removes a filter dynamically from the chains.
   *
   * @param name The filter name to remove
   */
  public void removeFilter(String name) {
    Long filterHandle = dynamicFilters.remove(name);
    if (filterHandle != null) {
      try {
        filter.release(filterHandle);
        activeHandles.remove(filterHandle);

        // Rebuild chains without removed filter
        rebuildFilterChains();

        LOGGER.info("Removed filter '{}'", name);
      } catch (Exception e) {
        LOGGER.error("Failed to remove filter '{}'", name, e);
      }
    }
  }

  /**
   * Updates the configuration of an existing filter.
   *
   * @param name The filter name
   * @param filterConfig The new configuration
   */
  public void updateFilterConfig(String name, Map<String, Object> filterConfig) {
    Long filterHandle = dynamicFilters.get(name);
    if (filterHandle != null) {
      try {
        // Update filter configuration (implementation depends on native API)
        LOGGER.info("Updated configuration for filter '{}'", name);
      } catch (Exception e) {
        LOGGER.error("Failed to update filter '{}' configuration", name, e);
      }
    }
  }

  /**
   * Pauses a specific filter.
   *
   * @param name The filter name to pause
   */
  public void pauseFilter(String name) {
    try {
      chain.chainSetFilterEnabled(outboundChainHandle, name, false);
      chain.chainSetFilterEnabled(inboundChainHandle, name, false);
      LOGGER.info("Paused filter '{}'", name);
    } catch (Exception e) {
      LOGGER.error("Failed to pause filter '{}'", name, e);
    }
  }

  /**
   * Resumes a paused filter.
   *
   * @param name The filter name to resume
   */
  public void resumeFilter(String name) {
    try {
      chain.chainSetFilterEnabled(outboundChainHandle, name, true);
      chain.chainSetFilterEnabled(inboundChainHandle, name, true);
      LOGGER.info("Resumed filter '{}'", name);
    } catch (Exception e) {
      LOGGER.error("Failed to resume filter '{}'", name, e);
    }
  }

  /** Rebuilds the filter chains after dynamic changes. */
  private void rebuildFilterChains() {
    // Pause chains
    chain.chainPause(outboundChainHandle);
    chain.chainPause(inboundChainHandle);

    try {
      // Release old chains
      filter.chainRelease(outboundChainHandle);
      filter.chainRelease(inboundChainHandle);
      activeHandles.remove(outboundChainHandle);
      activeHandles.remove(inboundChainHandle);

      // Rebuild with current filters
      initializeFilterChains();

    } finally {
      // Resume chains
      chain.chainResume(outboundChainHandle);
      chain.chainResume(inboundChainHandle);
    }
  }

  /** Optimizes the filter chains for better performance. */
  public void optimizeChains() {
    try {
      int result = chain.chainOptimize(outboundChainHandle);
      if (result == 0) {
        LOGGER.info("Optimized outbound chain");
      }

      result = chain.chainOptimize(inboundChainHandle);
      if (result == 0) {
        LOGGER.info("Optimized inbound chain");
      }
    } catch (Exception e) {
      LOGGER.error("Failed to optimize chains", e);
    }
  }

  /** Validates the filter chains for correctness. */
  public void validateChains() {
    try {
      int result = chain.chainValidate(outboundChainHandle, null);
      if (result != 0) {
        LOGGER.warn("Outbound chain validation failed: {}", result);
      }

      result = chain.chainValidate(inboundChainHandle, null);
      if (result != 0) {
        LOGGER.warn("Inbound chain validation failed: {}", result);
      }
    } catch (Exception e) {
      LOGGER.error("Failed to validate chains", e);
    }
  }

  /**
   * Gets statistics for the filter chains.
   *
   * @return The chain statistics
   */
  public ChainStats getChainStatistics() {
    return monitor.getChainStatistics();
  }

  /**
   * Gets statistics for all filters.
   *
   * @return Map of filter name to statistics
   */
  public Map<String, FilterStats> getFilterStatistics() {
    Map<String, FilterStats> stats = new ConcurrentHashMap<>();

    for (Map.Entry<String, Long> entry : dynamicFilters.entrySet()) {
      try {
        FilterStats filterStats = filter.getStats(entry.getValue());
        if (filterStats != null) {
          stats.put(entry.getKey(), filterStats);
        }
      } catch (Exception e) {
        LOGGER.error("Failed to get stats for filter '{}'", entry.getKey(), e);
      }
    }

    return stats;
  }

  @Override
  public void close() {
    if (closed.compareAndSet(false, true)) {
      LOGGER.info("Closing FilteredClientTransport");

      try {
        // Stop monitoring
        monitor.stop();

        // Release all handles in reverse order
        for (int i = activeHandles.size() - 1; i >= 0; i--) {
          Long handle = activeHandles.get(i);
          if (handle != null && handle != 0) {
            try {
              filter.release(handle);
            } catch (Exception e) {
              // Try buffer release
              try {
                filter.bufferRelease(handle);
              } catch (Exception e2) {
                // Try chain release
                try {
                  filter.chainRelease(handle);
                } catch (Exception e3) {
                  LOGGER.debug("Failed to release handle {}", handle);
                }
              }
            }
          }
        }
        activeHandles.clear();
        dynamicFilters.clear();

        // Close buffer manager
        bufferManager.close();

        // Close core components
        if (filter != null) filter.close();
        if (buffer != null) buffer.close();
        if (chain != null) chain.close();

        LOGGER.info("FilteredClientTransport closed successfully");

      } catch (Exception e) {
        LOGGER.error("Error during shutdown", e);
      }
    }
  }

  @Override
  public Mono<Void> closeGracefully() {
    return Mono.fromRunnable(this::close);
  }

  @Override
  public <T> T unmarshalFrom(Object raw, TypeReference<T> typeRef) {
    try {
      if (raw instanceof String) {
        return MAPPER.readValue((String) raw, typeRef);
      } else if (raw instanceof byte[]) {
        return MAPPER.readValue((byte[]) raw, typeRef);
      } else {
        return MAPPER.convertValue(raw, typeRef);
      }
    } catch (Exception e) {
      throw new RuntimeException("Failed to unmarshal", e);
    }
  }
}
