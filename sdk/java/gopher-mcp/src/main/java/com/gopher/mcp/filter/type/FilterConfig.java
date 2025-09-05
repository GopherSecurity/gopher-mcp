package com.gopher.mcp.filter.type;

/**
 * Configuration for creating a filter.
 */
public class FilterConfig {

    private String name;
    private int filterType;
    private String configJson;
    private int layer;
    private long memoryPool;

    /**
     * Default constructor
     */
    public FilterConfig() {
        this.memoryPool = 0;
    }

    /**
     * Constructor with basic parameters
     * @param name Filter name
     * @param filterType Filter type
     * @param configJson JSON configuration
     * @param layer Protocol layer
     */
    public FilterConfig(String name, int filterType, String configJson, int layer) {
        this.name = name;
        this.filterType = filterType;
        this.configJson = configJson;
        this.layer = layer;
        this.memoryPool = 0;
    }

    // Getters and Setters

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public int getFilterType() {
        return filterType;
    }

    public void setFilterType(int filterType) {
        this.filterType = filterType;
    }

    public String getConfigJson() {
        return configJson;
    }

    public void setConfigJson(String configJson) {
        this.configJson = configJson;
    }

    public int getLayer() {
        return layer;
    }

    public void setLayer(int layer) {
        this.layer = layer;
    }

    public long getMemoryPool() {
        return memoryPool;
    }

    public void setMemoryPool(long memoryPool) {
        this.memoryPool = memoryPool;
    }
}
