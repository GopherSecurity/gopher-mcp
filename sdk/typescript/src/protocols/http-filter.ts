/**
 * @file http-filter.ts
 * @brief HTTP Filter implementation using MCP C API
 *
 * This filter provides HTTP/HTTPS processing capabilities including:
 * - Request/response parsing and modification
 * - Header manipulation
 * - Body processing
 * - Protocol detection and routing
 */

import { mcpFilterLib } from "../core/ffi-bindings";
import { McpBuiltinFilterType, McpFilterStats } from "../types";

// HTTP filter types from mcp_filter_api.h
export enum HttpFilterType {
  HTTP_CODEC = McpBuiltinFilterType.HTTP_CODEC,
  HTTP_ROUTER = McpBuiltinFilterType.HTTP_ROUTER,
  HTTP_COMPRESSION = McpBuiltinFilterType.HTTP_COMPRESSION,
}

// HTTP methods
export enum HttpMethod {
  GET = "GET",
  POST = "POST",
  PUT = "PUT",
  DELETE = "DELETE",
  PATCH = "PATCH",
  HEAD = "HEAD",
  OPTIONS = "OPTIONS",
}

// HTTP status codes
export enum HttpStatus {
  OK = 200,
  CREATED = 201,
  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  INTERNAL_SERVER_ERROR = 500,
}

// HTTP headers interface
export interface HttpHeaders {
  [key: string]: string | string[];
}

// HTTP request interface
export interface HttpRequest {
  method: HttpMethod;
  path: string;
  headers: HttpHeaders;
  body?: Buffer;
  query?: Record<string, string>;
}

// HTTP response interface
export interface HttpResponse {
  status: HttpStatus;
  headers: HttpHeaders;
  body?: Buffer;
}

// HTTP filter configuration
export interface HttpFilterConfig {
  name: string;
  type: HttpFilterType;
  settings: {
    port?: number;
    host?: string;
    ssl?: boolean;
    compression?: boolean;
    maxBodySize?: number;
    timeout?: number;
    cors?: {
      enabled: boolean;
      origins?: string[];
      methods?: string[];
    };
  };
  layer: number;
  memoryPool: any;
}

// HTTP filter callbacks
export interface HttpFilterCallbacks {
  onRequest?: (request: HttpRequest) => Promise<HttpRequest | null>;
  onResponse?: (response: HttpResponse) => Promise<HttpResponse | null>;
  onError?: (error: Error, request?: HttpRequest) => void;
}

/**
 * HTTP Filter implementation using MCP C API
 */
export class HttpFilter {
  public readonly name: string;
  public readonly type: string;
  public readonly filterHandle: number;
  public readonly bufferHandle: number;
  public readonly memoryPool: number;

  private callbacks: HttpFilterCallbacks;
  private stats: McpFilterStats;
  private isProcessing: boolean = false;

  constructor(config: HttpFilterConfig, callbacks: HttpFilterCallbacks = {}) {
    this.name = config.name;
    this.type = config.type.toString();
    this.callbacks = callbacks;

    // Initialize MCP resources
    this.memoryPool = this.createMemoryPool();
    this.filterHandle = this.createFilter();
    this.bufferHandle = this.createBuffer();

    // Set up callbacks
    this.setupCallbacks();

    // Initialize statistics
    this.stats = {
      bytesProcessed: 0,
      packetsProcessed: 0,
      errors: 0,
      processingTimeUs: 0,
      throughputMbps: 0,
    };
  }

  /**
   * Create memory pool for the filter
   */
  private createMemoryPool(): number {
    const poolSize = 1024 * 1024; // 1MB
    const pool = mcpFilterLib.mcp_memory_pool_create(poolSize);
    if (!pool) {
      throw new Error("Failed to create memory pool for HTTP filter");
    }
    return pool as number;
  }

  /**
   * Create the filter using C API
   */
  private createFilter(): number {
    // Create filter configuration struct
    const configStruct = this.createFilterConfigStruct();

    // Create the filter
    const filter = mcpFilterLib.mcp_filter_create_builtin(
      0, // dispatcher (we'll handle this separately)
      HttpFilterType.HTTP_CODEC,
      configStruct
    );

    if (!filter) {
      throw new Error("Failed to create HTTP filter");
    }

    return filter as number;
  }

  /**
   * Create buffer for data processing
   */
  private createBuffer(): number {
    const buffer = mcpFilterLib.mcp_buffer_create_owned(
      8192, // 8KB initial size
      1 // MCP_BUFFER_OWNERSHIP_SHARED
    );

    if (!buffer) {
      throw new Error("Failed to create buffer for HTTP filter");
    }

    return buffer as number;
  }

  /**
   * Create filter configuration struct for C API
   */
  private createFilterConfigStruct(): any {
    // This would create the proper C struct
    // For now, we'll use a placeholder
    return null;
  }

  /**
   * Set up filter callbacks
   */
  private setupCallbacks(): void {
    // Set up the filter callbacks using C API
    const callbacksStruct = {
      onData: this.onDataCallback.bind(this),
      onWrite: this.onWriteCallback.bind(this),
      onNewConnection: this.onNewConnectionCallback.bind(this),
      onHighWatermark: this.onHighWatermarkCallback.bind(this),
      onLowWatermark: this.onLowWatermarkCallback.bind(this),
      onError: this.onErrorCallback.bind(this),
      userData: null,
    };

    // Set callbacks using C API
    const result = mcpFilterLib.mcp_filter_set_callbacks(
      this.filterHandle,
      callbacksStruct as any
    );

    if (result !== 0) {
      throw new Error("Failed to set filter callbacks");
    }
  }

  /**
   * Process HTTP data through the filter
   */
  public async processData(data: Buffer): Promise<Buffer> {
    if (this.isProcessing) {
      throw new Error("Filter is already processing data");
    }

    this.isProcessing = true;
    const startTime = process.hrtime.bigint();

    try {
      // Parse HTTP request
      const request = this.parseHttpRequest(data);

      // Apply request callbacks
      let modifiedRequest = request;
      if (this.callbacks.onRequest) {
        const result = await this.callbacks.onRequest(request);
        if (!result) {
          // Request was blocked
          return this.createBlockedResponse();
        }
        modifiedRequest = result;
      }

      // Process the request through C API
      const processedData = await this.processRequestThroughCAPI(
        modifiedRequest
      );

      // Apply response callbacks
      let response = this.parseHttpResponse(processedData);
      if (this.callbacks.onResponse) {
        const result = await this.callbacks.onResponse(response);
        if (result) {
          response = result;
        }
      }

      // Update statistics
      this.updateStats(data.length, startTime);

      return this.serializeHttpResponse(response);
    } catch (error) {
      this.stats.errors++;
      if (this.callbacks.onError) {
        this.callbacks.onError(error as Error);
      }
      throw error;
    } finally {
      this.isProcessing = false;
    }
  }

  /**
   * Parse HTTP request from buffer
   */
  private parseHttpRequest(data: Buffer): HttpRequest {
    const dataStr = data.toString("utf8");
    const lines = dataStr.split("\r\n");

    if (lines.length < 1) {
      throw new Error("Invalid HTTP request format");
    }

    // Parse request line
    const requestLine = lines[0]?.split(" ");
    if (!requestLine || requestLine.length < 2) {
      throw new Error("Invalid HTTP request line");
    }

    const method = requestLine[0];
    const pathWithQuery = requestLine[1];
    if (!pathWithQuery) {
      throw new Error("Invalid HTTP request path");
    }

    const [path, queryStr] = pathWithQuery.split("?");

    // Parse headers
    const headers: HttpHeaders = {};
    let bodyStart = 0;

    for (let i = 1; i < lines.length; i++) {
      if (lines[i] === "") {
        bodyStart = i + 1;
        break;
      }

      const headerParts = lines[i]?.split(": ");
      if (headerParts && headerParts.length >= 2) {
        const headerName = headerParts[0];
        const headerValue = headerParts.slice(1).join(": ");
        if (headerName) {
          headers[headerName.toLowerCase()] = headerValue;
        }
      }
    }

    // Parse query parameters
    const query: Record<string, string> = {};
    if (queryStr) {
      queryStr.split("&").forEach((param) => {
        const [key, value] = param.split("=");
        if (key) query[key] = value || "";
      });
    }

    // Parse body
    let body: Buffer | undefined;
    if (bodyStart > 0 && bodyStart < lines.length) {
      const bodyData = lines.slice(bodyStart).join("\r\n");
      if (bodyData) {
        body = Buffer.from(bodyData, "utf8");
      }
    }

    return {
      method: method as HttpMethod,
      path: path || "/",
      headers,
      body: body || Buffer.alloc(0),
      query,
    };
  }

  /**
   * Parse HTTP response from buffer
   */
  private parseHttpResponse(data: Buffer): HttpResponse {
    const dataStr = data.toString("utf8");
    const lines = dataStr.split("\r\n");

    if (lines.length < 1) {
      throw new Error("Invalid HTTP response format");
    }

    // Parse status line
    const statusLine = lines[0]?.split(" ");
    if (!statusLine || statusLine.length < 2) {
      throw new Error("Invalid HTTP status line");
    }

    const statusCode = statusLine[1];
    if (!statusCode) {
      throw new Error("Invalid HTTP status code");
    }

    // Parse headers
    const headers: HttpHeaders = {};
    let bodyStart = 0;

    for (let i = 1; i < lines.length; i++) {
      if (lines[i] === "") {
        bodyStart = i + 1;
        break;
      }

      const headerParts = lines[i]?.split(": ");
      if (headerParts && headerParts.length >= 2) {
        const headerName = headerParts[0];
        const headerValue = headerParts.slice(1).join(": ");
        if (headerName) {
          headers[headerName.toLowerCase()] = headerValue;
        }
      }
    }

    // Parse body
    let body: Buffer | undefined;
    if (bodyStart > 0 && bodyStart < lines.length) {
      const bodyData = lines.slice(bodyStart).join("\r\n");
      if (bodyData) {
        body = Buffer.from(bodyData, "utf8");
      }
    }

    return {
      status: parseInt(statusCode) as HttpStatus,
      headers,
      body: body || Buffer.alloc(0),
    };
  }

  /**
   * Process request through C API
   */
  private async processRequestThroughCAPI(
    request: HttpRequest
  ): Promise<Buffer> {
    // Add request data to buffer
    const requestData = this.serializeHttpRequest(request);

    // Clear existing buffer
    mcpFilterLib.mcp_buffer_drain(this.bufferHandle, 0);

    // Add new data
    const result = mcpFilterLib.mcp_buffer_add(
      this.bufferHandle,
      requestData,
      requestData.length
    );

    if (result !== 0) {
      throw new Error("Failed to add data to buffer");
    }

    // Process through filter
    const processedResult = mcpFilterLib.mcp_filter_post_data(
      this.filterHandle,
      requestData,
      requestData.length,
      null, // completion callback
      null // user data
    );

    if (processedResult !== 0) {
      throw new Error("Failed to process data through filter");
    }

    // Get processed data from buffer
    const bufferLength = mcpFilterLib.mcp_buffer_length(this.bufferHandle);
    if (bufferLength === 0) {
      return Buffer.alloc(0);
    }

    // Get contiguous data
    const dataPtr = { ptr: null as any };
    const actualLength = { value: 0 };

    const getResult = mcpFilterLib.mcp_buffer_get_contiguous(
      this.bufferHandle,
      0, // offset
      bufferLength, // length
      dataPtr, // actual length
      actualLength // actual length
    );

    if (getResult !== 0) {
      throw new Error("Failed to get buffer data");
    }

    // Convert to Buffer (this is a simplified approach)
    return Buffer.alloc(actualLength.value);
  }

  /**
   * Serialize HTTP request to buffer
   */
  private serializeHttpRequest(request: HttpRequest): Buffer {
    const lines: string[] = [];

    // Request line
    const queryStr = request.query
      ? "?" +
        Object.entries(request.query)
          .map(([k, v]) => `${k}=${v}`)
          .join("&")
      : "";
    lines.push(`${request.method} ${request.path}${queryStr} HTTP/1.1`);

    // Headers
    Object.entries(request.headers).forEach(([name, value]) => {
      if (Array.isArray(value)) {
        value.forEach((v) => lines.push(`${name}: ${v}`));
      } else {
        lines.push(`${name}: ${value}`);
      }
    });

    // Empty line
    lines.push("");

    // Body
    if (request.body) {
      lines.push(request.body.toString("utf8"));
    }

    return Buffer.from(lines.join("\r\n"), "utf8");
  }

  /**
   * Serialize HTTP response to buffer
   */
  private serializeHttpResponse(response: HttpResponse): Buffer {
    const lines: string[] = [];

    // Status line
    lines.push(`HTTP/1.1 ${response.status} ${HttpStatus[response.status]}`);

    // Headers
    Object.entries(response.headers).forEach(([name, value]) => {
      if (Array.isArray(value)) {
        value.forEach((v) => lines.push(`${name}: ${v}`));
      } else {
        lines.push(`${name}: ${value}`);
      }
    });

    // Empty line
    lines.push("");

    // Body
    if (response.body) {
      lines.push(response.body.toString("utf8"));
    }

    return Buffer.from(lines.join("\r\n"), "utf8");
  }

  /**
   * Create blocked response
   */
  private createBlockedResponse(): Buffer {
    const response: HttpResponse = {
      status: HttpStatus.FORBIDDEN,
      headers: {
        "content-type": "text/plain",
        "content-length": "0",
      },
      body: Buffer.alloc(0),
    };

    return this.serializeHttpResponse(response);
  }

  /**
   * Update filter statistics
   */
  private updateStats(bytesProcessed: number, startTime: bigint): void {
    const endTime = process.hrtime.bigint();
    const processingTimeUs = Number(endTime - startTime) / 1000; // Convert to microseconds

    this.stats.bytesProcessed += bytesProcessed;
    this.stats.packetsProcessed++;
    this.stats.processingTimeUs += processingTimeUs;

    // Calculate throughput (simplified)
    const totalTimeMs = this.stats.processingTimeUs / 1000;
    if (totalTimeMs > 0) {
      this.stats.throughputMbps =
        (this.stats.bytesProcessed * 8) / (totalTimeMs * 1000000);
    }
  }

  /**
   * Get filter statistics
   */
  public getStats(): McpFilterStats {
    return { ...this.stats };
  }

  /**
   * Update filter settings
   */
  public async updateSettings(_settings: any): Promise<void> {
    // Update configuration using C API
    // This would involve updating the filter configuration
    // For now, we'll just update our local settings
    if (this.callbacks.onRequest) {
      // Reconfigure filter if needed
    }
  }

  /**
   * Clean up filter resources
   */
  public async destroy(): Promise<void> {
    // Release buffer
    if (this.bufferHandle) {
      mcpFilterLib.mcp_filter_buffer_release(this.bufferHandle);
    }

    // Release filter
    if (this.filterHandle) {
      mcpFilterLib.mcp_filter_release(this.filterHandle);
    }

    // Destroy memory pool
    if (this.memoryPool) {
      mcpFilterLib.mcp_memory_pool_destroy(this.memoryPool);
    }
  }

  // C API callback implementations
  private onDataCallback(
    _buffer: number,
    _endStream: boolean,
    _userData: any
  ): number {
    // Handle incoming data
    return 0; // MCP_FILTER_CONTINUE
  }

  private onWriteCallback(
    _buffer: number,
    _endStream: boolean,
    _userData: any
  ): number {
    // Handle outgoing data
    return 0; // MCP_FILTER_CONTINUE
  }

  private onNewConnectionCallback(_state: number, _userData: any): number {
    // Handle new connection
    return 0; // MCP_FILTER_CONTINUE
  }

  private onHighWatermarkCallback(_filter: number, _userData: any): void {
    // Handle high watermark
  }

  private onLowWatermarkCallback(_filter: number, _userData: any): void {
    // Handle low watermark
  }

  private onErrorCallback(
    _filter: number,
    _error: number,
    _message: string,
    _userData: any
  ): void {
    // Handle errors
    this.stats.errors++;
  }
}
