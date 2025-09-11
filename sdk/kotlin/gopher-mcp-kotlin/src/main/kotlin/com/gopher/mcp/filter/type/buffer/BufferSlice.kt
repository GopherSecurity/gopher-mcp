package com.gopher.mcp.filter.type.buffer

import java.nio.ByteBuffer

/** Represents a slice of buffer data for zero-copy operations. */
class BufferSlice {

    companion object {
        // Buffer flags constants
        const val BUFFER_FLAG_READONLY = 0x01
        const val BUFFER_FLAG_ZERO_COPY = 0x08
    }

    var data: ByteBuffer? = null
    var length: Long = 0
    var flags: Int = 0

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param data ByteBuffer containing the data
     * @param length Length of the slice
     * @param flags Buffer flags
     */
    constructor(data: ByteBuffer?, length: Long, flags: Int) {
        this.data = data
        this.length = length
        this.flags = flags
    }

    /**
     * Check if the buffer slice is read-only
     *
     * @return true if read-only
     */
    fun isReadOnly(): Boolean {
        return (flags and BUFFER_FLAG_READONLY) != 0
    }

    /**
     * Check if the buffer slice is zero-copy
     *
     * @return true if zero-copy
     */
    fun isZeroCopy(): Boolean {
        return (flags and BUFFER_FLAG_ZERO_COPY) != 0
    }
}