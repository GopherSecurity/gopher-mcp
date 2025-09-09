package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/** Built-in filter types. Specifies the type of filter for common use cases. */
public enum FilterType {

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

  private final int value;

  FilterType(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this filter type
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding FilterType enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static FilterType fromValue(int value) {
    for (FilterType type : values()) {
      if (type.value == value) {
        return type;
      }
    }
    throw new IllegalArgumentException("Invalid FilterType value: " + value);
  }

  /**
   * Check if this is a proxy filter
   *
   * @return true if this is a proxy filter type
   */
  public boolean isProxy() {
    return this == TCP_PROXY || this == UDP_PROXY;
  }

  /**
   * Check if this is an HTTP filter
   *
   * @return true if this filter processes HTTP
   */
  public boolean isHttpFilter() {
    return value >= 10 && value <= 19;
  }

  /**
   * Check if this is a security filter
   *
   * @return true if this filter handles security
   */
  public boolean isSecurityFilter() {
    return value >= 20 && value <= 29;
  }

  /**
   * Check if this is an observability filter
   *
   * @return true if this filter handles observability
   */
  public boolean isObservabilityFilter() {
    return value >= 30 && value <= 39;
  }

  /**
   * Check if this is a resilience filter
   *
   * @return true if this filter handles resilience patterns
   */
  public boolean isResilienceFilter() {
    return value >= 40 && value <= 49;
  }
}
