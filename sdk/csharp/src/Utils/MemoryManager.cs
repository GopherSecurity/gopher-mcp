using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using GopherMcp.Types;

namespace GopherMcp.Utils
{
    /// <summary>
    /// Manages memory allocation and pooling for high-performance buffer operations
    /// </summary>
    public sealed class MemoryManager : IDisposable
    {
        private readonly ArrayPool<byte> _arrayPool;
        private readonly ConcurrentDictionary<int, BufferPool> _bufferPools;
        private readonly List<GCHandle> _pinnedMemory;
        private readonly object _pinnedMemoryLock = new object();
        private readonly Timer _gcPressureTimer;
        private readonly MemoryStatistics _statistics;
        private bool _disposed;

        /// <summary>
        /// Default instance for shared usage
        /// </summary>
        public static MemoryManager Default { get; } = new MemoryManager();

        /// <summary>
        /// Gets the current memory statistics
        /// </summary>
        public MemoryStatistics Statistics => _statistics.Clone();

        /// <summary>
        /// Gets or sets the high memory pressure threshold in bytes
        /// </summary>
        public long HighMemoryPressureThreshold { get; set; } = 100 * 1024 * 1024; // 100MB

        /// <summary>
        /// Gets or sets the critical memory pressure threshold in bytes
        /// </summary>
        public long CriticalMemoryPressureThreshold { get; set; } = 500 * 1024 * 1024; // 500MB

        /// <summary>
        /// Event raised when memory pressure changes
        /// </summary>
        public event EventHandler<MemoryPressureEventArgs> MemoryPressureChanged;

        /// <summary>
        /// Initializes a new instance of the MemoryManager class
        /// </summary>
        public MemoryManager() : this(ArrayPool<byte>.Shared)
        {
        }

        /// <summary>
        /// Initializes a new instance of the MemoryManager class with a custom array pool
        /// </summary>
        public MemoryManager(ArrayPool<byte> arrayPool)
        {
            _arrayPool = arrayPool ?? throw new ArgumentNullException(nameof(arrayPool));
            _bufferPools = new ConcurrentDictionary<int, BufferPool>();
            _pinnedMemory = new List<GCHandle>();
            _statistics = new MemoryStatistics();

            // Start GC pressure monitoring timer (every 5 seconds)
            _gcPressureTimer = new Timer(MonitorGcPressure, null, TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(5));
        }

        /// <summary>
        /// Rents a byte array from the pool
        /// </summary>
        /// <param name="minimumSize">Minimum size of the array</param>
        /// <returns>A rented byte array</returns>
        public byte[] RentArray(int minimumSize)
        {
            ThrowIfDisposed();

            if (minimumSize <= 0)
                throw new ArgumentOutOfRangeException(nameof(minimumSize));

            var array = _arrayPool.Rent(minimumSize);

            Interlocked.Increment(ref _statistics._allocations);
            Interlocked.Add(ref _statistics._currentlyAllocated, array.Length);
            Interlocked.Add(ref _statistics._totalAllocated, array.Length);

            if (_statistics._currentlyAllocated > _statistics._peakAllocated)
            {
                Interlocked.Exchange(ref _statistics._peakAllocated, _statistics._currentlyAllocated);
            }

            return array;
        }

        /// <summary>
        /// Returns a rented array to the pool
        /// </summary>
        /// <param name="array">Array to return</param>
        /// <param name="clearArray">Whether to clear the array before returning</param>
        public void ReturnArray(byte[] array, bool clearArray = false)
        {
            if (array == null)
                return;

            ThrowIfDisposed();

            _arrayPool.Return(array, clearArray);

            Interlocked.Increment(ref _statistics._deallocations);
            Interlocked.Add(ref _statistics._currentlyAllocated, -array.Length);
        }

        /// <summary>
        /// Gets or creates a buffer pool for a specific size
        /// </summary>
        /// <param name="bufferSize">Size of buffers in the pool</param>
        /// <param name="maxBuffers">Maximum number of buffers in the pool</param>
        /// <returns>A buffer pool for the specified size</returns>
        public BufferPool GetOrCreateBufferPool(int bufferSize, int maxBuffers = 100)
        {
            ThrowIfDisposed();

            return _bufferPools.GetOrAdd(bufferSize, size => new BufferPool(this, size, maxBuffers));
        }

        /// <summary>
        /// Allocates pinned memory that won't be moved by the GC
        /// </summary>
        /// <param name="size">Size of memory to allocate</param>
        /// <returns>A pinned memory allocation</returns>
        public PinnedMemory AllocatePinned(int size)
        {
            ThrowIfDisposed();

            if (size <= 0)
                throw new ArgumentOutOfRangeException(nameof(size));

            var array = new byte[size];
            var handle = GCHandle.Alloc(array, GCHandleType.Pinned);

            lock (_pinnedMemoryLock)
            {
                _pinnedMemory.Add(handle);
            }

            Interlocked.Increment(ref _statistics._pinnedAllocations);
            Interlocked.Add(ref _statistics._pinnedMemorySize, size);

            return new PinnedMemory(handle, array);
        }

        /// <summary>
        /// Releases a pinned memory allocation
        /// </summary>
        /// <param name="pinnedMemory">The pinned memory to release</param>
        public void ReleasePinned(PinnedMemory pinnedMemory)
        {
            ThrowIfDisposed();

            if (pinnedMemory.Handle.IsAllocated)
            {
                lock (_pinnedMemoryLock)
                {
                    _pinnedMemory.Remove(pinnedMemory.Handle);
                }

                Interlocked.Add(ref _statistics._pinnedMemorySize, -pinnedMemory.Array.Length);
                pinnedMemory.Dispose();
            }
        }

        /// <summary>
        /// Monitors GC pressure and raises events
        /// </summary>
        private void MonitorGcPressure(object state)
        {
            if (_disposed)
                return;

            var gen0Collections = GC.CollectionCount(0);
            var gen1Collections = GC.CollectionCount(1);
            var gen2Collections = GC.CollectionCount(2);
            var totalMemory = GC.GetTotalMemory(false);

            // Update statistics
            _statistics._gen0Collections = gen0Collections;
            _statistics._gen1Collections = gen1Collections;
            _statistics._gen2Collections = gen2Collections;
            _statistics._managedMemorySize = totalMemory;

            // Check memory pressure
            var pressure = MemoryPressureLevel.Normal;
            if (totalMemory > CriticalMemoryPressureThreshold)
            {
                pressure = MemoryPressureLevel.Critical;
                // Force a gen 2 collection in critical situations
                GC.Collect(2, GCCollectionMode.Forced, true, true);
            }
            else if (totalMemory > HighMemoryPressureThreshold)
            {
                pressure = MemoryPressureLevel.High;
                // Suggest a collection but don't force it
                GC.Collect(1, GCCollectionMode.Optimized, false);
            }

            if (pressure != _statistics._currentPressure)
            {
                _statistics._currentPressure = pressure;
                MemoryPressureChanged?.Invoke(this, new MemoryPressureEventArgs(pressure, totalMemory));
            }
        }

        /// <summary>
        /// Forces garbage collection and compacting
        /// </summary>
        public void ForceGarbageCollection()
        {
            ThrowIfDisposed();

            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, true, true);
            GC.WaitForPendingFinalizers();
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, true, true);
        }

        /// <summary>
        /// Trims all buffer pools to reduce memory usage
        /// </summary>
        public void TrimPools()
        {
            ThrowIfDisposed();

            foreach (var pool in _bufferPools.Values)
            {
                pool.Trim();
            }
        }

        /// <summary>
        /// Clears all buffer pools
        /// </summary>
        public void ClearPools()
        {
            ThrowIfDisposed();

            foreach (var pool in _bufferPools.Values)
            {
                pool.Clear();
            }
            _bufferPools.Clear();
        }

        /// <summary>
        /// Throws if the manager has been disposed
        /// </summary>
        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(MemoryManager));
        }

        /// <summary>
        /// Disposes the memory manager and releases all resources
        /// </summary>
        public void Dispose()
        {
            if (_disposed)
                return;

            _disposed = true;

            // Stop the GC pressure timer
            _gcPressureTimer?.Dispose();

            // Clear all buffer pools
            ClearPools();

            // Release all pinned memory
            lock (_pinnedMemoryLock)
            {
                foreach (var handle in _pinnedMemory)
                {
                    if (handle.IsAllocated)
                        handle.Free();
                }
                _pinnedMemory.Clear();
            }

            // Force a final garbage collection
            ForceGarbageCollection();
        }

        /// <summary>
        /// Buffer pool for a specific buffer size
        /// </summary>
        public sealed class BufferPool
        {
            private readonly MemoryManager _manager;
            private readonly int _bufferSize;
            private readonly int _maxBuffers;
            private readonly ConcurrentBag<byte[]> _pool;
            private int _currentCount;

            /// <summary>
            /// Gets the buffer size for this pool
            /// </summary>
            public int BufferSize => _bufferSize;

            /// <summary>
            /// Gets the current number of buffers in the pool
            /// </summary>
            public int Count => _currentCount;

            /// <summary>
            /// Gets the maximum number of buffers allowed
            /// </summary>
            public int MaxBuffers => _maxBuffers;

            internal BufferPool(MemoryManager manager, int bufferSize, int maxBuffers)
            {
                _manager = manager;
                _bufferSize = bufferSize;
                _maxBuffers = maxBuffers;
                _pool = new ConcurrentBag<byte[]>();
                _currentCount = 0;
            }

            /// <summary>
            /// Rents a buffer from the pool
            /// </summary>
            public byte[] Rent()
            {
                if (_pool.TryTake(out var buffer))
                {
                    Interlocked.Decrement(ref _currentCount);
                    return buffer;
                }

                return _manager.RentArray(_bufferSize);
            }

            /// <summary>
            /// Returns a buffer to the pool
            /// </summary>
            public void Return(byte[] buffer, bool clearBuffer = false)
            {
                if (buffer == null || buffer.Length < _bufferSize)
                    return;

                if (_currentCount < _maxBuffers)
                {
                    if (clearBuffer)
                        Array.Clear(buffer, 0, buffer.Length);

                    _pool.Add(buffer);
                    Interlocked.Increment(ref _currentCount);
                }
                else
                {
                    _manager.ReturnArray(buffer, clearBuffer);
                }
            }

            /// <summary>
            /// Trims the pool to reduce memory usage
            /// </summary>
            public void Trim()
            {
                var toRemove = _currentCount / 2;
                for (int i = 0; i < toRemove; i++)
                {
                    if (_pool.TryTake(out var buffer))
                    {
                        _manager.ReturnArray(buffer, false);
                        Interlocked.Decrement(ref _currentCount);
                    }
                }
            }

            /// <summary>
            /// Clears all buffers from the pool
            /// </summary>
            public void Clear()
            {
                while (_pool.TryTake(out var buffer))
                {
                    _manager.ReturnArray(buffer, false);
                    Interlocked.Decrement(ref _currentCount);
                }
            }
        }

        /// <summary>
        /// Represents pinned memory that won't be moved by the GC
        /// </summary>
        public sealed class PinnedMemory : IDisposable
        {
            /// <summary>
            /// Gets the GC handle for the pinned memory
            /// </summary>
            public GCHandle Handle { get; }

            /// <summary>
            /// Gets the pinned byte array
            /// </summary>
            public byte[] Array { get; }

            /// <summary>
            /// Gets the pointer to the pinned memory
            /// </summary>
            public IntPtr Pointer => Handle.AddrOfPinnedObject();

            /// <summary>
            /// Gets the size of the pinned memory
            /// </summary>
            public int Size => Array.Length;

            internal PinnedMemory(GCHandle handle, byte[] array)
            {
                Handle = handle;
                Array = array;
            }

            /// <summary>
            /// Gets a span over the pinned memory
            /// </summary>
            public Span<byte> AsSpan() => Array.AsSpan();

            /// <summary>
            /// Gets a memory over the pinned memory
            /// </summary>
            public Memory<byte> AsMemory() => Array.AsMemory();

            /// <summary>
            /// Disposes the pinned memory
            /// </summary>
            public void Dispose()
            {
                if (Handle.IsAllocated)
                    Handle.Free();
            }
        }

        /// <summary>
        /// Memory statistics
        /// </summary>
        public class MemoryStatistics
        {
            internal long _allocations;
            internal long _deallocations;
            internal long _currentlyAllocated;
            internal long _totalAllocated;
            internal long _peakAllocated;
            internal long _pinnedAllocations;
            internal long _pinnedMemorySize;
            internal long _managedMemorySize;
            internal int _gen0Collections;
            internal int _gen1Collections;
            internal int _gen2Collections;
            internal MemoryPressureLevel _currentPressure;

            /// <summary>
            /// Gets the total number of allocations
            /// </summary>
            public long Allocations => _allocations;

            /// <summary>
            /// Gets the total number of deallocations
            /// </summary>
            public long Deallocations => _deallocations;

            /// <summary>
            /// Gets the currently allocated memory in bytes
            /// </summary>
            public long CurrentlyAllocated => _currentlyAllocated;

            /// <summary>
            /// Gets the total allocated memory in bytes
            /// </summary>
            public long TotalAllocated => _totalAllocated;

            /// <summary>
            /// Gets the peak allocated memory in bytes
            /// </summary>
            public long PeakAllocated => _peakAllocated;

            /// <summary>
            /// Gets the number of pinned allocations
            /// </summary>
            public long PinnedAllocations => _pinnedAllocations;

            /// <summary>
            /// Gets the size of pinned memory in bytes
            /// </summary>
            public long PinnedMemorySize => _pinnedMemorySize;

            /// <summary>
            /// Gets the managed memory size in bytes
            /// </summary>
            public long ManagedMemorySize => _managedMemorySize;

            /// <summary>
            /// Gets the number of Gen 0 collections
            /// </summary>
            public int Gen0Collections => _gen0Collections;

            /// <summary>
            /// Gets the number of Gen 1 collections
            /// </summary>
            public int Gen1Collections => _gen1Collections;

            /// <summary>
            /// Gets the number of Gen 2 collections
            /// </summary>
            public int Gen2Collections => _gen2Collections;

            /// <summary>
            /// Gets the current memory pressure level
            /// </summary>
            public MemoryPressureLevel CurrentPressure => _currentPressure;

            /// <summary>
            /// Clones the statistics
            /// </summary>
            public MemoryStatistics Clone()
            {
                return new MemoryStatistics
                {
                    _allocations = _allocations,
                    _deallocations = _deallocations,
                    _currentlyAllocated = _currentlyAllocated,
                    _totalAllocated = _totalAllocated,
                    _peakAllocated = _peakAllocated,
                    _pinnedAllocations = _pinnedAllocations,
                    _pinnedMemorySize = _pinnedMemorySize,
                    _managedMemorySize = _managedMemorySize,
                    _gen0Collections = _gen0Collections,
                    _gen1Collections = _gen1Collections,
                    _gen2Collections = _gen2Collections,
                    _currentPressure = _currentPressure
                };
            }

            /// <summary>
            /// Gets a string representation of the statistics
            /// </summary>
            public override string ToString()
            {
                return $"MemoryStatistics: Allocated={CurrentlyAllocated:N0}, Peak={PeakAllocated:N0}, " +
                       $"Pinned={PinnedMemorySize:N0}, GC=[{Gen0Collections}/{Gen1Collections}/{Gen2Collections}], " +
                       $"Pressure={CurrentPressure}";
            }
        }

        /// <summary>
        /// Memory pressure levels
        /// </summary>
        public enum MemoryPressureLevel
        {
            /// <summary>Normal memory pressure</summary>
            Normal = 0,

            /// <summary>High memory pressure</summary>
            High = 1,

            /// <summary>Critical memory pressure</summary>
            Critical = 2
        }

        /// <summary>
        /// Event arguments for memory pressure changes
        /// </summary>
        public class MemoryPressureEventArgs : EventArgs
        {
            /// <summary>
            /// Gets the memory pressure level
            /// </summary>
            public MemoryPressureLevel PressureLevel { get; }

            /// <summary>
            /// Gets the current memory usage in bytes
            /// </summary>
            public long MemoryUsage { get; }

            /// <summary>
            /// Initializes a new instance of MemoryPressureEventArgs
            /// </summary>
            public MemoryPressureEventArgs(MemoryPressureLevel pressureLevel, long memoryUsage)
            {
                PressureLevel = pressureLevel;
                MemoryUsage = memoryUsage;
            }
        }
    }
}
