package com.gopher.mcp.filter.type.chain;

/**
 * Filter node configuration for chain
 */
public class FilterNode {
    private long filterHandle;
    private String name;
    private int priority;
    private boolean enabled;
    private boolean bypassOnError;
    private long configHandle;

    /**
     * Default constructor
     */
    public FilterNode() {
    }

    /**
     * Constructor with basic parameters
     * @param filterHandle Filter handle
     * @param name Node name
     * @param priority Priority in chain
     * @param enabled Enabled flag
     */
    public FilterNode(long filterHandle, String name, int priority, boolean enabled) {
        this.filterHandle = filterHandle;
        this.name = name;
        this.priority = priority;
        this.enabled = enabled;
        this.bypassOnError = false;
        this.configHandle = 0;
    }

    // Getters and Setters

    public long getFilterHandle() {
        return filterHandle;
    }

    public void setFilterHandle(long filterHandle) {
        this.filterHandle = filterHandle;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public int getPriority() {
        return priority;
    }

    public void setPriority(int priority) {
        this.priority = priority;
    }

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public boolean isBypassOnError() {
        return bypassOnError;
    }

    public void setBypassOnError(boolean bypassOnError) {
        this.bypassOnError = bypassOnError;
    }

    public long getConfigHandle() {
        return configHandle;
    }

    public void setConfigHandle(long configHandle) {
        this.configHandle = configHandle;
    }
}
