package com.gopher.mcp.filter.type.chain;

import com.gopher.mcp.jna.McpFilterChainLibrary;

/** Chain routing strategy. Specifies how requests are routed through filter chains. */
public enum ChainRoutingStrategy {

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

  private final int value;

  ChainRoutingStrategy(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this routing strategy
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding ChainRoutingStrategy enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static ChainRoutingStrategy fromValue(int value) {
    for (ChainRoutingStrategy strategy : values()) {
      if (strategy.value == value) {
        return strategy;
      }
    }
    throw new IllegalArgumentException("Invalid ChainRoutingStrategy value: " + value);
  }

  /**
   * Check if this is a load-balancing strategy
   *
   * @return true if the strategy distributes load
   */
  public boolean isLoadBalancing() {
    return this == ROUND_ROBIN || this == LEAST_LOADED;
  }

  /**
   * Check if this strategy requires state tracking
   *
   * @return true if the strategy needs to maintain state
   */
  public boolean requiresState() {
    return this == LEAST_LOADED || this == PRIORITY;
  }

  /**
   * Check if this strategy is deterministic
   *
   * @return true if the same input always produces same routing
   */
  public boolean isDeterministic() {
    return this == HASH_BASED;
  }
}
