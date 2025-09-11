using System;
using System.Runtime.InteropServices;
using GopherMcp.Core;

namespace GopherMcp.Types
{
    /// <summary>
    /// Buffer ownership model
    /// </summary>
    public enum BufferOwnership
    {
        /// <summary>
        /// Buffer is owned by the current context
        /// </summary>
        Owned = 0,
        
        /// <summary>
        /// Buffer is borrowed from another context
        /// </summary>
        Borrowed = 1,
        
        /// <summary>
        /// Buffer is shared between multiple contexts
        /// </summary>
        Shared = 2,
        
        /// <summary>
        /// Buffer has no ownership (view only)
        /// </summary>
        None = 3,
        
        /// <summary>
        /// Buffer is externally owned with callback
        /// </summary>
        External = 4,
        
        /// <summary>
        /// Buffer uses copy-on-write semantics
        /// </summary>
        CopyOnWrite = 5
    }
    
    /// <summary>
    /// Buffer flags for special handling
    /// </summary>
    [Flags]
    public enum BufferFlags : uint
    {
        /// <summary>
        /// No special flags
        /// </summary>
        None = 0x00,
        
        /// <summary>
        /// Buffer is read-only
        /// </summary>
        ReadOnly = 0x01,
        
        /// <summary>
        /// Buffer memory is owned
        /// </summary>
        Owned = 0x02,
        
        /// <summary>
        /// Buffer is from external source
        /// </summary>
        External = 0x04,
        
        /// <summary>
        /// Enable zero-copy operations
        /// </summary>
        ZeroCopy = 0x08,
        
        /// <summary>
        /// Buffer is pinned in memory
        /// </summary>
        Pinned = 0x10,
        
        /// <summary>
        /// Buffer can be fragmented
        /// </summary>
        Fragmented = 0x20,
        
        /// <summary>
        /// Buffer is contiguous
        /// </summary>
        Contiguous = 0x40,
        
        /// <summary>
        /// Buffer contains sensitive data
        /// </summary>
        Sensitive = 0x80,
        
        /// <summary>
        /// Buffer should be zeroed on release
        /// </summary>
        SecureWipe = 0x100
    }
    
    /// <summary>
    /// Buffer allocation strategy
    /// </summary>
    public enum BufferAllocationStrategy
    {
        /// <summary>
        /// Default allocation strategy
        /// </summary>
        Default = 0,
        
        /// <summary>
        /// Allocate from pool
        /// </summary>
        Pooled = 1,
        
        /// <summary>
        /// Allocate on heap
        /// </summary>
        Heap = 2,
        
        /// <summary>
        /// Allocate on stack (for small buffers)
        /// </summary>
        Stack = 3,
        
        /// <summary>
        /// Use memory-mapped allocation
        /// </summary>
        MemoryMapped = 4,
        
        /// <summary>
        /// Use shared memory
        /// </summary>
        SharedMemory = 5
    }
    
    /// <summary>
    /// Buffer slice for zero-copy access
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferSlice
    {
        /// <summary>
        /// Pointer to the data
        /// </summary>
        public IntPtr Data;
        
        /// <summary>
        /// Length of the data in bytes
        /// </summary>
        public UIntPtr Length;
        
        /// <summary>
        /// Buffer flags
        /// </summary>
        public BufferFlags Flags;
        
        /// <summary>
        /// Offset from the start of the original buffer
        /// </summary>
        public UIntPtr Offset;
        
        /// <summary>
        /// Reference to the parent buffer handle
        /// </summary>
        public ulong ParentBuffer;
        
        /// <summary>
        /// Create a new buffer slice
        /// </summary>
        public BufferSlice(IntPtr data, UIntPtr length, BufferFlags flags = BufferFlags.None)
        {
            Data = data;
            Length = length;
            Flags = flags;
            Offset = UIntPtr.Zero;
            ParentBuffer = 0;
        }
        
        /// <summary>
        /// Check if the slice is valid
        /// </summary>
        public bool IsValid => Data != IntPtr.Zero && Length != UIntPtr.Zero;
        
        /// <summary>
        /// Get the size in bytes
        /// </summary>
        public int Size => (int)Length.ToUInt32();
        
        /// <summary>
        /// Create a span from the slice
        /// </summary>
        public unsafe Span<byte> AsSpan()
        {
            if (!IsValid)
                return Span<byte>.Empty;
            
            return new Span<byte>(Data.ToPointer(), Size);
        }
        
        /// <summary>
        /// Create a read-only span from the slice
        /// </summary>
        public unsafe ReadOnlySpan<byte> AsReadOnlySpan()
        {
            if (!IsValid)
                return ReadOnlySpan<byte>.Empty;
            
            return new ReadOnlySpan<byte>(Data.ToPointer(), Size);
        }
    }
    
    /// <summary>
    /// Buffer pool configuration
    /// </summary>
    public class BufferPoolConfig
    {
        /// <summary>
        /// Gets or sets the size of each buffer in the pool
        /// </summary>
        public int BufferSize { get; set; } = 65536;
        
        /// <summary>
        /// Gets or sets the maximum number of buffers in the pool
        /// </summary>
        public int MaxBuffers { get; set; } = 1000;
        
        /// <summary>
        /// Gets or sets the number of buffers to preallocate
        /// </summary>
        public int PreallocateCount { get; set; } = 10;
        
        /// <summary>
        /// Gets or sets whether to use thread-local caching
        /// </summary>
        public bool UseThreadLocalCache { get; set; } = true;
        
        /// <summary>
        /// Gets or sets whether to zero memory on allocation
        /// </summary>
        public bool ZeroOnAllocate { get; set; } = false;
        
        /// <summary>
        /// Gets or sets whether to zero memory on release
        /// </summary>
        public bool ZeroOnRelease { get; set; } = false;
        
        /// <summary>
        /// Gets or sets the allocation strategy
        /// </summary>
        public BufferAllocationStrategy AllocationStrategy { get; set; } = BufferAllocationStrategy.Pooled;
        
        /// <summary>
        /// Gets or sets the minimum buffer size
        /// </summary>
        public int MinBufferSize { get; set; } = 4096;
        
        /// <summary>
        /// Gets or sets the buffer growth factor
        /// </summary>
        public double GrowthFactor { get; set; } = 2.0;
        
        /// <summary>
        /// Gets or sets the pool trim interval in milliseconds
        /// </summary>
        public int TrimIntervalMs { get; set; } = 60000;
        
        /// <summary>
        /// Gets or sets the target free buffer count after trimming
        /// </summary>
        public int TrimTargetFree { get; set; } = 50;
        
        /// <summary>
        /// Gets or sets whether to enable statistics tracking
        /// </summary>
        public bool EnableStatistics { get; set; } = true;
        
        /// <summary>
        /// Gets or sets the pool name for identification
        /// </summary>
        public string Name { get; set; }
        
        /// <summary>
        /// Initializes a new instance of BufferPoolConfig
        /// </summary>
        public BufferPoolConfig()
        {
        }
        
        /// <summary>
        /// Initializes a new instance of BufferPoolConfig with buffer size
        /// </summary>
        public BufferPoolConfig(int bufferSize, int maxBuffers)
        {
            BufferSize = bufferSize;
            MaxBuffers = maxBuffers;
        }
        
        /// <summary>
        /// Create a default configuration
        /// </summary>
        public static BufferPoolConfig Default => new BufferPoolConfig();
        
        /// <summary>
        /// Create a small buffer pool configuration
        /// </summary>
        public static BufferPoolConfig SmallBuffers => new BufferPoolConfig(4096, 500);
        
        /// <summary>
        /// Create a large buffer pool configuration
        /// </summary>
        public static BufferPoolConfig LargeBuffers => new BufferPoolConfig(1048576, 50);
    }
    
    /// <summary>
    /// Scatter-gather entry for vectored I/O
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ScatterGatherEntry
    {
        /// <summary>
        /// Pointer to the data
        /// </summary>
        public IntPtr Data;
        
        /// <summary>
        /// Length of the data in bytes
        /// </summary>
        public UIntPtr Length;
        
        /// <summary>
        /// Entry flags
        /// </summary>
        public uint Flags;
        
        /// <summary>
        /// User data associated with this entry
        /// </summary>
        public IntPtr UserData;
        
        /// <summary>
        /// Buffer (alias for Data)
        /// </summary>
        public IntPtr Buffer => Data;
        
        /// <summary>
        /// Offset (always 0 for scatter-gather entries)
        /// </summary>
        public int Offset => 0;
        
        /// <summary>
        /// Create a new scatter-gather entry
        /// </summary>
        public ScatterGatherEntry(IntPtr data, UIntPtr length)
        {
            Data = data;
            Length = length;
            Flags = 0;
            UserData = IntPtr.Zero;
        }
        
        /// <summary>
        /// Create a new scatter-gather entry with flags
        /// </summary>
        public ScatterGatherEntry(IntPtr data, UIntPtr length, uint flags, IntPtr userData)
        {
            Data = data;
            Length = length;
            Flags = flags;
            UserData = userData;
        }
        
        /// <summary>
        /// Get the size in bytes
        /// </summary>
        public int Size => (int)Length.ToUInt32();
    }
    
    /// <summary>
    /// Buffer statistics
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferStatistics
    {
        /// <summary>
        /// Total bytes allocated
        /// </summary>
        public ulong TotalBytesAllocated;
        
        /// <summary>
        /// Current bytes in use
        /// </summary>
        public ulong CurrentBytesInUse;
        
        /// <summary>
        /// Peak bytes used
        /// </summary>
        public ulong PeakBytesUsed;
        
        /// <summary>
        /// Number of allocations
        /// </summary>
        public ulong AllocationCount;
        
        /// <summary>
        /// Number of deallocations
        /// </summary>
        public ulong DeallocationCount;
        
        /// <summary>
        /// Number of active buffers
        /// </summary>
        public uint ActiveBuffers;
        
        /// <summary>
        /// Number of free buffers in pool
        /// </summary>
        public uint FreeBuffers;
        
        /// <summary>
        /// Number of buffer reuse hits
        /// </summary>
        public ulong ReuseHits;
        
        /// <summary>
        /// Number of buffer reuse misses
        /// </summary>
        public ulong ReuseMisses;
        
        /// <summary>
        /// Number of buffer overflows
        /// </summary>
        public ulong OverflowCount;
        
        /// <summary>
        /// Number of buffer underflows
        /// </summary>
        public ulong UnderflowCount;
        
        /// <summary>
        /// Number of fragmented buffers
        /// </summary>
        public uint FragmentedBuffers;
        
        /// <summary>
        /// Total fragment count
        /// </summary>
        public uint TotalFragments;
        
        /// <summary>
        /// Average fragment size
        /// </summary>
        public double AverageFragmentSize;
        
        /// <summary>
        /// Number of zero-copy operations
        /// </summary>
        public ulong ZeroCopyOperations;
        
        /// <summary>
        /// Number of copy operations
        /// </summary>
        public ulong CopyOperations;
        
        /// <summary>
        /// Total bytes copied
        /// </summary>
        public ulong TotalBytesCopied;
        
        /// <summary>
        /// Pool efficiency percentage
        /// </summary>
        public double PoolEfficiency => 
            (AllocationCount > 0) ? (double)ReuseHits / AllocationCount * 100 : 0;
        
        /// <summary>
        /// Get a string representation of the statistics
        /// </summary>
        public override string ToString()
        {
            return $"BufferStatistics: InUse={CurrentBytesInUse}, Peak={PeakBytesUsed}, " +
                   $"Active={ActiveBuffers}, Free={FreeBuffers}, " +
                   $"Efficiency={PoolEfficiency:F2}%";
        }
    }
    
    /// <summary>
    /// Memory layout attributes for buffer management
    /// </summary>
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
    public class MemoryLayoutAttribute : Attribute
    {
        /// <summary>
        /// Gets the offset in bytes
        /// </summary>
        public int Offset { get; }
        
        /// <summary>
        /// Gets the size in bytes
        /// </summary>
        public int Size { get; }
        
        /// <summary>
        /// Gets the alignment requirement
        /// </summary>
        public int Alignment { get; set; } = 1;
        
        /// <summary>
        /// Gets or sets whether the field is packed
        /// </summary>
        public bool Packed { get; set; } = false;
        
        /// <summary>
        /// Initializes a new instance of MemoryLayoutAttribute
        /// </summary>
        public MemoryLayoutAttribute(int offset, int size)
        {
            Offset = offset;
            Size = size;
        }
    }
    
    /// <summary>
    /// Buffer alignment attribute
    /// </summary>
    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
    public class BufferAlignmentAttribute : Attribute
    {
        /// <summary>
        /// Gets the alignment boundary
        /// </summary>
        public int Boundary { get; }
        
        /// <summary>
        /// Initializes a new instance of BufferAlignmentAttribute
        /// </summary>
        public BufferAlignmentAttribute(int boundary)
        {
            Boundary = boundary;
        }
    }
    
    /// <summary>
    /// Buffer reservation for zero-copy writing
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferReservation
    {
        /// <summary>
        /// Pointer to reserved memory
        /// </summary>
        public IntPtr Data;
        
        /// <summary>
        /// Capacity of reserved memory
        /// </summary>
        public UIntPtr Capacity;
        
        /// <summary>
        /// Buffer handle
        /// </summary>
        public ulong Buffer;
        
        /// <summary>
        /// Reservation ID
        /// </summary>
        public ulong ReservationId;
        
        /// <summary>
        /// Check if the reservation is valid
        /// </summary>
        public bool IsValid => Data != IntPtr.Zero && Capacity != UIntPtr.Zero;
        
        /// <summary>
        /// Get the capacity in bytes
        /// </summary>
        public int Size => (int)Capacity.ToUInt32();
    }
    
    /// <summary>
    /// Buffer fragment for external memory
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferFragment
    {
        /// <summary>
        /// Pointer to fragment data
        /// </summary>
        public IntPtr Data;
        
        /// <summary>
        /// Size of fragment
        /// </summary>
        public UIntPtr Size;
        
        /// <summary>
        /// Release callback delegate
        /// </summary>
        [MarshalAs(UnmanagedType.FunctionPtr)]
        public BufferReleaseCallback ReleaseCallback;
        
        /// <summary>
        /// User data for callback
        /// </summary>
        public IntPtr UserData;
    }
    
    /// <summary>
    /// Buffer release callback delegate
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void BufferReleaseCallback(IntPtr data, UIntPtr size, IntPtr userData);
    
    /// <summary>
    /// Drain tracker for monitoring buffer consumption
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct DrainTracker
    {
        /// <summary>
        /// Drain callback delegate
        /// </summary>
        [MarshalAs(UnmanagedType.FunctionPtr)]
        public DrainTrackerCallback Callback;
        
        /// <summary>
        /// User data for callback
        /// </summary>
        public IntPtr UserData;
    }
    
    /// <summary>
    /// Drain tracker callback delegate
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void DrainTrackerCallback(UIntPtr bytesDrained, IntPtr userData);
}