package com.gopher.mcp.filter.type

/**
 * Server context for filter operations.
 */
class FilterServerContext {
    var server: Long = 0
    var requestFilters: Long = 0
    var responseFilters: Long = 0
    
    // Additional fields from McpFilterServerContext
    var connectionId: Long = 0
    var serverAddress: String? = null
    var serverPort: Int = 0
    var clientAddress: String? = null
    var clientPort: Int = 0
    var protocol: String? = null
    var tlsVersion: String? = null
    var cipherSuite: String? = null
    var sessionData: Long? = null
    var sessionDataSize: Long = 0

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param server Server handle
     * @param requestFilters Request filters handle
     * @param responseFilters Response filters handle
     */
    constructor(server: Long, requestFilters: Long, responseFilters: Long) {
        this.server = server
        this.requestFilters = requestFilters
        this.responseFilters = responseFilters
    }
}