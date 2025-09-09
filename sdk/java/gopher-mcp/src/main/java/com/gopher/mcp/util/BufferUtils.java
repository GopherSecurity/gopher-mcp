package com.gopher.mcp.util;

import com.sun.jna.Native;
import com.sun.jna.Pointer;
import java.nio.ByteBuffer;

/**
 * Utility class for ByteBuffer and Pointer conversions. Provides helper methods for converting
 * between Java ByteBuffer and JNA Pointer types.
 */
public final class BufferUtils {

  /** Private constructor to prevent instantiation */
  private BufferUtils() {
    throw new AssertionError("BufferUtils is a utility class and should not be instantiated");
  }

  /**
   * Convert JNA Pointer to ByteBuffer
   *
   * @param pointer JNA Pointer
   * @param size Size in bytes
   * @return ByteBuffer or empty buffer if null/invalid
   */
  public static ByteBuffer toByteBuffer(Pointer pointer, long size) {
    if (pointer == null || size <= 0) {
      return ByteBuffer.allocate(0);
    }
    return pointer.getByteBuffer(0, size);
  }

  /**
   * Convert ByteBuffer to JNA Pointer
   *
   * @param buffer ByteBuffer to convert
   * @return JNA Pointer or null if buffer is null
   */
  public static Pointer toPointer(ByteBuffer buffer) {
    if (buffer == null) {
      return null;
    }
    if (buffer.isDirect()) {
      return Native.getDirectBufferPointer(buffer);
    } else {
      // For heap buffers, copy to direct buffer
      ByteBuffer direct = ByteBuffer.allocateDirect(buffer.remaining());
      direct.put(buffer.duplicate());
      direct.flip();
      return Native.getDirectBufferPointer(direct);
    }
  }

  /**
   * Check if a ByteBuffer is direct (off-heap)
   *
   * @param buffer ByteBuffer to check
   * @return true if the buffer is direct, false otherwise
   */
  public static boolean isDirect(ByteBuffer buffer) {
    return buffer != null && buffer.isDirect();
  }

  /**
   * Create a direct ByteBuffer from a byte array
   *
   * @param data Byte array
   * @return Direct ByteBuffer containing the data
   */
  public static ByteBuffer createDirectBuffer(byte[] data) {
    if (data == null || data.length == 0) {
      return ByteBuffer.allocateDirect(0);
    }
    ByteBuffer direct = ByteBuffer.allocateDirect(data.length);
    direct.put(data);
    direct.flip();
    return direct;
  }

  /**
   * Create a direct ByteBuffer with specified capacity
   *
   * @param capacity Buffer capacity
   * @return Direct ByteBuffer with specified capacity
   */
  public static ByteBuffer createDirectBuffer(int capacity) {
    if (capacity < 0) {
      throw new IllegalArgumentException("Buffer capacity cannot be negative");
    }
    return ByteBuffer.allocateDirect(capacity);
  }

  /**
   * Copy data from a Pointer to a byte array
   *
   * @param pointer Source pointer
   * @param size Number of bytes to copy
   * @return Byte array containing the copied data, or empty array if invalid
   */
  public static byte[] toByteArray(Pointer pointer, long size) {
    if (pointer == null || size <= 0) {
      return new byte[0];
    }
    if (size > Integer.MAX_VALUE) {
      throw new IllegalArgumentException("Size exceeds maximum array size: " + size);
    }
    return pointer.getByteArray(0, (int) size);
  }

  /**
   * Get the remaining bytes in a ByteBuffer without modifying its position
   *
   * @param buffer ByteBuffer to check
   * @return Number of remaining bytes, or 0 if buffer is null
   */
  public static int remaining(ByteBuffer buffer) {
    return buffer != null ? buffer.remaining() : 0;
  }

  /**
   * Convert byte array to JNA Pointer via direct ByteBuffer
   *
   * @param data Byte array to convert
   * @return JNA Pointer or null if data is null/empty
   */
  public static Pointer toPointer(byte[] data) {
    if (data == null || data.length == 0) {
      return null;
    }
    ByteBuffer direct = ByteBuffer.allocateDirect(data.length);
    direct.put(data);
    direct.flip();
    return Native.getDirectBufferPointer(direct);
  }
}
