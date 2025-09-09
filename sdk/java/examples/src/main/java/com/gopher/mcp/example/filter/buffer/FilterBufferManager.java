package com.gopher.mcp.example.filter.buffer;

import com.gopher.mcp.example.filter.utils.FilterConfiguration;
import com.gopher.mcp.filter.McpFilter;
import com.gopher.mcp.filter.McpFilterBuffer;
import com.gopher.mcp.filter.type.buffer.BufferOwnership;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.ByteBuffer;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Manages buffer allocation and pooling for the filter system.
 * Implements efficient buffer management with zero-copy operations,
 * watermark-based flow control, and comprehensive statistics.
 *
 * <p>Features:
 * <ul>
 *   <li>Object pooling for frequently used buffer sizes</li>
 *   <li>Zero-copy operations for optimal performance</li>
 *   <li>Watermark-based flow control</li>
 *   <li>Automatic buffer resizing</li>
 *   <li>Memory pressure detection</li>
 *   <li>Statistics collection and monitoring</li>
 * </ul>
 *
 * @author Gopher MCP SDK
 * @since 1.0.0
 */
public class FilterBufferManager implements AutoCloseable {
    private static final Logger LOGGER = LoggerFactory.getLogger(FilterBufferManager.class);

    // Core components
    private final McpFilterBuffer buffer;
    private final McpFilter filter;
    private final FilterConfiguration config;

    // Buffer pools for different sizes
    private final BufferPool smallPool;  // 4KB buffers
    private final BufferPool mediumPool; // 64KB buffers
    private final BufferPool largePool;  // 1MB buffers

    // Statistics
    private final AtomicLong totalAllocated = new AtomicLong(0);
    private final AtomicLong totalReleased = new AtomicLong(0);
    private final AtomicLong totalPoolHits = new AtomicLong(0);
    private final AtomicLong totalPoolMisses = new AtomicLong(0);
    private final AtomicInteger activeBuffers = new AtomicInteger(0);

    // Watermarks for flow control
    private volatile long lowWatermark;
    private volatile long highWatermark;
    private volatile long overflowWatermark;

    // Memory pressure monitoring
    private volatile boolean underMemoryPressure = false;
    private final AtomicLong currentMemoryUsage = new AtomicLong(0);

    /**
     * Creates a new buffer manager with the specified configuration.
     *
     * @param config The filter configuration
     */
    public FilterBufferManager(FilterConfiguration config) {
        this.config = config;
        this.buffer = new McpFilterBuffer();
        this.filter = new McpFilter();

        // Initialize buffer pools
        int poolSize = config.getBufferPoolSize();
        this.smallPool = new BufferPool(4096, poolSize / 4);
        this.mediumPool = new BufferPool(65536, poolSize / 2);
        this.largePool = new BufferPool(1048576, poolSize / 4);

        // Set watermarks (configurable percentages of max memory)
        long maxMemoryUsage = Runtime.getRuntime().maxMemory() / 10; // Use 10% of heap
        this.lowWatermark = maxMemoryUsage * 30 / 100;    // 30% of allocated memory
        this.highWatermark = maxMemoryUsage * 70 / 100;   // 70% of allocated memory
        this.overflowWatermark = maxMemoryUsage * 90 / 100; // 90% of allocated memory

        LOGGER.info("FilterBufferManager initialized with pools - Small: {}, Medium: {}, Large: {}",
            smallPool.getCapacity(), mediumPool.getCapacity(), largePool.getCapacity());
    }

    /**
     * Acquires a buffer from the pool or creates a new one.
     *
     * @return The buffer handle
     */
    public long acquireBuffer() {
        return acquireBuffer(config.getBufferSize());
    }

    /**
     * Acquires a buffer of the specified size.
     *
     * @param size The required buffer size
     * @return The buffer handle
     */
    public long acquireBuffer(int size) {
        // Check memory pressure
        if (underMemoryPressure) {
            handleMemoryPressure();
        }

        BufferPool pool = selectPool(size);
        Long handle;

        if (pool != null) {
            handle = pool.acquire();
            if (handle != null) {
                totalPoolHits.incrementAndGet();
                activeBuffers.incrementAndGet();
                return handle;
            }
        }

        // Pool miss - create new buffer
        totalPoolMisses.incrementAndGet();

        BufferOwnership ownership = config.isZeroCopyEnabled() ?
            BufferOwnership.SHARED : BufferOwnership.EXCLUSIVE;

        handle = buffer.createOwned(size, ownership);
        if (handle != 0) {
            totalAllocated.incrementAndGet();
            activeBuffers.incrementAndGet();
            currentMemoryUsage.addAndGet(size);

            // Check if we've exceeded watermarks
            checkWatermarks();
        }

        return handle;
    }

    /**
     * Releases a buffer back to the pool.
     *
     * @param handle The buffer handle to release
     */
    public void releaseBuffer(long handle) {
        if (handle == 0) {
            return;
        }

        try {
            // Get buffer size for pool selection
            long capacity = buffer.capacity(handle);
            BufferPool pool = selectPool((int)capacity);

            if (pool != null && pool.canAccept()) {
                // Clear buffer before returning to pool
                // buffer.clear(handle); // Not available in current API

                if (pool.release(handle)) {
                    activeBuffers.decrementAndGet();
                    totalReleased.incrementAndGet();
                    return;
                }
            }

            // Pool is full or buffer doesn't fit - destroy it
            filter.bufferRelease(handle);
            activeBuffers.decrementAndGet();
            totalReleased.incrementAndGet();
            currentMemoryUsage.addAndGet(-capacity);

        } catch (Exception e) {
            LOGGER.error("Failed to release buffer {}", handle, e);
        }
    }

    /**
     * Creates a zero-copy view of the provided data.
     *
     * @param data The data to create a view of
     * @return The buffer handle
     */
    public long createZeroCopyView(byte[] data) {
        if (!config.isZeroCopyEnabled()) {
            // Fall back to regular buffer
            long handle = acquireBuffer(data.length);
            if (handle != 0) {
                buffer.add(handle, data);
            }
            return handle;
        }

        long handle = buffer.createView(data);
        if (handle != 0) {
            activeBuffers.incrementAndGet();
            totalAllocated.incrementAndGet();
        }
        return handle;
    }

    /**
     * Reserves space in a buffer for zero-copy writing.
     *
     * @param handle The buffer handle
     * @param size The size to reserve
     * @return The reserved ByteBuffer or null if failed
     */
    public ByteBuffer reserveSpace(long handle, int size) {
        var reservation = buffer.reserve(handle, size);
        if (reservation != null) {
            return reservation.getData();
        }
        return null;
    }

    /**
     * Sets watermarks for flow control.
     *
     * @param low The low watermark
     * @param high The high watermark
     * @param overflow The overflow watermark
     */
    public void setWatermarks(long low, long high, long overflow) {
        if (low >= high || high >= overflow) {
            throw new IllegalArgumentException("Invalid watermarks: low < high < overflow required");
        }

        this.lowWatermark = low;
        this.highWatermark = high;
        this.overflowWatermark = overflow;

        LOGGER.info("Watermarks updated - Low: {}, High: {}, Overflow: {}", low, high, overflow);
    }

    /**
     * Checks if the current memory usage exceeds watermarks.
     */
    private void checkWatermarks() {
        long usage = currentMemoryUsage.get();

        if (usage > overflowWatermark) {
            LOGGER.warn("Memory usage exceeded overflow watermark: {} > {}", usage, overflowWatermark);
            underMemoryPressure = true;
            triggerEmergencyCleanup();
        } else if (usage > highWatermark) {
            if (!underMemoryPressure) {
                LOGGER.info("Memory usage exceeded high watermark: {} > {}", usage, highWatermark);
                underMemoryPressure = true;
            }
        } else if (usage < lowWatermark) {
            if (underMemoryPressure) {
                LOGGER.info("Memory usage below low watermark: {} < {}", usage, lowWatermark);
                underMemoryPressure = false;
            }
        }
    }

    /**
     * Handles memory pressure by releasing unused buffers.
     */
    private void handleMemoryPressure() {
        LOGGER.info("Handling memory pressure - current usage: {} MB",
            currentMemoryUsage.get() / 1024 / 1024);

        // Clear pools to free memory
        int released = 0;
        released += smallPool.clear();
        released += mediumPool.clear();
        released += largePool.clear();

        LOGGER.info("Released {} buffers due to memory pressure", released);

        // Force garbage collection if still under pressure
        if (currentMemoryUsage.get() > highWatermark) {
            System.gc();
        }
    }

    /**
     * Triggers emergency cleanup when overflow watermark is exceeded.
     */
    private void triggerEmergencyCleanup() {
        LOGGER.warn("Triggering emergency cleanup");

        // Clear all pools immediately
        smallPool.clearAll();
        mediumPool.clearAll();
        largePool.clearAll();

        // Force immediate garbage collection
        System.gc();
        System.runFinalization();
    }

    /**
     * Selects the appropriate pool for the given size.
     */
    private BufferPool selectPool(int size) {
        if (size <= 4096) {
            return smallPool;
        } else if (size <= 65536) {
            return mediumPool;
        } else if (size <= 1048576) {
            return largePool;
        }
        return null; // No pool for very large buffers
    }

    /**
     * Gets statistics about buffer usage.
     *
     * @return The buffer statistics
     */
    public BufferStatistics getStatistics() {
        return new BufferStatistics(
            totalAllocated.get(),
            totalReleased.get(),
            activeBuffers.get(),
            totalPoolHits.get(),
            totalPoolMisses.get(),
            currentMemoryUsage.get(),
            underMemoryPressure
        );
    }

    /**
     * Optimizes buffer pools based on usage patterns.
     */
    public void optimizePools() {
        long hits = totalPoolHits.get();
        long misses = totalPoolMisses.get();

        if (misses > hits * 2) {
            // Too many misses - increase pool sizes
            LOGGER.info("Optimizing pools - increasing capacity due to high miss rate");
            smallPool.increaseCapacity(10);
            mediumPool.increaseCapacity(10);
            largePool.increaseCapacity(5);
        } else if (hits > misses * 10) {
            // Very high hit rate - might have too many buffers
            LOGGER.info("Optimizing pools - reducing capacity due to low miss rate");
            smallPool.decreaseCapacity(5);
            mediumPool.decreaseCapacity(5);
            largePool.decreaseCapacity(2);
        }
    }

    @Override
    public void close() {
        LOGGER.info("Closing FilterBufferManager");

        try {
            // Clear all pools
            smallPool.close();
            mediumPool.close();
            largePool.close();

            // Log final statistics
            LOGGER.info("Final statistics - Allocated: {}, Released: {}, Active: {}, " +
                "Pool hits: {}, Pool misses: {}",
                totalAllocated.get(), totalReleased.get(), activeBuffers.get(),
                totalPoolHits.get(), totalPoolMisses.get());

            // Close components
            if (buffer != null) {
                buffer.close();
            }
            if (filter != null) {
                filter.close();
            }

        } catch (Exception e) {
            LOGGER.error("Error closing FilterBufferManager", e);
        }
    }

    /**
     * Internal buffer pool implementation.
     */
    private class BufferPool {
        private final int bufferSize;
        private final Queue<Long> available;
        private final AtomicInteger capacity;
        private final AtomicInteger size;
        private final ReentrantLock lock;

        BufferPool(int bufferSize, int initialCapacity) {
            this.bufferSize = bufferSize;
            this.available = new ConcurrentLinkedQueue<>();
            this.capacity = new AtomicInteger(initialCapacity);
            this.size = new AtomicInteger(0);
            this.lock = new ReentrantLock();
        }

        Long acquire() {
            Long handle = available.poll();
            if (handle != null) {
                size.decrementAndGet();
            }
            return handle;
        }

        boolean release(long handle) {
            if (size.get() >= capacity.get()) {
                return false;
            }

            available.offer(handle);
            size.incrementAndGet();
            return true;
        }

        boolean canAccept() {
            return size.get() < capacity.get();
        }

        int clear() {
            lock.lock();
            try {
                int cleared = 0;
                Long handle;
                while ((handle = available.poll()) != null) {
                    filter.bufferRelease(handle);
                    cleared++;
                }
                size.set(0);
                return cleared;
            } finally {
                lock.unlock();
            }
        }

        void clearAll() {
            lock.lock();
            try {
                Long handle;
                while ((handle = available.poll()) != null) {
                    try {
                        filter.bufferRelease(handle);
                    } catch (Exception e) {
                        // Ignore errors during emergency cleanup
                    }
                }
                size.set(0);
            } finally {
                lock.unlock();
            }
        }

        void increaseCapacity(int amount) {
            capacity.addAndGet(amount);
        }

        void decreaseCapacity(int amount) {
            int newCapacity = Math.max(1, capacity.get() - amount);
            capacity.set(newCapacity);

            // Remove excess buffers if needed
            while (size.get() > newCapacity) {
                Long handle = available.poll();
                if (handle != null) {
                    filter.bufferRelease(handle);
                    size.decrementAndGet();
                }
            }
        }

        int getCapacity() {
            return capacity.get();
        }

        void close() {
            clearAll();
        }
    }

    /**
     * Statistics about buffer usage.
     */
    public static class BufferStatistics {
        private final long totalAllocated;
        private final long totalReleased;
        private final int activeBuffers;
        private final long poolHits;
        private final long poolMisses;
        private final long memoryUsage;
        private final boolean underPressure;

        public BufferStatistics(long totalAllocated, long totalReleased, int activeBuffers,
                               long poolHits, long poolMisses, long memoryUsage, boolean underPressure) {
            this.totalAllocated = totalAllocated;
            this.totalReleased = totalReleased;
            this.activeBuffers = activeBuffers;
            this.poolHits = poolHits;
            this.poolMisses = poolMisses;
            this.memoryUsage = memoryUsage;
            this.underPressure = underPressure;
        }

        public long getTotalAllocated() { return totalAllocated; }
        public long getTotalReleased() { return totalReleased; }
        public int getActiveBuffers() { return activeBuffers; }
        public long getPoolHits() { return poolHits; }
        public long getPoolMisses() { return poolMisses; }
        public long getMemoryUsage() { return memoryUsage; }
        public boolean isUnderPressure() { return underPressure; }

        public double getPoolHitRate() {
            long total = poolHits + poolMisses;
            return total > 0 ? (double) poolHits / total : 0.0;
        }

        @Override
        public String toString() {
            return String.format("BufferStatistics{allocated=%d, released=%d, active=%d, " +
                "hitRate=%.2f%%, memory=%dMB, pressure=%s}",
                totalAllocated, totalReleased, activeBuffers,
                getPoolHitRate() * 100, memoryUsage / 1024 / 1024,
                underPressure);
        }
    }
}
