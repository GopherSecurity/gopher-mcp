package com.gopher.mcp.filter.type

/**
 * Client context for filter operations.
 */
class FilterClientContext {
    var client: Long = 0
    var requestFilters: Long = 0
    var responseFilters: Long = 0
    
    // Additional fields from McpFilterClientContext
    var requestId: Long = 0
    var clientId: String? = null
    var sessionId: String? = null
    var userAgent: String? = null
    var authToken: String? = null
    var requestMethod: String? = null
    var requestUri: String? = null
    var protocolVersion: String? = null
    var customHeaders: Long? = null
    var customHeadersCount: Int = 0

    /** Default constructor */
    constructor()

    /**
     * Constructor with all parameters
     *
     * @param client Client handle
     * @param requestFilters Request filters handle
     * @param responseFilters Response filters handle
     */
    constructor(client: Long, requestFilters: Long, responseFilters: Long) {
        this.client = client
        this.requestFilters = requestFilters
        this.responseFilters = responseFilters
    }
}