package com.gopher.mcp.util

import com.sun.jna.Native
import com.sun.jna.Pointer
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Utility functions for buffer operations
 */
object BufferUtils {
    
    /**
     * Convert JNA Pointer to ByteBuffer
     *
     * @param pointer JNA Pointer
     * @param size Size in bytes
     * @return ByteBuffer or empty buffer if null/invalid
     */
    @JvmStatic
    fun toByteBuffer(pointer: Pointer?, size: Long): ByteBuffer {
        if (pointer == null || size <= 0) {
            return ByteBuffer.allocate(0)
        }
        return pointer.getByteBuffer(0, size)
    }

    /**
     * Convert ByteBuffer to JNA Pointer
     *
     * @param buffer ByteBuffer to convert
     * @return JNA Pointer or null if buffer is null
     */
    @JvmStatic
    fun toPointer(buffer: ByteBuffer?): Pointer? {
        if (buffer == null) {
            return null
        }
        return if (buffer.isDirect) {
            Native.getDirectBufferPointer(buffer)
        } else {
            // For heap buffers, copy to direct buffer
            val direct = ByteBuffer.allocateDirect(buffer.remaining())
            direct.put(buffer.duplicate())
            direct.flip()
            Native.getDirectBufferPointer(direct)
        }
    }
    
    /**
     * Convert byte array to JNA Pointer
     *
     * @param data Byte array to convert
     * @return JNA Pointer or null if data is null/empty
     */
    @JvmStatic
    fun toPointer(data: ByteArray?): Pointer? {
        if (data == null || data.isEmpty()) {
            return null
        }
        val direct = ByteBuffer.allocateDirect(data.size)
        direct.put(data)
        direct.flip()
        return Native.getDirectBufferPointer(direct)
    }
    
    /**
     * Create a direct ByteBuffer with native byte order
     */
    @JvmStatic
    fun allocateDirect(capacity: Int): ByteBuffer {
        return ByteBuffer.allocateDirect(capacity).order(ByteOrder.nativeOrder())
    }
    
    /**
     * Copy data between ByteBuffers
     */
    @JvmStatic
    fun copy(src: ByteBuffer, dest: ByteBuffer, length: Int = src.remaining()): Int {
        val actualLength = minOf(length, src.remaining(), dest.remaining())
        
        if (src.hasArray() && dest.hasArray()) {
            // Array-backed buffers - use array copy
            System.arraycopy(
                src.array(), src.arrayOffset() + src.position(),
                dest.array(), dest.arrayOffset() + dest.position(),
                actualLength
            )
            src.position(src.position() + actualLength)
            dest.position(dest.position() + actualLength)
        } else {
            // Direct buffers - use bulk get/put
            val oldLimit = src.limit()
            src.limit(src.position() + actualLength)
            dest.put(src)
            src.limit(oldLimit)
        }
        
        return actualLength
    }
    
    /**
     * Convert ByteBuffer to byte array
     */
    @JvmStatic
    fun toByteArray(buffer: ByteBuffer): ByteArray {
        val array = ByteArray(buffer.remaining())
        val position = buffer.position()
        buffer.get(array)
        buffer.position(position) // Restore position
        return array
    }
    
    /**
     * Convert byte array to ByteBuffer
     */
    @JvmStatic
    fun toByteBuffer(array: ByteArray, direct: Boolean = false): ByteBuffer {
        return if (direct) {
            val buffer = allocateDirect(array.size)
            buffer.put(array)
            buffer.flip()
            buffer
        } else {
            ByteBuffer.wrap(array)
        }
    }
    
    /**
     * Slice a ByteBuffer
     */
    @JvmStatic
    fun slice(buffer: ByteBuffer, offset: Int, length: Int): ByteBuffer {
        val position = buffer.position()
        val limit = buffer.limit()
        
        buffer.position(offset)
        buffer.limit(offset + length)
        val slice = buffer.slice()
        
        buffer.position(position)
        buffer.limit(limit)
        
        return slice
    }
    
    /**
     * Compare two ByteBuffers
     */
    @JvmStatic
    fun compare(a: ByteBuffer, b: ByteBuffer): Int {
        val length = minOf(a.remaining(), b.remaining())
        
        for (i in 0 until length) {
            val diff = a.get(a.position() + i).compareTo(b.get(b.position() + i))
            if (diff != 0) return diff
        }
        
        return a.remaining().compareTo(b.remaining())
    }
    
    /**
     * Fill a ByteBuffer with a value
     */
    @JvmStatic
    fun fill(buffer: ByteBuffer, value: Byte) {
        if (buffer.hasArray()) {
            val array = buffer.array()
            val offset = buffer.arrayOffset() + buffer.position()
            val length = buffer.remaining()
            array.fill(value, offset, offset + length)
            buffer.position(buffer.limit())
        } else {
            while (buffer.hasRemaining()) {
                buffer.put(value)
            }
        }
    }
    
    /**
     * Clear a ByteBuffer (fill with zeros)
     */
    @JvmStatic
    fun clear(buffer: ByteBuffer) {
        fill(buffer, 0)
    }
    
    /**
     * Create a read-only view of a ByteBuffer
     */
    @JvmStatic
    fun readOnly(buffer: ByteBuffer): ByteBuffer {
        return buffer.asReadOnlyBuffer()
    }
    
    /**
     * Duplicate a ByteBuffer
     */
    @JvmStatic
    fun duplicate(buffer: ByteBuffer): ByteBuffer {
        return buffer.duplicate()
    }
    
    /**
     * Check if two ByteBuffers have the same content
     */
    @JvmStatic
    fun equals(a: ByteBuffer, b: ByteBuffer): Boolean {
        if (a.remaining() != b.remaining()) return false
        return compare(a, b) == 0
    }
    
    /**
     * Convert ByteBuffer to hex string
     */
    @JvmStatic
    fun toHexString(buffer: ByteBuffer): String {
        val sb = StringBuilder()
        val position = buffer.position()
        
        while (buffer.hasRemaining()) {
            sb.append(String.format("%02x", buffer.get()))
        }
        
        buffer.position(position)
        return sb.toString()
    }
    
    /**
     * Merge multiple ByteBuffers into one
     */
    @JvmStatic
    fun merge(vararg buffers: ByteBuffer): ByteBuffer {
        val totalSize = buffers.sumOf { it.remaining() }
        val result = allocateDirect(totalSize)
        
        buffers.forEach { buffer ->
            result.put(buffer)
        }
        
        result.flip()
        return result
    }
}