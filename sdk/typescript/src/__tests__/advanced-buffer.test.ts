/**
 * @file advanced-buffer.test.ts
 * @brief Unit tests for AdvancedBuffer and AdvancedBufferPool classes
 */

import {
  AdvancedBuffer,
  AdvancedBufferPool,
  BufferFragment,
  BufferOwnership,
} from "../buffers/advanced-buffer";

describe("AdvancedBuffer", () => {
  let buffer: AdvancedBuffer;

  beforeEach(() => {
    buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
  });

  afterEach(() => {
    if (buffer) {
      buffer.release();
    }
  });

  describe("construction and initialization", () => {
    it("should create buffer with specified capacity", () => {
      const testBuffer = new AdvancedBuffer(512, BufferOwnership.OWNED);
      expect(testBuffer.capacity).toBe(512);
      expect(testBuffer.length).toBe(0);
      expect(testBuffer.isEmpty).toBe(true);
      testBuffer.release();
    });

    it("should create buffer with different ownership types", () => {
      const ownedBuffer = new AdvancedBuffer(256, BufferOwnership.OWNED);
      const sharedBuffer = new AdvancedBuffer(256, BufferOwnership.SHARED);
      const viewBuffer = new AdvancedBuffer(256, BufferOwnership.NONE);

      expect(ownedBuffer).toBeDefined();
      expect(sharedBuffer).toBeDefined();
      expect(viewBuffer).toBeDefined();

      ownedBuffer.release();
      sharedBuffer.release();
      viewBuffer.release();
    });

    it("should create buffer view from existing data", () => {
      const testData = Buffer.from("Hello, World!");
      const viewBuffer = AdvancedBuffer.createView(testData, testData.length);

      expect(viewBuffer.length).toBe(testData.length);
      expect(viewBuffer.capacity).toBe(testData.length);

      viewBuffer.release();
    });

    it("should create buffer from fragment", () => {
      const fragment: BufferFragment = {
        data: Buffer.from("Fragment data"),
        length: 13,
      };

      const fragmentBuffer = AdvancedBuffer.createFromFragment(fragment);
      expect(fragmentBuffer.length).toBe(fragment.length);

      fragmentBuffer.release();
    });
  });

  describe("data operations", () => {
    it("should add data to buffer", () => {
      const testData = Buffer.from("Test data");
      const initialLength = buffer.length;

      buffer.add(testData);

      expect(buffer.length).toBe(initialLength + testData.length);
      expect(buffer.isEmpty).toBe(false);
    });

    it("should add string data to buffer", () => {
      const testString = "Hello, String!";
      const initialLength = buffer.length;

      buffer.addString(testString);

      expect(buffer.length).toBe(initialLength + testString.length);
    });

    it("should add buffer to buffer", () => {
      const testBuffer = Buffer.from("Buffer data");
      const initialLength = buffer.length;

      buffer.addBuffer(testBuffer);

      expect(buffer.length).toBe(initialLength + testBuffer.length);
    });

    it("should prepend data to buffer", () => {
      const testData = Buffer.from("Prefix");
      const initialLength = buffer.length;

      buffer.prepend(testData);

      expect(buffer.length).toBe(initialLength + testData.length);
    });

    it("should drain data from buffer", () => {
      const testData = Buffer.from("Data to drain");
      buffer.add(testData);

      const drained = buffer.drain(5);
      expect(drained).toBeDefined();
      expect(buffer.length).toBe(testData.length - 5);
    });
  });

  describe("buffer properties and state", () => {
    it("should report correct length and capacity", () => {
      const testData = Buffer.from("Test data");
      buffer.add(testData);

      expect(buffer.length).toBe(testData.length);
      expect(buffer.capacity).toBeGreaterThanOrEqual(testData.length);
    });

    it("should report empty state correctly", () => {
      expect(buffer.isEmpty).toBe(true);

      buffer.add(Buffer.from("data"));
      expect(buffer.isEmpty).toBe(false);
    });

    it("should handle watermarks correctly", () => {
      buffer.setWatermarks(10, 50, 1000);

      expect(buffer.isBelowLowWatermark).toBe(true);
      expect(buffer.isAboveHighWatermark).toBe(false);

      buffer.add(Buffer.alloc(60));
      expect(buffer.isBelowLowWatermark).toBe(false);
      expect(buffer.isAboveHighWatermark).toBe(false);

      buffer.add(Buffer.alloc(50));
      expect(buffer.isAboveHighWatermark).toBe(true);
    });
  });

  describe("buffer reservations", () => {
    it("should reserve space in buffer", () => {
      const reservation = buffer.reserve(100);
      expect(reservation).toBeDefined();
    });

    it("should commit reservation", () => {
      const reservation = buffer.reserve(50);
      const testData = Buffer.from("Committed data");

      buffer.commitReservation(reservation, testData.length);
      expect(buffer.length).toBe(testData.length);
    });

    it("should cancel reservation", () => {
      const reservation = buffer.reserve(50);
      const initialLength = buffer.length;

      buffer.cancelReservation(reservation);
      expect(buffer.length).toBe(initialLength);
    });
  });

  describe("buffer operations", () => {
    it("should get contiguous data", () => {
      const testData = Buffer.from("Contiguous data");
      buffer.add(testData);

      const contiguous = buffer.getContiguous(0, testData.length);
      expect(contiguous).toBeDefined();
      expect(contiguous.actualLength).toBe(testData.length);
    });

    it("should linearize buffer", () => {
      const testData = Buffer.from("Linearized data");
      buffer.add(testData);

      const linearized = buffer.linearize(testData.length);
      expect(linearized).toBeDefined();
      expect(linearized.length).toBe(testData.length);
    });

    it("should peek at buffer data", () => {
      const testData = Buffer.from("Peek data");
      buffer.add(testData);

      const peekBuffer = Buffer.alloc(4);
      buffer.peek(0, peekBuffer, 4);
      expect(peekBuffer).toBeDefined();
    });

    it("should search buffer for data", () => {
      const testData = Buffer.from("Searchable data");
      buffer.add(testData);

      const searchResult = buffer.search(Buffer.from("data"));
      expect(searchResult).toBeGreaterThanOrEqual(0);
    });

    it("should find byte in buffer", () => {
      const testData = Buffer.from("Find byte");
      buffer.add(testData);

      const byteIndex = buffer.findByte(98); // 'b' in "byte"
      expect(byteIndex).toBeGreaterThanOrEqual(0);
    });
  });

  describe("integer operations", () => {
    it("should write little-endian integers", () => {
      const testValue = 12345;
      buffer.writeLittleEndianInt(testValue, 4);

      expect(buffer.length).toBe(4);
    });

    it("should write big-endian integers", () => {
      const testValue = 67890;
      buffer.writeBigEndianInt(testValue, 4);

      expect(buffer.length).toBe(4);
    });

    it("should read little-endian integers", () => {
      const testValue = 12345;
      buffer.writeLittleEndianInt(testValue, 4);

      const readValue = buffer.readLittleEndianInt(4);
      expect(readValue).toBeDefined();
    });

    it("should read big-endian integers", () => {
      const testValue = 67890;
      buffer.writeBigEndianInt(testValue, 4);

      const readValue = buffer.readBigEndianInt(4);
      expect(readValue).toBeDefined();
    });
  });

  describe("buffer cloning and copying", () => {
    it("should clone buffer", () => {
      const testData = Buffer.from("Clone data");
      buffer.add(testData);

      const cloned = buffer.clone();
      expect(cloned.length).toBe(buffer.length);
      expect(cloned.capacity).toBe(buffer.capacity);

      cloned.release();
    });

    it("should create copy-on-write buffer", () => {
      const testData = Buffer.from("COW data");
      buffer.add(testData);

      const cowBuffer = buffer.createCopyOnWrite();
      expect(cowBuffer.length).toBe(buffer.length);

      cowBuffer.release();
    });
  });

  describe("buffer statistics", () => {
    it("should provide buffer statistics", () => {
      const testData = Buffer.from("Statistics data");
      buffer.add(testData);

      const stats = buffer.getStats();
      expect(stats).toBeDefined();
    });
  });

  describe("buffer cleanup", () => {
    it("should release buffer resources", () => {
      const testBuffer = new AdvancedBuffer(256, BufferOwnership.OWNED);
      testBuffer.add(Buffer.from("Cleanup test"));

      expect(() => testBuffer.release()).not.toThrow();
    });

    it("should handle multiple releases gracefully", () => {
      expect(() => buffer.release()).not.toThrow();
      expect(() => buffer.release()).not.toThrow(); // Should be safe to call multiple times
    });
  });
});

describe("AdvancedBufferPool", () => {
  let pool: AdvancedBufferPool;

  beforeEach(() => {
    pool = new AdvancedBufferPool(512, 10, 5, false, true);
  });

  afterEach(() => {
    if (pool) {
      pool.destroy();
    }
  });

  describe("pool creation and configuration", () => {
    it("should create pool with specified configuration", () => {
      expect(pool).toBeDefined();
      const stats = pool.getStats();
      expect(stats.freeCount).toBe(10);
    });

    it("should create pool with different configurations", () => {
      const smallPool = new AdvancedBufferPool(256, 5, 2, true, false);
      expect(smallPool).toBeDefined();
      const stats = smallPool.getStats();
      expect(stats.freeCount).toBe(5);

      smallPool.destroy();
    });
  });

  describe("buffer acquisition and release", () => {
    it("should acquire buffer from pool", () => {
      const acquiredBuffer = pool.acquire();
      expect(acquiredBuffer).toBeDefined();
      if (acquiredBuffer) {
        expect(acquiredBuffer.capacity).toBe(512);
        pool.release(acquiredBuffer);
      }
    });

    it("should release buffer back to pool", () => {
      const acquiredBuffer = pool.acquire();
      if (acquiredBuffer) {
        const initialFreeCount = pool.getStats().freeCount;

        pool.release(acquiredBuffer);

        const finalFreeCount = pool.getStats().freeCount;
        expect(finalFreeCount).toBe(initialFreeCount + 1);
      }
    });

    it("should handle multiple acquisitions", () => {
      const buffers: AdvancedBuffer[] = [];

      for (let i = 0; i < 5; i++) {
        const buffer = pool.acquire();
        if (buffer) {
          expect(buffer).toBeDefined();
          buffers.push(buffer);
        }
      }

      buffers.forEach((buffer) => pool.release(buffer));
    });
  });

  describe("pool statistics and monitoring", () => {
    it("should provide accurate pool statistics", () => {
      const stats = pool.getStats();

      expect(stats.freeCount).toBe(10);
      expect(stats.usedCount).toBe(0);
    });

    it("should track buffer usage correctly", () => {
      const initialStats = pool.getStats();
      const buffer = pool.acquire();

      if (buffer) {
        const afterAcquireStats = pool.getStats();
        expect(afterAcquireStats.usedCount).toBe(initialStats.usedCount + 1);
        expect(afterAcquireStats.freeCount).toBe(initialStats.freeCount - 1);

        pool.release(buffer);

        const afterReleaseStats = pool.getStats();
        expect(afterReleaseStats.usedCount).toBe(initialStats.usedCount);
        expect(afterReleaseStats.freeCount).toBe(initialStats.freeCount);
      }
    });
  });

  describe("pool optimization", () => {
    it("should handle pool destruction", () => {
      expect(() => pool.destroy()).not.toThrow();
    });
  });

  describe("edge cases and error handling", () => {
    it("should handle empty pool gracefully", () => {
      // Acquire all buffers
      const buffers: AdvancedBuffer[] = [];
      for (let i = 0; i < 10; i++) {
        const buffer = pool.acquire();
        if (buffer) buffers.push(buffer);
      }

      // Try to acquire from empty pool
      const extraBuffer = pool.acquire();
      expect(extraBuffer).toBeNull();

      // Release all buffers
      buffers.forEach((buffer) => pool.release(buffer));
    });

    it("should handle invalid buffer releases", () => {
      const invalidBuffer = new AdvancedBuffer(256, BufferOwnership.OWNED);

      // Should not throw when releasing buffer not from this pool
      expect(() => pool.release(invalidBuffer)).not.toThrow();

      invalidBuffer.release();
    });
  });
});
