package com.gopher.mcp.filter.type.chain

import com.gopher.mcp.jna.McpFilterChainLibrary

/**
 * Filter match condition.
 * Specifies how filter conditions are evaluated for routing.
 */
enum class FilterMatchCondition(private val value: Int) {

    /** Match all conditions. All conditions must be true. */
    ALL(McpFilterChainLibrary.MCP_MATCH_ALL),

    /** Match any condition. At least one condition must be true. */
    ANY(McpFilterChainLibrary.MCP_MATCH_ANY),

    /** Match no conditions. No conditions should be true. */
    NONE(McpFilterChainLibrary.MCP_MATCH_NONE),

    /** Custom match logic. User-defined matching function. */
    CUSTOM(McpFilterChainLibrary.MCP_MATCH_CUSTOM);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this match condition
     */
    fun getValue(): Int = value

    /**
     * Check if this is a logical operator
     *
     * @return true if this is a logical AND/OR/NOT operation
     */
    fun isLogicalOperator(): Boolean = this == ALL || this == ANY || this == NONE

    /**
     * Check if this requires custom implementation
     *
     * @return true if custom logic is needed
     */
    fun requiresCustomImplementation(): Boolean = this == CUSTOM

    /**
     * Get the inverse condition
     *
     * @return The logical inverse of this condition
     */
    fun inverse(): FilterMatchCondition {
        return when (this) {
            ALL -> NONE
            NONE -> ALL
            ANY -> NONE // Not strictly inverse, but commonly used
            CUSTOM -> CUSTOM // Custom remains custom
        }
    }

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding FilterMatchCondition enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): FilterMatchCondition {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid FilterMatchCondition value: $value")
        }
    }
}