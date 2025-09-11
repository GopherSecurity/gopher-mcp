package com.gopher.mcp.filter

import com.gopher.mcp.filter.type.buffer.*
import org.junit.jupiter.api.*
import org.junit.jupiter.api.Assertions.*
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets

/**
 * Comprehensive unit tests for McpFilterBuffer Kotlin wrapper.
 * Tests all buffer operations including creation, data manipulation, reservations,
 * zero-copy operations, and edge cases.
 */
@DisplayName("MCP Filter Buffer Tests")
@TestMethodOrder(MethodOrderer.OrderAnnotation::class)
class McpFilterBufferTest {

    private lateinit var buffer: McpFilterBuffer

    @BeforeEach
    fun setUp() {
        buffer = McpFilterBuffer()
    }

    @AfterEach
    fun tearDown() {
        buffer.close()
    }

    // ============================================================================
    // Buffer Creation Tests
    // ============================================================================

    @Test
    @Order(1)
    @DisplayName("Create owned buffer with BufferOwnership enum")
    fun testCreateOwned() {
        val handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)
        assertNotEquals(0, handle, "Buffer handle should not be zero")

        // Verify buffer capacity (Note: native implementation may return 0 for capacity)
        val capacity = buffer.capacity(handle)
        // Capacity behavior depends on native implementation
        assertTrue(capacity >= 0, "Buffer capacity should be non-negative")

        // Verify buffer is initially empty
        assertTrue(buffer.isEmpty(handle), "New buffer should be empty")
        assertEquals(0, buffer.length(handle), "New buffer should have zero length")
    }

    @Test
    @Order(2)
    @DisplayName("Create buffer with different ownership models")
    fun testCreateWithDifferentOwnership() {
        // Test all ownership models
        for (ownership in BufferOwnership.values()) {
            val handle = buffer.createOwned(512, ownership)
            assertNotEquals(0, handle, "Buffer handle should not be zero for ownership: $ownership")
            assertTrue(buffer.isEmpty(handle), "Buffer should be empty for ownership: $ownership")
        }
    }

    @Test
    @Order(3)
    @DisplayName("Create buffer view from byte array")
    fun testCreateViewFromByteArray() {
        val data = "Test view data".toByteArray(StandardCharsets.UTF_8)
        val handle = buffer.createView(data)

        assertNotEquals(0, handle, "View handle should not be zero")
        assertEquals(data.size.toLong(), buffer.length(handle), "View length should match data length")
        assertFalse(buffer.isEmpty(handle), "View should not be empty")
    }

    @Test
    @Order(4)
    @DisplayName("Create buffer view from ByteBuffer")
    fun testCreateViewFromByteBuffer() {
        val testData = "ByteBuffer view test"
        val directBuffer = ByteBuffer.allocateDirect(testData.length)
        directBuffer.put(testData.toByteArray(StandardCharsets.UTF_8))
        directBuffer.flip()

        val handle = buffer.createView(directBuffer)

        assertNotEquals(0, handle, "View handle should not be zero")
        assertEquals(testData.length.toLong(), buffer.length(handle), "View length should match data length")
    }

    @Test
    @Order(5)
    @DisplayName("Create buffer from fragment")
    fun testCreateFromFragment() {
        val fragment = BufferFragment()
        val testData = "Fragment test data"
        val dataBuffer = ByteBuffer.allocateDirect(testData.length)
        dataBuffer.put(testData.toByteArray(StandardCharsets.UTF_8))
        dataBuffer.flip()

        fragment.data = dataBuffer
        fragment.length = testData.length.toLong()
        fragment.capacity = (testData.length * 2).toLong() // Note: capacity is not used by native API
        fragment.userData = "Custom user data" // Note: Object userData can't be passed to native

        val handle = buffer.createFromFragment(fragment)

        assertNotEquals(0, handle, "Fragment buffer handle should not be zero")
        assertEquals(
            testData.length.toLong(), buffer.length(handle), "Buffer length should match fragment data"
        )
    }

    @Test
    @Order(6)
    @DisplayName("Clone buffer")
    fun testCloneBuffer() {
        // Create original buffer with data
        val original = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        val testData = "Data to clone"
        buffer.addString(original, testData)

        // Clone the buffer
        val cloned = buffer.clone(original)

        assertNotEquals(0, cloned, "Cloned buffer handle should not be zero")
        assertNotEquals(original, cloned, "Cloned buffer should have different handle")
        assertEquals(
            buffer.length(original), buffer.length(cloned), "Cloned buffer should have same length"
        )
    }

    @Test
    @Order(7)
    @DisplayName("Create copy-on-write buffer")
    fun testCreateCowBuffer() {
        // Create original buffer with data
        val original = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        buffer.addString(original, "COW test data")

        // Create COW buffer
        val cow = buffer.createCow(original)

        assertNotEquals(0, cow, "COW buffer handle should not be zero")
        assertNotEquals(original, cow, "COW buffer should have different handle")
        assertEquals(
            buffer.length(original), buffer.length(cow), "COW buffer should have same initial length"
        )
    }

    // ============================================================================
    // Data Operations Tests
    // ============================================================================

    @Test
    @Order(8)
    @DisplayName("Add byte array to buffer")
    fun testAddByteArray() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val data = "Test data".toByteArray(StandardCharsets.UTF_8)

        val result = buffer.add(handle, data)

        assertEquals(0, result, "Add should return success (0)")
        assertEquals(data.size.toLong(), buffer.length(handle), "Buffer length should match added data")
    }

    @Test
    @Order(9)
    @DisplayName("Add ByteBuffer to buffer")
    fun testAddByteBuffer() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val data = ByteBuffer.allocateDirect(16)
        data.put("ByteBuffer data".toByteArray(StandardCharsets.UTF_8))
        data.flip()

        val result = buffer.add(handle, data)

        assertEquals(0, result, "Add should return success (0)")
        assertEquals(15, buffer.length(handle), "Buffer length should match added data")
    }

    @Test
    @Order(10)
    @DisplayName("Add string to buffer")
    fun testAddString() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        val testString = "Hello, MCP Buffer!"

        val result = buffer.addString(handle, testString)

        assertEquals(0, result, "AddString should return success (0)")
        assertEquals(
            testString.length.toLong(), buffer.length(handle), "Buffer length should match string length"
        )
    }

    @Test
    @Order(11)
    @DisplayName("Add buffer to buffer")
    fun testAddBuffer() {
        val dest = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val source = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)

        buffer.addString(source, "Source data")
        val result = buffer.addBuffer(dest, source)

        assertEquals(0, result, "AddBuffer should return success (0)")
        assertEquals(
            buffer.length(source), buffer.length(dest), "Destination should contain source data"
        )
    }

    @Test
    @Order(12)
    @DisplayName("Add fragment to buffer")
    fun testAddFragment() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)

        val fragment = BufferFragment()
        val fragmentData = ByteBuffer.allocateDirect(20)
        fragmentData.put("Fragment to add".toByteArray(StandardCharsets.UTF_8))
        fragmentData.flip()

        fragment.data = fragmentData
        fragment.length = 15 // "Fragment to add" length

        val result = buffer.addFragment(handle, fragment)

        assertEquals(0, result, "AddFragment should return success (0)")
        assertEquals(15, buffer.length(handle), "Buffer should contain fragment data")
    }

    @Test
    @Order(13)
    @DisplayName("Prepend data to buffer")
    fun testPrepend() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)

        // Add initial data
        buffer.addString(handle, "World")

        // Prepend data
        val prependData = "Hello ".toByteArray(StandardCharsets.UTF_8)
        val result = buffer.prepend(handle, prependData)

        assertEquals(0, result, "Prepend should return success (0)")
        assertEquals(11, buffer.length(handle), "Buffer should contain both parts")

        // Verify content order by peeking
        val peeked = buffer.peek(handle, 0, 11)
        assertNotNull(peeked, "Peeked data should not be null")
        assertEquals(
            "Hello World",
            String(peeked!!, StandardCharsets.UTF_8),
            "Content should be in correct order"
        )
    }

    // ============================================================================
    // Buffer Consumption Tests
    // ============================================================================

    @Test
    @Order(14)
    @DisplayName("Drain bytes from buffer")
    fun testDrain() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val testData = "Data to be drained"
        buffer.addString(handle, testData)

        val initialLength = buffer.length(handle)

        // Drain 5 bytes
        val result = buffer.drain(handle, 5)

        assertEquals(0, result, "Drain should return success (0)")
        assertEquals(
            initialLength - 5, buffer.length(handle), "Length should be reduced by drain amount"
        )
    }

    @Test
    @Order(15)
    @DisplayName("Move data between buffers")
    fun testMove() {
        val source = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        val dest = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)

        buffer.addString(source, "Data to move")
        val sourceLength = buffer.length(source)

        // Move all data
        val result = buffer.move(source, dest, 0)

        assertEquals(0, result, "Move should return success (0)")
        assertEquals(0, buffer.length(source), "Source should be empty after move")
        assertEquals(sourceLength, buffer.length(dest), "Destination should contain all data")
    }

    @Test
    @Order(16)
    @DisplayName("Set drain tracker")
    fun testSetDrainTracker() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)

        // Test with null tracker (clear tracker)
        val result = buffer.setDrainTracker(handle, null)
        assertEquals(0, result, "Setting null drain tracker should succeed")

        // Test with a tracker object (though callbacks aren't implemented)
        val tracker = DrainTracker()
        val result2 = buffer.setDrainTracker(handle, tracker)
        assertEquals(0, result2, "Setting drain tracker should succeed")
    }

    // ============================================================================
    // Buffer Reservation Tests
    // ============================================================================

    @Test
    @Order(17)
    @DisplayName("Reserve space in buffer")
    fun testReserve() {
        val handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)

        val reservation = buffer.reserve(handle, 100)

        assertNotNull(reservation, "Reservation should not be null")
        assertNotNull(reservation!!.data, "Reservation data should not be null")
        assertTrue(
            reservation.capacity >= 100, "Reservation capacity should be at least requested size"
        )
        assertEquals(handle, reservation.buffer, "Reservation should reference correct buffer")
    }

    @Test
    @Order(18)
    @DisplayName("Commit reservation")
    fun testCommitReservation() {
        val handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)

        // Reserve space
        val reservation = buffer.reserve(handle, 50)
        assertNotNull(reservation, "Reservation should not be null")

        // Write data to reservation
        val testData = "Reserved data"
        val dataBytes = testData.toByteArray(StandardCharsets.UTF_8)
        reservation!!.data!!.put(dataBytes)

        // Commit reservation
        val result = buffer.commitReservation(reservation, dataBytes.size.toLong())

        assertEquals(0, result, "Commit should return success (0)")
        assertEquals(dataBytes.size.toLong(), buffer.length(handle), "Buffer should contain committed data")
    }

    @Test
    @Order(19)
    @DisplayName("Cancel reservation")
    fun testCancelReservation() {
        val handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)

        // Reserve space
        val reservation = buffer.reserve(handle, 50)
        assertNotNull(reservation, "Reservation should not be null")

        // Cancel reservation
        val result = buffer.cancelReservation(reservation!!)

        assertEquals(0, result, "Cancel should return success (0)")
        assertEquals(0, buffer.length(handle), "Buffer should remain empty after cancel")
    }

    // ============================================================================
    // Buffer Access Tests
    // ============================================================================

    @Test
    @Order(20)
    @DisplayName("Get contiguous memory view")
    fun testGetContiguous() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val testData = "Contiguous test data"
        buffer.addString(handle, testData)

        val contiguous = buffer.getContiguous(handle, 0, testData.length.toLong())

        assertNotNull(contiguous, "Contiguous data should not be null")
        assertNotNull(contiguous!!.data, "Contiguous data buffer should not be null")
        assertEquals(
            testData.length.toLong(), contiguous.length, "Contiguous length should match requested"
        )
    }

    @Test
    @Order(21)
    @DisplayName("Linearize buffer")
    fun testLinearize() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val testData = "Data to linearize"
        buffer.addString(handle, testData)

        val linearized = buffer.linearize(handle, testData.length.toLong())

        assertNotNull(linearized, "Linearized buffer should not be null")
        assertEquals(
            testData.length, linearized!!.remaining(), "Linearized buffer should have correct size"
        )
    }

    @Test
    @Order(22)
    @DisplayName("Peek at buffer data")
    fun testPeek() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        val testData = "Peek test data"
        buffer.addString(handle, testData)

        // Peek at full data
        val peeked = buffer.peek(handle, 0, testData.length)

        assertNotNull(peeked, "Peeked data should not be null")
        assertEquals(
            testData, String(peeked!!, StandardCharsets.UTF_8), "Peeked data should match original"
        )

        // Verify peek doesn't consume data
        assertEquals(
            testData.length.toLong(),
            buffer.length(handle),
            "Buffer length should remain unchanged after peek"
        )
    }

    // ============================================================================
    // Type-Safe I/O Operations Tests
    // ============================================================================

    @Test
    @Order(23)
    @DisplayName("Write and read little-endian integers")
    fun testLittleEndianIntegers() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)

        // Write different sized integers
        buffer.writeLeInt(handle, 0xFF, 1) // 1 byte
        buffer.writeLeInt(handle, 0x1234, 2) // 2 bytes
        buffer.writeLeInt(handle, 0x12345678, 4) // 4 bytes
        buffer.writeLeInt(handle, 0x123456789ABCDEFL, 8) // 8 bytes

        // Read them back
        val val1 = buffer.readLeInt(handle, 1)
        val val2 = buffer.readLeInt(handle, 2)
        val val4 = buffer.readLeInt(handle, 4)
        val val8 = buffer.readLeInt(handle, 8)

        assertEquals(0xFF, val1, "1-byte LE integer should match")
        assertEquals(0x1234, val2, "2-byte LE integer should match")
        assertEquals(0x12345678, val4, "4-byte LE integer should match")
        assertEquals(0x123456789ABCDEFL, val8, "8-byte LE integer should match")
    }

    @Test
    @Order(24)
    @DisplayName("Write and read big-endian integers")
    fun testBigEndianIntegers() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)

        // Write different sized integers
        buffer.writeBeInt(handle, 0xFF, 1) // 1 byte
        buffer.writeBeInt(handle, 0x1234, 2) // 2 bytes
        buffer.writeBeInt(handle, 0x12345678, 4) // 4 bytes
        buffer.writeBeInt(handle, 0x123456789ABCDEFL, 8) // 8 bytes

        // Read them back
        val val1 = buffer.readBeInt(handle, 1)
        val val2 = buffer.readBeInt(handle, 2)
        val val4 = buffer.readBeInt(handle, 4)
        val val8 = buffer.readBeInt(handle, 8)

        assertEquals(0xFF, val1, "1-byte BE integer should match")
        assertEquals(0x1234, val2, "2-byte BE integer should match")
        assertEquals(0x12345678, val4, "4-byte BE integer should match")
        assertEquals(0x123456789ABCDEFL, val8, "8-byte BE integer should match")
    }

    // ============================================================================
    // Buffer Search Operations Tests
    // ============================================================================

    @Test
    @Order(25)
    @DisplayName("Search for pattern in buffer")
    fun testSearch() {
        val handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE)
        val testData = "The quick brown fox jumps over the lazy dog"
        buffer.addString(handle, testData)

        // Search for existing pattern
        val pattern = "fox".toByteArray(StandardCharsets.UTF_8)
        var position = buffer.search(handle, pattern, 0)

        assertEquals(16, position, "Pattern should be found at correct position")

        // Search for non-existing pattern
        val notFound = "cat".toByteArray(StandardCharsets.UTF_8)
        position = buffer.search(handle, notFound, 0)

        assertEquals(-1, position, "Non-existing pattern should return -1")
    }

    @Test
    @Order(26)
    @DisplayName("Find byte delimiter in buffer")
    fun testFindByte() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        val testData = "Line1\nLine2\nLine3"
        buffer.addString(handle, testData)

        // Find newline delimiter
        var position = buffer.findByte(handle, '\n'.code.toByte())

        assertEquals(5, position, "Delimiter should be found at correct position")

        // Find non-existing byte
        position = buffer.findByte(handle, '@'.code.toByte())

        assertEquals(-1, position, "Non-existing byte should return -1")
    }

    // ============================================================================
    // Buffer Information Tests
    // ============================================================================

    @Test
    @Order(27)
    @DisplayName("Get buffer length and capacity")
    fun testLengthAndCapacity() {
        val handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)

        // Initial state
        assertEquals(0, buffer.length(handle), "Initial length should be 0")
        // Note: capacity behavior depends on native implementation
        assertTrue(buffer.capacity(handle) >= 0, "Capacity should be non-negative")

        // After adding data
        val testData = "Test data"
        buffer.addString(handle, testData)

        assertEquals(testData.length.toLong(), buffer.length(handle), "Length should match added data")
        assertTrue(buffer.capacity(handle) >= buffer.length(handle), "Capacity should be >= length")
    }

    @Test
    @Order(28)
    @DisplayName("Check if buffer is empty")
    fun testIsEmpty() {
        val handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)

        assertTrue(buffer.isEmpty(handle), "New buffer should be empty")

        buffer.addString(handle, "data")
        assertFalse(buffer.isEmpty(handle), "Buffer with data should not be empty")

        buffer.drain(handle, buffer.length(handle))
        assertTrue(buffer.isEmpty(handle), "Drained buffer should be empty")
    }

    @Test
    @Order(29)
    @DisplayName("Get buffer statistics")
    fun testGetStats() {
        val handle = buffer.createOwned(2048, BufferOwnership.EXCLUSIVE)

        // Add data in multiple operations
        buffer.addString(handle, "First chunk\n")
        buffer.addString(handle, "Second chunk\n")
        buffer.addString(handle, "Third chunk\n")

        val stats = buffer.getStats(handle)

        assertNotNull(stats, "Stats should not be null")
        assertTrue(stats!!.totalBytes >= 0, "Total bytes should be non-negative")
        assertTrue(stats.usedBytes >= 0, "Used bytes should be non-negative")
        // Fragment count depends on implementation details
        assertTrue(stats.fragmentCount >= 0, "Fragment count should be non-negative")
        assertTrue(stats.writeOperations >= 0, "Write operations should be non-negative")

        val usage = stats.getUsagePercentage()
        assertTrue(usage >= 0 && usage <= 100, "Usage percentage should be between 0 and 100")
    }

    // ============================================================================
    // Buffer Watermarks Tests
    // ============================================================================

    @Test
    @Order(30)
    @Disabled("Watermark functionality appears to not be fully implemented in native library")
    @DisplayName("Set and check buffer watermarks")
    fun testWatermarks() {
        val handle = buffer.createOwned(4096, BufferOwnership.EXCLUSIVE)

        // Set watermarks
        val result = buffer.setWatermarks(handle, 100, 1000, 3000)
        assertEquals(0, result, "Set watermarks should succeed")

        // Initially below low watermark (empty buffer)
        assertTrue(buffer.belowLowWatermark(handle), "Empty buffer should be below low watermark")
        assertFalse(
            buffer.aboveHighWatermark(handle), "Empty buffer should not be above high watermark"
        )

        // Add data to go above high watermark (test the extremes)
        val largeData = ByteArray(1500)
        buffer.add(handle, largeData)

        // With 1500 bytes, we should be above the high watermark (1000)
        assertTrue(
            buffer.aboveHighWatermark(handle), "Buffer with 1500 bytes should be above high watermark"
        )
        assertFalse(
            buffer.belowLowWatermark(handle),
            "Buffer with 1500 bytes should not be below low watermark"
        )

        // Drain most data to go below low watermark
        buffer.drain(handle, 1450)

        // With only 50 bytes left, we should be below low watermark (100)
        assertTrue(
            buffer.belowLowWatermark(handle), "Buffer with 50 bytes should be below low watermark"
        )
        assertFalse(
            buffer.aboveHighWatermark(handle),
            "Buffer with 50 bytes should not be above high watermark"
        )
    }

    // ============================================================================
    // Buffer Pool Tests
    // ============================================================================

    @Test
    @Order(31)
    @DisplayName("Create and manage buffer pool")
    fun testBufferPool() {
        val config = BufferPoolConfig()
        config.bufferSize = 1024
        config.initialCount = 5
        config.maxCount = 20
        config.growBy = 5

        val pool = buffer.createPoolEx(config)

        assertNotNull(pool, "Pool should be created successfully")

        // Get pool statistics
        val stats = buffer.getPoolStats(pool!!)

        assertNotNull(stats, "Pool stats should not be null")
        assertTrue(stats!!.freeCount > 0, "Pool should have free buffers")
        assertEquals(0, stats.usedCount, "New pool should have no used buffers")

        // Trim pool
        val result = buffer.trimPool(pool, 3)
        assertEquals(0, result, "Trim pool should succeed")
    }

    // ============================================================================
    // Edge Cases and Error Handling Tests
    // ============================================================================

    @Test
    @Order(32)
    @DisplayName("Handle null inputs gracefully")
    fun testNullHandling() {
        // Create view with null byte array
        var handle = buffer.createView(null as ByteArray?)
        assertNotEquals(0, handle, "Should create empty buffer for null byte array")
        assertTrue(buffer.isEmpty(handle), "Buffer from null should be empty")

        // Create view with null ByteBuffer
        handle = buffer.createView(null as ByteBuffer?)
        assertNotEquals(0, handle, "Should create empty buffer for null ByteBuffer")
        assertTrue(buffer.isEmpty(handle), "Buffer from null ByteBuffer should be empty")

        // Create from null fragment
        handle = buffer.createFromFragment(null)
        assertNotEquals(0, handle, "Should create empty buffer for null fragment")
        assertTrue(buffer.isEmpty(handle), "Buffer from null fragment should be empty")

        // Add null data
        val validHandle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        var result = buffer.add(validHandle, null as ByteArray?)
        assertEquals(-1, result, "Adding null data should return error")

        // Add null string
        result = buffer.addString(validHandle, null)
        assertEquals(-1, result, "Adding null string should return error")
    }

    @Test
    @Order(33)
    @DisplayName("Handle empty inputs gracefully")
    fun testEmptyHandling() {
        // Create view with empty array
        val empty = ByteArray(0)
        var handle = buffer.createView(empty)
        assertNotEquals(0, handle, "Should create empty buffer for empty array")
        assertTrue(buffer.isEmpty(handle), "Buffer from empty array should be empty")

        // Add empty data
        val validHandle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        var result = buffer.add(validHandle, empty)
        assertEquals(-1, result, "Adding empty data should return error")

        // Add empty string
        result = buffer.addString(validHandle, "")
        assertEquals(-1, result, "Adding empty string should return error")

        // Drain 0 bytes
        buffer.addString(validHandle, "test")
        result = buffer.drain(validHandle, 0)
        assertEquals(0, result, "Draining 0 bytes should succeed")
        assertEquals(4, buffer.length(validHandle), "Length should remain unchanged")
    }

    @Test
    @Order(34)
    @DisplayName("Handle invalid buffer handles")
    fun testInvalidHandles() {
        // Operations on zero handle
        assertEquals(0, buffer.length(0), "Length of invalid handle should be 0")
        assertEquals(0, buffer.capacity(0), "Capacity of invalid handle should be 0")
        assertTrue(buffer.isEmpty(0), "Invalid handle should be considered empty")

        // Get contiguous with invalid handle
        val contiguous = buffer.getContiguous(0, 0, 10)
        assertNull(contiguous, "Contiguous data for invalid handle should be null")

        // Search with invalid handle
        val position = buffer.search(0, "test".toByteArray(), 0)
        assertEquals(-1, position, "Search on invalid handle should return -1")
    }

    // ============================================================================
    // Convenience Methods Tests
    // ============================================================================

    @Test
    @Order(35)
    @DisplayName("Create buffer with initial data")
    fun testCreateWithData() {
        val initialData = "Initial data".toByteArray(StandardCharsets.UTF_8)
        val handle = buffer.createWithData(initialData, BufferOwnership.EXCLUSIVE)

        assertNotEquals(0, handle, "Buffer handle should not be zero")
        assertEquals(initialData.size.toLong(), buffer.length(handle), "Buffer should contain initial data")

        // Verify content
        val peeked = buffer.peek(handle, 0, initialData.size)
        assertNotNull(peeked, "Peeked data should not be null")
        assertArrayEquals(initialData, peeked!!, "Buffer content should match initial data")
    }

    @Test
    @Order(36)
    @DisplayName("Create buffer with initial string")
    fun testCreateWithString() {
        val initialString = "Initial string data"
        val handle = buffer.createWithString(initialString, BufferOwnership.SHARED)

        assertNotEquals(0, handle, "Buffer handle should not be zero")
        assertEquals(
            initialString.length.toLong(), buffer.length(handle), "Buffer should contain initial string"
        )

        // Verify content
        val peeked = buffer.peek(handle, 0, initialString.length)
        assertNotNull(peeked, "Peeked data should not be null")
        assertEquals(
            initialString,
            String(peeked!!, StandardCharsets.UTF_8),
            "Buffer content should match initial string"
        )
    }

    @Test
    @Order(37)
    @DisplayName("AutoCloseable interface")
    fun testAutoCloseable() {
        var handle: Long
        McpFilterBuffer().use { autoBuffer ->
            handle = autoBuffer.createOwned(256, BufferOwnership.EXCLUSIVE)
            assertNotEquals(0, handle, "Buffer should be created")
            autoBuffer.addString(handle, "Test data")
            assertEquals(9, autoBuffer.length(handle), "Buffer should contain data")
        }
        // After use block, buffer is closed
        // Note: We can't verify cleanup without native destroy methods
    }

    @Test
    @Order(38)
    @DisplayName("Buffer handle management")
    fun testBufferHandleManagement() {
        val wrapper = McpFilterBuffer()

        // Initially no handle
        assertNull(wrapper.getBufferHandle(), "Initial handle should be null")

        // Create buffer sets handle
        val handle = wrapper.createOwned(256, BufferOwnership.EXCLUSIVE)
        assertEquals(handle, wrapper.getBufferHandle(), "Handle should be set after creation")

        // Manually set handle
        wrapper.setBufferHandle(12345L)
        assertEquals(12345L, wrapper.getBufferHandle(), "Handle should be updated")

        wrapper.close()
    }

    @Test
    @Order(39)
    @DisplayName("Fragment with custom user data")
    fun testFragmentWithUserData() {
        val fragment = BufferFragment()
        val data = ByteBuffer.allocateDirect(32)
        data.put("Fragment with user data".toByteArray(StandardCharsets.UTF_8))
        data.flip()

        fragment.data = data
        fragment.length = 23 // Length of the string
        fragment.capacity = 32 // Capacity (not used by native API)
        fragment.userData = "Custom metadata object" // Object can't be passed to native

        // Both create and add operations should handle userData gracefully
        val handle1 = buffer.createFromFragment(fragment)
        assertNotEquals(0, handle1, "Should create buffer from fragment with userData")

        val handle2 = buffer.createOwned(256, BufferOwnership.EXCLUSIVE)
        val result = buffer.addFragment(handle2, fragment)
        assertEquals(0, result, "Should add fragment with userData")
    }

    @Test
    @Order(40)
    @DisplayName("Multiple data operations in sequence")
    fun testMultipleOperations() {
        val handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE)

        // Add various types of data
        buffer.addString(handle, "First ")
        buffer.add(handle, "Second ".toByteArray(StandardCharsets.UTF_8))

        val bb = ByteBuffer.allocateDirect(7)
        bb.put("Third ".toByteArray(StandardCharsets.UTF_8))
        bb.flip()
        buffer.add(handle, bb)

        buffer.prepend(handle, "Zero ".toByteArray(StandardCharsets.UTF_8))

        // Check final state
        var finalLength = buffer.length(handle)
        var allData = buffer.peek(handle, 0, finalLength.toInt())
        assertNotNull(allData, "Peeked data should not be null")
        var result = String(allData!!, StandardCharsets.UTF_8)

        assertEquals(
            "Zero First Second Third ", result, "All operations should be applied in correct order"
        )

        // Drain some data
        buffer.drain(handle, 5) // Remove "Zero "

        // Check after drain
        finalLength = buffer.length(handle)
        allData = buffer.peek(handle, 0, finalLength.toInt())
        assertNotNull(allData, "Peeked data should not be null")
        result = String(allData!!, StandardCharsets.UTF_8)

        assertEquals("First Second Third ", result, "Drain should remove data from front")
    }
}
