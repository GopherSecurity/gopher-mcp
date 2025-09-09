package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/** Buffer flags for memory management. Specifies buffer characteristics and access permissions. */
public enum BufferFlags {

  /** Read-only buffer. Buffer contents cannot be modified. */
  READONLY(McpFilterLibrary.MCP_BUFFER_FLAG_READONLY),

  /** Owned buffer. Buffer memory is owned by the filter. */
  OWNED(McpFilterLibrary.MCP_BUFFER_FLAG_OWNED),

  /** External buffer. Buffer memory is managed externally. */
  EXTERNAL(McpFilterLibrary.MCP_BUFFER_FLAG_EXTERNAL),

  /** Zero-copy buffer. Buffer supports zero-copy operations. */
  ZERO_COPY(McpFilterLibrary.MCP_BUFFER_FLAG_ZERO_COPY);

  private final int value;

  BufferFlags(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this buffer flag
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding BufferFlags enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static BufferFlags fromValue(int value) {
    for (BufferFlags flag : values()) {
      if (flag.value == value) {
        return flag;
      }
    }
    throw new IllegalArgumentException("Invalid BufferFlags value: " + value);
  }

  /**
   * Check if a flag is set in the given flags value
   *
   * @param flags The flags value to check
   * @return true if this flag is set
   */
  public boolean isSet(int flags) {
    return (flags & value) != 0;
  }

  /**
   * Combine multiple flags
   *
   * @param flags Array of flags to combine
   * @return Combined flags value
   */
  public static int combine(BufferFlags... flags) {
    int result = 0;
    for (BufferFlags flag : flags) {
      result |= flag.value;
    }
    return result;
  }

  /**
   * Extract all flags from a combined value
   *
   * @param flags Combined flags value
   * @return Array of individual flags
   */
  public static BufferFlags[] extract(int flags) {
    if (flags == 0) {
      return new BufferFlags[0];
    }

    int count = 0;
    for (BufferFlags flag : values()) {
      if (flag.isSet(flags)) {
        count++;
      }
    }

    BufferFlags[] result = new BufferFlags[count];
    int index = 0;
    for (BufferFlags flag : values()) {
      if (flag.isSet(flags)) {
        result[index++] = flag;
      }
    }

    return result;
  }

  /**
   * Check if the given flags indicate a read-only buffer
   *
   * @param flags The flags value to check
   * @return true if the buffer is read-only
   */
  public static boolean isReadOnly(int flags) {
    return READONLY.isSet(flags);
  }

  /**
   * Check if the given flags indicate an owned buffer
   *
   * @param flags The flags value to check
   * @return true if the buffer is owned
   */
  public static boolean isOwned(int flags) {
    return OWNED.isSet(flags);
  }

  /**
   * Check if the given flags indicate an external buffer
   *
   * @param flags The flags value to check
   * @return true if the buffer is external
   */
  public static boolean isExternal(int flags) {
    return EXTERNAL.isSet(flags);
  }

  /**
   * Check if the given flags indicate a zero-copy buffer
   *
   * @param flags The flags value to check
   * @return true if the buffer supports zero-copy
   */
  public static boolean isZeroCopy(int flags) {
    return ZERO_COPY.isSet(flags);
  }
}
