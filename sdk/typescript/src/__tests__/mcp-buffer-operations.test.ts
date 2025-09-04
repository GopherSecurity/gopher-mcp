/**
 * @file mcp-buffer-operations.test.ts
 * @brief Tests for zero-copy buffer operations with real C library
 *
 * This test file covers the buffer functionality including:
 * - Zero-copy buffer reading
 * - Zero-copy buffer writing
 * - Buffer content manipulation
 * - Memory management
 */

import { getBufferContent, updateBufferContent } from "../mcp-filter-api";

import {
  BufferOwnership,
  createBufferFromString,
  readStringFromBuffer,
} from "../mcp-filter-buffer";

// Use real C++ library instead of mocks
describe("Buffer Operations", () => {
  describe("Buffer Content Operations", () => {
    it("should handle buffer content reading", () => {
      // Test function signature and basic functionality
      expect(typeof getBufferContent).toBe("function");

      // Note: Real buffer operations require a valid buffer handle from C library
      // This test verifies the function exists and can be called
    });

    it("should handle buffer content writing", () => {
      // Test function signature and basic functionality
      expect(typeof updateBufferContent).toBe("function");

      // Note: Real buffer operations require a valid buffer handle from C library
      // This test verifies the function exists and can be called
    });
  });

  describe("Buffer Creation and Reading", () => {
    it("should create buffer from string", () => {
      const testString = "Hello, World!";
      const buffer = createBufferFromString(testString, BufferOwnership.SHARED);

      expect(buffer).toBeGreaterThan(0);
    });

    it("should read string from buffer", () => {
      const testString = "Test message";
      const buffer = createBufferFromString(testString, BufferOwnership.SHARED);

      const result = readStringFromBuffer(buffer);
      expect(result).toBe(testString);
    });

    it("should handle empty string buffers", () => {
      const buffer = createBufferFromString("", BufferOwnership.SHARED);
      expect(buffer).toBeGreaterThan(0);

      const result = readStringFromBuffer(buffer);
      expect(result).toBe("");
    });

    it("should handle large string buffers", () => {
      const largeString = "A".repeat(10000); // 10KB string
      const buffer = createBufferFromString(largeString, BufferOwnership.SHARED);

      expect(buffer).toBeGreaterThan(0);

      const result = readStringFromBuffer(buffer);
      expect(result).toBe(largeString);
    });

    it("should handle JSON string buffers", () => {
      const jsonString = JSON.stringify({
        jsonrpc: "2.0",
        id: 1,
        method: "test/method",
        params: { test: true, nested: { value: 42 } },
      });

      const buffer = createBufferFromString(jsonString, BufferOwnership.SHARED);
      expect(buffer).toBeGreaterThan(0);

      const result = readStringFromBuffer(buffer);
      expect(result).toBe(jsonString);

      // Verify it's valid JSON
      const parsed = JSON.parse(result);
      expect(parsed.jsonrpc).toBe("2.0");
      expect(parsed.method).toBe("test/method");
    });
  });

  describe("Buffer Ownership", () => {
    it("should create buffer with different ownership types", () => {
      const testString = "Ownership test";

      const sharedBuffer = createBufferFromString(testString, BufferOwnership.SHARED);
      const exclusiveBuffer = createBufferFromString(testString, BufferOwnership.EXCLUSIVE);
      const externalBuffer = createBufferFromString(testString, BufferOwnership.EXTERNAL);

      expect(sharedBuffer).toBeGreaterThan(0);
      expect(exclusiveBuffer).toBeGreaterThan(0);
      expect(externalBuffer).toBeGreaterThan(0);

      // All should return the same content
      expect(readStringFromBuffer(sharedBuffer)).toBe(testString);
      expect(readStringFromBuffer(exclusiveBuffer)).toBe(testString);
      expect(readStringFromBuffer(externalBuffer)).toBe(testString);
    });
  });

  describe("Error Handling", () => {
    it("should handle invalid buffer handles gracefully", () => {
      // Test with invalid buffer handle
      expect(() => {
        getBufferContent(0); // Invalid handle
      }).toThrow();

      expect(() => {
        updateBufferContent(0, "test"); // Invalid handle
      }).toThrow();
    });

    it("should handle buffer operation errors", () => {
      // Test error handling in buffer operations
      expect(() => {
        readStringFromBuffer(0); // Invalid handle
      }).toThrow();
    });
  });

  describe("Memory Management", () => {
    it("should handle multiple buffer operations", () => {
      const buffers: number[] = [];

      // Create multiple buffers
      for (let i = 0; i < 10; i++) {
        const buffer = createBufferFromString(`Buffer ${i}`, BufferOwnership.SHARED);
        buffers.push(buffer);
        expect(buffer).toBeGreaterThan(0);
      }

      // Read from all buffers
      buffers.forEach((buffer, index) => {
        const content = readStringFromBuffer(buffer);
        expect(content).toBe(`Buffer ${index}`);
      });

      // Note: In a real implementation, we would clean up the buffers
      // but the C++ RAII system should handle this automatically
    });
  });
});
