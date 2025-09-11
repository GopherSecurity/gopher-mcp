package com.gopher.mcp.filter.type.chain

/** Configuration for filter chain */
class ChainConfig {
    var name: String? = null
    var mode: Int = 0
    var routing: Int = 0
    var maxParallel: Int = 0
    var bufferSize: Int = 0
    var timeoutMs: Int = 0
    var stopOnError: Boolean = false

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param name Chain name
     * @param mode Execution mode
     * @param routing Routing strategy
     * @param maxParallel Maximum parallel filters
     * @param bufferSize Buffer size
     * @param timeoutMs Timeout in milliseconds
     * @param stopOnError Stop on error flag
     */
    constructor(
        name: String?,
        mode: Int,
        routing: Int,
        maxParallel: Int,
        bufferSize: Int,
        timeoutMs: Int,
        stopOnError: Boolean
    ) {
        this.name = name
        this.mode = mode
        this.routing = routing
        this.maxParallel = maxParallel
        this.bufferSize = bufferSize
        this.timeoutMs = timeoutMs
        this.stopOnError = stopOnError
    }
}