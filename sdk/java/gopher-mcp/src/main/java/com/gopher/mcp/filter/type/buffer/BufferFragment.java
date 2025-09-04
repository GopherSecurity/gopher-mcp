package com.gopher.mcp.filter.type.buffer;

import java.nio.ByteBuffer;

/**
 * External memory fragment for buffer operations
 */
public class BufferFragment {
    private ByteBuffer data;
    private long length;
    private long capacity;
    private Object userData;

    /**
     * Default constructor
     */
    public BufferFragment() {
    }

    /**
     * Constructor with all parameters
     * @param data Data buffer
     * @param length Data length
     * @param capacity Fragment capacity
     */
    public BufferFragment(ByteBuffer data, long length, long capacity) {
        this.data = data;
        this.length = length;
        this.capacity = capacity;
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

    public long getCapacity() {
        return capacity;
    }

    public void setCapacity(long capacity) {
        this.capacity = capacity;
    }

    public Object getUserData() {
        return userData;
    }

    public void setUserData(Object userData) {
        this.userData = userData;
    }
}
