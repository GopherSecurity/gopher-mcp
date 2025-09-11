package com.gopher.mcp.filter.type.chain

/**
 * Configuration for creating a chain router.
 */
data class RouterConfig(
    var strategy: Int = 0,
    var hashSeed: Int = 0,
    var routeTable: Long = 0,
    var customRouterData: Any? = null
) {
    /**
     * Constructor with strategy parameter
     *
     * @param strategy Routing strategy
     */
    constructor(strategy: Int) : this() {
        this.strategy = strategy
        this.hashSeed = 0
        this.routeTable = 0
        this.customRouterData = null
    }
}