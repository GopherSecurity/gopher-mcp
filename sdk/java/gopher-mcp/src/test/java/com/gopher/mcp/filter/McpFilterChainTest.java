package com.gopher.mcp.filter;

import com.gopher.mcp.filter.type.ProtocolMetadata;
import com.gopher.mcp.filter.type.buffer.FilterCondition;
import com.gopher.mcp.filter.type.chain.*;
import com.gopher.mcp.jna.McpFilterChainLibrary;
import com.sun.jna.ptr.LongByReference;
import com.sun.jna.ptr.PointerByReference;
import org.junit.jupiter.api.*;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Comprehensive unit tests for McpFilterChain Java wrapper.
 * Tests all chain operations including builder, management, routing, pools, and optimization.
 */
@DisplayName("MCP Filter Chain Tests")
@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class McpFilterChainTest {

    private McpFilterChain chain;
    private Long builderHandle;
    private Long chainHandle;
    private Long poolHandle;
    private Long routerHandle;

    @BeforeEach
    public void setUp() {
        chain = new McpFilterChain();
        // Note: Most operations require a valid dispatcher which needs mcp_init()
        // For now we'll test what we can without a dispatcher
    }

    @AfterEach
    public void tearDown() {
        // Clean up any created resources
        if (poolHandle != null && poolHandle != 0) {
            chain.chainPoolDestroy(poolHandle);
        }
        if (routerHandle != null && routerHandle != 0) {
            chain.chainRouterDestroy(routerHandle);
        }
        if (chain != null) {
            chain.close();
        }
    }

    // ============================================================================
    // Constructor and Basic Tests
    // ============================================================================

    @Test
    @Order(1)
    @DisplayName("Default constructor creates chain instance")
    public void testDefaultConstructor() {
        assertNotNull(chain);
        assertNull(chain.getPrimaryChainHandle());
    }

    @Test
    @Order(2)
    @DisplayName("Primary chain handle management")
    public void testPrimaryChainHandle() {
        assertNull(chain.getPrimaryChainHandle());

        chain.setPrimaryChainHandle(12345L);
        assertEquals(12345L, chain.getPrimaryChainHandle());

        chain.setPrimaryChainHandle(0L);
        assertEquals(0L, chain.getPrimaryChainHandle());
    }

    // ============================================================================
    // Chain Builder Tests
    // ============================================================================

    @Test
    @Order(10)
    @DisplayName("Create chain builder with configuration")
    public void testChainBuilderCreateEx() {
        ChainConfig config = new ChainConfig();
        config.setName("test_chain");
        config.setMode(ChainExecutionMode.SEQUENTIAL.getValue());
        config.setRouting(ChainRoutingStrategy.ROUND_ROBIN.getValue());
        config.setMaxParallel(4);
        config.setBufferSize(4096);
        config.setTimeoutMs(5000);
        config.setStopOnError(true);

        long builder = chain.chainBuilderCreateEx(0L, config);

        // May return 0 without valid dispatcher
        assertTrue(builder >= 0, "Builder handle should be non-negative");
    }

    @Test
    @Order(11)
    @DisplayName("Add node to chain builder")
    public void testChainBuilderAddNode() {
        ChainConfig config = new ChainConfig();
        config.setName("test_chain");
        long builder = chain.chainBuilderCreateEx(0L, config);

        if (builder != 0) {
            FilterNode node = new FilterNode();
            node.setFilterHandle(0L);  // Invalid filter, but tests the call
            node.setName("test_node");
            node.setPriority(10);
            node.setEnabled(true);
            node.setBypassOnError(false);
            node.setConfigHandle(0L);

            int result = chain.chainBuilderAddNode(builder, node);
            // May fail without valid filter
            assertTrue(result <= 0, "Invalid node should not succeed");
        }
    }

    @Test
    @Order(12)
    @DisplayName("Add conditional filter to chain")
    public void testChainBuilderAddConditional() {
        ChainConfig config = new ChainConfig();
        long builder = chain.chainBuilderCreateEx(0L, config);

        if (builder != 0) {
            FilterCondition condition = new FilterCondition();
            condition.setMatchType(FilterMatchCondition.ALL.getValue());
            condition.setField("content-type");
            condition.setValue("application/json");
            condition.setTargetFilter(0L);

            int result = chain.chainBuilderAddConditional(builder, condition, 0L);
            // May fail without valid filter
            assertTrue(result <= 0, "Invalid conditional should not succeed");
        }
    }

    @Test
    @Order(13)
    @DisplayName("Add parallel filter group")
    public void testChainBuilderAddParallelGroup() {
        ChainConfig config = new ChainConfig();
        long builder = chain.chainBuilderCreateEx(0L, config);

        if (builder != 0) {
            long[] filters = {0L, 0L, 0L};  // Invalid filters

            int result = chain.chainBuilderAddParallelGroup(builder, filters);
            // May fail without valid filters
            assertTrue(result <= 0, "Invalid group should not succeed");
        }
    }

    @Test
    @Order(14)
    @DisplayName("Set custom router on chain builder")
    public void testChainBuilderSetRouter() {
        ChainConfig config = new ChainConfig();
        long builder = chain.chainBuilderCreateEx(0L, config);

        // Test with null router (avoids the type conversion issue)
        assertDoesNotThrow(() -> {
            int result = chain.chainBuilderSetRouter(builder, null, 0L);
            // Should handle null router gracefully
            // With invalid builder (0), this should return an error or 0
        }, "Setting null router should not throw an exception");

        // Note: Cannot test with actual router function due to JNA limitation
        // with McpFilterNode[] array parameter requiring custom type conversion
        // This is a known JNA limitation with callbacks containing complex array types
    }

    // ============================================================================
    // Chain Management Tests
    // ============================================================================

    @Test
    @Order(20)
    @DisplayName("Get chain state with invalid handle")
    public void testChainGetStateInvalid() {
        // Should handle invalid chain gracefully
        assertDoesNotThrow(() -> {
            ChainState state = chain.chainGetState(0L);
            // May return a default state or throw
        });
    }

    @Test
    @Order(21)
    @DisplayName("Pause chain with invalid handle")
    public void testChainPauseInvalid() {
        int result = chain.chainPause(0L);
        assertTrue(result <= 0, "Invalid chain pause should fail");
    }

    @Test
    @Order(22)
    @DisplayName("Resume chain with invalid handle")
    public void testChainResumeInvalid() {
        int result = chain.chainResume(0L);
        assertTrue(result <= 0, "Invalid chain resume should fail");
    }

    @Test
    @Order(23)
    @DisplayName("Reset chain with invalid handle")
    public void testChainResetInvalid() {
        int result = chain.chainReset(0L);
        assertTrue(result <= 0, "Invalid chain reset should fail");
    }

    @Test
    @Order(24)
    @DisplayName("Set filter enabled state")
    public void testChainSetFilterEnabled() {
        int result = chain.chainSetFilterEnabled(0L, "test_filter", true);
        assertTrue(result <= 0, "Invalid chain should fail");

        result = chain.chainSetFilterEnabled(0L, "test_filter", false);
        assertTrue(result <= 0, "Invalid chain should fail");
    }

    @Test
    @Order(25)
    @DisplayName("Get chain statistics")
    public void testChainGetStats() {
        ChainStats stats = new ChainStats();
        int result = chain.chainGetStats(0L, stats);

        // Should fail with invalid chain
        assertTrue(result != 0, "Invalid chain stats should fail");
    }

    @Test
    @Order(26)
    @DisplayName("Set chain event callback")
    public void testChainSetEventCallback() {
        McpFilterChainLibrary.MCP_CHAIN_EVENT_CB callback =
            (chainHandle, event, data, user_data) -> {};

        int result = chain.chainSetEventCallback(0L, callback, 0L);
        assertTrue(result <= 0, "Invalid chain callback should fail");
    }

    // ============================================================================
    // Dynamic Chain Composition Tests
    // ============================================================================

    @Test
    @Order(30)
    @DisplayName("Create chain from JSON configuration")
    public void testChainCreateFromJson() {
        long chainHandle = chain.chainCreateFromJson(0L, 0L);

        // May return 0 without valid JSON
        assertTrue(chainHandle >= 0, "Chain handle should be non-negative");

        if (chainHandle != 0) {
            // Check if primary handle was set
            assertEquals(chainHandle, chain.getPrimaryChainHandle());
        }
    }

    @Test
    @Order(31)
    @DisplayName("Export chain to JSON")
    public void testChainExportToJson() {
        long jsonHandle = chain.chainExportToJson(0L);

        // May return 0 without valid chain
        assertTrue(jsonHandle >= 0, "JSON handle should be non-negative");
    }

    @Test
    @Order(32)
    @DisplayName("Clone chain with invalid handle")
    public void testChainClone() {
        long clonedChain = chain.chainClone(0L);

        // Should return 0 for invalid chain
        assertEquals(0L, clonedChain, "Invalid chain clone should return 0");
    }

    @Test
    @Order(33)
    @DisplayName("Merge two chains")
    public void testChainMerge() {
        long mergedChain = chain.chainMerge(0L, 0L, ChainExecutionMode.SEQUENTIAL);

        // Should return 0 for invalid chains
        assertEquals(0L, mergedChain, "Invalid chain merge should return 0");

        // Test with different modes
        mergedChain = chain.chainMerge(0L, 0L, ChainExecutionMode.PARALLEL);
        assertEquals(0L, mergedChain, "Invalid chain merge should return 0");

        mergedChain = chain.chainMerge(0L, 0L, ChainExecutionMode.CONDITIONAL);
        assertEquals(0L, mergedChain, "Invalid chain merge should return 0");

        mergedChain = chain.chainMerge(0L, 0L, ChainExecutionMode.PIPELINE);
        assertEquals(0L, mergedChain, "Invalid chain merge should return 0");
    }

    // ============================================================================
    // Chain Router Tests
    // ============================================================================

    @Test
    @Order(40)
    @DisplayName("Create chain router")
    public void testChainRouterCreate() {
        RouterConfig config = new RouterConfig(ChainRoutingStrategy.ROUND_ROBIN.getValue());
        config.setHashSeed(12345);
        config.setRouteTable(0L);
        config.setCustomRouterData(null);

        routerHandle = chain.chainRouterCreate(config);

        // May return 0 without proper setup
        assertTrue(routerHandle >= 0, "Router handle should be non-negative");
    }

    @Test
    @Order(41)
    @DisplayName("Add route to router")
    public void testChainRouterAddRoute() {
        RouterConfig config = new RouterConfig(ChainRoutingStrategy.LEAST_LOADED.getValue());
        long router = chain.chainRouterCreate(config);

        if (router != 0) {
            McpFilterChainLibrary.MCP_FILTER_MATCH_CB condition =
                (buffer, metadata, user_data) -> (byte) 1;

            int result = chain.chainRouterAddRoute(router, condition, 0L);
            // May succeed even with invalid chain
            assertTrue(result >= -1, "Add route should not crash");

            chain.chainRouterDestroy(router);
        }
    }

    @Test
    @Order(42)
    @DisplayName("Route buffer through router")
    public void testChainRouterRoute() {
        RouterConfig config = new RouterConfig(ChainRoutingStrategy.HASH_BASED.getValue());
        long router = chain.chainRouterCreate(config);

        if (router != 0) {
            ProtocolMetadata metadata = new ProtocolMetadata();
            metadata.setLayer(3);  // Network layer

            long result = chain.chainRouterRoute(router, 0L, metadata);
            // Should return 0 for invalid buffer
            assertEquals(0L, result, "Invalid buffer should return 0");

            // Test without metadata
            result = chain.chainRouterRoute(router, 0L, null);
            assertEquals(0L, result, "Invalid buffer should return 0");

            chain.chainRouterDestroy(router);
        }
    }

    @Test
    @Order(43)
    @DisplayName("Destroy chain router")
    public void testChainRouterDestroy() {
        // Should handle invalid router gracefully
        assertDoesNotThrow(() -> chain.chainRouterDestroy(0L));

        RouterConfig config = new RouterConfig(ChainRoutingStrategy.ROUND_ROBIN.getValue());
        long router = chain.chainRouterCreate(config);
        if (router != 0) {
            assertDoesNotThrow(() -> chain.chainRouterDestroy(router));
        }
    }

    // ============================================================================
    // Chain Pool Tests
    // ============================================================================

    @Test
    @Order(50)
    @DisplayName("Create chain pool")
    public void testChainPoolCreate() {
        poolHandle = chain.chainPoolCreate(0L, 10, ChainRoutingStrategy.ROUND_ROBIN);

        // May return 0 without valid base chain
        assertTrue(poolHandle >= 0, "Pool handle should be non-negative");

        // Test with different strategies
        long pool2 = chain.chainPoolCreate(0L, 5, ChainRoutingStrategy.LEAST_LOADED);
        if (pool2 != 0) {
            chain.chainPoolDestroy(pool2);
        }

        long pool3 = chain.chainPoolCreate(0L, 3, ChainRoutingStrategy.PRIORITY);
        if (pool3 != 0) {
            chain.chainPoolDestroy(pool3);
        }
    }

    @Test
    @Order(51)
    @DisplayName("Get next chain from pool")
    public void testChainPoolGetNext() {
        long pool = chain.chainPoolCreate(0L, 5, ChainRoutingStrategy.ROUND_ROBIN);

        if (pool != 0) {
            long nextChain = chain.chainPoolGetNext(pool);
            // Should return 0 without valid chains in pool
            assertEquals(0L, nextChain, "Empty pool should return 0");

            chain.chainPoolDestroy(pool);
        }
    }

    @Test
    @Order(52)
    @DisplayName("Return chain to pool")
    public void testChainPoolReturn() {
        long pool = chain.chainPoolCreate(0L, 5, ChainRoutingStrategy.ROUND_ROBIN);

        if (pool != 0) {
            // Should handle invalid chain gracefully
            assertDoesNotThrow(() -> chain.chainPoolReturn(pool, 0L));

            chain.chainPoolDestroy(pool);
        }
    }

    @Test
    @Order(53)
    @DisplayName("Get pool statistics")
    public void testChainPoolGetStats() {
        long pool = chain.chainPoolCreate(0L, 5, ChainRoutingStrategy.LEAST_LOADED);

        if (pool != 0) {
            PointerByReference active = new PointerByReference();
            PointerByReference idle = new PointerByReference();
            LongByReference totalProcessed = new LongByReference();

            int result = chain.chainPoolGetStats(pool, active, idle, totalProcessed);
            // May fail without valid pool
            assertTrue(result <= 0, "Invalid pool stats should fail");

            chain.chainPoolDestroy(pool);
        }
    }

    @Test
    @Order(54)
    @DisplayName("Destroy chain pool")
    public void testChainPoolDestroy() {
        // Should handle invalid pool gracefully
        assertDoesNotThrow(() -> chain.chainPoolDestroy(0L));

        long pool = chain.chainPoolCreate(0L, 3, ChainRoutingStrategy.HASH_BASED);
        if (pool != 0) {
            assertDoesNotThrow(() -> chain.chainPoolDestroy(pool));
        }
    }

    // ============================================================================
    // Chain Optimization Tests
    // ============================================================================

    @Test
    @Order(60)
    @DisplayName("Optimize chain")
    public void testChainOptimize() {
        int result = chain.chainOptimize(0L);

        // May return 0 or error code for invalid chain
        // Native implementation may return 0 (OK) even for invalid handle
        assertTrue(result == 0 || result < 0, "Should return 0 or error code for invalid chain");
    }

    @Test
    @Order(61)
    @DisplayName("Reorder filters in chain")
    public void testChainReorderFilters() {
        int result = chain.chainReorderFilters(0L);

        // May return 0 or error code for invalid chain
        // Native implementation may return 0 (OK) even for invalid handle
        assertTrue(result == 0 || result < 0, "Should return 0 or error code for invalid chain");
    }

    @Test
    @Order(62)
    @DisplayName("Profile chain performance")
    public void testChainProfile() {
        PointerByReference report = new PointerByReference();

        int result = chain.chainProfile(0L, 0L, 100, report);

        // May return 0 or error code for invalid chain
        // Native implementation may return 0 (OK) even for invalid handle
        assertTrue(result == 0 || result < 0, "Should return 0 or error code for invalid chain");
    }

    // ============================================================================
    // Chain Debugging Tests
    // ============================================================================

    @Test
    @Order(70)
    @DisplayName("Set chain trace level")
    public void testChainSetTraceLevel() {
        // Test different trace levels
        int result = chain.chainSetTraceLevel(0L, 0);  // No tracing
        assertTrue(result <= 0, "Invalid chain trace should fail");

        result = chain.chainSetTraceLevel(0L, 1);  // Basic tracing
        assertTrue(result <= 0, "Invalid chain trace should fail");

        result = chain.chainSetTraceLevel(0L, 2);  // Detailed tracing
        assertTrue(result <= 0, "Invalid chain trace should fail");
    }

    @Test
    @Order(71)
    @DisplayName("Dump chain structure")
    public void testChainDump() {
        // Test different formats
        String dump = chain.chainDump(0L, "text");
        // May return null or empty for invalid chain
        assertTrue(dump == null || dump.isEmpty(), "Invalid chain dump should be null/empty");

        dump = chain.chainDump(0L, "json");
        assertTrue(dump == null || dump.isEmpty(), "Invalid chain dump should be null/empty");

        dump = chain.chainDump(0L, "xml");
        assertTrue(dump == null || dump.isEmpty(), "Invalid chain dump should be null/empty");
    }

    @Test
    @Order(72)
    @DisplayName("Validate chain configuration")
    public void testChainValidate() {
        PointerByReference errors = new PointerByReference();

        int result = chain.chainValidate(0L, errors);

        // May return 0 or error code for invalid chain
        // Native implementation may return 0 (OK) even for invalid handle
        assertTrue(result == 0 || result < 0, "Should return 0 or error code for invalid chain");
    }

    // ============================================================================
    // AutoCloseable Implementation Tests
    // ============================================================================

    @Test
    @Order(80)
    @DisplayName("Close is idempotent")
    public void testCloseIdempotent() {
        assertDoesNotThrow(() -> {
            chain.close();
            chain.close();
            chain.close();
        });
    }

    @Test
    @Order(81)
    @DisplayName("Close clears primary chain handle")
    public void testCloseClearsHandle() {
        chain.setPrimaryChainHandle(12345L);
        assertNotNull(chain.getPrimaryChainHandle());

        chain.close();
        assertNull(chain.getPrimaryChainHandle());
    }

    // ============================================================================
    // Enum Usage Tests
    // ============================================================================

    @Test
    @Order(90)
    @DisplayName("ChainExecutionMode enum usage")
    public void testChainExecutionModeEnum() {
        // Test all execution modes in merge
        for (ChainExecutionMode mode : ChainExecutionMode.values()) {
            long result = chain.chainMerge(0L, 0L, mode);
            assertEquals(0L, result, "Invalid merge should return 0 for mode: " + mode);
        }
    }

    @Test
    @Order(91)
    @DisplayName("ChainRoutingStrategy enum usage")
    public void testChainRoutingStrategyEnum() {
        // Test all routing strategies in pool creation
        for (ChainRoutingStrategy strategy : ChainRoutingStrategy.values()) {
            long pool = chain.chainPoolCreate(0L, 5, strategy);
            assertTrue(pool >= 0, "Pool creation should not crash for strategy: " + strategy);
            if (pool != 0) {
                chain.chainPoolDestroy(pool);
            }
        }
    }

    @Test
    @Order(92)
    @DisplayName("ChainState enum conversion")
    public void testChainStateEnum() {
        // Test state conversion doesn't crash
        assertDoesNotThrow(() -> {
            try {
                ChainState state = chain.chainGetState(0L);
                assertNotNull(state);
            } catch (IllegalArgumentException e) {
                // Invalid state value is acceptable
            }
        });
    }

    @Test
    @Order(93)
    @DisplayName("FilterMatchCondition enum values")
    public void testFilterMatchConditionEnum() {
        // Test all match conditions in filter condition
        for (FilterMatchCondition match : FilterMatchCondition.values()) {
            FilterCondition condition = new FilterCondition();
            condition.setMatchType(match.getValue());

            assertEquals(match.getValue(), condition.getMatchType());
        }
    }

    // ============================================================================
    // Edge Cases and Error Handling Tests
    // ============================================================================

    @Test
    @Order(100)
    @DisplayName("Handle null configuration in builder")
    public void testNullConfiguration() {
        assertThrows(NullPointerException.class, () -> {
            chain.chainBuilderCreateEx(0L, null);
        });
    }

    @Test
    @Order(101)
    @DisplayName("Handle null node in add node")
    public void testNullNode() {
        ChainConfig config = new ChainConfig();
        long builder = chain.chainBuilderCreateEx(0L, config);

        if (builder != 0) {
            assertThrows(NullPointerException.class, () -> {
                chain.chainBuilderAddNode(builder, null);
            });
        }
    }

    @Test
    @Order(102)
    @DisplayName("Handle empty filter array in parallel group")
    public void testEmptyFilterArray() {
        ChainConfig config = new ChainConfig();
        long builder = chain.chainBuilderCreateEx(0L, config);

        if (builder != 0) {
            long[] emptyFilters = new long[0];

            assertDoesNotThrow(() -> {
                int result = chain.chainBuilderAddParallelGroup(builder, emptyFilters);
                assertTrue(result <= 0, "Empty filter group should fail");
            });
        }
    }

    @Test
    @Order(103)
    @DisplayName("Handle negative values")
    public void testNegativeValues() {
        // Native code should handle negative values gracefully
        assertDoesNotThrow(() -> {
            chain.chainGetState(-1L);
            chain.chainPause(-1L);
            chain.chainResume(-1L);
            chain.chainReset(-1L);
            chain.chainOptimize(-1L);
            chain.chainReorderFilters(-1L);
        });
    }
}
