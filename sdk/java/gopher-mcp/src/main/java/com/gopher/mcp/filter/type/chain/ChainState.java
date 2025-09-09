package com.gopher.mcp.filter.type.chain;

import com.gopher.mcp.jna.McpFilterChainLibrary;

/** Chain state. Represents the current operational state of a filter chain. */
public enum ChainState {

  /** Idle state. Chain is inactive and ready to process. */
  IDLE(McpFilterChainLibrary.MCP_CHAIN_STATE_IDLE),

  /** Processing state. Chain is actively processing data. */
  PROCESSING(McpFilterChainLibrary.MCP_CHAIN_STATE_PROCESSING),

  /** Paused state. Chain execution is temporarily suspended. */
  PAUSED(McpFilterChainLibrary.MCP_CHAIN_STATE_PAUSED),

  /** Error state. Chain encountered an error condition. */
  ERROR(McpFilterChainLibrary.MCP_CHAIN_STATE_ERROR),

  /** Completed state. Chain has finished processing. */
  COMPLETED(McpFilterChainLibrary.MCP_CHAIN_STATE_COMPLETED);

  private final int value;

  ChainState(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this chain state
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding ChainState enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static ChainState fromValue(int value) {
    for (ChainState state : values()) {
      if (state.value == value) {
        return state;
      }
    }
    throw new IllegalArgumentException("Invalid ChainState value: " + value);
  }

  /**
   * Check if the chain is in an active state
   *
   * @return true if the chain is actively processing
   */
  public boolean isActive() {
    return this == PROCESSING;
  }

  /**
   * Check if the chain can accept new work
   *
   * @return true if the chain can process new data
   */
  public boolean canAcceptWork() {
    return this == IDLE || this == COMPLETED;
  }

  /**
   * Check if the chain is in a terminal state
   *
   * @return true if the chain has finished or errored
   */
  public boolean isTerminal() {
    return this == ERROR || this == COMPLETED;
  }

  /**
   * Check if the chain can be resumed
   *
   * @return true if the chain can be resumed from this state
   */
  public boolean canResume() {
    return this == PAUSED;
  }

  /**
   * Check if the chain needs error handling
   *
   * @return true if the chain is in error state
   */
  public boolean requiresErrorHandling() {
    return this == ERROR;
  }

  /**
   * Get a human-readable description of the state
   *
   * @return Description of the current state
   */
  public String getDescription() {
    switch (this) {
      case IDLE:
        return "Chain is idle and ready to process";
      case PROCESSING:
        return "Chain is actively processing data";
      case PAUSED:
        return "Chain execution is paused";
      case ERROR:
        return "Chain encountered an error";
      case COMPLETED:
        return "Chain has completed processing";
      default:
        return "Unknown state: " + value;
    }
  }
}
