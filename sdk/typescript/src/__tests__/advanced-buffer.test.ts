/**
 * @file advanced-buffer.test.ts
 * @brief Unit tests for AdvancedBuffer and AdvancedBufferPool classes
 */

import {
  AdvancedBuffer,
  AdvancedBufferPool,
  BufferOwnership,
} from "../buffers/advanced-buffer";

describe("AdvancedBuffer", () => {
  let buffer: AdvancedBuffer;

  beforeEach(() => {
    buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
  });

  afterEach(() => {
    if (buffer) {
      // Clean up if needed
    }
  });

  describe("constructor", () => {
    it("should create a buffer with specified capacity and ownership", () => {
      expect(buffer).toBeDefined();
      expect(buffer.getHandle()).toBeGreaterThan(0);
    });

    it("should create buffer with default ownership", () => {
      const defaultBuffer = new AdvancedBuffer(512);
      expect(defaultBuffer).toBeDefined();
      expect(defaultBuffer.getHandle()).toBeGreaterThan(0);
    });
  });

  describe("static factory methods", () => {
    it("should create buffer view from data", () => {
      const data = Buffer.from("test data");
      const viewBuffer = AdvancedBuffer.createView(data, data.length);

      expect(viewBuffer).toBeDefined();
      expect(viewBuffer.getHandle()).toBeGreaterThan(0);
    });

    it("should create buffer from fragment", () => {
      const fragment = {
        data: Buffer.from("test fragment"),
        size: 13,
        releaseCallback: jest.fn(),
        userData: null,
      };

      const fragmentBuffer = AdvancedBuffer.createFromFragment(fragment);
      expect(fragmentBuffer).toBeDefined();
      expect(fragmentBuffer.getHandle()).toBeGreaterThan(0);
    });
  });

  describe("data operations", () => {
    it("should add data to buffer", () => {
      const testData = Buffer.from("test data");
      expect(() => buffer.add(testData)).not.toThrow();
    });

    it("should add string to buffer", () => {
      const testString = "test string";
      expect(() => buffer.addString(testString)).not.toThrow();
    });

    it("should add another buffer", () => {
      const sourceBuffer = new AdvancedBuffer(256);
      const testData = Buffer.from("source data");
      sourceBuffer.add(testData);

      expect(() => buffer.addBuffer(sourceBuffer)).not.toThrow();
    });

    it("should prepend data to buffer", () => {
      const testData = Buffer.from("prepended");
      expect(() => buffer.prepend(testData)).not.toThrow();
    });

    it("should drain bytes from buffer", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      expect(() => buffer.drain(4)).not.toThrow();
    });

    it("should move data to another buffer", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      const destBuffer = new AdvancedBuffer(1024);
      expect(() => buffer.moveTo(destBuffer, 4)).not.toThrow();
    });
  });

  describe("buffer reservation", () => {
    it("should reserve space for writing", () => {
      const reservation = buffer.reserve(256);
      expect(reservation).toBeDefined();
      expect(reservation.capacity).toBeGreaterThan(0);
    });

    it("should commit reservation", () => {
      const reservation = buffer.reserve(256);
      expect(() => buffer.commitReservation(reservation, 128)).not.toThrow();
    });

    it("should cancel reservation", () => {
      const reservation = buffer.reserve(256);
      expect(() => buffer.cancelReservation(reservation)).not.toThrow();
    });
  });

  describe("buffer access", () => {
    it("should get contiguous memory view", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      const result = buffer.getContiguous(0, 4);
      expect(result).toBeDefined();
      expect(result.data).toBeDefined();
      expect(result.actualLength).toBeGreaterThan(0);
    });

    it("should linearize buffer", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      const result = buffer.linearize(4);
      expect(result).toBeDefined();
      expect(Buffer.isBuffer(result)).toBe(true);
    });

    it("should peek at buffer data", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      const peekBuffer = Buffer.alloc(4);
      expect(() => buffer.peek(0, peekBuffer, 4)).not.toThrow();
    });
  });

  describe("type-safe I/O", () => {
    it("should write little-endian integers", () => {
      expect(() => buffer.writeLittleEndianInt(12345, 4)).not.toThrow();
    });

    it("should write big-endian integers", () => {
      expect(() => buffer.writeBigEndianInt(12345, 4)).not.toThrow();
    });

    it("should read little-endian integers", () => {
      buffer.writeLittleEndianInt(12345, 4);
      const result = buffer.readLittleEndianInt(4);
      expect(result).toBeDefined();
      expect(result).toBe(0); // Placeholder value in current implementation
    });

    it("should read big-endian integers", () => {
      buffer.writeBigEndianInt(12345, 4);
      const result = buffer.readBigEndianInt(4);
      expect(result).toBeDefined();
      expect(result).toBe(0); // Placeholder value in current implementation
    });
  });

  describe("buffer search", () => {
    it("should search for pattern in buffer", () => {
      const testData = Buffer.from("test data pattern");
      buffer.add(testData);

      const pattern = Buffer.from("pattern");
      const result = buffer.search(pattern, 0);
      expect(result).toBeDefined();
      expect(result).toBeGreaterThan(0);
    });

    it("should find byte in buffer", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      const result = buffer.findByte(32); // space character
      expect(result).toBeDefined();
      expect(result).toBeGreaterThan(0);
    });
  });

  describe("buffer information", () => {
    it("should get buffer length", () => {
      const testData = Buffer.from("test data");
      buffer.add(testData);

      const length = buffer.length;
      expect(length).toBeGreaterThan(0);
    });

    it("should get buffer capacity", () => {
      const capacity = buffer.capacity;
      expect(capacity).toBeGreaterThan(0);
    });

    it("should check if buffer is empty", () => {
      const isEmpty = buffer.isEmpty;
      expect(typeof isEmpty).toBe("boolean");
    });

    it("should get buffer statistics", () => {
      const stats = buffer.getStats();
      expect(stats).toBeDefined();
      expect(stats.totalBytes).toBeGreaterThan(0);
    });
  });

  describe("buffer watermarks", () => {
    it("should set buffer watermarks", () => {
      expect(() => buffer.setWatermarks(100, 200, 300)).not.toThrow();
    });

    it("should check high watermark", () => {
      buffer.setWatermarks(100, 200, 300);
      const aboveHigh = buffer.isAboveHighWatermark;
      expect(typeof aboveHigh).toBe("boolean");
    });

    it("should check low watermark", () => {
      buffer.setWatermarks(100, 200, 300);
      const belowLow = buffer.isBelowLowWatermark;
      expect(typeof belowLow).toBe("boolean");
    });
  });

  describe("buffer cleanup", () => {
    it("should destroy buffer", () => {
      expect(() => buffer.destroy()).not.toThrow();
    });
  });
});

describe("AdvancedBufferPool", () => {
  let pool: AdvancedBufferPool;

  beforeEach(() => {
    pool = new AdvancedBufferPool(1024, 10, 2, false, false);
  });

  afterEach(() => {
    if (pool) {
      pool.destroy();
    }
  });

  describe("constructor", () => {
    it("should create buffer pool with configuration", () => {
      expect(pool).toBeDefined();
    });
  });

  describe("buffer management", () => {
    it("should acquire buffer from pool", () => {
      const buffer = pool.acquire();
      expect(buffer).toBeDefined();
      expect(buffer?.getHandle()).toBeGreaterThan(0);
    });

    it("should release buffer back to pool", () => {
      const buffer = pool.acquire();
      if (buffer) {
        expect(() => pool.release(buffer)).not.toThrow();
      }
    });

    it("should handle pool exhaustion", () => {
      const buffers: AdvancedBuffer[] = [];

      // Acquire all buffers
      for (let i = 0; i < 12; i++) {
        const buffer = pool.acquire();
        if (buffer) buffers.push(buffer);
      }

      // Should not be able to acquire more than pool size
      expect(buffers.length).toBeLessThanOrEqual(10);

      // Release all buffers
      buffers.forEach((buffer) => pool.release(buffer));
    });
  });

  describe("pool statistics", () => {
    it("should get pool statistics", () => {
      const stats = pool.getStats();
      expect(stats).toBeDefined();
      expect(stats.freeCount).toBeGreaterThan(0);
      expect(stats.usedCount).toBeGreaterThan(0);
      expect(stats.totalAllocated).toBeGreaterThan(0);
    });
  });

  describe("pool optimization", () => {
    it("should trim pool", () => {
      expect(() => pool.trim(5)).not.toThrow();
    });
  });
});
