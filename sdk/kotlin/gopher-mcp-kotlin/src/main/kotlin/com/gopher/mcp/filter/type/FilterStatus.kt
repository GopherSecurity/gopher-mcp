package com.gopher.mcp.filter.type

import com.gopher.mcp.jna.McpFilterLibrary

/**
 * Filter status for processing control.
 * Determines whether filter processing should continue or stop.
 */
enum class FilterStatus(private val value: Int) {

    /** Continue processing to the next filter in the chain. */
    CONTINUE(McpFilterLibrary.MCP_FILTER_CONTINUE),

    /** Stop iteration and return from the filter chain. No further filters will be processed. */
    STOP_ITERATION(McpFilterLibrary.MCP_FILTER_STOP_ITERATION);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this filter status
     */
    fun getValue(): Int = value

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding FilterStatus enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): FilterStatus {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid FilterStatus value: $value")
        }
    }
}