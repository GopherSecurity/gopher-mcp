package com.gopher.mcp.filter.type

import com.gopher.mcp.jna.McpFilterLibrary

/**
 * Filter position in chain.
 * Specifies where a filter should be placed in the filter chain.
 */
enum class FilterPosition(private val value: Int) {

    /** Place filter at the first position in the chain. */
    FIRST(McpFilterLibrary.MCP_FILTER_POSITION_FIRST),

    /** Place filter at the last position in the chain. */
    LAST(McpFilterLibrary.MCP_FILTER_POSITION_LAST),

    /** Place filter before a specific reference filter. */
    BEFORE(McpFilterLibrary.MCP_FILTER_POSITION_BEFORE),

    /** Place filter after a specific reference filter. */
    AFTER(McpFilterLibrary.MCP_FILTER_POSITION_AFTER);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this filter position
     */
    fun getValue(): Int = value

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding FilterPosition enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): FilterPosition {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid FilterPosition value: $value")
        }
    }
}