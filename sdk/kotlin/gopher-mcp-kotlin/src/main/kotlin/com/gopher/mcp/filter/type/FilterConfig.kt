package com.gopher.mcp.filter.type

/**
 * Configuration for creating a filter.
 */
data class FilterConfig(
    var name: String? = null,
    var filterType: Int = 0,
    var configJson: String? = null,
    var layer: Int = 0,
    var memoryPool: Long = 0
) {
    /**
     * Constructor with basic parameters
     *
     * @param name Filter name
     * @param filterType Filter type
     * @param configJson JSON configuration
     * @param layer Protocol layer
     */
    constructor(name: String?, filterType: Int, configJson: String?, layer: Int) : this() {
        this.name = name
        this.filterType = filterType
        this.configJson = configJson
        this.layer = layer
        this.memoryPool = 0
    }
}