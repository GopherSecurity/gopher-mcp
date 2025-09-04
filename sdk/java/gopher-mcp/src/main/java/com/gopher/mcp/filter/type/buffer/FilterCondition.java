package com.gopher.mcp.filter.type.buffer;

/**
 * Filter condition for conditional execution
 */
public class FilterCondition {
    private int matchType;
    private String field;
    private String value;
    private long targetFilter;

    /**
     * Default constructor
     */
    public FilterCondition() {
    }

    /**
     * Constructor with all parameters
     * @param matchType Match type (ALL, ANY, NONE)
     * @param field Field name to match
     * @param value Field value to match
     * @param targetFilter Target filter handle
     */
    public FilterCondition(int matchType, String field, String value, long targetFilter) {
        this.matchType = matchType;
        this.field = field;
        this.value = value;
        this.targetFilter = targetFilter;
    }

    // Getters and Setters

    public int getMatchType() {
        return matchType;
    }

    public void setMatchType(int matchType) {
        this.matchType = matchType;
    }

    public String getField() {
        return field;
    }

    public void setField(String field) {
        this.field = field;
    }

    public String getValue() {
        return value;
    }

    public void setValue(String value) {
        this.value = value;
    }

    public long getTargetFilter() {
        return targetFilter;
    }

    public void setTargetFilter(long targetFilter) {
        this.targetFilter = targetFilter;
    }
}
