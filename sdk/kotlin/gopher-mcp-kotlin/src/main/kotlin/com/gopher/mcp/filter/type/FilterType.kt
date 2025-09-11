package com.gopher.mcp.filter.type

import com.gopher.mcp.jna.McpFilterLibrary

/**
 * Built-in filter types provided by the MCP Filter system.
 * Each filter type represents a specific functionality that can be
 * applied to message processing pipelines.
 */
enum class FilterType(val value: Int) {
    /** TCP proxy filter. Handles TCP connection proxying. */
    TCP_PROXY(McpFilterLibrary.MCP_FILTER_TCP_PROXY),
    
    /** UDP proxy filter. Handles UDP datagram proxying. */
    UDP_PROXY(McpFilterLibrary.MCP_FILTER_UDP_PROXY),
    
    /** HTTP codec filter. Encodes and decodes HTTP messages. */
    HTTP_CODEC(McpFilterLibrary.MCP_FILTER_HTTP_CODEC),
    
    /** HTTP router filter. Routes HTTP requests based on rules. */
    HTTP_ROUTER(McpFilterLibrary.MCP_FILTER_HTTP_ROUTER),
    
    /** HTTP compression filter. Compresses and decompresses HTTP content. */
    HTTP_COMPRESSION(McpFilterLibrary.MCP_FILTER_HTTP_COMPRESSION),
    
    /** TLS termination filter. Handles TLS/SSL termination. */
    TLS_TERMINATION(McpFilterLibrary.MCP_FILTER_TLS_TERMINATION),
    
    /** Authentication filter. Handles user authentication. */
    AUTHENTICATION(McpFilterLibrary.MCP_FILTER_AUTHENTICATION),
    
    /** Authorization filter. Handles access control and permissions. */
    AUTHORIZATION(McpFilterLibrary.MCP_FILTER_AUTHORIZATION),
    
    /** Access log filter. Logs access information. */
    ACCESS_LOG(McpFilterLibrary.MCP_FILTER_ACCESS_LOG),
    
    /** Metrics filter. Collects and reports metrics. */
    METRICS(McpFilterLibrary.MCP_FILTER_METRICS),
    
    /** Tracing filter. Handles distributed tracing. */
    TRACING(McpFilterLibrary.MCP_FILTER_TRACING),
    
    /** Rate limit filter. Enforces rate limiting policies. */
    RATE_LIMIT(McpFilterLibrary.MCP_FILTER_RATE_LIMIT),
    
    /** Circuit breaker filter. Implements circuit breaker pattern. */
    CIRCUIT_BREAKER(McpFilterLibrary.MCP_FILTER_CIRCUIT_BREAKER),
    
    /** Retry filter. Handles automatic retries. */
    RETRY(McpFilterLibrary.MCP_FILTER_RETRY),
    
    /** Load balancer filter. Distributes requests across backends. */
    LOAD_BALANCER(McpFilterLibrary.MCP_FILTER_LOAD_BALANCER),
    
    /** Custom filter. User-defined filter type. */
    CUSTOM(McpFilterLibrary.MCP_FILTER_CUSTOM);

    companion object {
        /**
         * Get FilterType from its integer value
         */
        @JvmStatic
        fun fromValue(value: Int): FilterType {
            return entries.find { it.value == value }
                ?: throw IllegalArgumentException("Invalid FilterType value: $value")
        }

        /**
         * Get FilterType from its name (case-insensitive)
         */
        @JvmStatic
        fun fromName(name: String): FilterType? = 
            entries.find { it.name.equals(name, ignoreCase = true) }
    }
    
    /**
     * Check if this is a proxy filter
     *
     * @return true if this is a proxy filter type
     */
    fun isProxy(): Boolean {
        return this == TCP_PROXY || this == UDP_PROXY
    }
    
    /**
     * Check if this is an HTTP filter
     *
     * @return true if this filter processes HTTP
     */
    fun isHttp(): Boolean {
        return this == HTTP_CODEC || this == HTTP_ROUTER || this == HTTP_COMPRESSION
    }
    
    /**
     * Check if this is a security filter
     *
     * @return true if this filter handles security
     */
    fun isSecurity(): Boolean {
        return this == AUTHENTICATION || this == AUTHORIZATION || this == TLS_TERMINATION
    }
    
    /**
     * Check if this is an observability filter
     *
     * @return true if this filter handles observability
     */
    fun isObservability(): Boolean {
        return this == ACCESS_LOG || this == METRICS || this == TRACING
    }
    
    /**
     * Check if this is a resilience filter
     *
     * @return true if this filter handles resilience
     */
    fun isResilience(): Boolean {
        return this == RATE_LIMIT || this == CIRCUIT_BREAKER || this == RETRY || this == LOAD_BALANCER
    }
}