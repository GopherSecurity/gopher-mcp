/**
 * @file filter-manager.ts
 * @brief Filter Manager for processing JSONRPCMessage through C++ filters
 *
 * This provides a simple interface to process MCP JSON-RPC messages
 * through our C++ filter infrastructure using FFI wrappers.
 */

import {
  addFilterToManager,
  BufferOwnership,
  BuiltinFilterType,
  createBufferFromString,
  createBuiltinFilter,
  createFilterManager,
  initializeFilterManager,
  postDataToFilter,
  readStringFromBuffer,
  releaseFilter,
  releaseFilterManager,
} from "./index";

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
}

/**
 * Filter Manager for processing JSONRPCMessage
 */
export class FilterManager {
  private filterManager: number;
  private filters: {
    // Network filters
    tcpProxy?: number;
    udpProxy?: number;
    
    // HTTP filters
    httpCodec?: number;
    httpRouter?: number;
    httpCompression?: number;
    
    // Security filters
    tlsTermination?: number;
    authentication?: number;
    authorization?: number;
    
    // Observability filters
    accessLog?: number;
    metrics?: number;
    tracing?: number;
    
    // Traffic management filters
    rateLimit?: number;
    circuitBreaker?: number;
    retry?: number;
    loadBalancer?: number;
    
    // Custom filters
    customFilters?: Map<string, number>;
    
    // Legacy filters (for backward compatibility)
    auth?: number;
    logging?: number;
  } = {};
  private config: FilterManagerConfig;
  private _isDestroyed: boolean = false;

  constructor(config: FilterManagerConfig = {}) {
    // Store configuration
    this.config = config;

    // Validate configuration
    this.validateConfig(config);

    // Create filter manager using FFI wrapper
    this.filterManager = createFilterManager(0, 0);

    // Setup filters based on configuration
    this.setupFilters(config);

    // Initialize filter manager
    initializeFilterManager(this.filterManager);
  }

  /**
   * Process JSON-RPC message through filters
   */
  async process(message: JSONRPCMessage): Promise<JSONRPCMessage> {
    this.ensureNotDestroyed();

    // Validate input message
    this.validateMessage(message);

    try {
      // Convert message to buffer using FFI wrapper
      const messageBuffer = createBufferFromString(
        JSON.stringify(message),
        BufferOwnership.SHARED
      );

      // Process through filters with error handling
      const processedBuffer = await this.processThroughFilters(messageBuffer);

      // Convert back to JSON-RPC message
      const processedMessage = JSON.parse(
        readStringFromBuffer(processedBuffer)
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
      // Convert response to buffer using FFI wrapper
      const responseBuffer = createBufferFromString(
        JSON.stringify(response),
        BufferOwnership.SHARED
      );

      // Process through response-specific filters
      const processedBuffer = await this.processThroughResponseFilters(
        responseBuffer
      );

      // Convert back to JSON-RPC message
      const processedResponse = JSON.parse(
        readStringFromBuffer(processedBuffer)
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
      // Release all individual filters
      this.releaseAllFilters();

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
   * Release all filters
   */
  private releaseAllFilters(): void {
    // Network filters
    if (this.filters.tcpProxy) {
      releaseFilter(this.filters.tcpProxy);
      delete this.filters.tcpProxy;
    }
    if (this.filters.udpProxy) {
      releaseFilter(this.filters.udpProxy);
      delete this.filters.udpProxy;
    }

    // HTTP filters
    if (this.filters.httpCodec) {
      releaseFilter(this.filters.httpCodec);
      delete this.filters.httpCodec;
    }
    if (this.filters.httpRouter) {
      releaseFilter(this.filters.httpRouter);
      delete this.filters.httpRouter;
    }
    if (this.filters.httpCompression) {
      releaseFilter(this.filters.httpCompression);
      delete this.filters.httpCompression;
    }

    // Security filters
    if (this.filters.tlsTermination) {
      releaseFilter(this.filters.tlsTermination);
      delete this.filters.tlsTermination;
    }
    if (this.filters.authentication) {
      releaseFilter(this.filters.authentication);
      delete this.filters.authentication;
    }
    if (this.filters.authorization) {
      releaseFilter(this.filters.authorization);
      delete this.filters.authorization;
    }

    // Observability filters
    if (this.filters.accessLog) {
      releaseFilter(this.filters.accessLog);
      delete this.filters.accessLog;
    }
    if (this.filters.metrics) {
      releaseFilter(this.filters.metrics);
      delete this.filters.metrics;
    }
    if (this.filters.tracing) {
      releaseFilter(this.filters.tracing);
      delete this.filters.tracing;
    }

    // Traffic management filters
    if (this.filters.rateLimit) {
      releaseFilter(this.filters.rateLimit);
      delete this.filters.rateLimit;
    }
    if (this.filters.circuitBreaker) {
      releaseFilter(this.filters.circuitBreaker);
      delete this.filters.circuitBreaker;
    }
    if (this.filters.retry) {
      releaseFilter(this.filters.retry);
      delete this.filters.retry;
    }
    if (this.filters.loadBalancer) {
      releaseFilter(this.filters.loadBalancer);
      delete this.filters.loadBalancer;
    }

    // Custom filters
    if (this.filters.customFilters) {
      for (const [_name, filterHandle] of this.filters.customFilters) {
        releaseFilter(filterHandle);
      }
      this.filters.customFilters.clear();
      delete this.filters.customFilters;
    }

    // Legacy filters
    if (this.filters.auth) {
      releaseFilter(this.filters.auth);
      delete this.filters.auth;
    }
    if (this.filters.logging) {
      releaseFilter(this.filters.logging);
      delete this.filters.logging;
    }
  }

  /**
   * Setup filters based on configuration
   */
  private setupFilters(config: FilterManagerConfig): void {
    // Initialize custom filters map
    this.filters.customFilters = new Map();

    // Setup Network Filters
    this.setupNetworkFilters(config.network);

    // Setup HTTP Filters
    this.setupHttpFilters(config.http);

    // Setup Security Filters
    this.setupSecurityFilters(config.security);

    // Setup Observability Filters
    this.setupObservabilityFilters(config.observability);

    // Setup Traffic Management Filters
    this.setupTrafficManagementFilters(config.trafficManagement);

    // Setup Custom Filters
    this.setupCustomFilters(config.customFilters);

    // Setup Legacy Filters (for backward compatibility)
    this.setupLegacyFilters(config);
  }

  /**
   * Setup Network Filters
   */
  private setupNetworkFilters(networkConfig?: NetworkFilterConfig): void {
    if (!networkConfig) return;

    // TCP Proxy filter
    if (networkConfig.tcpProxy?.enabled) {
      this.filters.tcpProxy = createBuiltinFilter(
        0,
        BuiltinFilterType.TCP_PROXY,
        {
          upstreamHost: networkConfig.tcpProxy.upstreamHost,
          upstreamPort: networkConfig.tcpProxy.upstreamPort,
          bindAddress: networkConfig.tcpProxy.bindAddress || "0.0.0.0",
          bindPort: networkConfig.tcpProxy.bindPort || 8080,
        }
      );
      addFilterToManager(this.filterManager, this.filters.tcpProxy);
    }

    // UDP Proxy filter
    if (networkConfig.udpProxy?.enabled) {
      this.filters.udpProxy = createBuiltinFilter(
        0,
        BuiltinFilterType.UDP_PROXY,
        {
          upstreamHost: networkConfig.udpProxy.upstreamHost,
          upstreamPort: networkConfig.udpProxy.upstreamPort,
          bindAddress: networkConfig.udpProxy.bindAddress || "0.0.0.0",
          bindPort: networkConfig.udpProxy.bindPort || 8080,
        }
      );
      addFilterToManager(this.filterManager, this.filters.udpProxy);
    }
  }

  /**
   * Setup HTTP Filters
   */
  private setupHttpFilters(httpConfig?: HttpFilterConfig): void {
    if (!httpConfig) return;

    // HTTP Codec filter
    if (httpConfig.codec?.enabled) {
      this.filters.httpCodec = createBuiltinFilter(
        0,
        BuiltinFilterType.HTTP_CODEC,
        {
          compressionLevel: httpConfig.codec.compressionLevel || 6,
          maxRequestSize: httpConfig.codec.maxRequestSize || 1024 * 1024, // 1MB
          maxResponseSize: httpConfig.codec.maxResponseSize || 1024 * 1024, // 1MB
        }
      );
      addFilterToManager(this.filterManager, this.filters.httpCodec);
    }

    // HTTP Router filter
    if (httpConfig.router?.enabled) {
      this.filters.httpRouter = createBuiltinFilter(
        0,
        BuiltinFilterType.HTTP_ROUTER,
        {
          routes: httpConfig.router.routes,
        }
      );
      addFilterToManager(this.filterManager, this.filters.httpRouter);
    }

    // HTTP Compression filter
    if (httpConfig.compression?.enabled) {
      this.filters.httpCompression = createBuiltinFilter(
        0,
        BuiltinFilterType.HTTP_COMPRESSION,
        {
          algorithms: httpConfig.compression.algorithms,
          minSize: httpConfig.compression.minSize || 1024, // 1KB
        }
      );
      addFilterToManager(this.filterManager, this.filters.httpCompression);
    }
  }

  /**
   * Setup Security Filters
   */
  private setupSecurityFilters(securityConfig?: SecurityFilterConfig): void {
    if (!securityConfig) return;

    // TLS Termination filter
    if (securityConfig.tlsTermination?.enabled) {
      this.filters.tlsTermination = createBuiltinFilter(
        0,
        BuiltinFilterType.TLS_TERMINATION,
        {
          certPath: securityConfig.tlsTermination.certPath,
          keyPath: securityConfig.tlsTermination.keyPath,
          caPath: securityConfig.tlsTermination.caPath,
          protocols: securityConfig.tlsTermination.protocols || ["TLSv1.2", "TLSv1.3"],
        }
      );
      addFilterToManager(this.filterManager, this.filters.tlsTermination);
    }

    // Authentication filter
    if (securityConfig.authentication) {
      this.filters.authentication = createBuiltinFilter(
        0,
        BuiltinFilterType.AUTHENTICATION,
        {
          method: securityConfig.authentication.method,
          secret: securityConfig.authentication.secret,
          key: securityConfig.authentication.key,
          issuer: securityConfig.authentication.issuer,
          audience: securityConfig.authentication.audience,
        }
      );
      addFilterToManager(this.filterManager, this.filters.authentication);
    }

    // Authorization filter
    if (securityConfig.authorization?.enabled) {
      this.filters.authorization = createBuiltinFilter(
        0,
        BuiltinFilterType.AUTHORIZATION,
        {
          policy: securityConfig.authorization.policy,
          rules: securityConfig.authorization.rules,
        }
      );
      addFilterToManager(this.filterManager, this.filters.authorization);
    }
  }

  /**
   * Setup Observability Filters
   */
  private setupObservabilityFilters(observabilityConfig?: ObservabilityFilterConfig): void {
    if (!observabilityConfig) return;

    // Access Log filter
    if (observabilityConfig.accessLog?.enabled) {
      this.filters.accessLog = createBuiltinFilter(
        0,
        BuiltinFilterType.ACCESS_LOG,
        {
          format: observabilityConfig.accessLog.format || "json",
          fields: observabilityConfig.accessLog.fields || ["timestamp", "method", "path", "status"],
          output: observabilityConfig.accessLog.output || "console",
        }
      );
      addFilterToManager(this.filterManager, this.filters.accessLog);
    }

    // Metrics filter
    if (observabilityConfig.metrics?.enabled) {
      this.filters.metrics = createBuiltinFilter(
        0,
        BuiltinFilterType.METRICS,
        {
          endpoint: observabilityConfig.metrics.endpoint,
          interval: observabilityConfig.metrics.interval || 60000, // 1 minute
          labels: observabilityConfig.metrics.labels || {},
        }
      );
      addFilterToManager(this.filterManager, this.filters.metrics);
    }

    // Tracing filter
    if (observabilityConfig.tracing?.enabled) {
      this.filters.tracing = createBuiltinFilter(
        0,
        BuiltinFilterType.TRACING,
        {
          serviceName: observabilityConfig.tracing.serviceName,
          endpoint: observabilityConfig.tracing.endpoint,
          samplingRate: observabilityConfig.tracing.samplingRate || 1.0,
        }
      );
      addFilterToManager(this.filterManager, this.filters.tracing);
    }
  }

  /**
   * Setup Traffic Management Filters
   */
  private setupTrafficManagementFilters(trafficConfig?: TrafficManagementFilterConfig): void {
    if (!trafficConfig) return;

    // Rate Limiting filter
    if (trafficConfig.rateLimit?.enabled) {
      this.filters.rateLimit = createBuiltinFilter(
        0,
        BuiltinFilterType.RATE_LIMIT,
        {
          requestsPerMinute: trafficConfig.rateLimit.requestsPerMinute,
          burstSize: trafficConfig.rateLimit.burstSize || 10,
          keyExtractor: trafficConfig.rateLimit.keyExtractor || "ip",
        }
      );
      addFilterToManager(this.filterManager, this.filters.rateLimit);
    }

    // Circuit Breaker filter
    if (trafficConfig.circuitBreaker?.enabled) {
      this.filters.circuitBreaker = createBuiltinFilter(
        0,
        BuiltinFilterType.CIRCUIT_BREAKER,
        {
          failureThreshold: trafficConfig.circuitBreaker.failureThreshold,
          timeout: trafficConfig.circuitBreaker.timeout,
          resetTimeout: trafficConfig.circuitBreaker.resetTimeout,
        }
      );
      addFilterToManager(this.filterManager, this.filters.circuitBreaker);
    }

    // Retry filter
    if (trafficConfig.retry?.enabled) {
      this.filters.retry = createBuiltinFilter(
        0,
        BuiltinFilterType.RETRY,
        {
          maxAttempts: trafficConfig.retry.maxAttempts,
          backoffStrategy: trafficConfig.retry.backoffStrategy,
          baseDelay: trafficConfig.retry.baseDelay || 1000, // 1 second
          maxDelay: trafficConfig.retry.maxDelay || 30000, // 30 seconds
        }
      );
      addFilterToManager(this.filterManager, this.filters.retry);
    }

    // Load Balancer filter
    if (trafficConfig.loadBalancer?.enabled) {
      this.filters.loadBalancer = createBuiltinFilter(
        0,
        BuiltinFilterType.LOAD_BALANCER,
        {
          strategy: trafficConfig.loadBalancer.strategy,
          upstreams: trafficConfig.loadBalancer.upstreams,
        }
      );
      addFilterToManager(this.filterManager, this.filters.loadBalancer);
    }
  }

  /**
   * Setup Custom Filters
   */
  private setupCustomFilters(customFilters?: CustomFilterConfig[]): void {
    if (!customFilters) return;

    for (const customFilter of customFilters) {
      if (customFilter.enabled) {
        const filterHandle = createBuiltinFilter(
          0,
          BuiltinFilterType.CUSTOM,
          customFilter.config
        );
        
        this.filters.customFilters!.set(customFilter.name, filterHandle);
        addFilterToManager(this.filterManager, filterHandle);
      }
    }
  }

  /**
   * Setup Legacy Filters (for backward compatibility)
   */
  private setupLegacyFilters(config: FilterManagerConfig): void {
    // Legacy Authentication filter
    if (config.auth) {
      this.filters.auth = createBuiltinFilter(
        0,
        BuiltinFilterType.AUTHENTICATION,
        {
          method: config.auth.method,
          secret: config.auth.secret,
          key: config.auth.key,
        }
      );
      addFilterToManager(this.filterManager, this.filters.auth);
    }

    // Legacy Rate limiting filter
    if (config.rateLimit) {
      this.filters.rateLimit = createBuiltinFilter(
        0,
        BuiltinFilterType.RATE_LIMIT,
        {
          requestsPerMinute: config.rateLimit.requestsPerMinute,
          burstSize: config.rateLimit.burstSize || 10,
        }
      );
      addFilterToManager(this.filterManager, this.filters.rateLimit);
    }

    // Legacy Logging filter
    if (config.logging) {
      this.filters.logging = createBuiltinFilter(
        0,
        BuiltinFilterType.ACCESS_LOG,
        {}
      );
      addFilterToManager(this.filterManager, this.filters.logging);
    }

    // Legacy Metrics filter
    if (config.metrics) {
      this.filters.metrics = createBuiltinFilter(
        0,
        BuiltinFilterType.METRICS,
        {}
      );
      addFilterToManager(this.filterManager, this.filters.metrics);
    }
  }

  /**
   * Process message through filters using C++ filter chain
   */
  private async processThroughFilters(buffer: number): Promise<number> {
    // Process through each filter using our FFI wrapper
    let processedBuffer = buffer;

    // Define the processing order for all filters
    const filterChain = [
      // Network filters (first - handle raw network traffic)
      { name: "tcpProxy", filter: this.filters.tcpProxy },
      { name: "udpProxy", filter: this.filters.udpProxy },
      
      // Security filters (early - authentication and authorization)
      { name: "tlsTermination", filter: this.filters.tlsTermination },
      { name: "authentication", filter: this.filters.authentication },
      { name: "authorization", filter: this.filters.authorization },
      { name: "auth", filter: this.filters.auth }, // Legacy auth
      
      // HTTP filters (protocol-specific processing)
      { name: "httpCodec", filter: this.filters.httpCodec },
      { name: "httpRouter", filter: this.filters.httpRouter },
      { name: "httpCompression", filter: this.filters.httpCompression },
      
      // Traffic management filters (rate limiting, circuit breaker, etc.)
      { name: "rateLimit", filter: this.filters.rateLimit },
      { name: "circuitBreaker", filter: this.filters.circuitBreaker },
      { name: "retry", filter: this.filters.retry },
      { name: "loadBalancer", filter: this.filters.loadBalancer },
      
      // Observability filters (logging, metrics, tracing)
      { name: "accessLog", filter: this.filters.accessLog },
      { name: "logging", filter: this.filters.logging }, // Legacy logging
      { name: "metrics", filter: this.filters.metrics },
      { name: "tracing", filter: this.filters.tracing },
      
      // Custom filters (user-defined)
      ...this.getCustomFilters(),
    ];

    for (const { name, filter } of filterChain) {
      if (filter) {
        try {
          processedBuffer = await this.processThroughFilter(
            filter,
            processedBuffer
          );
        } catch (error) {
          const shouldStop = this.config.errorHandling?.stopOnError ?? true;
          if (shouldStop) {
            throw new Error(`Filter '${name}' processing failed: ${error}`);
          }
          // Continue processing other filters if stopOnError is false
          console.warn(`Filter '${name}' failed, continuing: ${error}`);
        }
      }
    }

    return processedBuffer;
  }

  /**
   * Get custom filters as an array of {name, filter} objects
   */
  private getCustomFilters(): Array<{ name: string; filter: number }> {
    if (!this.filters.customFilters) {
      return [];
    }
    
    const customFilters: Array<{ name: string; filter: number }> = [];
    for (const [name, filterHandle] of this.filters.customFilters) {
      customFilters.push({ name: `custom_${name}`, filter: filterHandle });
    }
    return customFilters;
  }

  /**
   * Process response through response-specific filters
   * Typically includes logging, metrics, and compression (not auth/rate limiting)
   */
  private async processThroughResponseFilters(buffer: number): Promise<number> {
    // Process through response-specific filters only
    let processedBuffer = buffer;

    const responseFilterChain = [
      // HTTP response processing
      { name: "httpCompression", filter: this.filters.httpCompression },
      { name: "httpCodec", filter: this.filters.httpCodec },
      
      // Observability (response logging, metrics, tracing)
      { name: "accessLog", filter: this.filters.accessLog },
      { name: "logging", filter: this.filters.logging }, // Legacy logging
      { name: "metrics", filter: this.filters.metrics },
      { name: "tracing", filter: this.filters.tracing },
      
      // Custom response filters
      ...this.getCustomFilters(),
    ];

    for (const { name, filter } of responseFilterChain) {
      if (filter) {
        try {
          processedBuffer = await this.processThroughFilter(
            filter,
            processedBuffer
          );
        } catch (error) {
          const shouldStop = this.config.errorHandling?.stopOnError ?? true;
          if (shouldStop) {
            throw new Error(
              `Response filter '${name}' processing failed: ${error}`
            );
          }
          // Continue processing other filters if stopOnError is false
          console.warn(
            `Response filter '${name}' failed, continuing: ${error}`
          );
        }
      }
    }

    return processedBuffer;
  }

  /**
   * Process message through a single filter using FFI
   */
  private async processThroughFilter(
    filter: number,
    buffer: number
  ): Promise<number> {
    // Use postDataToFilter to process through the specific filter
    // This will call the C++ filter's processing logic
    return new Promise((resolve, reject) => {
      postDataToFilter(
        filter,
        new Uint8Array(), // Empty data since we're using buffer
        (result: any, _userData: any) => {
          if (result) {
            resolve(buffer); // Return the processed buffer
          } else {
            reject(new Error("Filter processing failed"));
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
