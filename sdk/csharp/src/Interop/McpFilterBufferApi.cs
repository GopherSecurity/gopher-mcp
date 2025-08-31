using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using McpHandles = GopherMcp.Interop.McpHandles;
using static GopherMcp.Interop.McpFilterApi;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for MCP Filter Buffer API (mcp_filter_buffer.h)
    /// Provides zero-copy buffer management for filter operations
    /// </summary>
    public static class McpFilterBufferApi
    {
        // Library name - adjust based on platform naming conventions
#if WINDOWS
        private const string LibraryName = "mcp_c.dll";
#elif MACOS
        private const string LibraryName = "libmcp_c.dylib";
#else
        private const string LibraryName = "libmcp_c.so";
#endif

        /* ============================================================================
         * Buffer Types and Enumerations
         * ============================================================================ */

        /// <summary>
        /// Buffer ownership model
        /// </summary>
        public enum mcp_buffer_ownership_t : int
        {
            MCP_BUFFER_OWNERSHIP_NONE = 0,
            MCP_BUFFER_OWNERSHIP_SHARED = 1,
            MCP_BUFFER_OWNERSHIP_EXCLUSIVE = 2,
            MCP_BUFFER_OWNERSHIP_EXTERNAL = 3
        }

        /// <summary>
        /// Buffer fragment release callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_buffer_fragment_release_cb(
            IntPtr data,
            UIntPtr size,
            IntPtr user_data);

        /// <summary>
        /// Buffer fragment for external memory
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_buffer_fragment_t
        {
            public IntPtr data;  // const void*
            public UIntPtr size;
            public mcp_buffer_fragment_release_cb release_callback;
            public IntPtr user_data;
        }

        /// <summary>
        /// Buffer reservation for writing
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_buffer_reservation_t
        {
            public IntPtr data;  // void*
            public UIntPtr capacity;
            public McpHandles.McpBufferHandle buffer;
            public ulong reservation_id;
        }

        /// <summary>
        /// Buffer statistics
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_buffer_stats_t
        {
            public UIntPtr total_bytes;
            public UIntPtr used_bytes;
            public UIntPtr slice_count;
            public UIntPtr fragment_count;
            public ulong read_operations;
            public ulong write_operations;
        }

        /// <summary>
        /// Drain tracker callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_drain_tracker_cb(
            UIntPtr bytes_drained,
            IntPtr user_data);

        /// <summary>
        /// Drain tracker for monitoring buffer consumption
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_drain_tracker_t
        {
            public mcp_drain_tracker_cb callback;
            public IntPtr user_data;
        }

        /* ============================================================================
         * Buffer Creation and Management
         * ============================================================================ */

        /// <summary>
        /// Create a new buffer with ownership
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpHandles.McpBufferHandle mcp_buffer_create_owned(
            UIntPtr initial_capacity,
            mcp_buffer_ownership_t ownership);

        /// <summary>
        /// Create a buffer view (zero-copy reference)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpHandles.McpBufferHandle mcp_buffer_create_view(
            IntPtr data,
            UIntPtr length);

        /// <summary>
        /// Create buffer from external fragment
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpHandles.McpBufferHandle mcp_buffer_create_from_fragment(
            ref mcp_buffer_fragment_t fragment);

        /// <summary>
        /// Clone a buffer (deep copy)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpHandles.McpBufferHandle mcp_buffer_clone(McpHandles.McpBufferHandle buffer);

        /// <summary>
        /// Create copy-on-write buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpHandles.McpBufferHandle mcp_buffer_create_cow(McpHandles.McpBufferHandle buffer);

        /* ============================================================================
         * Buffer Data Operations
         * ============================================================================ */

        /// <summary>
        /// Add data to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_add(
            McpHandles.McpBufferHandle buffer,
            IntPtr data,
            UIntPtr length);

        /// <summary>
        /// Add string to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_add_string(
            McpHandles.McpBufferHandle buffer,
            [MarshalAs(UnmanagedType.LPStr)] string str);

        /// <summary>
        /// Add another buffer to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_add_buffer(
            McpHandles.McpBufferHandle buffer,
            McpHandles.McpBufferHandle source);

        /// <summary>
        /// Add buffer fragment (zero-copy)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_add_fragment(
            McpHandles.McpBufferHandle buffer,
            ref mcp_buffer_fragment_t fragment);

        /// <summary>
        /// Prepend data to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_prepend(
            McpHandles.McpBufferHandle buffer,
            IntPtr data,
            UIntPtr length);

        /* ============================================================================
         * Buffer Consumption
         * ============================================================================ */

        /// <summary>
        /// Drain bytes from front of buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_drain(
            McpHandles.McpBufferHandle buffer,
            UIntPtr size);

        /// <summary>
        /// Move data from one buffer to another
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_move(
            McpHandles.McpBufferHandle source,
            McpHandles.McpBufferHandle destination,
            UIntPtr length);

        /// <summary>
        /// Set drain tracker for buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_set_drain_tracker(
            McpHandles.McpBufferHandle buffer,
            ref mcp_drain_tracker_t tracker);

        /* ============================================================================
         * Buffer Reservation (Zero-Copy Writing)
         * ============================================================================ */

        /// <summary>
        /// Reserve space for writing
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_reserve(
            McpHandles.McpBufferHandle buffer,
            UIntPtr min_size,
            out mcp_buffer_reservation_t reservation);

        /// <summary>
        /// Reserve for vectored I/O
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_reserve_iovec(
            McpHandles.McpBufferHandle buffer,
            IntPtr iovecs,
            UIntPtr iovec_count,
            out UIntPtr reserved);

        /// <summary>
        /// Commit reserved space
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_commit_reservation(
            ref mcp_buffer_reservation_t reservation,
            UIntPtr bytes_written);

        /// <summary>
        /// Cancel reservation
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_cancel_reservation(
            ref mcp_buffer_reservation_t reservation);

        /* ============================================================================
         * Buffer Access (Zero-Copy Reading)
         * ============================================================================ */

        /// <summary>
        /// Get contiguous memory view
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_get_contiguous(
            McpHandles.McpBufferHandle buffer,
            UIntPtr offset,
            UIntPtr length,
            out IntPtr data,
            out UIntPtr actual_length);

        /// <summary>
        /// Linearize buffer (ensure contiguous memory)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_linearize(
            McpHandles.McpBufferHandle buffer,
            UIntPtr size,
            out IntPtr data);

        /// <summary>
        /// Peek at buffer data without consuming
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_peek(
            McpHandles.McpBufferHandle buffer,
            UIntPtr offset,
            IntPtr data,
            UIntPtr length);

        /* ============================================================================
         * Type-Safe I/O Operations
         * ============================================================================ */

        /// <summary>
        /// Write integer with little-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_write_le_int(
            McpHandles.McpBufferHandle buffer,
            ulong value,
            UIntPtr size);

        /// <summary>
        /// Write integer with big-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_write_be_int(
            McpHandles.McpBufferHandle buffer,
            ulong value,
            UIntPtr size);

        /// <summary>
        /// Read integer with little-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_read_le_int(
            McpHandles.McpBufferHandle buffer,
            UIntPtr size,
            out ulong value);

        /// <summary>
        /// Read integer with big-endian byte order
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_read_be_int(
            McpHandles.McpBufferHandle buffer,
            UIntPtr size,
            out ulong value);

        /* ============================================================================
         * Buffer Search Operations
         * ============================================================================ */

        /// <summary>
        /// Search for pattern in buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_search(
            McpHandles.McpBufferHandle buffer,
            IntPtr pattern,
            UIntPtr pattern_size,
            UIntPtr start_position,
            out UIntPtr position);

        /// <summary>
        /// Find delimiter in buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_find_byte(
            McpHandles.McpBufferHandle buffer,
            byte delimiter,
            out UIntPtr position);

        /* ============================================================================
         * Buffer Information
         * ============================================================================ */

        /// <summary>
        /// Get buffer length
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_buffer_length(McpHandles.McpBufferHandle buffer);

        /// <summary>
        /// Get buffer capacity
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_buffer_capacity(McpHandles.McpBufferHandle buffer);

        /// <summary>
        /// Check if buffer is empty
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_buffer_is_empty(McpHandles.McpBufferHandle buffer);

        /// <summary>
        /// Get buffer statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_get_stats(
            McpHandles.McpBufferHandle buffer,
            out mcp_buffer_stats_t stats);

        /* ============================================================================
         * Buffer Watermarks
         * ============================================================================ */

        /// <summary>
        /// Set buffer watermarks for flow control
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_set_watermarks(
            McpHandles.McpBufferHandle buffer,
            UIntPtr low_watermark,
            UIntPtr high_watermark,
            UIntPtr overflow_watermark);

        /// <summary>
        /// Check if buffer is above high watermark
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_buffer_above_high_watermark(McpHandles.McpBufferHandle buffer);

        /// <summary>
        /// Check if buffer is below low watermark
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_buffer_below_low_watermark(McpHandles.McpBufferHandle buffer);

        /* ============================================================================
         * Advanced Buffer Pool
         * ============================================================================ */

        /// <summary>
        /// Buffer pool configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_buffer_pool_config_t
        {
            public UIntPtr buffer_size;
            public UIntPtr max_buffers;
            public UIntPtr prealloc_count;
            public mcp_bool_t use_thread_local;
            public mcp_bool_t zero_on_alloc;
        }

        /// <summary>
        /// Create buffer pool with configuration
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpBufferPoolHandle mcp_buffer_pool_create_ex(
            ref mcp_buffer_pool_config_t config);

        /// <summary>
        /// Get pool statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_pool_get_stats(
            McpBufferPoolHandle pool,
            out UIntPtr free_count,
            out UIntPtr used_count,
            out UIntPtr total_allocated);

        /// <summary>
        /// Trim pool to reduce memory usage
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_pool_trim(
            McpBufferPoolHandle pool,
            UIntPtr target_free);

        /* ============================================================================
         * Helper Methods
         * ============================================================================ */

        /// <summary>
        /// Helper to add byte array to buffer
        /// </summary>
        public static mcp_result_t AddData(McpHandles.McpBufferHandle buffer, byte[] data)
        {
            if (data == null || data.Length == 0)
                return mcp_result_t.MCP_ERROR_INVALID_ARGUMENT;

            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return mcp_buffer_add(
                    buffer,
                    handle.AddrOfPinnedObject(),
                    new UIntPtr((uint)data.Length));
            }
            finally
            {
                handle.Free();
            }
        }

        /// <summary>
        /// Helper to prepend byte array to buffer
        /// </summary>
        public static mcp_result_t PrependData(McpHandles.McpBufferHandle buffer, byte[] data)
        {
            if (data == null || data.Length == 0)
                return mcp_result_t.MCP_ERROR_INVALID_ARGUMENT;

            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return mcp_buffer_prepend(
                    buffer,
                    handle.AddrOfPinnedObject(),
                    new UIntPtr((uint)data.Length));
            }
            finally
            {
                handle.Free();
            }
        }

        /// <summary>
        /// Helper to peek data as byte array
        /// </summary>
        public static byte[] PeekData(McpHandles.McpBufferHandle buffer, uint offset, uint length)
        {
            var data = new byte[length];
            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                var result = mcp_buffer_peek(
                    buffer,
                    new UIntPtr(offset),
                    handle.AddrOfPinnedObject(),
                    new UIntPtr(length));

                if (result != mcp_result_t.MCP_OK)
                    return null;

                return data;
            }
            finally
            {
                handle.Free();
            }
        }

        /// <summary>
        /// Helper to search for byte pattern
        /// </summary>
        public static bool SearchPattern(McpHandles.McpBufferHandle buffer, byte[] pattern, uint startPos, out uint position)
        {
            position = 0;
            if (pattern == null || pattern.Length == 0)
                return false;

            var handle = GCHandle.Alloc(pattern, GCHandleType.Pinned);
            try
            {
                UIntPtr pos;
                var result = mcp_buffer_search(
                    buffer,
                    handle.AddrOfPinnedObject(),
                    new UIntPtr((uint)pattern.Length),
                    new UIntPtr(startPos),
                    out pos);

                if (result == mcp_result_t.MCP_OK)
                {
                    position = (uint)pos;
                    return true;
                }

                return false;
            }
            finally
            {
                handle.Free();
            }
        }

        /// <summary>
        /// Helper to create buffer from byte array
        /// </summary>
        public static McpHandles.McpBufferHandle CreateFromData(byte[] data, mcp_buffer_ownership_t ownership)
        {
            if (data == null || data.Length == 0)
                return new McpHandles.McpBufferHandle();

            var buffer = mcp_buffer_create_owned(new UIntPtr((uint)data.Length), ownership);
            if (!buffer.IsInvalid)
            {
                AddData(buffer, data);
            }
            return buffer;
        }
    }
}