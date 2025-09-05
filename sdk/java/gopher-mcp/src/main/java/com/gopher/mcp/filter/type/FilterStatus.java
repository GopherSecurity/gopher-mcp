package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/**
 * Filter status for processing control.
 * Determines whether filter processing should continue or stop.
 */
public enum FilterStatus {
    
    /**
     * Continue processing to the next filter in the chain.
     */
    CONTINUE(McpFilterLibrary.MCP_FILTER_CONTINUE),
    
    /**
     * Stop iteration and return from the filter chain.
     * No further filters will be processed.
     */
    STOP_ITERATION(McpFilterLibrary.MCP_FILTER_STOP_ITERATION);
    
    private final int value;
    
    FilterStatus(int value) {
        this.value = value;
    }
    
    /**
     * Get the integer value for JNA calls
     * @return The numeric value of this filter status
     */
    public int getValue() {
        return value;
    }
    
    /**
     * Convert from integer value to enum
     * @param value The integer value from native code
     * @return The corresponding FilterStatus enum value
     * @throws IllegalArgumentException if value is not valid
     */
    public static FilterStatus fromValue(int value) {
        for (FilterStatus status : values()) {
            if (status.value == value) {
                return status;
            }
        }
        throw new IllegalArgumentException("Invalid FilterStatus value: " + value);
    }
}