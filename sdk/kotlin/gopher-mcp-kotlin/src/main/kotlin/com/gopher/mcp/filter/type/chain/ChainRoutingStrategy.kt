package com.gopher.mcp.filter.type.chain

import com.gopher.mcp.jna.McpFilterChainLibrary

/**
 * Chain routing strategy.
 * Specifies how requests are routed through filter chains.
 */
enum class ChainRoutingStrategy(private val value: Int) {

    /** Round-robin routing. Distributes requests evenly in circular order. */
    ROUND_ROBIN(McpFilterChainLibrary.MCP_ROUTING_ROUND_ROBIN),

    /** Least loaded routing. Routes to the chain with lowest current load. */
    LEAST_LOADED(McpFilterChainLibrary.MCP_ROUTING_LEAST_LOADED),

    /** Hash-based routing. Uses hash function to determine routing. */
    HASH_BASED(McpFilterChainLibrary.MCP_ROUTING_HASH_BASED),

    /** Priority-based routing. Routes based on priority levels. */
    PRIORITY(McpFilterChainLibrary.MCP_ROUTING_PRIORITY),

    /** Custom routing. User-defined routing logic. */
    CUSTOM(McpFilterChainLibrary.MCP_ROUTING_CUSTOM);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this routing strategy
     */
    fun getValue(): Int = value

    /**
     * Check if this is a load-balancing strategy
     *
     * @return true if the strategy distributes load
     */
    fun isLoadBalancing(): Boolean = this == ROUND_ROBIN || this == LEAST_LOADED

    /**
     * Check if this strategy requires state tracking
     *
     * @return true if the strategy needs to maintain state
     */
    fun requiresState(): Boolean = this == LEAST_LOADED || this == PRIORITY

    /**
     * Check if this strategy is deterministic
     *
     * @return true if the same input always produces same routing
     */
    fun isDeterministic(): Boolean = this == HASH_BASED

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding ChainRoutingStrategy enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): ChainRoutingStrategy {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid ChainRoutingStrategy value: $value")
        }
    }
}