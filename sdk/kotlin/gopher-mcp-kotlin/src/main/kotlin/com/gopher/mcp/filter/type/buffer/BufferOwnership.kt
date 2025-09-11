package com.gopher.mcp.filter.type.buffer

import com.gopher.mcp.jna.McpFilterBufferLibrary

/**
 * Buffer ownership models for MCP Filter Buffer API.
 * Defines how buffer memory is managed and shared.
 */
enum class BufferOwnership(private val value: Int) {

    /**
     * No ownership - view only.
     * Buffer is a read-only view of existing memory.
     * Cannot modify the underlying data.
     */
    NONE(McpFilterBufferLibrary.MCP_BUFFER_OWNERSHIP_NONE),

    /**
     * Shared ownership - reference counted.
     * Multiple buffers can share the same underlying memory.
     * Memory is freed when the last reference is released.
     */
    SHARED(McpFilterBufferLibrary.MCP_BUFFER_OWNERSHIP_SHARED),

    /**
     * Exclusive ownership.
     * Buffer has sole ownership of the memory.
     * Memory is freed when the buffer is destroyed.
     */
    EXCLUSIVE(McpFilterBufferLibrary.MCP_BUFFER_OWNERSHIP_EXCLUSIVE),

    /**
     * External ownership - managed by callback.
     * Memory is owned by external code and managed via callbacks.
     * Useful for integrating with external memory management systems.
     */
    EXTERNAL(McpFilterBufferLibrary.MCP_BUFFER_OWNERSHIP_EXTERNAL);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this ownership model
     */
    fun getValue(): Int = value

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding BufferOwnership enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): BufferOwnership {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid BufferOwnership value: $value")
        }
    }
}