package com.gopher.mcp.filter.type.chain

/**
 * Filter node configuration for chain
 */
data class FilterNode(
    var filterHandle: Long = 0,
    var name: String? = null,
    var priority: Int = 0,
    var enabled: Boolean = false,
    var bypassOnError: Boolean = false,
    var configHandle: Long = 0
) {
    /**
     * Constructor with basic parameters
     *
     * @param filterHandle Filter handle
     * @param name Node name
     * @param priority Priority in chain
     * @param enabled Enabled flag
     */
    constructor(filterHandle: Long, name: String?, priority: Int, enabled: Boolean) : this() {
        this.filterHandle = filterHandle
        this.name = name
        this.priority = priority
        this.enabled = enabled
        this.bypassOnError = false
        this.configHandle = 0
    }
}