using System;
using System.Runtime.InteropServices;

namespace GopherMcp.Core
{
    /// <summary>
    /// P/Invoke bindings for MCP C Filter API functions
    /// </summary>
    public static class McpFilterApi
    {
        private const string LibraryName = "gopher_mcp_c";
        
        // ============================================================================
        // Filter Lifecycle Management
        // ============================================================================
        
        /// <summary>
        /// Create a new filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_filter_create(
            ulong dispatcher,
            ref McpFilterConfig config);
        
        /// <summary>
        /// Create a built-in filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_filter_create_builtin(
            ulong dispatcher,
            McpBuiltinFilterType type,
            IntPtr config);
        
        /// <summary>
        /// Retain filter (increment reference count)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_retain(ulong filter);
        
        /// <summary>
        /// Release filter (decrement reference count)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_release(ulong filter);
        
        /// <summary>
        /// Set filter callbacks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_set_callbacks(
            ulong filter,
            ref McpFilterCallbacks callbacks);
        
        /// <summary>
        /// Set protocol metadata for filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_set_protocol_metadata(
            ulong filter,
            ref McpProtocolMetadata metadata);
        
        /// <summary>
        /// Get protocol metadata from filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_get_protocol_metadata(
            ulong filter,
            out McpProtocolMetadata metadata);
        
        // ============================================================================
        // Filter Manager
        // ============================================================================
        
        /// <summary>
        /// Create filter manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_filter_manager_create(
            ulong connection,
            ulong dispatcher);
        
        /// <summary>
        /// Add filter to manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_manager_add_filter(
            ulong manager,
            ulong filter);
        
        /// <summary>
        /// Add filter chain to manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_manager_add_chain(
            ulong manager,
            ulong chain);
        
        /// <summary>
        /// Initialize filter manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_manager_initialize(ulong manager);
        
        /// <summary>
        /// Release filter manager
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_manager_release(ulong manager);
        
        // ============================================================================
        // Statistics and Monitoring
        // ============================================================================
        
        /// <summary>
        /// Get filter statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_get_stats(
            ulong filter,
            out McpFilterStats stats);
        
        /// <summary>
        /// Reset filter statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_reset_stats(ulong filter);
        
        // ============================================================================
        // Thread-Safe Operations
        // ============================================================================
        
        /// <summary>
        /// Post data to filter from any thread
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_post_data(
            ulong filter,
            IntPtr data,
            UIntPtr length,
            McpPostCompletionCallback callback,
            IntPtr userData);
        
        // ============================================================================
        // Resource Guard Management
        // ============================================================================
        
        /// <summary>
        /// Create filter resource guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_filter_guard_create(ulong dispatcher);
        
        /// <summary>
        /// Add filter to resource guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_guard_add_filter(
            IntPtr guard,
            ulong filter);
        
        /// <summary>
        /// Release resource guard (cleanup all tracked resources)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_guard_release(IntPtr guard);
    }
    
    // ============================================================================
    // Enumerations
    // ============================================================================
    
    /// <summary>
    /// Filter status for processing control
    /// </summary>
    public enum McpFilterStatus
    {
        Continue = 0,
        StopIteration = 1
    }
    
    /// <summary>
    /// Filter position in chain
    /// </summary>
    public enum McpFilterPosition
    {
        First = 0,
        Last = 1,
        Before = 2,
        After = 3
    }
    
    /// <summary>
    /// Protocol layers (OSI model)
    /// </summary>
    public enum McpProtocolLayer
    {
        Layer3Network = 3,
        Layer4Transport = 4,
        Layer5Session = 5,
        Layer6Presentation = 6,
        Layer7Application = 7
    }
    
    /// <summary>
    /// Transport protocols for L4
    /// </summary>
    public enum McpTransportProtocol
    {
        Tcp = 0,
        Udp = 1,
        Quic = 2,
        Sctp = 3
    }
    
    /// <summary>
    /// Application protocols for L7
    /// </summary>
    public enum McpAppProtocol
    {
        Http = 0,
        Https = 1,
        Http2 = 2,
        Http3 = 3,
        Grpc = 4,
        WebSocket = 5,
        JsonRpc = 6,
        Custom = 99
    }
    
    /// <summary>
    /// Built-in filter types
    /// </summary>
    public enum McpBuiltinFilterType
    {
        // Network filters
        TcpProxy = 0,
        UdpProxy = 1,
        
        // HTTP filters
        HttpCodec = 10,
        HttpRouter = 11,
        HttpCompression = 12,
        
        // Security filters
        TlsTermination = 20,
        Authentication = 21,
        Authorization = 22,
        
        // Observability
        AccessLog = 30,
        Metrics = 31,
        Tracing = 32,
        
        // Traffic management
        RateLimit = 40,
        CircuitBreaker = 41,
        Retry = 42,
        LoadBalancer = 43,
        
        // Custom filter
        Custom = 100
    }
    
    /// <summary>
    /// Filter error codes
    /// </summary>
    public enum McpFilterError
    {
        None = 0,
        InvalidConfig = -1000,
        InitializationFailed = -1001,
        BufferOverflow = -1002,
        ProtocolViolation = -1003,
        UpstreamTimeout = -1004,
        CircuitOpen = -1005,
        ResourceExhausted = -1006,
        InvalidState = -1007
    }
    
    // ============================================================================
    // Structures
    // ============================================================================
    
    /// <summary>
    /// Filter configuration
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpFilterConfig
    {
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Name;
        
        public McpBuiltinFilterType Type;
        
        public IntPtr Settings;  // mcp_json_value_t
        
        public McpProtocolLayer Layer;
        
        public ulong MemoryPool;  // mcp_memory_pool_t
    }
    
    /// <summary>
    /// Protocol metadata for different layers
    /// </summary>
    [StructLayout(LayoutKind.Explicit)]
    public struct McpProtocolMetadata
    {
        [FieldOffset(0)]
        public McpProtocolLayer Layer;
        
        // L3 - Network layer
        [FieldOffset(8)]
        public uint SrcIp;
        
        [FieldOffset(12)]
        public uint DstIp;
        
        [FieldOffset(16)]
        public byte Protocol;
        
        [FieldOffset(17)]
        public byte Ttl;
        
        // L4 - Transport layer (overlaps with L3)
        [FieldOffset(8)]
        public ushort SrcPort;
        
        [FieldOffset(10)]
        public ushort DstPort;
        
        [FieldOffset(12)]
        public McpTransportProtocol TransportProtocol;
        
        [FieldOffset(16)]
        public uint SequenceNum;
        
        // L5 - Session layer (overlaps with L3/L4)
        [FieldOffset(8)]
        public byte IsTls;
        
        [FieldOffset(16)]
        public IntPtr Alpn;  // const char*
        
        [FieldOffset(24)]
        public IntPtr Sni;   // const char*
        
        [FieldOffset(32)]
        public uint SessionId;
        
        // L7 - Application layer (overlaps with others)
        [FieldOffset(8)]
        public McpAppProtocol AppProtocol;
        
        [FieldOffset(16)]
        public ulong Headers;  // mcp_map_t
        
        [FieldOffset(24)]
        public IntPtr Method;  // const char*
        
        [FieldOffset(32)]
        public IntPtr Path;    // const char*
        
        [FieldOffset(40)]
        public uint StatusCode;
    }
    
    /// <summary>
    /// Filter callbacks structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpFilterCallbacks
    {
        public McpFilterDataCallback OnData;
        public McpFilterWriteCallback OnWrite;
        public McpFilterEventCallback OnNewConnection;
        public McpFilterWatermarkCallback OnHighWatermark;
        public McpFilterWatermarkCallback OnLowWatermark;
        public McpFilterErrorCallback OnError;
        public IntPtr UserData;
    }
    
    /// <summary>
    /// Filter statistics
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpFilterStats
    {
        public ulong BytesProcessed;
        public ulong PacketsProcessed;
        public ulong Errors;
        public ulong ProcessingTimeUs;
        public double ThroughputMbps;
    }
    
    // ============================================================================
    // Callback Delegates
    // ============================================================================
    
    /// <summary>
    /// Filter data callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate McpFilterStatus McpFilterDataCallback(
        ulong buffer,
        byte endStream,
        IntPtr userData);
    
    /// <summary>
    /// Filter write callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate McpFilterStatus McpFilterWriteCallback(
        ulong buffer,
        byte endStream,
        IntPtr userData);
    
    /// <summary>
    /// Connection event callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate McpFilterStatus McpFilterEventCallback(
        int state,  // mcp_connection_state_t
        IntPtr userData);
    
    /// <summary>
    /// Watermark callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpFilterWatermarkCallback(
        ulong filter,
        IntPtr userData);
    
    /// <summary>
    /// Error callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpFilterErrorCallback(
        ulong filter,
        McpFilterError error,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message,
        IntPtr userData);
    
    /// <summary>
    /// Completion callback for async operations
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpFilterCompletionCallback(
        int result,
        IntPtr userData);
    
    /// <summary>
    /// Post completion callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpPostCompletionCallback(
        int result,
        IntPtr userData);
    
    /// <summary>
    /// Request callback for server
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpFilterRequestCallback(
        ulong responseBuffer,
        int result,
        IntPtr userData);
}