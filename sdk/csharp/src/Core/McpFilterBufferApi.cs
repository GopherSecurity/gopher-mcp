using System;
using System.Runtime.InteropServices;

namespace GopherMcp.Core
{
    /// <summary>
    /// P/Invoke bindings for MCP C Filter Buffer API functions
    /// </summary>
    public static class McpFilterBufferApi
    {
        private const string LibraryName = "gopher_mcp_c";
        
        // ============================================================================
        // Buffer Creation and Management
        // ============================================================================
        
        /// <summary>
        /// Create a new buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_buffer_create_owned(
            UIntPtr initialCapacity,
            BufferOwnership ownership);
        
        /// <summary>
        /// Create a buffer view (zero-copy reference)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_buffer_create_view(
            IntPtr data,
            UIntPtr length);
        
        /// <summary>
        /// Create buffer from external fragment
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_buffer_create_from_fragment(
            ref BufferFragment fragment);
        
        /// <summary>
        /// Clone a buffer (deep copy)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_buffer_clone(ulong buffer);
        
        /// <summary>
        /// Create copy-on-write buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_buffer_create_cow(ulong buffer);
        
        /// <summary>
        /// Create buffer handle from data (from mcp_c_filter_api.h)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_filter_buffer_create(
            IntPtr data,
            UIntPtr length,
            uint flags);
        
        /// <summary>
        /// Release buffer handle (from mcp_c_filter_api.h)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_buffer_release(ulong buffer);
        
        /// <summary>
        /// Get buffer length (from mcp_c_filter_api.h)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_filter_buffer_length(ulong buffer);
        
        // ============================================================================
        // Buffer Data Operations
        // ============================================================================
        
        /// <summary>
        /// Add data to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_add(
            ulong buffer,
            IntPtr data,
            UIntPtr length);
        
        /// <summary>
        /// Add string to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_add_string(
            ulong buffer,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string str);
        
        /// <summary>
        /// Add another buffer to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_add_buffer(
            ulong buffer,
            ulong source);
        
        /// <summary>
        /// Add buffer fragment (zero-copy)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_add_fragment(
            ulong buffer,
            ref BufferFragment fragment);
        
        /// <summary>
        /// Prepend data to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_prepend(
            ulong buffer,
            IntPtr data,
            UIntPtr length);
        
        // ============================================================================
        // Buffer Consumption
        // ============================================================================
        
        /// <summary>
        /// Drain bytes from front of buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_drain(
            ulong buffer,
            UIntPtr size);
        
        /// <summary>
        /// Move data from one buffer to another
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_move(
            ulong source,
            ulong destination,
            UIntPtr length);
        
        /// <summary>
        /// Set drain tracker for buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_set_drain_tracker(
            ulong buffer,
            ref DrainTracker tracker);
        
        // ============================================================================
        // Buffer Reservation (Zero-Copy Writing)
        // ============================================================================
        
        /// <summary>
        /// Reserve space for writing
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_reserve(
            ulong buffer,
            UIntPtr minSize,
            out BufferReservation reservation);
        
        /// <summary>
        /// Reserve for vectored I/O
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_reserve_iovec(
            ulong buffer,
            IntPtr iovecs,
            UIntPtr iovecCount,
            out UIntPtr reserved);
        
        /// <summary>
        /// Commit reserved space
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_commit_reservation(
            ref BufferReservation reservation,
            UIntPtr bytesWritten);
        
        /// <summary>
        /// Cancel reservation
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_cancel_reservation(
            ref BufferReservation reservation);
        
        /// <summary>
        /// Reserve buffer space for writing (from mcp_c_filter_api.h)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_reserve_buffer(
            ulong buffer,
            UIntPtr size,
            out BufferSlice slice);
        
        /// <summary>
        /// Commit written data to buffer (from mcp_c_filter_api.h)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_commit_buffer(
            ulong buffer,
            UIntPtr bytesWritten);
        
        // ============================================================================
        // Buffer Access (Zero-Copy Reading)
        // ============================================================================
        
        /// <summary>
        /// Get contiguous memory view
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_get_contiguous(
            ulong buffer,
            UIntPtr offset,
            UIntPtr length,
            out IntPtr data,
            out UIntPtr actualLength);
        
        /// <summary>
        /// Linearize buffer (ensure contiguous memory)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_linearize(
            ulong buffer,
            UIntPtr size,
            out IntPtr data);
        
        /// <summary>
        /// Peek at buffer data without consuming
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_peek(
            ulong buffer,
            UIntPtr offset,
            IntPtr data,
            UIntPtr length);
        
        /// <summary>
        /// Get buffer slices for zero-copy access (from mcp_c_filter_api.h)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_get_buffer_slices(
            ulong buffer,
            [In, Out] BufferSlice[] slices,
            ref UIntPtr sliceCount);
        
        // ============================================================================
        // Type-Safe I/O Operations
        // ============================================================================
        
        /// <summary>
        /// Write integer with little-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_write_le_int(
            ulong buffer,
            ulong value,
            UIntPtr size);
        
        /// <summary>
        /// Write integer with big-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_write_be_int(
            ulong buffer,
            ulong value,
            UIntPtr size);
        
        /// <summary>
        /// Read integer with little-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_read_le_int(
            ulong buffer,
            UIntPtr size,
            out ulong value);
        
        /// <summary>
        /// Read integer with big-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_read_be_int(
            ulong buffer,
            UIntPtr size,
            out ulong value);
        
        // ============================================================================
        // Buffer Search Operations
        // ============================================================================
        
        /// <summary>
        /// Search for pattern in buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_search(
            ulong buffer,
            IntPtr pattern,
            UIntPtr patternSize,
            UIntPtr startPosition,
            out UIntPtr position);
        
        /// <summary>
        /// Find delimiter in buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_find_byte(
            ulong buffer,
            byte delimiter,
            out UIntPtr position);
        
        // ============================================================================
        // Buffer Information
        // ============================================================================
        
        /// <summary>
        /// Get buffer length
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_buffer_length(ulong buffer);
        
        /// <summary>
        /// Get buffer capacity
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_buffer_capacity(ulong buffer);
        
        /// <summary>
        /// Check if buffer is empty
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern byte mcp_buffer_is_empty(ulong buffer);
        
        /// <summary>
        /// Get buffer statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_get_stats(
            ulong buffer,
            out BufferStats stats);
        
        // ============================================================================
        // Buffer Watermarks
        // ============================================================================
        
        /// <summary>
        /// Set buffer watermarks for flow control
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_set_watermarks(
            ulong buffer,
            UIntPtr lowWatermark,
            UIntPtr highWatermark,
            UIntPtr overflowWatermark);
        
        /// <summary>
        /// Check if buffer is above high watermark
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern byte mcp_buffer_above_high_watermark(ulong buffer);
        
        /// <summary>
        /// Check if buffer is below low watermark
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern byte mcp_buffer_below_low_watermark(ulong buffer);
        
        // ============================================================================
        // Buffer Pool Management
        // ============================================================================
        
        /// <summary>
        /// Create buffer pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_buffer_pool_create(
            UIntPtr bufferSize,
            UIntPtr maxBuffers);
        
        /// <summary>
        /// Acquire buffer from pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_buffer_pool_acquire(IntPtr pool);
        
        /// <summary>
        /// Release buffer back to pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_buffer_pool_release(
            IntPtr pool,
            ulong buffer);
        
        /// <summary>
        /// Destroy buffer pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_buffer_pool_destroy(IntPtr pool);
    }
    
    // ============================================================================
    // Enumerations
    // ============================================================================
    
    /// <summary>
    /// Buffer ownership model
    /// </summary>
    public enum BufferOwnership
    {
        None = 0,
        Shared = 1,
        Exclusive = 2,
        External = 3
    }
    
    /// <summary>
    /// Buffer flags
    /// </summary>
    [Flags]
    public enum BufferFlags : uint
    {
        ReadOnly = 0x01,
        Owned = 0x02,
        External = 0x04,
        ZeroCopy = 0x08
    }
    
    // ============================================================================
    // Structures
    // ============================================================================
    
    /// <summary>
    /// Buffer fragment for external memory
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferFragment
    {
        public IntPtr Data;
        public UIntPtr Size;
        public BufferReleaseCallback ReleaseCallback;
        public IntPtr UserData;
    }
    
    /// <summary>
    /// Buffer reservation for writing
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferReservation
    {
        public IntPtr Data;
        public UIntPtr Capacity;
        public ulong Buffer;
        public ulong ReservationId;
    }
    
    /// <summary>
    /// Buffer statistics
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferStats
    {
        public UIntPtr TotalBytes;
        public UIntPtr UsedBytes;
        public UIntPtr SliceCount;
        public UIntPtr FragmentCount;
        public ulong ReadOperations;
        public ulong WriteOperations;
    }
    
    /// <summary>
    /// Buffer slice for zero-copy access
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct BufferSlice
    {
        public IntPtr Data;
        public UIntPtr Length;
        public uint Flags;
    }
    
    /// <summary>
    /// Drain tracker for monitoring buffer consumption
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct DrainTracker
    {
        public DrainTrackerCallback Callback;
        public IntPtr UserData;
    }
    
    // ============================================================================
    // Callback Delegates
    // ============================================================================
    
    /// <summary>
    /// Buffer release callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void BufferReleaseCallback(
        IntPtr data,
        UIntPtr size,
        IntPtr userData);
    
    /// <summary>
    /// Drain tracker callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void DrainTrackerCallback(
        UIntPtr bytesDrained,
        IntPtr userData);
    
    // ============================================================================
    // Scatter-Gather I/O Support
    // ============================================================================
    
    /// <summary>
    /// Scatter-gather entry for zero-copy I/O
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ScatterGatherEntry
    {
        public IntPtr Data;
        public UIntPtr Length;
        public uint Flags;
        public IntPtr UserData;
    }
    
    /// <summary>
    /// Scatter-gather I/O functions
    /// </summary>
    public static class ScatterGatherIO
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Perform scatter read operation
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_scatter_read(
            ulong buffer,
            [In, Out] ScatterGatherEntry[] entries,
            UIntPtr entryCount,
            out UIntPtr totalRead);
        
        /// <summary>
        /// Perform gather write operation
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_buffer_gather_write(
            ulong buffer,
            [In] ScatterGatherEntry[] entries,
            UIntPtr entryCount,
            out UIntPtr totalWritten);
    }
}