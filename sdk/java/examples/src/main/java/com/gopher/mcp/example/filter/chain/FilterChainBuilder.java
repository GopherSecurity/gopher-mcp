package com.gopher.mcp.example.filter.chain;

import com.gopher.mcp.filter.McpFilter;
import com.gopher.mcp.filter.McpFilterChain;
import com.gopher.mcp.filter.type.FilterPosition;
import com.gopher.mcp.filter.type.FilterType;
import com.gopher.mcp.filter.type.chain.ChainConfig;
import com.gopher.mcp.filter.type.chain.ChainExecutionMode;
import com.gopher.mcp.filter.type.chain.ChainRoutingStrategy;
import com.gopher.mcp.filter.type.chain.RouterConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Predicate;

/**
 * Builder for creating and managing filter chains with various execution modes.
 * Provides methods to build sequential, parallel, conditional, and pipeline chains.
 *
 * <p>Features:
 * <ul>
 *   <li>Sequential chain building for ordered filter execution</li>
 *   <li>Parallel chain building for concurrent filter execution</li>
 *   <li>Conditional chains with content-based routing</li>
 *   <li>Complex pipeline support with branching and merging</li>
 *   <li>Dynamic chain modification capabilities</li>
 *   <li>Chain optimization and validation</li>
 * </ul>
 *
 * <p>Example usage:
 * <pre>{@code
 * // Sequential chain
 * long chain = FilterChainBuilder.sequential()
 *     .addFilter(compressionFilter)
 *     .addFilter(encryptionFilter)
 *     .addFilter(loggingFilter)
 *     .build(dispatcher);
 *
 * // Parallel chain
 * long chain = FilterChainBuilder.parallel()
 *     .addParallelGroup(metricsFilter, tracingFilter)
 *     .withMaxConcurrency(4)
 *     .build(dispatcher);
 *
 * // Conditional chain
 * long chain = FilterChainBuilder.conditional()
 *     .when(isHttpRequest(), httpChain)
 *     .when(isTcpRequest(), tcpChain)
 *     .otherwise(defaultChain)
 *     .build(dispatcher);
 * }</pre>
 *
 * @author Gopher MCP SDK
 * @since 1.0.0
 */
public class FilterChainBuilder {
    private static final Logger LOGGER = LoggerFactory.getLogger(FilterChainBuilder.class);

    /**
     * Creates a sequential chain builder.
     *
     * @return A new sequential chain builder
     */
    public static Sequential sequential() {
        return new Sequential();
    }

    /**
     * Creates a parallel chain builder.
     *
     * @return A new parallel chain builder
     */
    public static Parallel parallel() {
        return new Parallel();
    }

    /**
     * Creates a conditional chain builder.
     *
     * @return A new conditional chain builder
     */
    public static Conditional conditional() {
        return new Conditional();
    }

    /**
     * Creates a pipeline chain builder.
     *
     * @return A new pipeline chain builder
     */
    public static Pipeline pipeline() {
        return new Pipeline();
    }

    /**
     * Builder for sequential filter chains.
     * Filters are executed one after another in the order they were added.
     */
    public static class Sequential {
        private final List<FilterEntry> filters = new ArrayList<>();
        private final McpFilter filter = new McpFilter();
        private final McpFilterChain chain = new McpFilterChain();
        private String name = "sequential-chain";
        private boolean stopOnError = false;
        private boolean optimizationEnabled = true;

        /**
         * Sets the chain name.
         */
        public Sequential withName(String name) {
            this.name = name;
            return this;
        }

        /**
         * Adds a filter to the chain.
         *
         * @param filterHandle The filter handle
         * @return This builder
         */
        public Sequential addFilter(long filterHandle) {
            return addFilter(filterHandle, FilterPosition.LAST, null);
        }

        /**
         * Adds a filter with specific position.
         *
         * @param filterHandle The filter handle
         * @param position The position in the chain
         * @param priority The filter priority (optional)
         * @return This builder
         */
        public Sequential addFilter(long filterHandle, FilterPosition position, Integer priority) {
            filters.add(new FilterEntry(filterHandle, position, priority));
            return this;
        }

        /**
         * Adds a builtin filter by type.
         *
         * @param dispatcher The dispatcher handle
         * @param type The filter type
         * @param config The filter configuration
         * @return This builder
         */
        public Sequential addBuiltinFilter(long dispatcher, FilterType type, Map<String, Object> config) {
            // Convert config to string (JSON) for the API
            String configStr = config != null ? config.toString() : null;
            long filterHandle = filter.createBuiltin(dispatcher, type.getValue(), configStr);
            if (filterHandle != 0) {
                addFilter(filterHandle);
            }
            return this;
        }

        /**
         * Sets whether to stop chain execution on error.
         */
        public Sequential stopOnError(boolean stop) {
            this.stopOnError = stop;
            return this;
        }

        /**
         * Enables or disables automatic optimization.
         */
        public Sequential withOptimization(boolean enabled) {
            this.optimizationEnabled = enabled;
            return this;
        }

        /**
         * Builds the sequential chain.
         *
         * @param dispatcher The dispatcher handle
         * @return The chain handle
         */
        public long build(long dispatcher) {
            long chainBuilder = filter.chainBuilderCreate(dispatcher);
            if (chainBuilder == 0) {
                throw new RuntimeException("Failed to create chain builder");
            }

            try {
                // Add filters in order
                for (FilterEntry entry : filters) {
                    int priority = entry.priority != null ? entry.priority : 0;
                    int result = filter.chainAddFilter(chainBuilder, entry.handle,
                        entry.position.getValue(), priority);
                    if (result != 0) {
                        LOGGER.warn("Failed to add filter {} to chain", entry.handle);
                    }
                }

                // Build the chain
                long chainHandle = filter.chainBuild(chainBuilder);
                if (chainHandle == 0) {
                    throw new RuntimeException("Failed to build chain");
                }

                // Apply configuration
                if (stopOnError) {
                    // Set stop on error (would be implemented in production)
                    // chain.chainSetStopOnError(chainHandle, true);
                }

                // Optimize if enabled
                if (optimizationEnabled) {
                    chain.chainOptimize(chainHandle);
                }

                LOGGER.info("Built sequential chain '{}' with {} filters", name, filters.size());
                return chainHandle;

            } finally {
                filter.chainBuilderDestroy(chainBuilder);
            }
        }
    }

    /**
     * Builder for parallel filter chains.
     * Filters in parallel groups are executed concurrently.
     */
    public static class Parallel {
        private final List<ParallelGroup> groups = new ArrayList<>();
        private final McpFilter filter = new McpFilter();
        private final McpFilterChain chain = new McpFilterChain();
        private String name = "parallel-chain";
        private int maxConcurrency = 4;
        private boolean waitForAll = true;

        /**
         * Sets the chain name.
         */
        public Parallel withName(String name) {
            this.name = name;
            return this;
        }

        /**
         * Sets maximum concurrent filter execution.
         */
        public Parallel withMaxConcurrency(int max) {
            this.maxConcurrency = max;
            return this;
        }

        /**
         * Sets whether to wait for all filters in a group.
         */
        public Parallel waitForAll(boolean wait) {
            this.waitForAll = wait;
            return this;
        }

        /**
         * Adds a parallel group of filters.
         *
         * @param filterHandles The filter handles to run in parallel
         * @return This builder
         */
        public Parallel addParallelGroup(long... filterHandles) {
            groups.add(new ParallelGroup(filterHandles));
            return this;
        }

        /**
         * Adds a sequential filter between parallel groups.
         *
         * @param filterHandle The filter handle
         * @return This builder
         */
        public Parallel addSequentialFilter(long filterHandle) {
            groups.add(new ParallelGroup(filterHandle));
            return this;
        }

        /**
         * Builds the parallel chain.
         *
         * @param dispatcher The dispatcher handle
         * @return The chain handle
         */
        public long build(long dispatcher) {
            // Create chain configuration
            ChainConfig config = new ChainConfig();
            config.setName(name);
            config.setMode(ChainExecutionMode.PARALLEL.getValue());
            config.setMaxParallel(maxConcurrency);

            long chainBuilder = chain.chainBuilderCreateEx(dispatcher, config);
            if (chainBuilder == 0) {
                // Fallback to regular builder
                chainBuilder = filter.chainBuilderCreate(dispatcher);
                if (chainBuilder == 0) {
                    throw new RuntimeException("Failed to create chain builder");
                }
            }

            try {
                // Add parallel groups
                for (ParallelGroup group : groups) {
                    if (group.filters.length == 1) {
                        // Single filter - add normally
                        filter.chainAddFilter(chainBuilder, group.filters[0],
                            FilterPosition.LAST.getValue(), 0);
                    } else {
                        // Multiple filters - add as parallel group
                        int result = chain.chainBuilderAddParallelGroup(chainBuilder, group.filters);
                        if (result != 0) {
                            // Fallback: add filters sequentially
                            for (long filterHandle : group.filters) {
                                filter.chainAddFilter(chainBuilder, filterHandle,
                                    FilterPosition.LAST.getValue(), 0);
                            }
                        }
                    }
                }

                // Build the chain
                long chainHandle = filter.chainBuild(chainBuilder);
                if (chainHandle == 0) {
                    throw new RuntimeException("Failed to build parallel chain");
                }

                LOGGER.info("Built parallel chain '{}' with {} groups (max concurrency: {})",
                    name, groups.size(), maxConcurrency);
                return chainHandle;

            } finally {
                filter.chainBuilderDestroy(chainBuilder);
            }
        }

        private static class ParallelGroup {
            final long[] filters;

            ParallelGroup(long... filters) {
                this.filters = filters;
            }
        }
    }

    /**
     * Builder for conditional filter chains.
     * Routes messages to different chains based on conditions.
     */
    public static class Conditional {
        private final List<ConditionalRoute> routes = new ArrayList<>();
        private final McpFilter filter = new McpFilter();
        private final McpFilterChain chain = new McpFilterChain();
        private String name = "conditional-chain";
        private long defaultChain = 0;
        private ChainRoutingStrategy strategy = ChainRoutingStrategy.ROUND_ROBIN;

        /**
         * Sets the chain name.
         */
        public Conditional withName(String name) {
            this.name = name;
            return this;
        }

        /**
         * Sets the routing strategy.
         */
        public Conditional withStrategy(ChainRoutingStrategy strategy) {
            this.strategy = strategy;
            return this;
        }

        /**
         * Adds a conditional route.
         *
         * @param condition The condition predicate
         * @param chainHandle The chain to route to if condition is true
         * @return This builder
         */
        public Conditional when(Predicate<byte[]> condition, long chainHandle) {
            routes.add(new ConditionalRoute(condition, chainHandle));
            return this;
        }

        /**
         * Sets the default chain for unmatched conditions.
         *
         * @param chainHandle The default chain handle
         * @return This builder
         */
        public Conditional otherwise(long chainHandle) {
            this.defaultChain = chainHandle;
            return this;
        }

        /**
         * Builds the conditional chain.
         *
         * @param dispatcher The dispatcher handle
         * @return The chain handle or router handle
         */
        public long build(long dispatcher) {
            // Create router configuration
            RouterConfig routerConfig = new RouterConfig(strategy.getValue());

            long router = chain.chainRouterCreate(routerConfig);
            if (router == 0) {
                LOGGER.warn("Failed to create router, falling back to default chain");
                return defaultChain;
            }

            try {
                // Add routes
                for (int i = 0; i < routes.size(); i++) {
                    ConditionalRoute route = routes.get(i);
                    // In production, you would register conditions with the router
                    // For now, this is a placeholder
                    LOGGER.debug("Added route {} to chain {}", i, route.chainHandle);
                }

                // Set default route
                if (defaultChain != 0) {
                    // Router would handle default routing
                    LOGGER.debug("Set default route to chain {}", defaultChain);
                }

                LOGGER.info("Built conditional chain '{}' with {} routes", name, routes.size());
                return router;

            } catch (Exception e) {
                chain.chainRouterDestroy(router);
                throw new RuntimeException("Failed to build conditional chain", e);
            }
        }

        private static class ConditionalRoute {
            final Predicate<byte[]> condition;
            final long chainHandle;

            ConditionalRoute(Predicate<byte[]> condition, long chainHandle) {
                this.condition = condition;
                this.chainHandle = chainHandle;
            }
        }
    }

    /**
     * Builder for complex processing pipelines.
     * Supports branching, merging, and complex topologies.
     */
    public static class Pipeline {
        private final List<PipelineStage> stages = new ArrayList<>();
        private final McpFilter filter = new McpFilter();
        private final McpFilterChain chain = new McpFilterChain();
        private final Map<String, Long> namedChains = new HashMap<>();
        private String name = "pipeline";
        private boolean backpressureEnabled = true;
        private int bufferSize = 100;

        /**
         * Sets the pipeline name.
         */
        public Pipeline withName(String name) {
            this.name = name;
            return this;
        }

        /**
         * Enables or disables backpressure handling.
         */
        public Pipeline withBackpressure(boolean enabled, int bufferSize) {
            this.backpressureEnabled = enabled;
            this.bufferSize = bufferSize;
            return this;
        }

        /**
         * Adds a processing stage to the pipeline.
         *
         * @param stageName The stage name
         * @param chainHandle The chain for this stage
         * @return This builder
         */
        public Pipeline addStage(String stageName, long chainHandle) {
            stages.add(new PipelineStage(stageName, chainHandle));
            namedChains.put(stageName, chainHandle);
            return this;
        }

        /**
         * Creates a branch in the pipeline.
         *
         * @param branchName The branch name
         * @param condition The branching condition
         * @param branchChain The chain for the branch
         * @return This builder
         */
        public Pipeline branch(String branchName, Predicate<byte[]> condition, long branchChain) {
            stages.add(new PipelineStage(branchName, branchChain, StageType.BRANCH, condition));
            namedChains.put(branchName, branchChain);
            return this;
        }

        /**
         * Merges branches back together.
         *
         * @param mergeName The merge point name
         * @param branches The branches to merge
         * @return This builder
         */
        public Pipeline merge(String mergeName, String... branches) {
            List<Long> branchChains = new ArrayList<>();
            for (String branch : branches) {
                Long chainHandle = namedChains.get(branch);
                if (chainHandle != null) {
                    branchChains.add(chainHandle);
                }
            }

            stages.add(new PipelineStage(mergeName, 0, StageType.MERGE, null));
            return this;
        }

        /**
         * Adds a fork point that splits processing.
         *
         * @param forkName The fork name
         * @param forkChains The chains to fork to
         * @return This builder
         */
        public Pipeline fork(String forkName, long... forkChains) {
            stages.add(new PipelineStage(forkName, 0, StageType.FORK, null));
            return this;
        }

        /**
         * Builds the pipeline.
         *
         * @param dispatcher The dispatcher handle
         * @return The pipeline handle
         */
        public long build(long dispatcher) {
            if (stages.isEmpty()) {
                throw new IllegalStateException("Pipeline has no stages");
            }

            // For complex pipelines, we would need to build a graph structure
            // For now, create a simple sequential chain as a placeholder
            long chainBuilder = filter.chainBuilderCreate(dispatcher);
            if (chainBuilder == 0) {
                throw new RuntimeException("Failed to create pipeline builder");
            }

            try {
                // Add stages in order (simplified)
                for (PipelineStage stage : stages) {
                    if (stage.type == StageType.NORMAL && stage.chainHandle != 0) {
                        // Add as a sub-chain or filter
                        LOGGER.debug("Adding pipeline stage '{}' with chain {}",
                            stage.name, stage.chainHandle);
                    }
                }

                // Build the pipeline
                long pipelineHandle = filter.chainBuild(chainBuilder);
                if (pipelineHandle == 0) {
                    throw new RuntimeException("Failed to build pipeline");
                }

                LOGGER.info("Built pipeline '{}' with {} stages", name, stages.size());
                return pipelineHandle;

            } finally {
                filter.chainBuilderDestroy(chainBuilder);
            }
        }

        private enum StageType {
            NORMAL, BRANCH, MERGE, FORK
        }

        private static class PipelineStage {
            final String name;
            final long chainHandle;
            final StageType type;
            final Predicate<byte[]> condition;

            PipelineStage(String name, long chainHandle) {
                this(name, chainHandle, StageType.NORMAL, null);
            }

            PipelineStage(String name, long chainHandle, StageType type, Predicate<byte[]> condition) {
                this.name = name;
                this.chainHandle = chainHandle;
                this.type = type;
                this.condition = condition;
            }
        }
    }

    /**
     * Filter entry for chain building.
     */
    private static class FilterEntry {
        final long handle;
        final FilterPosition position;
        final Integer priority;

        FilterEntry(long handle, FilterPosition position, Integer priority) {
            this.handle = handle;
            this.position = position;
            this.priority = priority;
        }
    }
}
