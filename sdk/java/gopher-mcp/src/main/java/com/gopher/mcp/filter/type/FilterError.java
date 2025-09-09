package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/** Filter error codes. Specifies error conditions that can occur during filter processing. */
public enum FilterError {

  /** No error. Operation completed successfully. */
  NONE(McpFilterLibrary.MCP_FILTER_ERROR_NONE),

  /** Invalid configuration. Filter configuration is malformed or invalid. */
  INVALID_CONFIG(McpFilterLibrary.MCP_FILTER_ERROR_INVALID_CONFIG),

  /** Initialization failed. Filter failed to initialize properly. */
  INITIALIZATION_FAILED(McpFilterLibrary.MCP_FILTER_ERROR_INITIALIZATION_FAILED),

  /** Buffer overflow. Buffer capacity exceeded. */
  BUFFER_OVERFLOW(McpFilterLibrary.MCP_FILTER_ERROR_BUFFER_OVERFLOW),

  /** Protocol violation. Protocol rules were violated. */
  PROTOCOL_VIOLATION(McpFilterLibrary.MCP_FILTER_ERROR_PROTOCOL_VIOLATION),

  /** Upstream timeout. Timeout waiting for upstream response. */
  UPSTREAM_TIMEOUT(McpFilterLibrary.MCP_FILTER_ERROR_UPSTREAM_TIMEOUT),

  /** Circuit open. Circuit breaker is in open state. */
  CIRCUIT_OPEN(McpFilterLibrary.MCP_FILTER_ERROR_CIRCUIT_OPEN),

  /** Resource exhausted. System resources are exhausted. */
  RESOURCE_EXHAUSTED(McpFilterLibrary.MCP_FILTER_ERROR_RESOURCE_EXHAUSTED),

  /** Invalid state. Operation performed in invalid state. */
  INVALID_STATE(McpFilterLibrary.MCP_FILTER_ERROR_INVALID_STATE);

  private final int value;

  FilterError(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this error code
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding FilterError enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static FilterError fromValue(int value) {
    for (FilterError error : values()) {
      if (error.value == value) {
        return error;
      }
    }
    throw new IllegalArgumentException("Invalid FilterError value: " + value);
  }

  /**
   * Check if this represents an error condition
   *
   * @return true if this is an error (not NONE)
   */
  public boolean isError() {
    return this != NONE;
  }

  /**
   * Check if this is a configuration error
   *
   * @return true if this is related to configuration
   */
  public boolean isConfigurationError() {
    return this == INVALID_CONFIG || this == INITIALIZATION_FAILED;
  }

  /**
   * Check if this is a runtime error
   *
   * @return true if this error occurs during runtime
   */
  public boolean isRuntimeError() {
    return this == BUFFER_OVERFLOW
        || this == PROTOCOL_VIOLATION
        || this == UPSTREAM_TIMEOUT
        || this == CIRCUIT_OPEN
        || this == RESOURCE_EXHAUSTED
        || this == INVALID_STATE;
  }

  /**
   * Check if this error is retryable
   *
   * @return true if the operation can be retried
   */
  public boolean isRetryable() {
    return this == UPSTREAM_TIMEOUT || this == RESOURCE_EXHAUSTED;
  }

  /**
   * Get a human-readable error message
   *
   * @return Description of the error
   */
  public String getMessage() {
    switch (this) {
      case NONE:
        return "No error";
      case INVALID_CONFIG:
        return "Invalid filter configuration";
      case INITIALIZATION_FAILED:
        return "Filter initialization failed";
      case BUFFER_OVERFLOW:
        return "Buffer overflow occurred";
      case PROTOCOL_VIOLATION:
        return "Protocol violation detected";
      case UPSTREAM_TIMEOUT:
        return "Upstream request timed out";
      case CIRCUIT_OPEN:
        return "Circuit breaker is open";
      case RESOURCE_EXHAUSTED:
        return "System resources exhausted";
      case INVALID_STATE:
        return "Operation performed in invalid state";
      default:
        return "Unknown error: " + value;
    }
  }
}
