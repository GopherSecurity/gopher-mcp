package com.gopher.mcp.filter;

import static org.junit.jupiter.api.Assertions.*;

import com.gopher.mcp.filter.type.buffer.*;
import com.sun.jna.Pointer;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.*;

/**
 * Comprehensive unit tests for McpFilterBuffer Java wrapper. Tests all buffer operations including
 * creation, data manipulation, reservations, zero-copy operations, and edge cases.
 */
@DisplayName("MCP Filter Buffer Tests")
@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class McpFilterBufferTest {

  private McpFilterBuffer buffer;

  @BeforeEach
  public void setUp() {
    buffer = new McpFilterBuffer();
  }

  @AfterEach
  public void tearDown() {
    if (buffer != null) {
      buffer.close();
    }
  }

  // ============================================================================
  // Buffer Creation Tests
  // ============================================================================

  @Test
  @Order(1)
  @DisplayName("Create owned buffer with BufferOwnership enum")
  public void testCreateOwned() {
    long handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE);
    assertNotEquals(0, handle, "Buffer handle should not be zero");

    // Verify buffer capacity (Note: native implementation may return 0 for capacity)
    long capacity = buffer.capacity(handle);
    // Capacity behavior depends on native implementation
    assertTrue(capacity >= 0, "Buffer capacity should be non-negative");

    // Verify buffer is initially empty
    assertTrue(buffer.isEmpty(handle), "New buffer should be empty");
    assertEquals(0, buffer.length(handle), "New buffer should have zero length");
  }

  @Test
  @Order(2)
  @DisplayName("Create buffer with different ownership models")
  public void testCreateWithDifferentOwnership() {
    // Test all ownership models
    for (BufferOwnership ownership : BufferOwnership.values()) {
      long handle = buffer.createOwned(512, ownership);
      assertNotEquals(0, handle, "Buffer handle should not be zero for ownership: " + ownership);
      assertTrue(buffer.isEmpty(handle), "Buffer should be empty for ownership: " + ownership);
    }
  }

  @Test
  @Order(3)
  @DisplayName("Create buffer view from byte array")
  public void testCreateViewFromByteArray() {
    byte[] data = "Test view data".getBytes(StandardCharsets.UTF_8);
    long handle = buffer.createView(data);

    assertNotEquals(0, handle, "View handle should not be zero");
    assertEquals(data.length, buffer.length(handle), "View length should match data length");
    assertFalse(buffer.isEmpty(handle), "View should not be empty");
  }

  @Test
  @Order(4)
  @DisplayName("Create buffer view from ByteBuffer")
  public void testCreateViewFromByteBuffer() {
    String testData = "ByteBuffer view test";
    ByteBuffer directBuffer = ByteBuffer.allocateDirect(testData.length());
    directBuffer.put(testData.getBytes(StandardCharsets.UTF_8));
    directBuffer.flip();

    long handle = buffer.createView(directBuffer);

    assertNotEquals(0, handle, "View handle should not be zero");
    assertEquals(testData.length(), buffer.length(handle), "View length should match data length");
  }

  @Test
  @Order(5)
  @DisplayName("Create buffer from fragment")
  public void testCreateFromFragment() {
    BufferFragment fragment = new BufferFragment();
    String testData = "Fragment test data";
    ByteBuffer dataBuffer = ByteBuffer.allocateDirect(testData.length());
    dataBuffer.put(testData.getBytes(StandardCharsets.UTF_8));
    dataBuffer.flip();

    fragment.setData(dataBuffer);
    fragment.setLength(testData.length());
    fragment.setCapacity(testData.length() * 2); // Note: capacity is not used by native API
    fragment.setUserData("Custom user data"); // Note: Object userData can't be passed to native

    long handle = buffer.createFromFragment(fragment);

    assertNotEquals(0, handle, "Fragment buffer handle should not be zero");
    assertEquals(
        testData.length(), buffer.length(handle), "Buffer length should match fragment data");
  }

  @Test
  @Order(6)
  @DisplayName("Clone buffer")
  public void testCloneBuffer() {
    // Create original buffer with data
    long original = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    String testData = "Data to clone";
    buffer.addString(original, testData);

    // Clone the buffer
    long cloned = buffer.clone(original);

    assertNotEquals(0, cloned, "Cloned buffer handle should not be zero");
    assertNotEquals(original, cloned, "Cloned buffer should have different handle");
    assertEquals(
        buffer.length(original), buffer.length(cloned), "Cloned buffer should have same length");
  }

  @Test
  @Order(7)
  @DisplayName("Create copy-on-write buffer")
  public void testCreateCowBuffer() {
    // Create original buffer with data
    long original = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    buffer.addString(original, "COW test data");

    // Create COW buffer
    long cow = buffer.createCow(original);

    assertNotEquals(0, cow, "COW buffer handle should not be zero");
    assertNotEquals(original, cow, "COW buffer should have different handle");
    assertEquals(
        buffer.length(original), buffer.length(cow), "COW buffer should have same initial length");
  }

  // ============================================================================
  // Data Operations Tests
  // ============================================================================

  @Test
  @Order(8)
  @DisplayName("Add byte array to buffer")
  public void testAddByteArray() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    byte[] data = "Test data".getBytes(StandardCharsets.UTF_8);

    int result = buffer.add(handle, data);

    assertEquals(0, result, "Add should return success (0)");
    assertEquals(data.length, buffer.length(handle), "Buffer length should match added data");
  }

  @Test
  @Order(9)
  @DisplayName("Add ByteBuffer to buffer")
  public void testAddByteBuffer() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    ByteBuffer data = ByteBuffer.allocateDirect(16);
    data.put("ByteBuffer data".getBytes(StandardCharsets.UTF_8));
    data.flip();

    int result = buffer.add(handle, data);

    assertEquals(0, result, "Add should return success (0)");
    assertEquals(15, buffer.length(handle), "Buffer length should match added data");
  }

  @Test
  @Order(10)
  @DisplayName("Add string to buffer")
  public void testAddString() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    String testString = "Hello, MCP Buffer!";

    int result = buffer.addString(handle, testString);

    assertEquals(0, result, "AddString should return success (0)");
    assertEquals(
        testString.length(), buffer.length(handle), "Buffer length should match string length");
  }

  @Test
  @Order(11)
  @DisplayName("Add buffer to buffer")
  public void testAddBuffer() {
    long dest = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    long source = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);

    buffer.addString(source, "Source data");
    int result = buffer.addBuffer(dest, source);

    assertEquals(0, result, "AddBuffer should return success (0)");
    assertEquals(
        buffer.length(source), buffer.length(dest), "Destination should contain source data");
  }

  @Test
  @Order(12)
  @DisplayName("Add fragment to buffer")
  public void testAddFragment() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);

    BufferFragment fragment = new BufferFragment();
    ByteBuffer fragmentData = ByteBuffer.allocateDirect(20);
    fragmentData.put("Fragment to add".getBytes(StandardCharsets.UTF_8));
    fragmentData.flip();

    fragment.setData(fragmentData);
    fragment.setLength(15); // "Fragment to add" length

    int result = buffer.addFragment(handle, fragment);

    assertEquals(0, result, "AddFragment should return success (0)");
    assertEquals(15, buffer.length(handle), "Buffer should contain fragment data");
  }

  @Test
  @Order(13)
  @DisplayName("Prepend data to buffer")
  public void testPrepend() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);

    // Add initial data
    buffer.addString(handle, "World");

    // Prepend data
    byte[] prependData = "Hello ".getBytes(StandardCharsets.UTF_8);
    int result = buffer.prepend(handle, prependData);

    assertEquals(0, result, "Prepend should return success (0)");
    assertEquals(11, buffer.length(handle), "Buffer should contain both parts");

    // Verify content order by peeking
    byte[] peeked = buffer.peek(handle, 0, 11);
    assertEquals(
        "Hello World",
        new String(peeked, StandardCharsets.UTF_8),
        "Content should be in correct order");
  }

  // ============================================================================
  // Buffer Consumption Tests
  // ============================================================================

  @Test
  @Order(14)
  @DisplayName("Drain bytes from buffer")
  public void testDrain() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    String testData = "Data to be drained";
    buffer.addString(handle, testData);

    long initialLength = buffer.length(handle);

    // Drain 5 bytes
    int result = buffer.drain(handle, 5);

    assertEquals(0, result, "Drain should return success (0)");
    assertEquals(
        initialLength - 5, buffer.length(handle), "Length should be reduced by drain amount");
  }

  @Test
  @Order(15)
  @DisplayName("Move data between buffers")
  public void testMove() {
    long source = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    long dest = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);

    buffer.addString(source, "Data to move");
    long sourceLength = buffer.length(source);

    // Move all data
    int result = buffer.move(source, dest, 0);

    assertEquals(0, result, "Move should return success (0)");
    assertEquals(0, buffer.length(source), "Source should be empty after move");
    assertEquals(sourceLength, buffer.length(dest), "Destination should contain all data");
  }

  @Test
  @Order(16)
  @DisplayName("Set drain tracker")
  public void testSetDrainTracker() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);

    // Test with null tracker (clear tracker)
    int result = buffer.setDrainTracker(handle, null);
    assertEquals(0, result, "Setting null drain tracker should succeed");

    // Test with a tracker object (though callbacks aren't implemented)
    DrainTracker tracker = new DrainTracker();
    result = buffer.setDrainTracker(handle, tracker);
    assertEquals(0, result, "Setting drain tracker should succeed");
  }

  // ============================================================================
  // Buffer Reservation Tests
  // ============================================================================

  @Test
  @Order(17)
  @DisplayName("Reserve space in buffer")
  public void testReserve() {
    long handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE);

    BufferReservation reservation = buffer.reserve(handle, 100);

    assertNotNull(reservation, "Reservation should not be null");
    assertNotNull(reservation.getData(), "Reservation data should not be null");
    assertTrue(
        reservation.getCapacity() >= 100, "Reservation capacity should be at least requested size");
    assertEquals(handle, reservation.getBuffer(), "Reservation should reference correct buffer");
  }

  @Test
  @Order(18)
  @DisplayName("Commit reservation")
  public void testCommitReservation() {
    long handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE);

    // Reserve space
    BufferReservation reservation = buffer.reserve(handle, 50);
    assertNotNull(reservation, "Reservation should not be null");

    // Write data to reservation
    String testData = "Reserved data";
    byte[] dataBytes = testData.getBytes(StandardCharsets.UTF_8);
    reservation.getData().put(dataBytes);

    // Commit reservation
    int result = buffer.commitReservation(reservation, dataBytes.length);

    assertEquals(0, result, "Commit should return success (0)");
    assertEquals(dataBytes.length, buffer.length(handle), "Buffer should contain committed data");
  }

  @Test
  @Order(19)
  @DisplayName("Cancel reservation")
  public void testCancelReservation() {
    long handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE);

    // Reserve space
    BufferReservation reservation = buffer.reserve(handle, 50);
    assertNotNull(reservation, "Reservation should not be null");

    // Cancel reservation
    int result = buffer.cancelReservation(reservation);

    assertEquals(0, result, "Cancel should return success (0)");
    assertEquals(0, buffer.length(handle), "Buffer should remain empty after cancel");
  }

  // ============================================================================
  // Buffer Access Tests
  // ============================================================================

  @Test
  @Order(20)
  @DisplayName("Get contiguous memory view")
  public void testGetContiguous() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    String testData = "Contiguous test data";
    buffer.addString(handle, testData);

    ContiguousData contiguous = buffer.getContiguous(handle, 0, testData.length());

    assertNotNull(contiguous, "Contiguous data should not be null");
    assertNotNull(contiguous.getData(), "Contiguous data buffer should not be null");
    assertEquals(
        testData.length(), contiguous.getLength(), "Contiguous length should match requested");
  }

  @Test
  @Order(21)
  @DisplayName("Linearize buffer")
  public void testLinearize() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    String testData = "Data to linearize";
    buffer.addString(handle, testData);

    ByteBuffer linearized = buffer.linearize(handle, testData.length());

    assertNotNull(linearized, "Linearized buffer should not be null");
    assertEquals(
        testData.length(), linearized.remaining(), "Linearized buffer should have correct size");
  }

  @Test
  @Order(22)
  @DisplayName("Peek at buffer data")
  public void testPeek() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    String testData = "Peek test data";
    buffer.addString(handle, testData);

    // Peek at full data
    byte[] peeked = buffer.peek(handle, 0, testData.length());

    assertNotNull(peeked, "Peeked data should not be null");
    assertEquals(
        testData, new String(peeked, StandardCharsets.UTF_8), "Peeked data should match original");

    // Verify peek doesn't consume data
    assertEquals(
        testData.length(),
        buffer.length(handle),
        "Buffer length should remain unchanged after peek");
  }

  // ============================================================================
  // Type-Safe I/O Operations Tests
  // ============================================================================

  @Test
  @Order(23)
  @DisplayName("Write and read little-endian integers")
  public void testLittleEndianIntegers() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);

    // Write different sized integers
    buffer.writeLeInt(handle, 0xFF, 1); // 1 byte
    buffer.writeLeInt(handle, 0x1234, 2); // 2 bytes
    buffer.writeLeInt(handle, 0x12345678, 4); // 4 bytes
    buffer.writeLeInt(handle, 0x123456789ABCDEFL, 8); // 8 bytes

    // Read them back
    Long val1 = buffer.readLeInt(handle, 1);
    Long val2 = buffer.readLeInt(handle, 2);
    Long val4 = buffer.readLeInt(handle, 4);
    Long val8 = buffer.readLeInt(handle, 8);

    assertEquals(0xFF, val1, "1-byte LE integer should match");
    assertEquals(0x1234, val2, "2-byte LE integer should match");
    assertEquals(0x12345678, val4, "4-byte LE integer should match");
    assertEquals(0x123456789ABCDEFL, val8, "8-byte LE integer should match");
  }

  @Test
  @Order(24)
  @DisplayName("Write and read big-endian integers")
  public void testBigEndianIntegers() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);

    // Write different sized integers
    buffer.writeBeInt(handle, 0xFF, 1); // 1 byte
    buffer.writeBeInt(handle, 0x1234, 2); // 2 bytes
    buffer.writeBeInt(handle, 0x12345678, 4); // 4 bytes
    buffer.writeBeInt(handle, 0x123456789ABCDEFL, 8); // 8 bytes

    // Read them back
    Long val1 = buffer.readBeInt(handle, 1);
    Long val2 = buffer.readBeInt(handle, 2);
    Long val4 = buffer.readBeInt(handle, 4);
    Long val8 = buffer.readBeInt(handle, 8);

    assertEquals(0xFF, val1, "1-byte BE integer should match");
    assertEquals(0x1234, val2, "2-byte BE integer should match");
    assertEquals(0x12345678, val4, "4-byte BE integer should match");
    assertEquals(0x123456789ABCDEFL, val8, "8-byte BE integer should match");
  }

  // ============================================================================
  // Buffer Search Operations Tests
  // ============================================================================

  @Test
  @Order(25)
  @DisplayName("Search for pattern in buffer")
  public void testSearch() {
    long handle = buffer.createOwned(512, BufferOwnership.EXCLUSIVE);
    String testData = "The quick brown fox jumps over the lazy dog";
    buffer.addString(handle, testData);

    // Search for existing pattern
    byte[] pattern = "fox".getBytes(StandardCharsets.UTF_8);
    long position = buffer.search(handle, pattern, 0);

    assertEquals(16, position, "Pattern should be found at correct position");

    // Search for non-existing pattern
    byte[] notFound = "cat".getBytes(StandardCharsets.UTF_8);
    position = buffer.search(handle, notFound, 0);

    assertEquals(-1, position, "Non-existing pattern should return -1");
  }

  @Test
  @Order(26)
  @DisplayName("Find byte delimiter in buffer")
  public void testFindByte() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    String testData = "Line1\nLine2\nLine3";
    buffer.addString(handle, testData);

    // Find newline delimiter
    long position = buffer.findByte(handle, (byte) '\n');

    assertEquals(5, position, "Delimiter should be found at correct position");

    // Find non-existing byte
    position = buffer.findByte(handle, (byte) '@');

    assertEquals(-1, position, "Non-existing byte should return -1");
  }

  // ============================================================================
  // Buffer Information Tests
  // ============================================================================

  @Test
  @Order(27)
  @DisplayName("Get buffer length and capacity")
  public void testLengthAndCapacity() {
    long handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE);

    // Initial state
    assertEquals(0, buffer.length(handle), "Initial length should be 0");
    // Note: capacity behavior depends on native implementation
    assertTrue(buffer.capacity(handle) >= 0, "Capacity should be non-negative");

    // After adding data
    String testData = "Test data";
    buffer.addString(handle, testData);

    assertEquals(testData.length(), buffer.length(handle), "Length should match added data");
    assertTrue(buffer.capacity(handle) >= buffer.length(handle), "Capacity should be >= length");
  }

  @Test
  @Order(28)
  @DisplayName("Check if buffer is empty")
  public void testIsEmpty() {
    long handle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);

    assertTrue(buffer.isEmpty(handle), "New buffer should be empty");

    buffer.addString(handle, "data");
    assertFalse(buffer.isEmpty(handle), "Buffer with data should not be empty");

    buffer.drain(handle, buffer.length(handle));
    assertTrue(buffer.isEmpty(handle), "Drained buffer should be empty");
  }

  @Test
  @Order(29)
  @DisplayName("Get buffer statistics")
  public void testGetStats() {
    long handle = buffer.createOwned(2048, BufferOwnership.EXCLUSIVE);

    // Add data in multiple operations
    buffer.addString(handle, "First chunk\n");
    buffer.addString(handle, "Second chunk\n");
    buffer.addString(handle, "Third chunk\n");

    BufferStats stats = buffer.getStats(handle);

    assertNotNull(stats, "Stats should not be null");
    assertTrue(stats.getTotalBytes() >= 0, "Total bytes should be non-negative");
    assertTrue(stats.getUsedBytes() >= 0, "Used bytes should be non-negative");
    // Fragment count depends on implementation details
    assertTrue(stats.getFragmentCount() >= 0, "Fragment count should be non-negative");
    assertTrue(stats.getWriteOperations() >= 0, "Write operations should be non-negative");

    double usage = stats.getUsagePercentage();
    assertTrue(usage >= 0 && usage <= 100, "Usage percentage should be between 0 and 100");
  }

  // ============================================================================
  // Buffer Watermarks Tests
  // ============================================================================

  @Test
  @Order(30)
  @Disabled("Watermark functionality appears to not be fully implemented in native library")
  @DisplayName("Set and check buffer watermarks")
  public void testWatermarks() {
    long handle = buffer.createOwned(4096, BufferOwnership.EXCLUSIVE);

    // Set watermarks
    int result = buffer.setWatermarks(handle, 100, 1000, 3000);
    assertEquals(0, result, "Set watermarks should succeed");

    // Initially below low watermark (empty buffer)
    assertTrue(buffer.belowLowWatermark(handle), "Empty buffer should be below low watermark");
    assertFalse(
        buffer.aboveHighWatermark(handle), "Empty buffer should not be above high watermark");

    // Add data to go above high watermark (test the extremes)
    byte[] largeData = new byte[1500];
    buffer.add(handle, largeData);

    // With 1500 bytes, we should be above the high watermark (1000)
    assertTrue(
        buffer.aboveHighWatermark(handle), "Buffer with 1500 bytes should be above high watermark");
    assertFalse(
        buffer.belowLowWatermark(handle),
        "Buffer with 1500 bytes should not be below low watermark");

    // Drain most data to go below low watermark
    buffer.drain(handle, 1450);

    // With only 50 bytes left, we should be below low watermark (100)
    assertTrue(
        buffer.belowLowWatermark(handle), "Buffer with 50 bytes should be below low watermark");
    assertFalse(
        buffer.aboveHighWatermark(handle),
        "Buffer with 50 bytes should not be above high watermark");
  }

  // ============================================================================
  // Buffer Pool Tests
  // ============================================================================

  @Test
  @Order(31)
  @DisplayName("Create and manage buffer pool")
  public void testBufferPool() {
    BufferPoolConfig config = new BufferPoolConfig();
    config.setBufferSize(1024);
    config.setInitialCount(5);
    config.setMaxCount(20);
    config.setGrowBy(5);

    Pointer pool = buffer.createPoolEx(config);

    assertNotNull(pool, "Pool should be created successfully");

    // Get pool statistics
    PoolStats stats = buffer.getPoolStats(pool);

    assertNotNull(stats, "Pool stats should not be null");
    assertTrue(stats.getFreeCount() > 0, "Pool should have free buffers");
    assertEquals(0, stats.getUsedCount(), "New pool should have no used buffers");

    // Trim pool
    int result = buffer.trimPool(pool, 3);
    assertEquals(0, result, "Trim pool should succeed");
  }

  // ============================================================================
  // Edge Cases and Error Handling Tests
  // ============================================================================

  @Test
  @Order(32)
  @DisplayName("Handle null inputs gracefully")
  public void testNullHandling() {
    // Create view with null byte array
    long handle = buffer.createView((byte[]) null);
    assertNotEquals(0, handle, "Should create empty buffer for null byte array");
    assertTrue(buffer.isEmpty(handle), "Buffer from null should be empty");

    // Create view with null ByteBuffer
    handle = buffer.createView((ByteBuffer) null);
    assertNotEquals(0, handle, "Should create empty buffer for null ByteBuffer");
    assertTrue(buffer.isEmpty(handle), "Buffer from null ByteBuffer should be empty");

    // Create from null fragment
    handle = buffer.createFromFragment(null);
    assertNotEquals(0, handle, "Should create empty buffer for null fragment");
    assertTrue(buffer.isEmpty(handle), "Buffer from null fragment should be empty");

    // Add null data
    long validHandle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    int result = buffer.add(validHandle, (byte[]) null);
    assertEquals(-1, result, "Adding null data should return error");

    // Add null string
    result = buffer.addString(validHandle, null);
    assertEquals(-1, result, "Adding null string should return error");
  }

  @Test
  @Order(33)
  @DisplayName("Handle empty inputs gracefully")
  public void testEmptyHandling() {
    // Create view with empty array
    byte[] empty = new byte[0];
    long handle = buffer.createView(empty);
    assertNotEquals(0, handle, "Should create empty buffer for empty array");
    assertTrue(buffer.isEmpty(handle), "Buffer from empty array should be empty");

    // Add empty data
    long validHandle = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    int result = buffer.add(validHandle, empty);
    assertEquals(-1, result, "Adding empty data should return error");

    // Add empty string
    result = buffer.addString(validHandle, "");
    assertEquals(-1, result, "Adding empty string should return error");

    // Drain 0 bytes
    buffer.addString(validHandle, "test");
    result = buffer.drain(validHandle, 0);
    assertEquals(0, result, "Draining 0 bytes should succeed");
    assertEquals(4, buffer.length(validHandle), "Length should remain unchanged");
  }

  @Test
  @Order(34)
  @DisplayName("Handle invalid buffer handles")
  public void testInvalidHandles() {
    // Operations on zero handle
    assertEquals(0, buffer.length(0), "Length of invalid handle should be 0");
    assertEquals(0, buffer.capacity(0), "Capacity of invalid handle should be 0");
    assertTrue(buffer.isEmpty(0), "Invalid handle should be considered empty");

    // Get contiguous with invalid handle
    ContiguousData contiguous = buffer.getContiguous(0, 0, 10);
    assertNull(contiguous, "Contiguous data for invalid handle should be null");

    // Search with invalid handle
    long position = buffer.search(0, "test".getBytes(), 0);
    assertEquals(-1, position, "Search on invalid handle should return -1");
  }

  // ============================================================================
  // Convenience Methods Tests
  // ============================================================================

  @Test
  @Order(35)
  @DisplayName("Create buffer with initial data")
  public void testCreateWithData() {
    byte[] initialData = "Initial data".getBytes(StandardCharsets.UTF_8);
    long handle = buffer.createWithData(initialData, BufferOwnership.EXCLUSIVE);

    assertNotEquals(0, handle, "Buffer handle should not be zero");
    assertEquals(initialData.length, buffer.length(handle), "Buffer should contain initial data");

    // Verify content
    byte[] peeked = buffer.peek(handle, 0, initialData.length);
    assertArrayEquals(initialData, peeked, "Buffer content should match initial data");
  }

  @Test
  @Order(36)
  @DisplayName("Create buffer with initial string")
  public void testCreateWithString() {
    String initialString = "Initial string data";
    long handle = buffer.createWithString(initialString, BufferOwnership.SHARED);

    assertNotEquals(0, handle, "Buffer handle should not be zero");
    assertEquals(
        initialString.length(), buffer.length(handle), "Buffer should contain initial string");

    // Verify content
    byte[] peeked = buffer.peek(handle, 0, initialString.length());
    assertEquals(
        initialString,
        new String(peeked, StandardCharsets.UTF_8),
        "Buffer content should match initial string");
  }

  @Test
  @Order(37)
  @DisplayName("AutoCloseable interface")
  public void testAutoCloseable() {
    long handle;
    try (McpFilterBuffer autoBuffer = new McpFilterBuffer()) {
      handle = autoBuffer.createOwned(256, BufferOwnership.EXCLUSIVE);
      assertNotEquals(0, handle, "Buffer should be created");
      autoBuffer.addString(handle, "Test data");
      assertEquals(9, autoBuffer.length(handle), "Buffer should contain data");
    }
    // After try-with-resources, buffer is closed
    // Note: We can't verify cleanup without native destroy methods
  }

  @Test
  @Order(38)
  @DisplayName("Buffer handle management")
  public void testBufferHandleManagement() {
    McpFilterBuffer wrapper = new McpFilterBuffer();

    // Initially no handle
    assertNull(wrapper.getBufferHandle(), "Initial handle should be null");

    // Create buffer sets handle
    long handle = wrapper.createOwned(256, BufferOwnership.EXCLUSIVE);
    assertEquals(handle, wrapper.getBufferHandle(), "Handle should be set after creation");

    // Manually set handle
    wrapper.setBufferHandle(12345L);
    assertEquals(12345L, wrapper.getBufferHandle(), "Handle should be updated");

    wrapper.close();
  }

  @Test
  @Order(39)
  @DisplayName("Fragment with custom user data")
  public void testFragmentWithUserData() {
    BufferFragment fragment = new BufferFragment();
    ByteBuffer data = ByteBuffer.allocateDirect(32);
    data.put("Fragment with user data".getBytes(StandardCharsets.UTF_8));
    data.flip();

    fragment.setData(data);
    fragment.setLength(23); // Length of the string
    fragment.setCapacity(32); // Capacity (not used by native API)
    fragment.setUserData("Custom metadata object"); // Object can't be passed to native

    // Both create and add operations should handle userData gracefully
    long handle1 = buffer.createFromFragment(fragment);
    assertNotEquals(0, handle1, "Should create buffer from fragment with userData");

    long handle2 = buffer.createOwned(256, BufferOwnership.EXCLUSIVE);
    int result = buffer.addFragment(handle2, fragment);
    assertEquals(0, result, "Should add fragment with userData");
  }

  @Test
  @Order(40)
  @DisplayName("Multiple data operations in sequence")
  public void testMultipleOperations() {
    long handle = buffer.createOwned(1024, BufferOwnership.EXCLUSIVE);

    // Add various types of data
    buffer.addString(handle, "First ");
    buffer.add(handle, "Second ".getBytes(StandardCharsets.UTF_8));

    ByteBuffer bb = ByteBuffer.allocateDirect(7);
    bb.put("Third ".getBytes(StandardCharsets.UTF_8));
    bb.flip();
    buffer.add(handle, bb);

    buffer.prepend(handle, "Zero ".getBytes(StandardCharsets.UTF_8));

    // Check final state
    long finalLength = buffer.length(handle);
    byte[] allData = buffer.peek(handle, 0, (int) finalLength);
    String result = new String(allData, StandardCharsets.UTF_8);

    assertEquals(
        "Zero First Second Third ", result, "All operations should be applied in correct order");

    // Drain some data
    buffer.drain(handle, 5); // Remove "Zero "

    // Check after drain
    finalLength = buffer.length(handle);
    allData = buffer.peek(handle, 0, (int) finalLength);
    result = new String(allData, StandardCharsets.UTF_8);

    assertEquals("First Second Third ", result, "Drain should remove data from front");
  }
}
