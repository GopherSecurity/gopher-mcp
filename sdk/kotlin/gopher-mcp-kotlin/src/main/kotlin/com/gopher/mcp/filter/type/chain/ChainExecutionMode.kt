package com.gopher.mcp.filter.type.chain

import com.gopher.mcp.jna.McpFilterChainLibrary

/**
 * Chain execution mode.
 * Specifies how filters in a chain are executed.
 */
enum class ChainExecutionMode(val value: Int) {

    /** Sequential execution. Filters are executed one after another in order. */
    SEQUENTIAL(McpFilterChainLibrary.MCP_CHAIN_MODE_SEQUENTIAL),

    /** Parallel execution. Multiple filters can execute simultaneously. */
    PARALLEL(McpFilterChainLibrary.MCP_CHAIN_MODE_PARALLEL),

    /** Conditional execution. Filters execute based on conditions. */
    CONDITIONAL(McpFilterChainLibrary.MCP_CHAIN_MODE_CONDITIONAL),

    /** Pipeline execution. Filters form a processing pipeline. */
    PIPELINE(McpFilterChainLibrary.MCP_CHAIN_MODE_PIPELINE);

    /**
     * Check if this mode supports parallel execution
     *
     * @return true if the mode allows parallel processing
     */
    fun supportsParallel(): Boolean = this == PARALLEL || this == PIPELINE

    /**
     * Check if this mode requires condition evaluation
     *
     * @return true if the mode uses conditions
     */
    fun requiresConditions(): Boolean = this == CONDITIONAL

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding ChainExecutionMode enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): ChainExecutionMode {
            return entries.find { it.value == value }
                ?: throw IllegalArgumentException("Invalid ChainExecutionMode value: $value")
        }
    }
}