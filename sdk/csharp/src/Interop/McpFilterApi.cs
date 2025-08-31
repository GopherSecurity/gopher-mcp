using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for MCP C++ Filter Architecture API (mcp_filter_api.h)
    /// Provides FFI-safe bindings for filter-based network processing
    /// </summary>
    public static class McpFilterApi
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
         * Core Types and Enumerations
         * ============================================================================ */

        // Opaque handle types
        // Note: Using ulong (uint64_t) for these handle types as defined in the header
        public struct McpFilterChainHandle { public ulong Handle; }
        public struct McpFilterManagerHandle { public ulong Handle; }
        public struct McpBufferHandle { public ulong Handle; }
        public struct McpFilterFactoryHandle { public ulong Handle; }
        public struct McpFilterChainBuilderHandle { public IntPtr Handle; }

        /// <summary>
        /// Filter status for processing control
        /// </summary>
        public enum mcp_filter_status_t : int
        {
            MCP_FILTER_CONTINUE = 0,
            MCP_FILTER_STOP_ITERATION = 1
        }

        /// <summary>
        /// Filter position in chain
        /// </summary>
        public enum mcp_filter_position_t : int
        {
            MCP_FILTER_POSITION_FIRST = 0,
            MCP_FILTER_POSITION_LAST = 1,
            MCP_FILTER_POSITION_BEFORE = 2,
            MCP_FILTER_POSITION_AFTER = 3
        }

        /// <summary>
        /// Protocol layers (OSI model)
        /// </summary>
        public enum mcp_protocol_layer_t : int
        {
            MCP_PROTOCOL_LAYER_3_NETWORK = 3,
            MCP_PROTOCOL_LAYER_4_TRANSPORT = 4,
            MCP_PROTOCOL_LAYER_5_SESSION = 5,
            MCP_PROTOCOL_LAYER_6_PRESENTATION = 6,
            MCP_PROTOCOL_LAYER_7_APPLICATION = 7
        }

        /// <summary>
        /// Transport protocols for L4
        /// </summary>
        public enum mcp_transport_protocol_t : int
        {
            MCP_TRANSPORT_PROTOCOL_TCP = 0,
            MCP_TRANSPORT_PROTOCOL_UDP = 1,
            MCP_TRANSPORT_PROTOCOL_QUIC = 2,
            MCP_TRANSPORT_PROTOCOL_SCTP = 3
        }

        /// <summary>
        /// Application protocols for L7
        /// </summary>
        public enum mcp_app_protocol_t : int
        {
            MCP_APP_PROTOCOL_HTTP = 0,
            MCP_APP_PROTOCOL_HTTPS = 1,
            MCP_APP_PROTOCOL_HTTP2 = 2,
            MCP_APP_PROTOCOL_HTTP3 = 3,
            MCP_APP_PROTOCOL_GRPC = 4,
            MCP_APP_PROTOCOL_WEBSOCKET = 5,
            MCP_APP_PROTOCOL_JSONRPC = 6,
            MCP_APP_PROTOCOL_CUSTOM = 99
        }

        /// <summary>
        /// Built-in filter types
        /// </summary>
        public enum mcp_builtin_filter_type_t : int
        {
            // Network filters
            MCP_FILTER_TCP_PROXY = 0,
            MCP_FILTER_UDP_PROXY = 1,

            // HTTP filters
            MCP_FILTER_HTTP_CODEC = 10,
            MCP_FILTER_HTTP_ROUTER = 11,
            MCP_FILTER_HTTP_COMPRESSION = 12,

            // Security filters
            MCP_FILTER_TLS_TERMINATION = 20,
            MCP_FILTER_AUTHENTICATION = 21,
            MCP_FILTER_AUTHORIZATION = 22,

            // Observability
            MCP_FILTER_ACCESS_LOG = 30,
            MCP_FILTER_METRICS = 31,
            MCP_FILTER_TRACING = 32,

            // Traffic management
            MCP_FILTER_RATE_LIMIT = 40,
            MCP_FILTER_CIRCUIT_BREAKER = 41,
            MCP_FILTER_RETRY = 42,
            MCP_FILTER_LOAD_BALANCER = 43,

            // Custom filter
            MCP_FILTER_CUSTOM = 100
        }

        /// <summary>
        /// Filter error codes
        /// </summary>
        public enum mcp_filter_error_t : int
        {
            MCP_FILTER_ERROR_NONE = 0,
            MCP_FILTER_ERROR_INVALID_CONFIG = -1000,
            MCP_FILTER_ERROR_INITIALIZATION_FAILED = -1001,
            MCP_FILTER_ERROR_BUFFER_OVERFLOW = -1002,
            MCP_FILTER_ERROR_PROTOCOL_VIOLATION = -1003,
            MCP_FILTER_ERROR_UPSTREAM_TIMEOUT = -1004,
            MCP_FILTER_ERROR_CIRCUIT_OPEN = -1005,
            MCP_FILTER_ERROR_RESOURCE_EXHAUSTED = -1006,
            MCP_FILTER_ERROR_INVALID_STATE = -1007
        }

        // Buffer flags
        public const uint MCP_BUFFER_FLAG_READONLY = 0x01;
        public const uint MCP_BUFFER_FLAG_OWNED = 0x02;
        public const uint MCP_BUFFER_FLAG_EXTERNAL = 0x04;
        public const uint MCP_BUFFER_FLAG_ZERO_COPY = 0x08;

        /* ============================================================================
         * Data Structures
         * ============================================================================ */

        /// <summary>
        /// Filter configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_filter_config_t
        {
            [MarshalAs(UnmanagedType.LPStr)]
            public string name;
            public mcp_builtin_filter_type_t type;
            public McpJsonValueHandle settings;
            public mcp_protocol_layer_t layer;
            public McpMemoryPoolHandle memory_pool;
        }

        /// <summary>
        /// Buffer slice for zero-copy access
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_buffer_slice_t
        {
            public IntPtr data;  // const uint8_t*
            public UIntPtr length;
            public uint flags;
        }

        /// <summary>
        /// Protocol metadata for different layers
        /// Note: This is not a true union due to C# limitations with overlapping reference types
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_protocol_metadata_t
        {
            public mcp_protocol_layer_t layer;

            // Union data - use the appropriate field based on layer
            public L3Data l3;
            public L4Data l4;
            public L5Data l5;
            public L7Data l7;

            [StructLayout(LayoutKind.Sequential)]
            public struct L3Data
            {
                public uint src_ip;
                public uint dst_ip;
                public byte protocol;
                public byte ttl;
            }

            [StructLayout(LayoutKind.Sequential)]
            public struct L4Data
            {
                public ushort src_port;
                public ushort dst_port;
                public mcp_transport_protocol_t protocol;
                public uint sequence_num;
            }

            [StructLayout(LayoutKind.Sequential)]
            public struct L5Data
            {
                public mcp_bool_t is_tls;
                public IntPtr alpn;  // const char*
                public IntPtr sni;   // const char*
                public uint session_id;
            }

            [StructLayout(LayoutKind.Sequential)]
            public struct L7Data
            {
                public mcp_app_protocol_t protocol;
                public IntPtr headers;  // mcp_map_t - using IntPtr instead of McpMapHandle
                public IntPtr method;   // const char*
                public IntPtr path;     // const char*
                public uint status_code;
            }
        }

        /* ============================================================================
         * Callback Types
         * ============================================================================ */

        /// <summary>
        /// Filter data callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate mcp_filter_status_t mcp_filter_data_cb(
            McpBufferHandle buffer,
            mcp_bool_t end_stream,
            IntPtr user_data);

        /// <summary>
        /// Filter write callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate mcp_filter_status_t mcp_filter_write_cb(
            McpBufferHandle buffer,
            mcp_bool_t end_stream,
            IntPtr user_data);

        /// <summary>
        /// Connection event callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate mcp_filter_status_t mcp_filter_event_cb(
            mcp_connection_state_t state,
            IntPtr user_data);

        /// <summary>
        /// Watermark callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_filter_watermark_cb(
            McpFilterHandle filter,
            IntPtr user_data);

        /// <summary>
        /// Error callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_filter_error_cb(
            McpFilterHandle filter,
            mcp_filter_error_t error,
            [MarshalAs(UnmanagedType.LPStr)] string message,
            IntPtr user_data);

        /// <summary>
        /// Completion callback for async operations
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_filter_completion_cb(
            mcp_result_t result,
            IntPtr user_data);

        /// <summary>
        /// Post completion callback
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_post_completion_cb(
            mcp_result_t result,
            IntPtr user_data);

        /// <summary>
        /// Request callback for server
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_filter_request_cb(
            McpBufferHandle response_buffer,
            mcp_result_t result,
            IntPtr user_data);

        /// <summary>
        /// Filter callbacks structure
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_filter_callbacks_t
        {
            public mcp_filter_data_cb on_data;
            public mcp_filter_write_cb on_write;
            public mcp_filter_event_cb on_new_connection;
            public mcp_filter_watermark_cb on_high_watermark;
            public mcp_filter_watermark_cb on_low_watermark;
            public mcp_filter_error_cb on_error;
            public IntPtr user_data;
        }

        /* ============================================================================
         * Filter Lifecycle Management
         * ============================================================================ */

        /// <summary>
        /// Create a new filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterHandle mcp_filter_create(
            McpDispatcherHandle dispatcher,
            ref mcp_filter_config_t config);

        /// <summary>
        /// Create a built-in filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterHandle mcp_filter_create_builtin(
            McpDispatcherHandle dispatcher,
            mcp_builtin_filter_type_t type,
            McpJsonValueHandle config);

        /// <summary>
        /// Retain filter (increment reference count)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_retain(McpFilterHandle filter);

        /// <summary>
        /// Release filter (decrement reference count)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_release(McpFilterHandle filter);

        /// <summary>
        /// Set filter callbacks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_set_callbacks(
            McpFilterHandle filter,
            ref mcp_filter_callbacks_t callbacks);

        /// <summary>
        /// Set protocol metadata for filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_set_protocol_metadata(
            McpFilterHandle filter,
            ref mcp_protocol_metadata_t metadata);

        /// <summary>
        /// Get protocol metadata from filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_get_protocol_metadata(
            McpFilterHandle filter,
            out mcp_protocol_metadata_t metadata);

        /* ============================================================================
         * Filter Chain Management
         * ============================================================================ */

        /// <summary>
        /// Create filter chain builder
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterChainBuilderHandle mcp_filter_chain_builder_create(
            McpDispatcherHandle dispatcher);

        /// <summary>
        /// Add filter to chain builder
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_chain_add_filter(
            McpFilterChainBuilderHandle builder,
            McpFilterHandle filter,
            mcp_filter_position_t position,
            McpFilterHandle reference_filter);

        /// <summary>
        /// Build filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterChainHandle mcp_filter_chain_build(
            McpFilterChainBuilderHandle builder);

        /// <summary>
        /// Destroy filter chain builder
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_chain_builder_destroy(
            McpFilterChainBuilderHandle builder);

        /// <summary>
        /// Retain filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_chain_retain(McpFilterChainHandle chain);

        /// <summary>
        /// Release filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_chain_release(McpFilterChainHandle chain);

        /* ============================================================================
         * Filter Manager
         * ============================================================================ */

        /// <summary>
        /// Create filter manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterManagerHandle mcp_filter_manager_create(
            McpConnectionHandle connection,
            McpDispatcherHandle dispatcher);

        /// <summary>
        /// Add filter to manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_manager_add_filter(
            McpFilterManagerHandle manager,
            McpFilterHandle filter);

        /// <summary>
        /// Add filter chain to manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_manager_add_chain(
            McpFilterManagerHandle manager,
            McpFilterChainHandle chain);

        /// <summary>
        /// Initialize filter manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_manager_initialize(
            McpFilterManagerHandle manager);

        /// <summary>
        /// Release filter manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_manager_release(McpFilterManagerHandle manager);

        /* ============================================================================
         * Zero-Copy Buffer Operations
         * ============================================================================ */

        /// <summary>
        /// Get buffer slices for zero-copy access
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_get_buffer_slices(
            McpBufferHandle buffer,
            [In, Out] mcp_buffer_slice_t[] slices,
            ref UIntPtr slice_count);

        /// <summary>
        /// Reserve buffer space for writing
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_reserve_buffer(
            McpBufferHandle buffer,
            UIntPtr size,
            out mcp_buffer_slice_t slice);

        /// <summary>
        /// Commit written data to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_commit_buffer(
            McpBufferHandle buffer,
            UIntPtr bytes_written);

        /// <summary>
        /// Create buffer handle from data
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpBufferHandle mcp_filter_buffer_create(
            IntPtr data,
            UIntPtr length,
            uint flags);

        /// <summary>
        /// Release buffer handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_buffer_release(McpBufferHandle buffer);

        /// <summary>
        /// Get buffer length
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_filter_buffer_length(McpBufferHandle buffer);

        /* ============================================================================
         * Client/Server Integration
         * ============================================================================ */

        /// <summary>
        /// Client context for filtered operations
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_filter_client_context_t
        {
            public McpClientHandle client;
            public McpFilterChainHandle request_filters;
            public McpFilterChainHandle response_filters;
        }

        /// <summary>
        /// Send client request through filters
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_send_filtered(
            ref mcp_filter_client_context_t context,
            IntPtr data,
            UIntPtr length,
            mcp_filter_completion_cb callback,
            IntPtr user_data);

        /// <summary>
        /// Server context for filtered operations
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_filter_server_context_t
        {
            public McpServerHandle server;
            public McpFilterChainHandle request_filters;
            public McpFilterChainHandle response_filters;
        }

        /// <summary>
        /// Process server request through filters
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_process_filtered(
            ref mcp_filter_server_context_t context,
            McpRequestIdHandle request_id,
            McpBufferHandle request_buffer,
            mcp_filter_request_cb callback,
            IntPtr user_data);

        /* ============================================================================
         * Thread-Safe Operations
         * ============================================================================ */

        /// <summary>
        /// Post data to filter from any thread
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_post_data(
            McpFilterHandle filter,
            IntPtr data,
            UIntPtr length,
            mcp_post_completion_cb callback,
            IntPtr user_data);

        /* ============================================================================
         * Memory Management
         * ============================================================================ */

        // Filter resource guard handle
        public struct McpFilterResourceGuardHandle
        {
            public IntPtr Handle;
        }

        /// <summary>
        /// Create filter resource guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterResourceGuardHandle mcp_filter_guard_create(
            McpDispatcherHandle dispatcher);

        /// <summary>
        /// Add filter to resource guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_guard_add_filter(
            McpFilterResourceGuardHandle guard,
            McpFilterHandle filter);

        /// <summary>
        /// Release resource guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_guard_release(McpFilterResourceGuardHandle guard);

        /* ============================================================================
         * Buffer Pool Management
         * ============================================================================ */

        // Buffer pool handle
        public struct McpBufferPoolHandle
        {
            public IntPtr Handle;
        }

        /// <summary>
        /// Create buffer pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpBufferPoolHandle mcp_buffer_pool_create(
            UIntPtr buffer_size,
            UIntPtr max_buffers);

        /// <summary>
        /// Acquire buffer from pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpBufferHandle mcp_buffer_pool_acquire(McpBufferPoolHandle pool);

        /// <summary>
        /// Release buffer back to pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_buffer_pool_release(
            McpBufferPoolHandle pool,
            McpBufferHandle buffer);

        /// <summary>
        /// Destroy buffer pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_buffer_pool_destroy(McpBufferPoolHandle pool);

        /* ============================================================================
         * Statistics and Monitoring
         * ============================================================================ */

        /// <summary>
        /// Filter statistics
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_filter_stats_t
        {
            public ulong bytes_processed;
            public ulong packets_processed;
            public ulong errors;
            public ulong processing_time_us;
            public double throughput_mbps;
        }

        /// <summary>
        /// Get filter statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_get_stats(
            McpFilterHandle filter,
            out mcp_filter_stats_t stats);

        /// <summary>
        /// Reset filter statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_filter_reset_stats(McpFilterHandle filter);

        /* ============================================================================
         * Helper Methods
         * ============================================================================ */

        /// <summary>
        /// Helper to create buffer from byte array
        /// </summary>
        public static McpBufferHandle CreateBuffer(byte[] data, uint flags = 0)
        {
            if (data == null || data.Length == 0)
                return new McpBufferHandle { Handle = 0 };

            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return mcp_filter_buffer_create(
                    handle.AddrOfPinnedObject(),
                    new UIntPtr((uint)data.Length),
                    flags);
            }
            finally
            {
                handle.Free();
            }
        }

        /// <summary>
        /// Helper to get buffer data as byte array
        /// </summary>
        public static byte[] GetBufferData(McpBufferHandle buffer)
        {
            var length = mcp_filter_buffer_length(buffer);
            if (length == UIntPtr.Zero)
                return null;

            var slices = new mcp_buffer_slice_t[1];
            var sliceCount = new UIntPtr(1);

            if (mcp_filter_get_buffer_slices(buffer, slices, ref sliceCount) != mcp_result_t.MCP_OK)
                return null;

            if (sliceCount == UIntPtr.Zero || slices[0].data == IntPtr.Zero)
                return null;

            var data = new byte[(int)slices[0].length];
            Marshal.Copy(slices[0].data, data, 0, data.Length);
            return data;
        }

        /// <summary>
        /// Helper to post data to filter
        /// </summary>
        public static mcp_result_t PostData(McpFilterHandle filter, byte[] data)
        {
            if (data == null || data.Length == 0)
                return mcp_result_t.MCP_ERROR_INVALID_ARGUMENT;

            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return mcp_filter_post_data(
                    filter,
                    handle.AddrOfPinnedObject(),
                    new UIntPtr((uint)data.Length),
                    null,
                    IntPtr.Zero);
            }
            finally
            {
                handle.Free();
            }
        }
    }
}