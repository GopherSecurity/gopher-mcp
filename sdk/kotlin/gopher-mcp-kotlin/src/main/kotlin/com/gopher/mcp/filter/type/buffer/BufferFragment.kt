package com.gopher.mcp.filter.type.buffer

import java.nio.ByteBuffer

/** External memory fragment for buffer operations */
class BufferFragment {
    var data: ByteBuffer? = null
    var length: Long = 0
    var capacity: Long = 0
    var userData: Any? = null

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param data Data buffer
     * @param length Data length
     * @param capacity Fragment capacity
     */
    constructor(data: ByteBuffer?, length: Long, capacity: Long) {
        this.data = data
        this.length = length
        this.capacity = capacity
    }
}