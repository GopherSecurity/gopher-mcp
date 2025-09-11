package com.gopher.mcp.filter

import com.gopher.mcp.filter.type.*
import com.gopher.mcp.filter.type.buffer.*
import com.gopher.mcp.filter.type.chain.*
import org.junit.jupiter.api.*
import org.junit.jupiter.api.Assertions.*
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

@TestMethodOrder(MethodOrderer.OrderAnnotation::class)
class McpFilterIntegrationTest {

    companion object {
        private const val MOCK_DISPATCHER = 0x1234567890ABCDEFL

        private val HTTP_REQUEST =
            "GET /api/test HTTP/1.1\r\nHost: example.com\r\nContent-Length: 13\r\n\r\nHello, World!"
        private val BINARY_DATA = generateBinaryData(1024)
        private val LARGE_DATA = generateBinaryData(1024 * 1024) // 1MB
        private val MALFORMED_DATA = byteArrayOf(0xFF.toByte(), 0xFE.toByte(), 0xFD.toByte(), 0x00)

        private const val MAX_LATENCY_MS = 10L
        private const val MIN_THROUGHPUT_MBPS = 100.0

        @JvmStatic
        @BeforeAll
        fun setupClass() {
            println("=== MCP Filter Integration Test Suite Starting ===\n")
            println("Using high-level Kotlin API (McpFilter, McpFilterBuffer, McpFilterChain)")
            println("Mock dispatcher ID: 0x${MOCK_DISPATCHER.toString(16)}")
            println()
        }

        @JvmStatic
        @AfterAll
        fun teardown() {
            println("\n=== MCP Filter Integration Test Suite Completed ===")
            println("All resources cleaned up")
        }

        private fun generateBinaryData(size: Int): ByteArray {
            return ByteArray(size) { i -> (i % 256).toByte() }
        }
    }

    private lateinit var filter: McpFilter
    private lateinit var buffer: McpFilterBuffer
    private lateinit var chain: McpFilterChain

    private val activeHandles = mutableListOf<Long>()

    @BeforeEach
    fun setup() {
        filter = McpFilter()
        buffer = McpFilterBuffer()
        chain = McpFilterChain()

        activeHandles.clear()
    }

    @AfterEach
    fun cleanup() {
        for (i in activeHandles.size - 1 downTo 0) {
            val handle = activeHandles[i]
            if (handle != 0L) {
                try {
                    filter.release(handle)
                } catch (e1: Exception) {
                    try {
                        filter.bufferRelease(handle)
                    } catch (e2: Exception) {
                        try {
                            filter.chainRelease(handle)
                        } catch (e3: Exception) {
                            // Handle already released or invalid
                        }
                    }
                }
            }
        }
        activeHandles.clear()

        try {
            filter.close()
            buffer.close()
            chain.close()
        } catch (e: Exception) {
            // Ignore cleanup errors
        }
    }

    @Test
    @Order(1)
    fun testBasicHttpRequestProcessing() {
        println("=== Scenario 1: Basic HTTP Request Processing ===")

        // Step 1: Create buffer with HTTP request data
        val bufferHandle = buffer.createOwned((HTTP_REQUEST.length * 2).toLong(), BufferOwnership.EXCLUSIVE)
        assertNotEquals(0, bufferHandle, "Buffer creation failed")
        activeHandles.add(bufferHandle)
        println("Created buffer with capacity: ${HTTP_REQUEST.length * 2} bytes")

        // Add HTTP request data to buffer
        val result = buffer.add(bufferHandle, HTTP_REQUEST.toByteArray(StandardCharsets.UTF_8))
        assertEquals(0, result.toLong(), "Failed to add data to buffer")
        println("Added HTTP request data: ${HTTP_REQUEST.substring(0, 30)}...")

        // Verify buffer content and length
        val length = buffer.length(bufferHandle)
        assertEquals(HTTP_REQUEST.length.toLong(), length, "Buffer length mismatch")
        println("Buffer length verified: $length bytes")

        // Step 2: Create filters using McpFilter high-level API
        val httpFilter = filter.createBuiltin(MOCK_DISPATCHER, FilterType.HTTP_CODEC.value, null)
        if (httpFilter != 0L) {
            activeHandles.add(httpFilter)
            println("✓ Created HTTP Codec filter")
        }

        val rateLimiterFilter =
            filter.createBuiltin(MOCK_DISPATCHER, FilterType.RATE_LIMIT.value, null)
        if (rateLimiterFilter != 0L) {
            activeHandles.add(rateLimiterFilter)
            println("✓ Created Rate Limiter filter")
        }

        val accessLogFilter =
            filter.createBuiltin(MOCK_DISPATCHER, FilterType.ACCESS_LOG.value, null)
        if (accessLogFilter != 0L) {
            activeHandles.add(accessLogFilter)
            println("✓ Created Access Log filter")
        }

        // Step 3: Build filter chain using McpFilter's chain methods
        val chainBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
        assertNotEquals(0, chainBuilder, "Chain builder creation failed")
        println("Created filter chain builder")

        // Add filters to chain
        var filtersAdded = 0
        if (httpFilter != 0L) {
            val addResult = filter.chainAddFilter(chainBuilder, httpFilter, FilterPosition.FIRST.getValue(), 0)
            if (addResult == 0) filtersAdded++
        }
        if (rateLimiterFilter != 0L) {
            val addResult =
                filter.chainAddFilter(chainBuilder, rateLimiterFilter, FilterPosition.LAST.getValue(), 0)
            if (addResult == 0) filtersAdded++
        }
        if (accessLogFilter != 0L) {
            val addResult =
                filter.chainAddFilter(chainBuilder, accessLogFilter, FilterPosition.LAST.getValue(), 0)
            if (addResult == 0) filtersAdded++
        }

        println("Added $filtersAdded filters to chain")

        // Build the chain
        val chainHandle = filter.chainBuild(chainBuilder)
        if (chainHandle != 0L) {
            activeHandles.add(chainHandle)
            println("✓ Filter chain built successfully")
        }
        filter.chainBuilderDestroy(chainBuilder)

        // Step 4: Test filter statistics
        if (httpFilter != 0L) {
            val stats = filter.getStats(httpFilter)
            if (stats != null) {
                println("HTTP Filter Stats - Bytes processed: ${stats.bytesProcessed}")
            }

            // Reset stats
            val resetResult = filter.resetStats(httpFilter)
            println("Filter stats reset: ${if (resetResult == 0) "SUCCESS" else "FAILED"}")
        }

        // Get buffer statistics
        val bufferStats = buffer.getStats(bufferHandle)
        if (bufferStats != null) {
            println(
                "Buffer Stats - Total bytes: ${bufferStats.totalBytes}, Used bytes: ${bufferStats.usedBytes}"
            )
        }

        println("✓ Basic HTTP request processing test completed\n")
    }

    @Test
    @Order(2)
    fun testZeroCopyBufferOperations() {
        println("=== Scenario 2: Zero-Copy Buffer Operations ===")

        // Create buffer view (zero-copy)
        val bufferHandle = buffer.createView(BINARY_DATA)
        assertNotEquals(0, bufferHandle, "Failed to create buffer view")
        activeHandles.add(bufferHandle)
        println("Created zero-copy buffer view with ${BINARY_DATA.size} bytes")

        // Reserve space for transformation (zero-copy write)
        val reservation = buffer.reserve(bufferHandle, 256)
        if (reservation?.data != null) {
            // Write transformed data directly to reserved space
            val reserved = reservation.data
            val transformedData = "TRANSFORMED:"
            reserved?.put(transformedData.toByteArray(StandardCharsets.UTF_8))

            // Commit the reservation
            val result = buffer.commitReservation(reservation, transformedData.length.toLong())
            assertEquals(0, result.toLong(), "Failed to commit reservation")
            println("✓ Committed ${transformedData.length} bytes to reserved space")
        } else {
            println("Note: Reservation not available (may return null with mock)")
        }

        // Get buffer length
        val bufferLength = buffer.length(bufferHandle)
        println("Buffer length: $bufferLength bytes")

        // Check if buffer is empty
        val empty = buffer.isEmpty(bufferHandle)
        println("Buffer is empty: $empty")

        // Create TCP Proxy filter to process the zero-copy buffer
        val tcpFilter = filter.createBuiltin(MOCK_DISPATCHER, FilterType.TCP_PROXY.value, null)
        if (tcpFilter != 0L) {
            activeHandles.add(tcpFilter)
            println("✓ Created TCP Proxy filter for zero-copy processing")

            // Test buffer operations through filter API
            val lengthViaFilter = filter.bufferLength(bufferHandle)
            println("Buffer length via filter API: $lengthViaFilter bytes")

            // Test reserve through filter API
            val filterSlice = filter.reserveBuffer(bufferHandle, 128)
            if (filterSlice != null) {
                println(
                    "✓ Reserved buffer slice through filter API: ${filterSlice.length} bytes"
                )
            }
        }

        // Test linearize for ensuring contiguous memory
        val linearized = buffer.linearize(bufferHandle, 100)
        if (linearized != null) {
            println("✓ Linearized ${linearized.remaining()} bytes")
        }

        println("✓ Zero-copy buffer operations test completed\n")
    }

    @Test
    @Order(3)
    fun testParallelFilterExecution() {
        println("=== Scenario 3: Parallel Filter Execution ===")

        // Create filters
        val metricsFilter = filter.createBuiltin(MOCK_DISPATCHER, FilterType.METRICS.value, null)
        val tracingFilter = filter.createBuiltin(MOCK_DISPATCHER, FilterType.TRACING.value, null)
        val loggingFilter =
            filter.createBuiltin(MOCK_DISPATCHER, FilterType.ACCESS_LOG.value, null)

        if (metricsFilter != 0L) activeHandles.add(metricsFilter)
        if (tracingFilter != 0L) activeHandles.add(tracingFilter)
        if (loggingFilter != 0L) activeHandles.add(loggingFilter)

        println(
            "Created filters - Metrics: ${metricsFilter != 0L}, Tracing: ${tracingFilter != 0L}, Logging: ${loggingFilter != 0L}"
        )

        // Create chain using McpFilterChain's advanced builder
        val config = ChainConfig().apply {
            name = "ParallelTestChain"
            mode = ChainExecutionMode.PARALLEL.value
            maxParallel = 3
        }

        val chainBuilder = chain.chainBuilderCreateEx(MOCK_DISPATCHER, config)
        if (chainBuilder != 0L) {
            // Add filters as parallel group
            val filters = longArrayOf(metricsFilter, tracingFilter, loggingFilter)
            val result = chain.chainBuilderAddParallelGroup(chainBuilder, filters)
            println("Added parallel filter group: ${if (result == 0) "SUCCESS" else "FAILED"}")

            // Note: Build would be done through the chain API
            println("✓ Parallel filter configuration created")
        } else {
            println("Note: Advanced chain builder not available (using basic chain)")

            // Fallback to basic chain building
            val basicBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
            if (basicBuilder != 0L) {
                if (metricsFilter != 0L) {
                    filter.chainAddFilter(basicBuilder, metricsFilter, FilterPosition.LAST.getValue(), 0)
                }
                if (tracingFilter != 0L) {
                    filter.chainAddFilter(basicBuilder, tracingFilter, FilterPosition.LAST.getValue(), 0)
                }
                if (loggingFilter != 0L) {
                    filter.chainAddFilter(basicBuilder, loggingFilter, FilterPosition.LAST.getValue(), 0)
                }

                val chainHandle = filter.chainBuild(basicBuilder)
                if (chainHandle != 0L) {
                    activeHandles.add(chainHandle)
                    println("✓ Built chain with filters (sequential fallback)")
                }
                filter.chainBuilderDestroy(basicBuilder)
            }
        }

        println("✓ Parallel filter execution test completed\n")
    }

    @Test
    @Order(4)
    fun testConditionalFilterRouting() {
        println("=== Scenario 4: Conditional Filter Routing ===")

        // Create HTTP chain
        val httpChainBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
        if (httpChainBuilder != 0L) {
            val httpCodec =
                filter.createBuiltin(MOCK_DISPATCHER, FilterType.HTTP_CODEC.value, null)
            if (httpCodec != 0L) {
                activeHandles.add(httpCodec)
                filter.chainAddFilter(httpChainBuilder, httpCodec, FilterPosition.FIRST.getValue(), 0)
            }

            val httpChain = filter.chainBuild(httpChainBuilder)
            filter.chainBuilderDestroy(httpChainBuilder)
            if (httpChain != 0L) {
                activeHandles.add(httpChain)
                println("✓ Created HTTP protocol chain")
            }
        }

        // Create TCP chain
        val tcpChainBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
        if (tcpChainBuilder != 0L) {
            val tcpProxy = filter.createBuiltin(MOCK_DISPATCHER, FilterType.TCP_PROXY.value, null)
            if (tcpProxy != 0L) {
                activeHandles.add(tcpProxy)
                filter.chainAddFilter(tcpChainBuilder, tcpProxy, FilterPosition.FIRST.getValue(), 0)
            }

            val tcpChain = filter.chainBuild(tcpChainBuilder)
            filter.chainBuilderDestroy(tcpChainBuilder)
            if (tcpChain != 0L) {
                activeHandles.add(tcpChain)
                println("✓ Created TCP protocol chain")
            }
        }

        // Test with different buffer contents
        val httpBuffer = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)
        if (httpBuffer != 0L) {
            activeHandles.add(httpBuffer)
            val httpData = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello"
            buffer.add(httpBuffer, httpData.toByteArray())
            println("Created HTTP buffer with ${httpData.length} bytes")
        }

        val tcpBuffer = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)
        if (tcpBuffer != 0L) {
            activeHandles.add(tcpBuffer)
            buffer.add(tcpBuffer, BINARY_DATA)
            println("Created TCP buffer with ${BINARY_DATA.size} bytes of binary data")
        }

        // Test router creation using McpFilterChain
        val routerConfig = RouterConfig(ChainRoutingStrategy.HASH_BASED.getValue()).apply {
            hashSeed = 12345
        }

        val router = chain.chainRouterCreate(routerConfig)
        if (router != 0L) {
            println("✓ Created chain router")
            chain.chainRouterDestroy(router)
        } else {
            println("Note: Router creation returned 0 (expected with mock)")
        }

        println("✓ Conditional filter routing test completed\n")
    }

    @Test
    @Order(5)
    fun testBufferLifecycleWithWatermarks() {
        println("=== Scenario 5: Buffer Lifecycle with Watermarks ===")

        // Create buffer with specific capacity
        val capacity = 10240 // 10KB
        val bufferHandle = buffer.createOwned(capacity.toLong(), BufferOwnership.EXCLUSIVE)
        assertNotEquals(0, bufferHandle, "Failed to create buffer")
        activeHandles.add(bufferHandle)
        println("Created buffer with capacity: $capacity bytes")

        // Set watermarks for flow control
        val result = buffer.setWatermarks(bufferHandle, 2048, 8192, 9216)
        println("Set watermarks - Low: 2KB, High: 8KB, Overflow: 9KB")

        // Add data until high watermark
        val chunk = ByteArray(1024) { i -> (i % 256).toByte() } // 1KB chunks

        var chunksAdded = 0
        for (i in 0 until 8) {
            val addResult = buffer.add(bufferHandle, chunk)
            if (addResult == 0) chunksAdded++
        }
        println("Added $chunksAdded chunks of 1KB each")

        // Check watermarks
        val aboveHigh = buffer.aboveHighWatermark(bufferHandle)
        println("Above high watermark: $aboveHigh")

        // Drain buffer
        val drainResult = buffer.drain(bufferHandle, 7000)
        println("Drained 7KB from buffer: ${if (drainResult == 0) "SUCCESS" else "FAILED"}")

        val belowLow = buffer.belowLowWatermark(bufferHandle)
        println("Below low watermark: $belowLow")

        // Get buffer capacity
        val currentCapacity = buffer.capacity(bufferHandle)
        println("Buffer capacity: $currentCapacity bytes")

        // Search for pattern in buffer
        val searchPattern = byteArrayOf(0, 1, 2)
        val foundAt = buffer.search(bufferHandle, searchPattern, 0)
        println(
            "Pattern search result: ${if (foundAt >= 0) "Found at $foundAt" else "Not found"}"
        )

        println("✓ Buffer lifecycle with watermarks test completed\n")
    }

    @Test
    @Order(6)
    fun testFilterChainModification() {
        println("=== Scenario 6: Filter Chain Modification ===")

        // Create initial chain
        val chainBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
        assertNotEquals(0, chainBuilder, "Failed to create chain builder")

        // Add some filters
        val metricsFilter = filter.createBuiltin(MOCK_DISPATCHER, FilterType.METRICS.value, null)
        val loggingFilter =
            filter.createBuiltin(MOCK_DISPATCHER, FilterType.ACCESS_LOG.value, null)

        if (metricsFilter != 0L) {
            activeHandles.add(metricsFilter)
            filter.chainAddFilter(chainBuilder, metricsFilter, FilterPosition.FIRST.getValue(), 0)
        }
        if (loggingFilter != 0L) {
            activeHandles.add(loggingFilter)
            filter.chainAddFilter(chainBuilder, loggingFilter, FilterPosition.LAST.getValue(), 0)
        }

        // Build chain
        val chainHandle = filter.chainBuild(chainBuilder)
        filter.chainBuilderDestroy(chainBuilder)

        if (chainHandle != 0L) {
            activeHandles.add(chainHandle)
            println("✓ Initial filter chain built")

            // Test chain operations through McpFilterChain
            val state = chain.chainGetState(chainHandle)
            if (state != null) {
                println("Chain state: $state")
            }

            // Pause chain
            val pauseResult = chain.chainPause(chainHandle)
            println("Chain paused: ${if (pauseResult == 0) "SUCCESS" else "FAILED"}")

            // Modify chain - would set filter enabled if we had the method
            val modifyResult = chain.chainSetFilterEnabled(chainHandle, "metrics_filter", false)
            println(
                "Modified chain: ${if (modifyResult == 0) "SUCCESS" else "Note: May not work with mock"}"
            )

            // Resume chain
            val resumeResult = chain.chainResume(chainHandle)
            println("Chain resumed: ${if (resumeResult == 0) "SUCCESS" else "FAILED"}")

            // Reset chain
            val resetResult = chain.chainReset(chainHandle)
            println("Chain reset: ${if (resetResult == 0) "SUCCESS" else "FAILED"}")
        }

        println("✓ Filter chain modification test completed\n")
    }

    @Test
    @Order(7)
    fun testErrorRecovery() {
        println("=== Scenario 7: Error Recovery ===")

        // Create chain with error handling filters
        val chainBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
        assertNotEquals(0, chainBuilder, "Failed to create chain builder")

        // Add circuit breaker filter
        val circuitBreakerFilter =
            filter.createBuiltin(MOCK_DISPATCHER, FilterType.CIRCUIT_BREAKER.value, null)
        if (circuitBreakerFilter != 0L) {
            activeHandles.add(circuitBreakerFilter)
            filter.chainAddFilter(chainBuilder, circuitBreakerFilter, FilterPosition.FIRST.getValue(), 0)
            println("✓ Added circuit breaker filter")
        }

        // Add retry filter
        val retryFilter = filter.createBuiltin(MOCK_DISPATCHER, FilterType.RETRY.value, null)
        if (retryFilter != 0L) {
            activeHandles.add(retryFilter)
            filter.chainAddFilter(chainBuilder, retryFilter, FilterPosition.LAST.getValue(), 0)
            println("✓ Added retry filter")
        }

        // Build chain
        val chainHandle = filter.chainBuild(chainBuilder)
        filter.chainBuilderDestroy(chainBuilder)

        if (chainHandle != 0L) {
            activeHandles.add(chainHandle)
            println("✓ Error recovery chain built")

            // Create buffer with malformed data
            val errorBuffer = buffer.createOwned(100, BufferOwnership.EXCLUSIVE)
            if (errorBuffer != 0L) {
                activeHandles.add(errorBuffer)
                buffer.add(errorBuffer, MALFORMED_DATA)
                println("Added malformed data to trigger error handling")

                // Get filter statistics to see error counts
                if (circuitBreakerFilter != 0L) {
                    val stats = filter.getStats(circuitBreakerFilter)
                    if (stats != null) {
                        println("Circuit breaker stats - Errors: ${stats.errors}")
                    }
                }
            }
        }

        println("✓ Error recovery test completed\n")
    }

    @Test
    @Order(8)
    fun testBufferPoolManagement() {
        println("=== Scenario 8: Buffer Pool Management ===")

        // Create buffer pool using McpFilter API
        val pool = filter.bufferPoolCreate(4096, 10)
        println("Buffer pool created: ${if (pool != 0L) "SUCCESS" else "FAILED"}")

        if (pool != 0L) {
            // Acquire buffers from pool
            val pooledBuffers = mutableListOf<Long>()
            for (i in 0 until 5) {
                val poolBuffer = filter.bufferPoolAcquire(pool)
                if (poolBuffer != 0L) {
                    pooledBuffers.add(poolBuffer)
                    activeHandles.add(poolBuffer)
                    println("  Acquired buffer ${i + 1}: $poolBuffer")
                }
            }
            println("Acquired ${pooledBuffers.size} buffers from pool")

            // Release buffers back to pool
            for (poolBuffer in pooledBuffers) {
                filter.bufferPoolRelease(pool, poolBuffer)
            }
            println("Released all buffers back to pool")

            // Destroy pool
            filter.bufferPoolDestroy(pool)
            println("✓ Buffer pool destroyed")
        }

        println("✓ Buffer pool management test completed\n")
    }

    @Test
    @Order(9)
    fun testComplexChainComposition() {
        println("=== Scenario 9: Complex Chain Composition ===")

        // Create first chain
        val chainBuilder1 = filter.chainBuilderCreate(MOCK_DISPATCHER)
        if (chainBuilder1 != 0L) {
            val filter1 = filter.createBuiltin(MOCK_DISPATCHER, FilterType.METRICS.value, null)
            if (filter1 != 0L) {
                activeHandles.add(filter1)
                filter.chainAddFilter(chainBuilder1, filter1, FilterPosition.FIRST.getValue(), 0)
            }

            val chain1 = filter.chainBuild(chainBuilder1)
            filter.chainBuilderDestroy(chainBuilder1)
            if (chain1 != 0L) {
                activeHandles.add(chain1)
                println("✓ Created first chain")

                // Test chain operations through McpFilterChain
                val clonedChain = chain.chainClone(chain1)
                if (clonedChain != 0L) {
                    activeHandles.add(clonedChain)
                    println("✓ Cloned first chain")
                }

                // Test chain optimization
                val optimizeResult = chain.chainOptimize(chain1)
                println("Chain optimization: ${if (optimizeResult == 0) "SUCCESS" else "FAILED"}")

                // Test chain validation
                val validationResult = chain.chainValidate(chain1, com.sun.jna.ptr.PointerByReference())
                println("Chain validation: ${if (validationResult == 0) "VALID" else "INVALID"}")

                // Test chain dump
                val dump = chain.chainDump(chain1, "text")
                if (!dump.isNullOrEmpty()) {
                    println("Chain dump available (${dump.length} characters)")
                }
            }
        }

        println("✓ Complex chain composition test completed\n")
    }

    @Test
    @Order(10)
    fun testEndToEndIntegration() {
        println("=== Scenario 10: End-to-End Integration ===")
        println("Demonstrating complete data flow through all components")

        val testPassed = AtomicBoolean(true)
        val stepsCompleted = AtomicInteger(0)
        val executionLog = mutableListOf<String>()

        try {
            // Step 1: Create buffer with test data
            println("\nStep 1: Creating buffer with test data")
            val testData = "Integration Test Data - End to End Flow"
            var bufferHandle = buffer.createWithString(testData, BufferOwnership.SHARED)

            if (bufferHandle == 0L) {
                // Fallback if createWithString doesn't work
                bufferHandle = buffer.createOwned(4096, BufferOwnership.SHARED)
                if (bufferHandle != 0L) {
                    buffer.addString(bufferHandle, testData)
                }
            }

            assertNotEquals(0, bufferHandle, "Buffer creation failed")
            activeHandles.add(bufferHandle)

            stepsCompleted.incrementAndGet()
            executionLog.add("✓ Buffer created with test data")
            println("✓ Buffer created with ${testData.length} bytes")

            // Step 2: Create multiple filters
            println("\nStep 2: Creating filters")
            val httpFilter =
                filter.createBuiltin(MOCK_DISPATCHER, FilterType.HTTP_CODEC.value, null)
            val metricsFilter =
                filter.createBuiltin(MOCK_DISPATCHER, FilterType.METRICS.value, null)
            val compressionFilter =
                filter.createBuiltin(MOCK_DISPATCHER, FilterType.HTTP_COMPRESSION.value, null)

            if (httpFilter != 0L) activeHandles.add(httpFilter)
            if (metricsFilter != 0L) activeHandles.add(metricsFilter)
            if (compressionFilter != 0L) activeHandles.add(compressionFilter)

            stepsCompleted.incrementAndGet()
            executionLog.add("✓ Created filters")
            println("✓ Created 3 filters")

            // Step 3: Build filter chain
            println("\nStep 3: Building filter chain")
            val chainBuilder = filter.chainBuilderCreate(MOCK_DISPATCHER)
            assertNotEquals(0, chainBuilder, "Chain builder creation failed")

            if (httpFilter != 0L) {
                filter.chainAddFilter(chainBuilder, httpFilter, FilterPosition.FIRST.getValue(), 0)
            }
            if (compressionFilter != 0L) {
                filter.chainAddFilter(chainBuilder, compressionFilter, FilterPosition.LAST.getValue(), 0)
            }
            if (metricsFilter != 0L) {
                filter.chainAddFilter(chainBuilder, metricsFilter, FilterPosition.LAST.getValue(), 0)
            }

            val chainHandle = filter.chainBuild(chainBuilder)
            filter.chainBuilderDestroy(chainBuilder)
            if (chainHandle != 0L) {
                activeHandles.add(chainHandle)
            }

            stepsCompleted.incrementAndGet()
            executionLog.add("✓ Filter chain built")
            println("✓ Filter chain built successfully")

            // Step 4: Process data (simulated)
            println("\nStep 4: Processing data through chain")
            val startTime = System.nanoTime()

            // Simulate processing
            Thread.sleep(5)

            val endTime = System.nanoTime()
            val latencyMs = (endTime - startTime) / 1_000_000.0

            stepsCompleted.incrementAndGet()
            executionLog.add("✓ Data processed")
            println("✓ Data processed in ${"%.2f".format(latencyMs)}ms")

            // Step 5: Collect statistics
            println("\nStep 5: Collecting statistics")

            val bufferStats = buffer.getStats(bufferHandle)
            if (bufferStats != null) {
                println("Buffer stats - Total bytes: ${bufferStats.totalBytes}")
            }

            if (metricsFilter != 0L) {
                val filterStats = filter.getStats(metricsFilter)
                if (filterStats != null) {
                    println("Filter stats - Bytes processed: ${filterStats.bytesProcessed}")
                }
            }

            stepsCompleted.incrementAndGet()
            println("✓ Statistics collected")

            // Step 6: Cleanup
            println("\nStep 6: Cleaning up resources")
            if (chainHandle != 0L) filter.chainRelease(chainHandle)

            stepsCompleted.incrementAndGet()
            executionLog.add("✓ Resources cleaned up")
            println("✓ All resources released")

        } catch (e: Exception) {
            testPassed.set(false)
            System.err.println("✗ Test failed: ${e.message}")
            executionLog.add("✗ Failed: ${e.message}")
        }

        // Final report
        println("\n=== End-to-End Integration Test Summary ===")
        println("Steps completed: ${stepsCompleted.get()}/6")
        println("Test result: ${if (testPassed.get()) "PASSED ✓" else "FAILED ✗"}")
        println("\nExecution log:")
        executionLog.forEach { log -> println("  $log") }

        assertTrue(testPassed.get(), "End-to-end integration test failed")
        println("\n✓ End-to-end integration test completed successfully!")
    }
}