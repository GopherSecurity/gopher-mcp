/**
 * @file advanced-buffer.test.ts
 * @brief Comprehensive unit tests for AdvancedBuffer class
 *
 * Tests zero-copy buffer operations, fragment management, reservations,
 * integer I/O, search functionality, watermarks, and buffer pools.
 * All resources are managed using RAII guards for automatic cleanup.
 */

import {
  AdvancedBuffer,
  AdvancedBufferPool,
  BufferFlags,
  BufferOwnership,
} from "../buffers/advanced-buffer";
import { mcpFilterLib } from "../core/ffi-bindings";

// Mock the native library for testing
jest.mock("../core/ffi-bindings", () => ({
  mcpFilterLib: {
    mcp_buffer_create_owned: jest.fn(),
    mcp_buffer_create_view: jest.fn(),
    mcp_buffer_create_from_fragment: jest.fn(),
    mcp_buffer_clone: jest.fn(),
    mcp_buffer_create_cow: jest.fn(),
    mcp_buffer_add: jest.fn(),
    mcp_buffer_add_string: jest.fn(),
    mcp_buffer_add_buffer: jest.fn(),
    mcp_buffer_add_fragment: jest.fn(),
    mcp_buffer_prepend: jest.fn(),
    mcp_buffer_drain: jest.fn(),
    mcp_buffer_move: jest.fn(),
    mcp_buffer_set_drain_tracker: jest.fn(),
    mcp_buffer_reserve: jest.fn(),
    mcp_buffer_commit_reservation: jest.fn(),
    mcp_buffer_cancel_reservation: jest.fn(),
    mcp_buffer_get_contiguous: jest.fn(),
    mcp_buffer_linearize: jest.fn(),
    mcp_buffer_peek: jest.fn(),
    mcp_buffer_write_le_int: jest.fn(),
    mcp_buffer_write_be_int: jest.fn(),
    mcp_buffer_read_le_int: jest.fn(),
    mcp_buffer_read_be_int: jest.fn(),
    mcp_buffer_search: jest.fn(),
    mcp_buffer_find_byte: jest.fn(),
    mcp_buffer_length: jest.fn(),
    mcp_buffer_is_empty: jest.fn(),
    mcp_buffer_get_stats: jest.fn(),
    mcp_buffer_set_watermarks: jest.fn(),
    mcp_buffer_above_high_watermark: jest.fn(),
    mcp_buffer_below_low_watermark: jest.fn(),
    mcp_filter_buffer_release: jest.fn(),
    mcp_buffer_pool_create_ex: jest.fn(),
    mcp_buffer_pool_acquire: jest.fn(),
    mcp_buffer_pool_release: jest.fn(),
    mcp_buffer_pool_get_stats: jest.fn(),
    mcp_buffer_pool_trim: jest.fn(),
    mcp_buffer_pool_destroy: jest.fn(),
  },
}));

describe("AdvancedBuffer", () => {
  let mockHandle: number;
  let buffer: AdvancedBuffer;

  beforeEach(() => {
    mockHandle = 12345;
    jest.clearAllMocks();

    // Default mock implementations
    (mcpFilterLib.mcp_buffer_create_owned as jest.Mock).mockReturnValue(
      mockHandle
    );
    (mcpFilterLib.mcp_buffer_length as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_is_empty as jest.Mock).mockReturnValue(1);
    (mcpFilterLib.mcp_buffer_add as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_add_string as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_add_buffer as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_add_fragment as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_prepend as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_drain as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_move as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_set_drain_tracker as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_reserve as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_commit_reservation as jest.Mock).mockReturnValue(
      0
    );
    (mcpFilterLib.mcp_buffer_cancel_reservation as jest.Mock).mockReturnValue(
      0
    );
    (mcpFilterLib.mcp_buffer_get_contiguous as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_linearize as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_peek as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_write_le_int as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_write_be_int as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_read_le_int as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_read_be_int as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_search as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_find_byte as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_get_stats as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_set_watermarks as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_above_high_watermark as jest.Mock).mockReturnValue(
      0
    );
    (mcpFilterLib.mcp_buffer_below_low_watermark as jest.Mock).mockReturnValue(
      0
    );
  });

  afterEach(() => {
    if (buffer) {
      buffer.destroy();
    }
  });

  describe("constructor and basic operations", () => {
    it("should create buffer with initial capacity and ownership", () => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);

      expect(mcpFilterLib.mcp_buffer_create_owned).toHaveBeenCalledWith(
        1024,
        BufferOwnership.OWNED
      );
      expect(buffer.getHandle()).toBe(mockHandle);
    });

    it("should throw error on creation failure", () => {
      (mcpFilterLib.mcp_buffer_create_owned as jest.Mock).mockReturnValue(0);

      expect(() => {
        new AdvancedBuffer(1024, BufferOwnership.OWNED);
      }).toThrow("Failed to create advanced buffer");
    });

    it("should create buffer view (zero-copy reference)", () => {
      const data = Buffer.from("test data");
      const viewHandle = 67890;
      (mcpFilterLib.mcp_buffer_create_view as jest.Mock).mockReturnValue(
        viewHandle
      );

      const viewBuffer = AdvancedBuffer.createView(data, data.length);

      expect(mcpFilterLib.mcp_buffer_create_view).toHaveBeenCalledWith(
        data,
        data.length
      );
      expect(viewBuffer.getHandle()).toBe(viewHandle);
      expect(viewBuffer.getHandle()).toBe(viewHandle);
    });

    it("should create buffer from external fragment", () => {
      const fragment = {
        data: Buffer.from("fragment data"),
        size: 13,
        releaseCallback: jest.fn(),
        userData: { test: "data" },
      };
      const fragmentHandle = 11111;
      (
        mcpFilterLib.mcp_buffer_create_from_fragment as jest.Mock
      ).mockReturnValue(fragmentHandle);

      const fragmentBuffer = AdvancedBuffer.createFromFragment(fragment);

      expect(mcpFilterLib.mcp_buffer_create_from_fragment).toHaveBeenCalled();
      expect(fragmentBuffer.getHandle()).toBe(fragmentHandle);
      expect(fragmentBuffer.getHandle()).toBe(fragmentHandle);
    });
  });

  describe("buffer cloning and copy-on-write", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should clone buffer (deep copy)", () => {
      const cloneHandle = 22222;
      (mcpFilterLib.mcp_buffer_clone as jest.Mock).mockReturnValue(cloneHandle);

      const cloned = buffer.clone();

      expect(mcpFilterLib.mcp_buffer_clone).toHaveBeenCalledWith(mockHandle);
      expect(cloned.getHandle()).toBe(cloneHandle);
      expect(cloned.getHandle()).toBe(cloneHandle);
    });

    it("should create copy-on-write buffer", () => {
      const cowHandle = 33333;
      (mcpFilterLib.mcp_buffer_create_cow as jest.Mock).mockReturnValue(
        cowHandle
      );

      const cow = buffer.createCopyOnWrite();

      expect(mcpFilterLib.mcp_buffer_create_cow).toHaveBeenCalledWith(
        mockHandle
      );
      expect(cow.getHandle()).toBe(cowHandle);
      expect(cow.getHandle()).toBe(cowHandle);
    });
  });

  describe("buffer data operations", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should add data to buffer", () => {
      const data = Buffer.from("test data");

      buffer.add(data);

      expect(mcpFilterLib.mcp_buffer_add).toHaveBeenCalledWith(
        mockHandle,
        data,
        data.length
      );
    });

    it("should add data with specified length", () => {
      const data = Buffer.from("test data");
      const length = 4;

      buffer.add(data, length);

      expect(mcpFilterLib.mcp_buffer_add).toHaveBeenCalledWith(
        mockHandle,
        data,
        length
      );
    });

    it("should add string to buffer", () => {
      const str = "string data";

      buffer.addString(str);

      expect(mcpFilterLib.mcp_buffer_add_string).toHaveBeenCalledWith(
        mockHandle,
        str
      );
    });

    it("should add another buffer to buffer", () => {
      const sourceBuffer = new AdvancedBuffer(512, BufferOwnership.OWNED);
      const sourceHandle = 44444;
      (sourceBuffer as any).handle = sourceHandle;

      buffer.addBuffer(sourceBuffer);

      expect(mcpFilterLib.mcp_buffer_add_buffer).toHaveBeenCalledWith(
        mockHandle,
        sourceHandle
      );
    });

    it("should add buffer fragment (zero-copy)", () => {
      const fragment = {
        data: Buffer.from("fragment data"),
        size: 13,
        releaseCallback: jest.fn(),
        userData: { test: "data" },
      };

      buffer.addFragment(fragment);

      expect(mcpFilterLib.mcp_buffer_add).toHaveBeenCalledWith(
        mockHandle,
        fragment.data,
        fragment.size
      );
    });

    it("should prepend data to buffer", () => {
      const data = Buffer.from("prepend");

      buffer.prepend(data);

      expect(mcpFilterLib.mcp_buffer_prepend).toHaveBeenCalledWith(
        mockHandle,
        data,
        data.length
      );
    });

    it("should prepend data with specified length", () => {
      const data = Buffer.from("prepend data");
      const length = 7;

      buffer.prepend(data, length);

      expect(mcpFilterLib.mcp_buffer_prepend).toHaveBeenCalledWith(
        mockHandle,
        data,
        length
      );
    });
  });

  describe("buffer consumption operations", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should drain bytes from front of buffer", () => {
      const size = 5;

      buffer.drain(size);

      expect(mcpFilterLib.mcp_buffer_drain).toHaveBeenCalledWith(
        mockHandle,
        size
      );
    });

    it("should move data from one buffer to another", () => {
      const destination = new AdvancedBuffer(512, BufferOwnership.OWNED);
      const destHandle = 55555;
      (destination as any).handle = destHandle;
      const length = 10;

      buffer.moveTo(destination, length);

      expect(mcpFilterLib.mcp_buffer_move).toHaveBeenCalledWith(
        mockHandle,
        destHandle,
        length
      );
    });

    it("should move all data when length is 0", () => {
      const destination = new AdvancedBuffer(512, BufferOwnership.OWNED);
      const destHandle = 55555;
      (destination as any).handle = destHandle;

      buffer.moveTo(destination);

      expect(mcpFilterLib.mcp_buffer_move).toHaveBeenCalledWith(
        mockHandle,
        destHandle,
        0
      );
    });

    it("should set drain tracker for buffer", () => {
      const tracker = {
        callback: jest.fn(),
        userData: { test: "tracker" },
      };

      buffer.setDrainTracker(tracker);

      expect(mcpFilterLib.mcp_buffer_set_drain_tracker).toHaveBeenCalledWith(
        mockHandle,
        tracker
      );
    });
  });

  describe("buffer reservation system", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should reserve space for writing", () => {
      const minSize = 100;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_reserve as jest.Mock).mockImplementation(
        (handle, _size, reservation) => {
          reservation.data = Buffer.alloc(100);
          reservation.capacity = 100;
          reservation.buffer = handle;
          reservation.reservationId = 12345;
          return 0;
        }
      );

      const reservation = buffer.reserve(minSize);

      expect(mcpFilterLib.mcp_buffer_reserve).toHaveBeenCalledWith(
        mockHandle,
        minSize,
        reservation
      );
      expect(reservation.buffer).toBe(mockHandle);
    });

    it("should commit reserved space", () => {
      const reservation = {
        data: Buffer.alloc(100),
        capacity: 100,
        buffer: mockHandle,
        reservationId: 12345,
      };
      const bytesWritten = 50;

      buffer.commitReservation(reservation, bytesWritten);

      expect(mcpFilterLib.mcp_buffer_commit_reservation).toHaveBeenCalledWith(
        reservation,
        bytesWritten
      );
    });

    it("should cancel reservation", () => {
      const reservation = {
        data: Buffer.alloc(100),
        capacity: 100,
        buffer: mockHandle,
        reservationId: 12345,
      };

      buffer.cancelReservation(reservation);

      expect(mcpFilterLib.mcp_buffer_cancel_reservation).toHaveBeenCalledWith(
        reservation
      );
    });
  });

  describe("buffer access operations", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should get contiguous memory view", () => {
      const offset = 5;
      const length = 10;
      const mockActualLength = 10;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_get_contiguous as jest.Mock).mockImplementation(
        (_handle, _offset, _length, _dataPtr, actualLength) => {
          actualLength.value = mockActualLength;
          return 0;
        }
      );

      const result = buffer.getContiguous(offset, length);

      expect(mcpFilterLib.mcp_buffer_get_contiguous).toHaveBeenCalledWith(
        mockHandle,
        offset,
        length,
        expect.any(Object),
        expect.any(Object)
      );
      expect(result.actualLength).toBe(mockActualLength);
    });

    it("should linearize buffer (ensure contiguous memory)", () => {
      const size = 100;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_linearize as jest.Mock).mockImplementation(
        (_handle, size, dataPtr) => {
          dataPtr.ptr = Buffer.alloc(size);
          return 0;
        }
      );

      const result = buffer.linearize(size);

      expect(mcpFilterLib.mcp_buffer_linearize).toHaveBeenCalledWith(
        mockHandle,
        size,
        expect.any(Object)
      );
      expect(result).toBeInstanceOf(Buffer);
    });

    it("should peek at buffer data without consuming", () => {
      const offset = 2;
      const data = Buffer.alloc(5);
      const length = 5;

      buffer.peek(offset, data, length);

      expect(mcpFilterLib.mcp_buffer_peek).toHaveBeenCalledWith(
        mockHandle,
        offset,
        data,
        length
      );
    });
  });

  describe("type-safe I/O operations", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should write little-endian integer", () => {
      const value = 0x1234;
      const size = 2;

      buffer.writeLittleEndianInt(value, size);

      expect(mcpFilterLib.mcp_buffer_write_le_int).toHaveBeenCalledWith(
        mockHandle,
        value,
        size
      );
    });

    it("should write big-endian integer", () => {
      const value = 0x5678;
      const size = 2;

      buffer.writeBigEndianInt(value, size);

      expect(mcpFilterLib.mcp_buffer_write_be_int).toHaveBeenCalledWith(
        mockHandle,
        value,
        size
      );
    });

    it("should read little-endian integer", () => {
      const size = 4;
      const mockValue = 0x12345678;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_read_le_int as jest.Mock).mockImplementation(
        (_handle, _size, value) => {
          value.value = mockValue;
          return 0;
        }
      );

      const result = buffer.readLittleEndianInt(size);

      expect(mcpFilterLib.mcp_buffer_read_le_int).toHaveBeenCalledWith(
        mockHandle,
        size,
        expect.any(Object)
      );
      expect(result).toBe(mockValue);
    });

    it("should read big-endian integer", () => {
      const size = 4;
      const mockValue = 0x87654321;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_read_be_int as jest.Mock).mockImplementation(
        (_handle, _size, value) => {
          value.value = mockValue;
          return 0;
        }
      );

      const result = buffer.readBigEndianInt(size);

      expect(mcpFilterLib.mcp_buffer_read_be_int).toHaveBeenCalledWith(
        mockHandle,
        size,
        expect.any(Object)
      );
      expect(result).toBe(mockValue);
    });
  });

  describe("buffer search operations", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should search for pattern in buffer", () => {
      const pattern = Buffer.from("fox");
      const offset = 0;
      const mockPosition = 16;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_search as jest.Mock).mockImplementation(
        (_handle, _pattern, _patternSize, _offset, position) => {
          position.value = mockPosition;
          return 0;
        }
      );

      const result = buffer.search(pattern, offset);

      expect(mcpFilterLib.mcp_buffer_search).toHaveBeenCalledWith(
        mockHandle,
        pattern,
        pattern.length,
        offset,
        expect.any(Object)
      );
      expect(result).toBe(mockPosition);
    });

    it("should find byte in buffer", () => {
      const byte = 0x0a; // newline
      const mockPosition = 6;

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_find_byte as jest.Mock).mockImplementation(
        (_handle, _byte, position) => {
          position.value = mockPosition;
          return 0;
        }
      );

      const result = buffer.findByte(byte);

      expect(mcpFilterLib.mcp_buffer_find_byte).toHaveBeenCalledWith(
        mockHandle,
        byte,
        expect.any(Object)
      );
      expect(result).toBe(mockPosition);
    });
  });

  describe("buffer information and statistics", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should get buffer length", () => {
      const mockLength = 100;
      (mcpFilterLib.mcp_buffer_length as jest.Mock).mockReturnValue(mockLength);

      const length = buffer.length;

      expect(mcpFilterLib.mcp_buffer_length).toHaveBeenCalledWith(mockHandle);
      expect(length).toBe(mockLength);
    });

    it("should check if buffer is empty", () => {
      const mockEmpty = 1; // true
      (mcpFilterLib.mcp_buffer_is_empty as jest.Mock).mockReturnValue(
        mockEmpty
      );

      const isEmpty = buffer.isEmpty;

      expect(mcpFilterLib.mcp_buffer_is_empty).toHaveBeenCalledWith(mockHandle);
      expect(isEmpty).toBe(true);
    });

    it("should get buffer statistics", () => {
      const mockStats = {
        totalBytes: 1000,
        usedBytes: 500,
        sliceCount: 5,
        fragmentCount: 2,
        readOperations: 10,
        writeOperations: 8,
      };

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_get_stats as jest.Mock).mockImplementation(
        (_handle, stats) => {
          stats.totalBytes = mockStats.totalBytes;
          stats.usedBytes = mockStats.usedBytes;
          stats.sliceCount = mockStats.sliceCount;
          stats.fragmentCount = mockStats.fragmentCount;
          stats.readOperations = mockStats.readOperations;
          stats.writeOperations = mockStats.writeOperations;
          return 0;
        }
      );

      const stats = buffer.getStats();

      expect(mcpFilterLib.mcp_buffer_get_stats).toHaveBeenCalledWith(
        mockHandle,
        expect.any(Object)
      );
      expect(stats.totalBytes).toBe(mockStats.totalBytes);
    });
  });

  describe("buffer watermarks and flow control", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should set buffer watermarks for flow control", () => {
      const lowWatermark = 100;
      const highWatermark = 500;
      const overflowWatermark = 1000;

      buffer.setWatermarks(lowWatermark, highWatermark, overflowWatermark);

      expect(mcpFilterLib.mcp_buffer_set_watermarks).toHaveBeenCalledWith(
        mockHandle,
        lowWatermark,
        highWatermark,
        overflowWatermark
      );
    });

    it("should check if buffer is above high watermark", () => {
      const mockAbove = 1; // true
      (
        mcpFilterLib.mcp_buffer_above_high_watermark as jest.Mock
      ).mockReturnValue(mockAbove);

      const isAbove = buffer.isAboveHighWatermark;

      expect(mcpFilterLib.mcp_buffer_above_high_watermark).toHaveBeenCalledWith(
        mockHandle
      );
      expect(isAbove).toBe(true);
    });

    it("should check if buffer is below low watermark", () => {
      const mockBelow = 1; // true
      (
        mcpFilterLib.mcp_buffer_below_low_watermark as jest.Mock
      ).mockReturnValue(mockBelow);

      const isBelow = buffer.isBelowLowWatermark;

      expect(mcpFilterLib.mcp_buffer_below_low_watermark).toHaveBeenCalledWith(
        mockHandle
      );
      expect(isBelow).toBe(true);
    });
  });

  describe("buffer lifecycle management", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should get the underlying buffer handle", () => {
      const handle = buffer.getHandle();

      expect(handle).toBe(mockHandle);
    });

    it("should release the buffer", () => {
      buffer.release();

      expect(mcpFilterLib.mcp_filter_buffer_release).toHaveBeenCalledWith(
        mockHandle
      );
      expect(buffer.getHandle()).toBe(0);
    });

    it("should destroy the buffer", () => {
      buffer.destroy();

      expect(mcpFilterLib.mcp_filter_buffer_release).toHaveBeenCalledWith(
        mockHandle
      );
    });

    it("should handle multiple destroy calls gracefully", () => {
      buffer.destroy();
      buffer.destroy(); // Should not throw

      expect(mcpFilterLib.mcp_filter_buffer_release).toHaveBeenCalledTimes(1);
    });
  });

  describe("error handling", () => {
    beforeEach(() => {
      buffer = new AdvancedBuffer(1024, BufferOwnership.OWNED);
    });

    it("should throw error on add failure", () => {
      (mcpFilterLib.mcp_buffer_add as jest.Mock).mockReturnValue(-1);

      expect(() => {
        buffer.add(Buffer.from("test"));
      }).toThrow("Failed to add data to buffer");
    });

    it("should throw error on drain failure", () => {
      (mcpFilterLib.mcp_buffer_drain as jest.Mock).mockReturnValue(-1);

      expect(() => {
        buffer.drain(5);
      }).toThrow("Failed to drain buffer");
    });

    it("should throw error on reserve failure", () => {
      (mcpFilterLib.mcp_buffer_reserve as jest.Mock).mockReturnValue(-1);

      expect(() => {
        buffer.reserve(100);
      }).toThrow("Failed to reserve buffer space");
    });
  });
});

describe("AdvancedBufferPool", () => {
  let mockPool: number;
  let pool: AdvancedBufferPool;

  beforeEach(() => {
    mockPool = 99999;
    jest.clearAllMocks();

    (mcpFilterLib.mcp_buffer_pool_create_ex as jest.Mock).mockReturnValue(
      mockPool
    );
    (mcpFilterLib.mcp_buffer_pool_get_stats as jest.Mock).mockReturnValue(0);
    (mcpFilterLib.mcp_buffer_pool_trim as jest.Mock).mockReturnValue(0);
  });

  afterEach(() => {
    if (pool) {
      pool.destroy();
    }
  });

  describe("constructor and configuration", () => {
    it("should create buffer pool with configuration", () => {
      const bufferSize = 4096;
      const maxBuffers = 10;
      const preallocCount = 5;
      const useThreadLocal = true;
      const zeroOnAlloc = true;

      pool = new AdvancedBufferPool(
        bufferSize,
        maxBuffers,
        preallocCount,
        useThreadLocal,
        zeroOnAlloc
      );

      expect(mcpFilterLib.mcp_buffer_pool_create_ex).toHaveBeenCalledWith({
        bufferSize,
        maxBuffers,
        preallocCount,
        useThreadLocal: 1,
        zeroOnAlloc: 1,
      });
    });

    it("should throw error on pool creation failure", () => {
      (mcpFilterLib.mcp_buffer_pool_create_ex as jest.Mock).mockReturnValue(0);

      expect(() => {
        new AdvancedBufferPool(1024, 5);
      }).toThrow("Failed to create buffer pool");
    });
  });

  describe("buffer acquisition and release", () => {
    beforeEach(() => {
      pool = new AdvancedBufferPool(1024, 5);
    });

    it("should acquire buffer from pool", () => {
      const mockBufferHandle = 11111;
      (mcpFilterLib.mcp_buffer_pool_acquire as jest.Mock).mockReturnValue(
        mockBufferHandle
      );

      const buffer = pool.acquire();

      expect(mcpFilterLib.mcp_buffer_pool_acquire).toHaveBeenCalledWith(
        mockPool
      );
      expect(buffer).toBeInstanceOf(AdvancedBuffer);
      expect(buffer?.getHandle()).toBe(mockBufferHandle);
    });

    it("should return null when pool is exhausted", () => {
      (mcpFilterLib.mcp_buffer_pool_acquire as jest.Mock).mockReturnValue(0);

      const buffer = pool.acquire();

      expect(buffer).toBeNull();
    });

    it("should release buffer back to pool", () => {
      const mockBuffer = new AdvancedBuffer(1024, BufferOwnership.SHARED);
      const bufferHandle = 22222;
      (mockBuffer as any).handle = bufferHandle;

      pool.release(mockBuffer);

      expect(mcpFilterLib.mcp_buffer_pool_release).toHaveBeenCalledWith(
        mockPool,
        bufferHandle
      );
    });
  });

  describe("pool statistics and management", () => {
    beforeEach(() => {
      pool = new AdvancedBufferPool(1024, 5);
    });

    it("should get pool statistics", () => {
      const mockStats = {
        freeCount: 3,
        usedCount: 2,
        totalAllocated: 5120,
      };

      // Mock the C API to modify the output parameters
      (mcpFilterLib.mcp_buffer_pool_get_stats as jest.Mock).mockImplementation(
        (_pool, freeCount, usedCount, totalAllocated) => {
          freeCount.value = mockStats.freeCount;
          usedCount.value = mockStats.usedCount;
          totalAllocated.value = mockStats.totalAllocated;
          return 0;
        }
      );

      const stats = pool.getStats();

      expect(mcpFilterLib.mcp_buffer_pool_get_stats).toHaveBeenCalledWith(
        mockPool,
        expect.any(Object),
        expect.any(Object),
        expect.any(Object)
      );
      expect(stats.freeCount).toBe(mockStats.freeCount);
      expect(stats.usedCount).toBe(mockStats.usedCount);
      expect(stats.totalAllocated).toBe(mockStats.totalAllocated);
    });

    it("should trim pool to reduce memory usage", () => {
      const targetFree = 2;

      pool.trim(targetFree);

      expect(mcpFilterLib.mcp_buffer_pool_trim).toHaveBeenCalledWith(
        mockPool,
        targetFree
      );
    });

    it("should destroy the pool", () => {
      pool.destroy();

      expect(mcpFilterLib.mcp_buffer_pool_destroy).toHaveBeenCalledWith(
        mockPool
      );
    });

    it("should handle multiple destroy calls gracefully", () => {
      pool.destroy();
      pool.destroy(); // Should not throw

      expect(mcpFilterLib.mcp_buffer_pool_destroy).toHaveBeenCalledTimes(1);
    });
  });
});

describe("BufferOwnership and BufferFlags", () => {
  it("should have correct ownership values", () => {
    expect(BufferOwnership.NONE).toBe(0);
    expect(BufferOwnership.SHARED).toBe(1);
    expect(BufferOwnership.OWNED).toBe(2);
    expect(BufferOwnership.EXCLUSIVE).toBe(2);
    expect(BufferOwnership.EXTERNAL).toBe(3);
  });

  it("should have correct flag values", () => {
    expect(BufferFlags.READONLY).toBe(0x01);
    expect(BufferFlags.OWNED).toBe(0x02);
    expect(BufferFlags.EXTERNAL).toBe(0x04);
    expect(BufferFlags.ZERO_COPY).toBe(0x08);
  });
});
