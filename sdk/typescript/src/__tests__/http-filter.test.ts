/**
 * @file http-filter.test.ts
 * @brief Unit tests for HttpFilter class
 */

import {
  HttpFilter,
  HttpFilterCallbacks,
  HttpFilterConfig,
  HttpFilterType,
  HttpMethod,
  HttpRequest,
  HttpResponse,
  HttpStatus,
} from "../protocols/http-filter";

describe("HttpFilter", () => {
  let httpFilter: HttpFilter;
  let mockCallbacks: HttpFilterCallbacks;

  beforeEach(() => {
    mockCallbacks = {
      onRequest: jest.fn(),
      onResponse: jest.fn(),
      onError: jest.fn(),
      onData: jest.fn(),
    };

    const config: HttpFilterConfig = {
      port: 8080,
      host: "localhost",
      maxConnections: 100,
      timeout: 30000,
      enableCompression: true,
      enableLogging: true,
    };

    httpFilter = new HttpFilter(config);
  });

  afterEach(() => {
    if (httpFilter) {
      httpFilter.destroy();
    }
  });

  describe("construction and initialization", () => {
    it("should create HTTP filter with configuration", () => {
      expect(httpFilter).toBeDefined();
      expect(httpFilter.config.port).toBe(8080);
      expect(httpFilter.config.host).toBe("localhost");
      expect(httpFilter.config.maxConnections).toBe(100);
    });

    it("should create HTTP filter with default values", () => {
      const defaultFilter = new HttpFilter({});
      expect(defaultFilter).toBeDefined();
      expect(defaultFilter.config.port).toBe(80);
      expect(defaultFilter.config.host).toBe("0.0.0.0");

      defaultFilter.destroy();
    });

    it("should create HTTP filter with custom type", () => {
      const customFilter = new HttpFilter({}, HttpFilterType.REVERSE_PROXY);
      expect(customFilter.type).toBe(HttpFilterType.REVERSE_PROXY);

      customFilter.destroy();
    });
  });

  describe("callback management", () => {
    it("should set callbacks correctly", () => {
      httpFilter.setCallbacks(mockCallbacks);

      expect(httpFilter.callbacks).toBe(mockCallbacks);
    });

    it("should handle missing callbacks gracefully", () => {
      const filterWithoutCallbacks = new HttpFilter({});
      expect(() =>
        filterWithoutCallbacks.setCallbacks(undefined)
      ).not.toThrow();

      filterWithoutCallbacks.destroy();
    });

    it("should validate callback functions", () => {
      const invalidCallbacks = {
        onRequest: "not a function" as any,
        onResponse: null as any,
        onError: undefined as any,
        onData: jest.fn(),
      };

      expect(() => httpFilter.setCallbacks(invalidCallbacks)).not.toThrow();
    });
  });

  describe("HTTP request processing", () => {
    beforeEach(() => {
      httpFilter.setCallbacks(mockCallbacks);
    });

    it("should process valid HTTP request", () => {
      const request: HttpRequest = {
        method: HttpMethod.GET,
        url: "/api/test",
        headers: {
          "Content-Type": "application/json",
          "User-Agent": "TestClient/1.0",
        },
        body: Buffer.from("{}"),
      };

      const result = httpFilter.processRequest(request);
      expect(result).toBeDefined();
      expect(mockCallbacks.onRequest).toHaveBeenCalledWith(request);
    });

    it("should handle different HTTP methods", () => {
      const methods = [
        HttpMethod.GET,
        HttpMethod.POST,
        HttpMethod.PUT,
        HttpMethod.DELETE,
      ];

      methods.forEach((method) => {
        const request: HttpRequest = {
          method,
          url: `/test/${method}`,
          headers: {},
          body: Buffer.alloc(0),
        };

        const result = httpFilter.processRequest(request);
        expect(result).toBeDefined();
      });
    });

    it("should process request with query parameters", () => {
      const request: HttpRequest = {
        method: HttpMethod.GET,
        url: "/search?q=test&page=1",
        headers: {},
        body: Buffer.alloc(0),
      };

      const result = httpFilter.processRequest(request);
      expect(result).toBeDefined();
    });

    it("should handle request with large body", () => {
      const largeBody = Buffer.alloc(1024 * 1024); // 1MB
      const request: HttpRequest = {
        method: HttpMethod.POST,
        url: "/upload",
        headers: {
          "Content-Length": largeBody.length.toString(),
        },
        body: largeBody,
      };

      const result = httpFilter.processRequest(request);
      expect(result).toBeDefined();
    });
  });

  describe("HTTP response processing", () => {
    beforeEach(() => {
      httpFilter.setCallbacks(mockCallbacks);
    });

    it("should process valid HTTP response", () => {
      const response: HttpResponse = {
        status: HttpStatus.OK,
        headers: {
          "Content-Type": "application/json",
          "Content-Length": "25",
        },
        body: Buffer.from('{"message": "success"}'),
      };

      const result = httpFilter.processResponse(response);
      expect(result).toBeDefined();
      expect(mockCallbacks.onResponse).toHaveBeenCalledWith(response);
    });

    it("should handle different HTTP status codes", () => {
      const statusCodes = [
        HttpStatus.OK,
        HttpStatus.CREATED,
        HttpStatus.BAD_REQUEST,
        HttpStatus.NOT_FOUND,
        HttpStatus.INTERNAL_SERVER_ERROR,
      ];

      statusCodes.forEach((status) => {
        const response: HttpResponse = {
          status,
          headers: {},
          body: Buffer.alloc(0),
        };

        const result = httpFilter.processResponse(response);
        expect(result).toBeDefined();
      });
    });

    it("should process response with custom headers", () => {
      const response: HttpResponse = {
        status: HttpStatus.OK,
        headers: {
          "X-Custom-Header": "custom-value",
          "Cache-Control": "no-cache",
          ETag: '"abc123"',
        },
        body: Buffer.alloc(0),
      };

      const result = httpFilter.processResponse(response);
      expect(result).toBeDefined();
    });
  });

  describe("data processing", () => {
    beforeEach(() => {
      httpFilter.setCallbacks(mockCallbacks);
    });

    it("should process incoming data", () => {
      const data = Buffer.from("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

      const result = httpFilter.processData(data);
      expect(result).toBeDefined();
      expect(mockCallbacks.onData).toHaveBeenCalledWith(data);
    });

    it("should handle chunked data", () => {
      const chunks = [
        Buffer.from("HTTP/1.1 200 OK\r\n"),
        Buffer.from("Transfer-Encoding: chunked\r\n\r\n"),
        Buffer.from("5\r\nHello\r\n"),
        Buffer.from("0\r\n\r\n"),
      ];

      chunks.forEach((chunk) => {
        const result = httpFilter.processData(chunk);
        expect(result).toBeDefined();
      });
    });

    it("should handle binary data", () => {
      const binaryData = Buffer.from([
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
      ]); // PNG header

      const result = httpFilter.processData(binaryData);
      expect(result).toBeDefined();
    });
  });

  describe("error handling", () => {
    beforeEach(() => {
      httpFilter.setCallbacks(mockCallbacks);
    });

    it("should handle malformed HTTP requests", () => {
      const malformedData = Buffer.from("INVALID HTTP REQUEST");

      const result = httpFilter.processData(malformedData);
      expect(result).toBeDefined();
      expect(mockCallbacks.onError).toHaveBeenCalled();
    });

    it("should handle oversized requests", () => {
      const oversizedBody = Buffer.alloc(1024 * 1024 * 10); // 10MB
      const request: HttpRequest = {
        method: HttpMethod.POST,
        url: "/upload",
        headers: {
          "Content-Length": oversizedBody.length.toString(),
        },
        body: oversizedBody,
      };

      const result = httpFilter.processRequest(request);
      expect(result).toBeDefined();
    });

    it("should handle network errors gracefully", () => {
      const errorData = Buffer.from("ERROR: Connection reset");

      const result = httpFilter.processData(errorData);
      expect(result).toBeDefined();
    });
  });

  describe("filter statistics and monitoring", () => {
    it("should track request count", () => {
      const request: HttpRequest = {
        method: HttpMethod.GET,
        url: "/test",
        headers: {},
        body: Buffer.alloc(0),
      };

      httpFilter.processRequest(request);
      httpFilter.processRequest(request);

      const stats = httpFilter.getStats();
      expect(stats.requestCount).toBe(2);
    });

    it("should track response count", () => {
      const response: HttpResponse = {
        status: HttpStatus.OK,
        headers: {},
        body: Buffer.alloc(0),
      };

      httpFilter.processResponse(response);
      httpFilter.processResponse(response);

      const stats = httpFilter.getStats();
      expect(stats.responseCount).toBe(2);
    });

    it("should track error count", () => {
      const malformedData = Buffer.from("INVALID");
      httpFilter.processData(malformedData);

      const stats = httpFilter.getStats();
      expect(stats.errorCount).toBeGreaterThan(0);
    });

    it("should track data throughput", () => {
      const data = Buffer.alloc(1024);
      httpFilter.processData(data);

      const stats = httpFilter.getStats();
      expect(stats.bytesProcessed).toBeGreaterThan(0);
    });

    it("should reset statistics", () => {
      const request: HttpRequest = {
        method: HttpMethod.GET,
        url: "/test",
        headers: {},
        body: Buffer.alloc(0),
      };

      httpFilter.processRequest(request);
      httpFilter.resetStats();

      const stats = httpFilter.getStats();
      expect(stats.requestCount).toBe(0);
      expect(stats.bytesProcessed).toBe(0);
    });
  });

  describe("filter configuration", () => {
    it("should update configuration", () => {
      const newConfig: HttpFilterConfig = {
        port: 9090,
        host: "127.0.0.1",
        maxConnections: 200,
        timeout: 60000,
        enableCompression: false,
        enableLogging: false,
      };

      httpFilter.updateConfig(newConfig);
      expect(httpFilter.config.port).toBe(9090);
      expect(httpFilter.config.host).toBe("127.0.0.1");
    });

    it("should validate configuration values", () => {
      const invalidConfig = {
        port: -1,
        host: "",
        maxConnections: 0,
        timeout: -1000,
      } as any;

      expect(() => httpFilter.updateConfig(invalidConfig)).not.toThrow();
    });

    it("should handle partial configuration updates", () => {
      const partialConfig = {
        port: 9090,
        enableCompression: false,
      };

      httpFilter.updateConfig(partialConfig);
      expect(httpFilter.config.port).toBe(9090);
      expect(httpFilter.config.enableCompression).toBe(false);
      expect(httpFilter.config.host).toBe("localhost"); // Should remain unchanged
    });
  });

  describe("filter lifecycle", () => {
    it("should start and stop filter", () => {
      expect(() => httpFilter.start()).not.toThrow();
      expect(httpFilter.isRunning()).toBe(true);

      expect(() => httpFilter.stop()).not.toThrow();
      expect(httpFilter.isRunning()).toBe(false);
    });

    it("should pause and resume filter", () => {
      httpFilter.start();

      expect(() => httpFilter.pause()).not.toThrow();
      expect(httpFilter.isPaused()).toBe(true);

      expect(() => httpFilter.resume()).not.toThrow();
      expect(httpFilter.isPaused()).toBe(false);
    });

    it("should handle multiple start/stop cycles", () => {
      for (let i = 0; i < 3; i++) {
        httpFilter.start();
        expect(httpFilter.isRunning()).toBe(true);

        httpFilter.stop();
        expect(httpFilter.isRunning()).toBe(false);
      }
    });
  });

  describe("filter cleanup", () => {
    it("should destroy filter resources", () => {
      expect(() => httpFilter.destroy()).not.toThrow();
    });

    it("should handle multiple destroy calls gracefully", () => {
      expect(() => httpFilter.destroy()).not.toThrow();
      expect(() => httpFilter.destroy()).not.toThrow(); // Should be safe to call multiple times
    });

    it("should cleanup callbacks on destroy", () => {
      httpFilter.setCallbacks(mockCallbacks);
      httpFilter.destroy();

      expect(httpFilter.callbacks).toBeUndefined();
    });
  });

  describe("edge cases", () => {
    it("should handle empty requests", () => {
      const emptyRequest: HttpRequest = {
        method: HttpMethod.GET,
        url: "",
        headers: {},
        body: Buffer.alloc(0),
      };

      const result = httpFilter.processRequest(emptyRequest);
      expect(result).toBeDefined();
    });

    it("should handle requests with no headers", () => {
      const request: HttpRequest = {
        method: HttpMethod.GET,
        url: "/test",
        headers: {},
        body: Buffer.alloc(0),
      };

      const result = httpFilter.processRequest(request);
      expect(result).toBeDefined();
    });

    it("should handle very long URLs", () => {
      const longUrl = "/" + "a".repeat(1000);
      const request: HttpRequest = {
        method: HttpMethod.GET,
        url: longUrl,
        headers: {},
        body: Buffer.alloc(0),
      };

      const result = httpFilter.processRequest(request);
      expect(result).toBeDefined();
    });

    it("should handle concurrent requests", async () => {
      const promises = Array.from({ length: 10 }, (_, i) => {
        const request: HttpRequest = {
          method: HttpMethod.GET,
          url: `/concurrent/${i}`,
          headers: {},
          body: Buffer.alloc(0),
        };
        return httpFilter.processRequest(request);
      });

      const results = await Promise.all(promises);
      results.forEach((result) => {
        expect(result).toBeDefined();
      });
    });
  });
});
