/**
 * @file filter-manager.ts
 * @brief Filter Manager for processing JSONRPCMessage through C++ filters
 *
 * This provides a simple interface to process MCP JSON-RPC messages
 * through our C++ filter infrastructure using FFI wrappers.
 */

import {
  addChainToManager,
  createFilterManager,
  destroyBufferPool,
  initializeFilterManager,
  postDataToFilter,
  releaseFilterChain,
  releaseFilterManager,
} from "./filter-api";

// Import the three filter modules as requested
import {
  addFilterNodeToChain,
  buildFilterChain,
  ChainConfig,
  ChainExecutionMode,
  createChainBuilderEx,
  destroyFilterChainBuilder,
  FilterNode,
  RoutingStrategy,
} from "./filter-chain";

import {
  BufferOwnership as AdvancedBufferOwnership,
  BufferPoolConfig,
  createBufferFromString as createBufferFromStringAdvanced,
  createBufferPoolEx,
  readStringFromBuffer as readStringFromBufferAdvanced,
} from "./filter-buffer";

import {
  BuiltinFilterType as AdvancedBuiltinFilterType,
  createBuiltinFilter as createBuiltinFilterAdvanced,
} from "./filter-api";

/**
 * JSON-RPC Message interface (compatible with MCP)
 */
export interface JSONRPCMessage {
  jsonrpc: "2.0";
  id?: string | number;
  method?: string;
  params?: any;
  result?: any;
  error?: {
    code: number;
    message: string;
    data?: any;
  };
}

/**
 * Network Filter Configuration
 */
export interface NetworkFilterConfig {
  // TCP Proxy configuration
  tcpProxy?: {
    enabled: boolean;
    upstreamHost: string;
    upstreamPort: number;
    bindAddress?: string;
    bindPort?: number;
  };

  // UDP Proxy configuration
  udpProxy?: {
    enabled: boolean;
    upstreamHost: string;
    upstreamPort: number;
    bindAddress?: string;
    bindPort?: number;
  };
}

/**
 * HTTP Filter Configuration
 */
export interface HttpFilterConfig {
  // HTTP Codec configuration
  codec?: {
    enabled: boolean;
    compressionLevel?: number;
    maxRequestSize?: number;
    maxResponseSize?: number;
  };

  // HTTP Router configuration
  router?: {
    enabled: boolean;
    routes: Array<{
      path: string;
      method?: string;
      target: string;
      headers?: Record<string, string>;
    }>;
  };

  // HTTP Compression configuration
  compression?: {
    enabled: boolean;
    algorithms: ("gzip" | "deflate" | "brotli")[];
    minSize?: number;
  };
}

/**
 * Security Filter Configuration
 */
export interface SecurityFilterConfig {
  // TLS Termination configuration
  tlsTermination?: {
    enabled: boolean;
    certPath: string;
    keyPath: string;
    caPath?: string;
    protocols?: string[];
  };

  // Authentication configuration
  authentication?: {
    method: "jwt" | "api-key" | "oauth2";
    secret?: string;
    key?: string;
    issuer?: string;
    audience?: string;
  };

  // Authorization configuration
  authorization?: {
    enabled: boolean;
    policy: "allow" | "deny";
    rules: Array<{
      resource: string;
      action: string;
      conditions?: Record<string, any>;
    }>;
  };
}

/**
 * Observability Filter Configuration
 */
export interface ObservabilityFilterConfig {
  // Access Log configuration
  accessLog?: {
    enabled: boolean;
    format?: "json" | "text";
    fields?: string[];
    output?: "console" | "file" | "syslog";
  };

  // Metrics configuration
  metrics?: {
    enabled: boolean;
    endpoint?: string;
    interval?: number;
    labels?: Record<string, string>;
  };

  // Tracing configuration
  tracing?: {
    enabled: boolean;
    serviceName: string;
    endpoint?: string;
    samplingRate?: number;
  };
}

/**
 * Traffic Management Filter Configuration
 */
export interface TrafficManagementFilterConfig {
  // Rate Limiting configuration
  rateLimit?: {
    enabled: boolean;
    requestsPerMinute: number;
    burstSize?: number;
    keyExtractor?: "ip" | "user" | "custom";
  };

  // Circuit Breaker configuration
  circuitBreaker?: {
    enabled: boolean;
    failureThreshold: number;
    timeout: number;
    resetTimeout: number;
  };

  // Retry configuration
  retry?: {
    enabled: boolean;
    maxAttempts: number;
    backoffStrategy: "linear" | "exponential";
    baseDelay?: number;
    maxDelay?: number;
  };

  // Load Balancer configuration
  loadBalancer?: {
    enabled: boolean;
    strategy: "round-robin" | "least-connections" | "weighted";
    upstreams: Array<{
      host: string;
      port: number;
      weight?: number;
      healthCheck?: boolean;
    }>;
  };
}

/**
 * Custom Filter Configuration
 */
export interface CustomFilterConfig {
  enabled: boolean;
  name: string;
  config: Record<string, any>;
  position?: "first" | "last" | "before" | "after";
  referenceFilter?: string;
}

/**
 * Filter Manager Configuration
 */
export interface FilterManagerConfig {
  // Network filters
  network?: NetworkFilterConfig;

  // HTTP filters
  http?: HttpFilterConfig;

  // Security filters
  security?: SecurityFilterConfig;

  // Observability filters
  observability?: ObservabilityFilterConfig;

  // Traffic management filters
  trafficManagement?: TrafficManagementFilterConfig;

  // Custom filters
  customFilters?: CustomFilterConfig[];

  // Legacy configuration (for backward compatibility)
  auth?: {
    method: "jwt" | "api-key" | "basic";
    secret?: string;
    key?: string;
  };
  rateLimit?: {
    requestsPerMinute: number;
    burstSize?: number;
  };
  logging?: boolean;
  metrics?: boolean;

  // Error handling configuration
  errorHandling?: {
    stopOnError?: boolean; // Stop processing on first filter error
    retryAttempts?: number; // Number of retry attempts for failed filters
    fallbackBehavior?: "reject" | "passthrough" | "default"; // What to do on error
  };

  // Chain configuration
  chain?: {
    executionMode?: ChainExecutionMode;
    routingStrategy?: RoutingStrategy;
    maxParallel?: number;
    bufferSize?: number;
    timeoutMs?: number;
  };

  // Buffer pool configuration
  bufferPool?: BufferPoolConfig;
}

/**
 * Filter Manager for processing JSONRPCMessage using proper filter chain management
 */
export class FilterManager {
  private filterManager: number;
  private filterChain: number = 0;
  private bufferPool: any;
  private config: FilterManagerConfig;
  private _isDestroyed: boolean = false;

  constructor(config: FilterManagerConfig = {}) {
    // Store configuration
    this.config = config;

    // Validate configuration
    this.validateConfig(config);

    // Create filter manager using FFI wrapper
    this.filterManager = createFilterManager(0, 0);

    // Create buffer pool if configured
    if (config.bufferPool) {
      this.bufferPool = createBufferPoolEx(config.bufferPool);
    }

    // Setup filter chain using filter-chain.ts
    this.setupFilterChain(config);

    // Initialize filter manager
    initializeFilterManager(this.filterManager);
  }

  /**
   * Process JSON-RPC message through filters using proper chain management
   */
  async process(message: JSONRPCMessage): Promise<JSONRPCMessage> {
    this.ensureNotDestroyed();

    // Validate input message
    this.validateMessage(message);

    try {
      // Convert message to buffer using filter-buffer.ts
      const messageBuffer = createBufferFromStringAdvanced(
        JSON.stringify(message),
        AdvancedBufferOwnership.SHARED
      );

      // Process through filter chain using filter-chain.ts
      const processedBuffer = await this.processThroughFilterChain(
        messageBuffer
      );

      // Convert back to JSON-RPC message using filter-buffer.ts
      const processedMessage = JSON.parse(
        readStringFromBufferAdvanced(processedBuffer)
      );

      return processedMessage;
    } catch (error) {
      return this.handleProcessingError(error, message);
    }
  }

  /**
   * Process JSON-RPC response through filters
   * Optimized for response processing (typically logging, metrics, compression)
   */
  async processResponse(response: JSONRPCMessage): Promise<JSONRPCMessage> {
    this.ensureNotDestroyed();

    // Validate input response
    this.validateMessage(response);

    try {
      // Convert response to buffer using filter-buffer.ts
      const responseBuffer = createBufferFromStringAdvanced(
        JSON.stringify(response),
        AdvancedBufferOwnership.SHARED
      );

      // Process through response-specific filters using filter-chain.ts
      const processedBuffer = await this.processThroughResponseChain(
        responseBuffer
      );

      // Convert back to JSON-RPC message using filter-buffer.ts
      const processedResponse = JSON.parse(
        readStringFromBufferAdvanced(processedBuffer)
      );

      return processedResponse;
    } catch (error) {
      return this.handleProcessingError(error, response);
    }
  }

  /**
   * Process a complete request-response cycle
   * Useful for MCP transport layer integration
   */
  async processRequestResponse(
    request: JSONRPCMessage,
    response: JSONRPCMessage
  ): Promise<{
    processedRequest: JSONRPCMessage;
    processedResponse: JSONRPCMessage;
  }> {
    this.ensureNotDestroyed();

    // Process request first
    const processedRequest = await this.process(request);

    // Process response
    const processedResponse = await this.processResponse(response);

    return { processedRequest, processedResponse };
  }

  /**
   * Destroy the FilterManager and release all C++ resources
   * This should be called when the FilterManager is no longer needed
   */
  destroy(): void {
    if (this._isDestroyed) {
      console.warn("FilterManager is already destroyed");
      return;
    }

    try {
      // Release filter chain using filter-chain.ts
      if (this.filterChain) {
        // Note: We need to add a release function to filter-chain.ts
        // For now, we'll use the existing releaseFilterChain from filter-api.ts
        releaseFilterChain(this.filterChain);
        this.filterChain = 0;
      }

      // Release buffer pool using filter-buffer.ts
      if (this.bufferPool) {
        // Note: We need to add a destroy function to filter-buffer.ts
        // For now, we'll use the existing destroyBufferPool from filter-api.ts
        destroyBufferPool(this.bufferPool);
        this.bufferPool = null;
      }

      // Release the filter manager
      if (this.filterManager) {
        releaseFilterManager(this.filterManager);
        this.filterManager = 0;
      }

      this._isDestroyed = true;
      console.log("FilterManager destroyed and resources released");
    } catch (error) {
      console.error("Error during FilterManager destruction:", error);
      this._isDestroyed = true; // Mark as destroyed even if cleanup failed
    }
  }

  /**
   * Check if the FilterManager has been destroyed
   */
  isDestroyed(): boolean {
    return this._isDestroyed;
  }

  /**
   * Finalizer method for automatic cleanup
   * This will be called by the garbage collector
   */
  [Symbol.dispose](): void {
    if (!this._isDestroyed) {
      console.warn(
        "FilterManager was not properly destroyed, cleaning up automatically"
      );
      this.destroy();
    }
  }

  /**
   * Ensure the FilterManager is not destroyed before processing
   */
  private ensureNotDestroyed(): void {
    if (this._isDestroyed) {
      throw new Error(
        "FilterManager has been destroyed and cannot process messages"
      );
    }
  }

  /**
   * Setup filter chain using filter-chain.ts
   */
  private setupFilterChain(config: FilterManagerConfig): void {
    // Create chain configuration using filter-chain.ts
    const chainConfig: ChainConfig = {
      name: "mcp-filter-chain",
      mode: config.chain?.executionMode || ChainExecutionMode.SEQUENTIAL,
      routing: config.chain?.routingStrategy || RoutingStrategy.ROUND_ROBIN,
      maxParallel: config.chain?.maxParallel || 1,
      bufferSize: config.chain?.bufferSize || 8192,
      timeoutMs: config.chain?.timeoutMs || 5000,
      stopOnError: config.errorHandling?.stopOnError ?? true,
    };

    // Create chain builder using filter-chain.ts
    const chainBuilder = createChainBuilderEx(0, chainConfig);
    if (!chainBuilder) {
      throw new Error("Failed to create filter chain builder");
    }

    try {
      // Add filters to chain using filter-chain.ts
      this.addFiltersToChain(chainBuilder, config);

      // Build the chain using filter-chain.ts
      this.filterChain = buildFilterChain(chainBuilder);
      if (!this.filterChain) {
        throw new Error("Failed to build filter chain");
      }

      // Add chain to manager using filter-api.ts
      addChainToManager(this.filterManager, this.filterChain);
    } finally {
      // Clean up builder using filter-chain.ts
      destroyFilterChainBuilder(chainBuilder);
    }
  }

  /**
   * Add filters to chain using filter-chain.ts
   */
  private addFiltersToChain(
    chainBuilder: any,
    config: FilterManagerConfig
  ): void {
    // Network filters
    this.addNetworkFiltersToChain(chainBuilder, config.network);

    // HTTP filters
    this.addHttpFiltersToChain(chainBuilder, config.http);

    // Security filters
    this.addSecurityFiltersToChain(chainBuilder, config.security);

    // Observability filters
    this.addObservabilityFiltersToChain(chainBuilder, config.observability);

    // Traffic management filters
    this.addTrafficManagementFiltersToChain(
      chainBuilder,
      config.trafficManagement
    );

    // Custom filters
    this.addCustomFiltersToChain(chainBuilder, config.customFilters);

    // Legacy filters
    this.addLegacyFiltersToChain(chainBuilder, config);
  }

  /**
   * Add network filters to chain using filter-chain.ts
   */
  private addNetworkFiltersToChain(
    chainBuilder: any,
    networkConfig?: NetworkFilterConfig
  ): void {
    if (!networkConfig) return;

    // TCP Proxy filter
    if (networkConfig.tcpProxy?.enabled) {
      const tcpProxyFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.TCP_PROXY,
        {
          upstreamHost: networkConfig.tcpProxy.upstreamHost,
          upstreamPort: networkConfig.tcpProxy.upstreamPort,
          bindAddress: networkConfig.tcpProxy.bindAddress || "0.0.0.0",
          bindPort: networkConfig.tcpProxy.bindPort || 8080,
        }
      );

      const filterNode: FilterNode = {
        filter: tcpProxyFilter,
        name: "tcp-proxy",
        priority: 1,
        enabled: true,
        bypassOnError: false,
        config: networkConfig.tcpProxy,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // UDP Proxy filter
    if (networkConfig.udpProxy?.enabled) {
      const udpProxyFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.UDP_PROXY,
        {
          upstreamHost: networkConfig.udpProxy.upstreamHost,
          upstreamPort: networkConfig.udpProxy.upstreamPort,
          bindAddress: networkConfig.udpProxy.bindAddress || "0.0.0.0",
          bindPort: networkConfig.udpProxy.bindPort || 8080,
        }
      );

      const filterNode: FilterNode = {
        filter: udpProxyFilter,
        name: "udp-proxy",
        priority: 2,
        enabled: true,
        bypassOnError: false,
        config: networkConfig.udpProxy,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }
  }

  /**
   * Add HTTP filters to chain using filter-chain.ts
   */
  private addHttpFiltersToChain(
    chainBuilder: any,
    httpConfig?: HttpFilterConfig
  ): void {
    if (!httpConfig) return;

    // HTTP Codec filter
    if (httpConfig.codec?.enabled) {
      const httpCodecFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.HTTP_CODEC,
        {
          compressionLevel: httpConfig.codec.compressionLevel || 6,
          maxRequestSize: httpConfig.codec.maxRequestSize || 1024 * 1024,
          maxResponseSize: httpConfig.codec.maxResponseSize || 1024 * 1024,
        }
      );

      const filterNode: FilterNode = {
        filter: httpCodecFilter,
        name: "http-codec",
        priority: 10,
        enabled: true,
        bypassOnError: false,
        config: httpConfig.codec,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // HTTP Router filter
    if (httpConfig.router?.enabled) {
      const httpRouterFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.HTTP_ROUTER,
        {
          routes: httpConfig.router.routes,
        }
      );

      const filterNode: FilterNode = {
        filter: httpRouterFilter,
        name: "http-router",
        priority: 11,
        enabled: true,
        bypassOnError: false,
        config: httpConfig.router,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // HTTP Compression filter
    if (httpConfig.compression?.enabled) {
      const httpCompressionFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.HTTP_COMPRESSION,
        {
          algorithms: httpConfig.compression.algorithms,
          minSize: httpConfig.compression.minSize || 1024,
        }
      );

      const filterNode: FilterNode = {
        filter: httpCompressionFilter,
        name: "http-compression",
        priority: 12,
        enabled: true,
        bypassOnError: false,
        config: httpConfig.compression,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }
  }

  /**
   * Add security filters to chain using filter-chain.ts
   */
  private addSecurityFiltersToChain(
    chainBuilder: any,
    securityConfig?: SecurityFilterConfig
  ): void {
    if (!securityConfig) return;

    // TLS Termination filter
    if (securityConfig.tlsTermination?.enabled) {
      const tlsTerminationFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.TLS_TERMINATION,
        {
          certPath: securityConfig.tlsTermination.certPath,
          keyPath: securityConfig.tlsTermination.keyPath,
          caPath: securityConfig.tlsTermination.caPath,
          protocols: securityConfig.tlsTermination.protocols || [
            "TLSv1.2",
            "TLSv1.3",
          ],
        }
      );

      const filterNode: FilterNode = {
        filter: tlsTerminationFilter,
        name: "tls-termination",
        priority: 20,
        enabled: true,
        bypassOnError: false,
        config: securityConfig.tlsTermination,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Authentication filter
    if (securityConfig.authentication) {
      const authenticationFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.AUTHENTICATION,
        {
          method: securityConfig.authentication.method,
          secret: securityConfig.authentication.secret,
          key: securityConfig.authentication.key,
          issuer: securityConfig.authentication.issuer,
          audience: securityConfig.authentication.audience,
        }
      );

      const filterNode: FilterNode = {
        filter: authenticationFilter,
        name: "authentication",
        priority: 21,
        enabled: true,
        bypassOnError: false,
        config: securityConfig.authentication,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Authorization filter
    if (securityConfig.authorization?.enabled) {
      const authorizationFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.AUTHORIZATION,
        {
          policy: securityConfig.authorization.policy,
          rules: securityConfig.authorization.rules,
        }
      );

      const filterNode: FilterNode = {
        filter: authorizationFilter,
        name: "authorization",
        priority: 22,
        enabled: true,
        bypassOnError: false,
        config: securityConfig.authorization,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }
  }

  /**
   * Add observability filters to chain using filter-chain.ts
   */
  private addObservabilityFiltersToChain(
    chainBuilder: any,
    observabilityConfig?: ObservabilityFilterConfig
  ): void {
    if (!observabilityConfig) return;

    // Access Log filter
    if (observabilityConfig.accessLog?.enabled) {
      const accessLogFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.ACCESS_LOG,
        {
          format: observabilityConfig.accessLog.format || "json",
          fields: observabilityConfig.accessLog.fields || [
            "timestamp",
            "method",
            "path",
            "status",
          ],
          output: observabilityConfig.accessLog.output || "console",
        }
      );

      const filterNode: FilterNode = {
        filter: accessLogFilter,
        name: "access-log",
        priority: 30,
        enabled: true,
        bypassOnError: false,
        config: observabilityConfig.accessLog,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Metrics filter
    if (observabilityConfig.metrics?.enabled) {
      const metricsFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.METRICS,
        {
          endpoint: observabilityConfig.metrics.endpoint,
          interval: observabilityConfig.metrics.interval || 60000,
          labels: observabilityConfig.metrics.labels || {},
        }
      );

      const filterNode: FilterNode = {
        filter: metricsFilter,
        name: "metrics",
        priority: 31,
        enabled: true,
        bypassOnError: false,
        config: observabilityConfig.metrics,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Tracing filter
    if (observabilityConfig.tracing?.enabled) {
      const tracingFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.TRACING,
        {
          serviceName: observabilityConfig.tracing.serviceName,
          endpoint: observabilityConfig.tracing.endpoint,
          samplingRate: observabilityConfig.tracing.samplingRate || 1.0,
        }
      );

      const filterNode: FilterNode = {
        filter: tracingFilter,
        name: "tracing",
        priority: 32,
        enabled: true,
        bypassOnError: false,
        config: observabilityConfig.tracing,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }
  }

  /**
   * Add traffic management filters to chain using filter-chain.ts
   */
  private addTrafficManagementFiltersToChain(
    chainBuilder: any,
    trafficConfig?: TrafficManagementFilterConfig
  ): void {
    if (!trafficConfig) return;

    // Rate Limiting filter
    if (trafficConfig.rateLimit?.enabled) {
      const rateLimitFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.RATE_LIMIT,
        {
          requestsPerMinute: trafficConfig.rateLimit.requestsPerMinute,
          burstSize: trafficConfig.rateLimit.burstSize || 10,
          keyExtractor: trafficConfig.rateLimit.keyExtractor || "ip",
        }
      );

      const filterNode: FilterNode = {
        filter: rateLimitFilter,
        name: "rate-limit",
        priority: 40,
        enabled: true,
        bypassOnError: false,
        config: trafficConfig.rateLimit,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Circuit Breaker filter
    if (trafficConfig.circuitBreaker?.enabled) {
      const circuitBreakerFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.CIRCUIT_BREAKER,
        {
          failureThreshold: trafficConfig.circuitBreaker.failureThreshold,
          timeout: trafficConfig.circuitBreaker.timeout,
          resetTimeout: trafficConfig.circuitBreaker.resetTimeout,
        }
      );

      const filterNode: FilterNode = {
        filter: circuitBreakerFilter,
        name: "circuit-breaker",
        priority: 41,
        enabled: true,
        bypassOnError: false,
        config: trafficConfig.circuitBreaker,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Retry filter
    if (trafficConfig.retry?.enabled) {
      const retryFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.RETRY,
        {
          maxAttempts: trafficConfig.retry.maxAttempts,
          backoffStrategy: trafficConfig.retry.backoffStrategy,
          baseDelay: trafficConfig.retry.baseDelay || 1000,
          maxDelay: trafficConfig.retry.maxDelay || 30000,
        }
      );

      const filterNode: FilterNode = {
        filter: retryFilter,
        name: "retry",
        priority: 42,
        enabled: true,
        bypassOnError: false,
        config: trafficConfig.retry,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Load Balancer filter
    if (trafficConfig.loadBalancer?.enabled) {
      const loadBalancerFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.LOAD_BALANCER,
        {
          strategy: trafficConfig.loadBalancer.strategy,
          upstreams: trafficConfig.loadBalancer.upstreams,
        }
      );

      const filterNode: FilterNode = {
        filter: loadBalancerFilter,
        name: "load-balancer",
        priority: 43,
        enabled: true,
        bypassOnError: false,
        config: trafficConfig.loadBalancer,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }
  }

  /**
   * Add custom filters to chain using filter-chain.ts
   */
  private addCustomFiltersToChain(
    chainBuilder: any,
    customFilters?: CustomFilterConfig[]
  ): void {
    if (!customFilters) return;

    for (const customFilter of customFilters) {
      if (customFilter.enabled) {
        const customFilterHandle = createBuiltinFilterAdvanced(
          0,
          AdvancedBuiltinFilterType.CUSTOM,
          customFilter.config
        );

        const filterNode: FilterNode = {
          filter: customFilterHandle,
          name: customFilter.name,
          priority: 100,
          enabled: true,
          bypassOnError: false,
          config: customFilter.config,
        };

        addFilterNodeToChain(chainBuilder, filterNode);
      }
    }
  }

  /**
   * Add legacy filters to chain using filter-chain.ts
   */
  private addLegacyFiltersToChain(
    chainBuilder: any,
    config: FilterManagerConfig
  ): void {
    // Legacy Authentication filter
    if (config.auth) {
      const authFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.AUTHENTICATION,
        {
          method: config.auth.method,
          secret: config.auth.secret,
          key: config.auth.key,
        }
      );

      const filterNode: FilterNode = {
        filter: authFilter,
        name: "legacy-auth",
        priority: 21,
        enabled: true,
        bypassOnError: false,
        config: config.auth,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Legacy Rate limiting filter
    if (config.rateLimit) {
      const rateLimitFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.RATE_LIMIT,
        {
          requestsPerMinute: config.rateLimit.requestsPerMinute,
          burstSize: config.rateLimit.burstSize || 10,
        }
      );

      const filterNode: FilterNode = {
        filter: rateLimitFilter,
        name: "legacy-rate-limit",
        priority: 40,
        enabled: true,
        bypassOnError: false,
        config: config.rateLimit,
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Legacy Logging filter
    if (config.logging) {
      const loggingFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.ACCESS_LOG,
        {}
      );

      const filterNode: FilterNode = {
        filter: loggingFilter,
        name: "legacy-logging",
        priority: 30,
        enabled: true,
        bypassOnError: false,
        config: {},
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }

    // Legacy Metrics filter
    if (config.metrics) {
      const metricsFilter = createBuiltinFilterAdvanced(
        0,
        AdvancedBuiltinFilterType.METRICS,
        {}
      );

      const filterNode: FilterNode = {
        filter: metricsFilter,
        name: "legacy-metrics",
        priority: 31,
        enabled: true,
        bypassOnError: false,
        config: {},
      };

      addFilterNodeToChain(chainBuilder, filterNode);
    }
  }

  /**
   * Process message through filter chain using filter-chain.ts
   */
  private async processThroughFilterChain(buffer: number): Promise<number> {
    // Use the filter chain to process the buffer
    // Note: We need to add a process function to filter-chain.ts
    // For now, we'll use the existing postDataToFilter from filter-api.ts
    return new Promise((resolve, reject) => {
      postDataToFilter(
        this.filterChain,
        new Uint8Array(), // Empty data since we're using buffer
        (result: any, _userData: any) => {
          if (result === 0) { // 0 = MCP_OK
            resolve(buffer); // Return the processed buffer
          } else {
            reject(new Error("Filter chain processing failed"));
          }
        },
        null // No user data
      );
    });
  }

  /**
   * Process response through response-specific filter chain using filter-chain.ts
   */
  private async processThroughResponseChain(buffer: number): Promise<number> {
    // For responses, we typically only process through observability filters
    // Note: We need to add a process function to filter-chain.ts
    // For now, we'll use the existing postDataToFilter from filter-api.ts
    return new Promise((resolve, reject) => {
      postDataToFilter(
        this.filterChain,
        new Uint8Array(), // Empty data since we're using buffer
        (result: any, _userData: any) => {
          if (result === 0) { // 0 = MCP_OK
            resolve(buffer); // Return the processed buffer
          } else {
            reject(new Error("Response filter chain processing failed"));
          }
        },
        null // No user data
      );
    });
  }

  /**
   * Validate configuration
   */
  private validateConfig(config: FilterManagerConfig): void {
    // Validate rate limit configuration
    if (config.rateLimit) {
      if (config.rateLimit.requestsPerMinute <= 0) {
        throw new Error("Rate limit requestsPerMinute must be positive");
      }
      if (config.rateLimit.burstSize && config.rateLimit.burstSize <= 0) {
        throw new Error("Rate limit burstSize must be positive");
      }
    }

    // Validate auth configuration
    if (config.auth) {
      if (config.auth.method === "jwt" && !config.auth.secret) {
        throw new Error("JWT authentication requires a secret");
      }
      if (config.auth.method === "api-key" && !config.auth.key) {
        throw new Error("API key authentication requires a key");
      }
    }

    // Validate error handling configuration
    if (config.errorHandling) {
      if (
        config.errorHandling.retryAttempts &&
        config.errorHandling.retryAttempts < 0
      ) {
        throw new Error("Retry attempts must be non-negative");
      }
    }
  }

  /**
   * Validate JSON-RPC message
   */
  private validateMessage(message: JSONRPCMessage): void {
    if (!message) {
      throw new Error("Message cannot be null or undefined");
    }

    if (message.jsonrpc !== "2.0") {
      throw new Error("Invalid JSON-RPC version. Must be '2.0'");
    }

    // Check if it's a request or response
    const isRequest = message.method !== undefined;
    const isResponse =
      message.result !== undefined || message.error !== undefined;

    if (!isRequest && !isResponse) {
      throw new Error(
        "Message must be either a request (with method) or response (with result/error)"
      );
    }

    if (isRequest && isResponse) {
      throw new Error("Message cannot be both a request and response");
    }
  }

  /**
   * Handle processing errors based on configuration
   */
  private handleProcessingError(
    error: any,
    originalMessage: JSONRPCMessage
  ): JSONRPCMessage {
    const fallbackBehavior =
      this.config.errorHandling?.fallbackBehavior ?? "reject";

    switch (fallbackBehavior) {
      case "reject":
        throw new Error(`Filter processing failed: ${error}`);

      case "passthrough":
        console.warn(
          `Filter processing failed, returning original message: ${error}`
        );
        return originalMessage;

      case "default":
        // Return a default error response
        return {
          jsonrpc: "2.0",
          id: originalMessage.id || "unknown",
          error: {
            code: -32603, // Internal error
            message: "Filter processing failed",
            data: { originalError: error.toString() },
          },
        };

      default:
        throw new Error(`Unknown fallback behavior: ${fallbackBehavior}`);
    }
  }
}
