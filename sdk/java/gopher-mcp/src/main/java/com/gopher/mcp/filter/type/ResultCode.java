package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/** Result codes for MCP operations. Standard return codes for API functions. */
public enum ResultCode {

  /** Operation succeeded. */
  OK(McpFilterLibrary.MCP_OK),

  /** Operation failed. */
  ERROR(McpFilterLibrary.MCP_ERROR);

  private final int value;

  ResultCode(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this result code
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding ResultCode enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static ResultCode fromValue(int value) {
    for (ResultCode code : values()) {
      if (code.value == value) {
        return code;
      }
    }
    throw new IllegalArgumentException("Invalid ResultCode value: " + value);
  }

  /**
   * Check if the result indicates success
   *
   * @return true if this is OK
   */
  public boolean isSuccess() {
    return this == OK;
  }

  /**
   * Check if the result indicates failure
   *
   * @return true if this is ERROR
   */
  public boolean isError() {
    return this == ERROR;
  }

  /**
   * Check if an integer result indicates success
   *
   * @param result The result value to check
   * @return true if the result is OK (0)
   */
  public static boolean isSuccess(int result) {
    return result == OK.value;
  }

  /**
   * Check if an integer result indicates failure
   *
   * @param result The result value to check
   * @return true if the result is ERROR (-1)
   */
  public static boolean isError(int result) {
    return result == ERROR.value;
  }

  /**
   * Throw an exception if the result indicates failure
   *
   * @param result The result value to check
   * @param errorMessage Message for the exception
   * @throws RuntimeException if result is ERROR
   */
  public static void checkResult(int result, String errorMessage) {
    if (isError(result)) {
      throw new RuntimeException(errorMessage);
    }
  }

  /**
   * Convert this result to a boolean
   *
   * @return true if OK, false if ERROR
   */
  public boolean toBoolean() {
    return isSuccess();
  }
}
