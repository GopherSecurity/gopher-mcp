package com.gopher.mcp.filter.type.buffer;

import java.nio.ByteBuffer;

/**
 * Represents a slice of buffer data for zero-copy operations.
 */
public class BufferSlice {

    // Buffer flags constants
    public static final int BUFFER_FLAG_READONLY = 0x01;
    public static final int BUFFER_FLAG_ZERO_COPY = 0x08;

    private ByteBuffer data;
    private long length;
    private int flags;

    /**
     * Default constructor
     */
    public BufferSlice() {
    }

    /**
     * Constructor with all parameters
     * @param data ByteBuffer containing the data
     * @param length Length of the slice
     * @param flags Buffer flags
     */
    public BufferSlice(ByteBuffer data, long length, int flags) {
        this.data = data;
        this.length = length;
        this.flags = flags;
    }

    // Getters and Setters

    public ByteBuffer getData() {
        return data;
    }

    public void setData(ByteBuffer data) {
        this.data = data;
    }

    public long getLength() {
        return length;
    }

    public void setLength(long length) {
        this.length = length;
    }

    public int getFlags() {
        return flags;
    }

    public void setFlags(int flags) {
        this.flags = flags;
    }

    /**
     * Check if the buffer slice is read-only
     * @return true if read-only
     */
    public boolean isReadOnly() {
        return (flags & BUFFER_FLAG_READONLY) != 0;
    }

    /**
     * Check if the buffer slice is zero-copy
     * @return true if zero-copy
     */
    public boolean isZeroCopy() {
        return (flags & BUFFER_FLAG_ZERO_COPY) != 0;
    }
}
