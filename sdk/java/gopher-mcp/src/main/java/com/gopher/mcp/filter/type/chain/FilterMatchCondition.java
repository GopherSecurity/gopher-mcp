package com.gopher.mcp.filter.type.chain;

import com.gopher.mcp.jna.McpFilterChainLibrary;

/**
 * Filter match condition.
 * Specifies how filter conditions are evaluated for routing.
 */
public enum FilterMatchCondition {
    
    /**
     * Match all conditions.
     * All conditions must be true.
     */
    ALL(McpFilterChainLibrary.MCP_MATCH_ALL),
    
    /**
     * Match any condition.
     * At least one condition must be true.
     */
    ANY(McpFilterChainLibrary.MCP_MATCH_ANY),
    
    /**
     * Match no conditions.
     * No conditions should be true.
     */
    NONE(McpFilterChainLibrary.MCP_MATCH_NONE),
    
    /**
     * Custom match logic.
     * User-defined matching function.
     */
    CUSTOM(McpFilterChainLibrary.MCP_MATCH_CUSTOM);
    
    private final int value;
    
    FilterMatchCondition(int value) {
        this.value = value;
    }
    
    /**
     * Get the integer value for JNA calls
     * @return The numeric value of this match condition
     */
    public int getValue() {
        return value;
    }
    
    /**
     * Convert from integer value to enum
     * @param value The integer value from native code
     * @return The corresponding FilterMatchCondition enum value
     * @throws IllegalArgumentException if value is not valid
     */
    public static FilterMatchCondition fromValue(int value) {
        for (FilterMatchCondition condition : values()) {
            if (condition.value == value) {
                return condition;
            }
        }
        throw new IllegalArgumentException("Invalid FilterMatchCondition value: " + value);
    }
    
    /**
     * Check if this is a logical operator
     * @return true if this is a logical AND/OR/NOT operation
     */
    public boolean isLogicalOperator() {
        return this == ALL || this == ANY || this == NONE;
    }
    
    /**
     * Check if this requires custom implementation
     * @return true if custom logic is needed
     */
    public boolean requiresCustomImplementation() {
        return this == CUSTOM;
    }
    
    /**
     * Get the inverse condition
     * @return The logical inverse of this condition
     */
    public FilterMatchCondition inverse() {
        switch (this) {
            case ALL:
                return NONE;
            case NONE:
                return ALL;
            case ANY:
                return NONE;  // Not strictly inverse, but commonly used
            case CUSTOM:
                return CUSTOM;  // Custom remains custom
            default:
                return this;
        }
    }
}