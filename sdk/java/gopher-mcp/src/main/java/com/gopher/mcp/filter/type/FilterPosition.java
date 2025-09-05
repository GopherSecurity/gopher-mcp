package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/**
 * Filter position in chain.
 * Specifies where a filter should be placed in the filter chain.
 */
public enum FilterPosition {
    
    /**
     * Place filter at the first position in the chain.
     */
    FIRST(McpFilterLibrary.MCP_FILTER_POSITION_FIRST),
    
    /**
     * Place filter at the last position in the chain.
     */
    LAST(McpFilterLibrary.MCP_FILTER_POSITION_LAST),
    
    /**
     * Place filter before a specific reference filter.
     */
    BEFORE(McpFilterLibrary.MCP_FILTER_POSITION_BEFORE),
    
    /**
     * Place filter after a specific reference filter.
     */
    AFTER(McpFilterLibrary.MCP_FILTER_POSITION_AFTER);
    
    private final int value;
    
    FilterPosition(int value) {
        this.value = value;
    }
    
    /**
     * Get the integer value for JNA calls
     * @return The numeric value of this filter position
     */
    public int getValue() {
        return value;
    }
    
    /**
     * Convert from integer value to enum
     * @param value The integer value from native code
     * @return The corresponding FilterPosition enum value
     * @throws IllegalArgumentException if value is not valid
     */
    public static FilterPosition fromValue(int value) {
        for (FilterPosition position : values()) {
            if (position.value == value) {
                return position;
            }
        }
        throw new IllegalArgumentException("Invalid FilterPosition value: " + value);
    }
}