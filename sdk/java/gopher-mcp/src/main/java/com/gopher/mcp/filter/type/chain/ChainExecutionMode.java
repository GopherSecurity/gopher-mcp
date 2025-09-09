package com.gopher.mcp.filter.type.chain;

import com.gopher.mcp.jna.McpFilterChainLibrary;

/** Chain execution mode. Specifies how filters in a chain are executed. */
public enum ChainExecutionMode {

  /** Sequential execution. Filters are executed one after another in order. */
  SEQUENTIAL(McpFilterChainLibrary.MCP_CHAIN_MODE_SEQUENTIAL),

  /** Parallel execution. Multiple filters can execute simultaneously. */
  PARALLEL(McpFilterChainLibrary.MCP_CHAIN_MODE_PARALLEL),

  /** Conditional execution. Filters execute based on conditions. */
  CONDITIONAL(McpFilterChainLibrary.MCP_CHAIN_MODE_CONDITIONAL),

  /** Pipeline execution. Filters form a processing pipeline. */
  PIPELINE(McpFilterChainLibrary.MCP_CHAIN_MODE_PIPELINE);

  private final int value;

  ChainExecutionMode(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this execution mode
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding ChainExecutionMode enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static ChainExecutionMode fromValue(int value) {
    for (ChainExecutionMode mode : values()) {
      if (mode.value == value) {
        return mode;
      }
    }
    throw new IllegalArgumentException("Invalid ChainExecutionMode value: " + value);
  }

  /**
   * Check if this mode supports parallel execution
   *
   * @return true if the mode allows parallel processing
   */
  public boolean supportsParallel() {
    return this == PARALLEL || this == PIPELINE;
  }

  /**
   * Check if this mode requires condition evaluation
   *
   * @return true if the mode uses conditions
   */
  public boolean requiresConditions() {
    return this == CONDITIONAL;
  }
}
