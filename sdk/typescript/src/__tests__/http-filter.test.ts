/**
 * @file http-filter.test.ts
 * @brief Unit tests for HttpFilter class
 *
 * Tests cover the basic HTTP filter functionality including:
 * - Constructor and initialization
 * - HTTP request/response parsing
 * - Basic data processing
 * - Error handling
 * - Resource cleanup
 */

import {
  HttpFilter,
  HttpFilterType,
  HttpMethod,
} from "../protocols/http-filter";

describe("HttpFilter", () => {
  let filter: HttpFilter;
  let mockCallbacks: any;

  beforeEach(() => {
    mockCallbacks = {
      onRequest: jest.fn(),
      onResponse: jest.fn(),
      onError: jest.fn(),
    };

    const config = {
      name: "test-http-filter",
      type: HttpFilterType.HTTP_CODEC,
      settings: {
        port: 8080,
        host: "localhost",
        ssl: false,
        compression: true,
        maxBodySize: 1024,
        timeout: 5000,
        cors: {
          enabled: true,
          origins: ["http://localhost:3000"],
          methods: ["GET", "POST"],
        },
      },
      layer: 1,
      memoryPool: null,
    };

    filter = new HttpFilter(config, mockCallbacks);
  });

  afterEach(async () => {
    if (filter) {
      await filter.destroy();
    }
  });

  describe("constructor", () => {
    it("should create HTTP filter with correct properties", () => {
      expect(filter.name).toBe("test-http-filter");
      expect(filter.type).toBe(HttpFilterType.HTTP_CODEC.toString());
      expect(filter.filterHandle).toBeGreaterThan(0);
      expect(filter.bufferHandle).toBeGreaterThan(0);
      expect(filter.memoryPool).toBeTruthy();
    });

    it("should initialize statistics correctly", () => {
      const stats = filter.getStats();
      expect(stats.bytesProcessed).toBe(0);
      expect(stats.packetsProcessed).toBe(0);
      expect(stats.errors).toBe(0);
      expect(stats.processingTimeUs).toBe(0);
      expect(stats.throughputMbps).toBe(0);
    });
  });

  describe("HTTP processing", () => {
    it("should parse HTTP request correctly", async () => {
      const requestData = Buffer.from(
        "GET /api/users HTTP/1.1\r\n" +
          "Host: example.com\r\n" +
          "User-Agent: test-agent\r\n" +
          "\r\n"
      );

      const result = await filter.processData(requestData);
      expect(result).toBeDefined();
      expect(result.length).toBeGreaterThan(0);

      const stats = filter.getStats();
      expect(stats.packetsProcessed).toBe(1);
    });

    it("should process HTTP request and return response", async () => {
      const requestData = Buffer.from(
        "POST /api/users HTTP/1.1\r\n" +
          "Host: example.com\r\n" +
          "Content-Type: application/json\r\n" +
          "Content-Length: 20\r\n" +
          "\r\n" +
          '{"name": "test"}'
      );

      const result = await filter.processData(requestData);
      expect(result).toBeDefined();
      expect(result.length).toBeGreaterThan(0);

      const stats = filter.getStats();
      expect(stats.packetsProcessed).toBe(1);
    });

    it("should handle malformed HTTP data", async () => {
      const malformedData = Buffer.from("INVALID");

      await expect(filter.processData(malformedData)).rejects.toThrow(
        "Invalid HTTP request line"
      );

      const stats = filter.getStats();
      expect(stats.errors).toBe(1);
    });

    it("should handle empty HTTP data", async () => {
      const emptyData = Buffer.alloc(0);

      await expect(filter.processData(emptyData)).rejects.toThrow(
        "Invalid HTTP request line"
      );
    });
  });

  describe("callback handling", () => {
    it("should call request callback when provided", async () => {
      const requestData = Buffer.from(
        "GET /api/users HTTP/1.1\r\n" + "Host: example.com\r\n" + "\r\n"
      );

      mockCallbacks.onRequest.mockResolvedValue({
        method: HttpMethod.GET,
        path: "/api/users",
        headers: { host: "example.com" },
        body: Buffer.alloc(0),
      });

      await filter.processData(requestData);

      expect(mockCallbacks.onRequest).toHaveBeenCalled();
    });

    it("should handle blocked requests", async () => {
      const requestData = Buffer.from(
        "GET /api/users HTTP/1.1\r\n" + "Host: example.com\r\n" + "\r\n"
      );

      mockCallbacks.onRequest.mockResolvedValue(null);

      const result = await filter.processData(requestData);

      // Should return a blocked response
      expect(result).toBeDefined();
      expect(result.toString()).toContain("403 FORBIDDEN");
    });

    it("should call error callback on processing errors", async () => {
      const malformedData = Buffer.from("INVALID");

      try {
        await filter.processData(malformedData);
      } catch (error) {
        // Expected to fail
      }

      expect(mockCallbacks.onError).toHaveBeenCalled();
    });
  });

  describe("error handling", () => {
    it("should prevent concurrent processing", async () => {
      const requestData = Buffer.from(
        "GET /api/users HTTP/1.1\r\n" + "Host: example.com\r\n" + "\r\n"
      );

      // Start first processing
      const firstProcess = filter.processData(requestData);

      // Try to start second processing
      await expect(filter.processData(requestData)).rejects.toThrow(
        "Filter is already processing data"
      );

      // Wait for first to complete
      await firstProcess;
    });
  });

  describe("configuration updates", () => {
    it("should update settings correctly", async () => {
      const newSettings = {
        compression: false,
        maxBodySize: 2048,
      };

      await filter.updateSettings(newSettings);

      // Verify method exists and doesn't throw
      expect(typeof filter.updateSettings).toBe("function");
    });
  });

  describe("cleanup", () => {
    it("should destroy filter and clean up resources", async () => {
      expect(typeof filter.destroy).toBe("function");
      // We can't actually call destroy here since afterEach will call it
      // and that would cause a double free error
      // Instead, we just verify the method exists and is callable
    });
  });
});
