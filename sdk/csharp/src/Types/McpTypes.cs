using System;
using System.Runtime.InteropServices;

namespace GopherMcp.Types
{
    /// <summary>
    /// FFI-safe boolean type (guaranteed 1 byte)
    /// </summary>
    public enum McpBool : byte
    {
        False = 0,
        True = 1
    }
    
    /// <summary>
    /// Result codes for all API operations
    /// </summary>
    public enum McpResult : int
    {
        /// <summary>Operation completed successfully</summary>
        Ok = 0,
        
        /// <summary>Invalid argument provided</summary>
        InvalidArgument = -1,
        
        /// <summary>Null pointer error</summary>
        NullPointer = -2,
        
        /// <summary>Out of memory</summary>
        OutOfMemory = -3,
        
        /// <summary>Resource not found</summary>
        NotFound = -4,
        
        /// <summary>Resource already exists</summary>
        AlreadyExists = -5,
        
        /// <summary>Permission denied</summary>
        PermissionDenied = -6,
        
        /// <summary>I/O error occurred</summary>
        IoError = -7,
        
        /// <summary>Operation timed out</summary>
        Timeout = -8,
        
        /// <summary>Operation was cancelled</summary>
        Cancelled = -9,
        
        /// <summary>Feature not implemented</summary>
        NotImplemented = -10,
        
        /// <summary>Invalid state for operation</summary>
        InvalidState = -11,
        
        /// <summary>Buffer too small</summary>
        BufferTooSmall = -12,
        
        /// <summary>Protocol error</summary>
        ProtocolError = -13,
        
        /// <summary>Connection failed</summary>
        ConnectionFailed = -14,
        
        /// <summary>Connection closed</summary>
        ConnectionClosed = -15,
        
        /// <summary>Already initialized</summary>
        AlreadyInitialized = -16,
        
        /// <summary>Not initialized</summary>
        NotInitialized = -17,
        
        /// <summary>Resource exhausted</summary>
        ResourceExhausted = -18,
        
        /// <summary>Invalid format</summary>
        InvalidFormat = -19,
        
        /// <summary>Cleanup failed</summary>
        CleanupFailed = -20,
        
        /// <summary>Resource limit reached</summary>
        ResourceLimit = -21,
        
        /// <summary>No memory available</summary>
        NoMemory = -22,
        
        /// <summary>Unknown error</summary>
        Unknown = -999
    }
    
    /// <summary>
    /// MCP Role enumeration
    /// </summary>
    public enum McpRole : int
    {
        /// <summary>User role</summary>
        User = 0,
        
        /// <summary>Assistant role</summary>
        Assistant = 1
    }
    
    /// <summary>
    /// Logging levels
    /// </summary>
    public enum McpLogLevel : int
    {
        /// <summary>No logging</summary>
        None = -1,
        
        /// <summary>Debug level logging</summary>
        Debug = 0,
        
        /// <summary>Informational messages</summary>
        Info = 1,
        
        /// <summary>Normal but significant condition</summary>
        Notice = 2,
        
        /// <summary>Warning conditions</summary>
        Warning = 3,
        
        /// <summary>Error conditions</summary>
        Error = 4,
        
        /// <summary>Critical conditions</summary>
        Critical = 5,
        
        /// <summary>Action must be taken immediately</summary>
        Alert = 6,
        
        /// <summary>System is unusable</summary>
        Emergency = 7
    }
    
    /// <summary>
    /// Logging levels (deprecated, use McpLogLevel)
    /// </summary>
    [Obsolete("Use McpLogLevel instead")]
    public enum McpLoggingLevel : int
    {
        /// <summary>Debug level logging</summary>
        Debug = 0,
        
        /// <summary>Informational messages</summary>
        Info = 1,
        
        /// <summary>Normal but significant condition</summary>
        Notice = 2,
        
        /// <summary>Warning conditions</summary>
        Warning = 3,
        
        /// <summary>Error conditions</summary>
        Error = 4,
        
        /// <summary>Critical conditions</summary>
        Critical = 5,
        
        /// <summary>Action must be taken immediately</summary>
        Alert = 6,
        
        /// <summary>System is unusable</summary>
        Emergency = 7
    }
    
    /// <summary>
    /// Transport types for MCP communication
    /// </summary>
    public enum McpTransportType : int
    {
        /// <summary>HTTP with Server-Sent Events</summary>
        HttpSse = 0,
        
        /// <summary>Standard input/output</summary>
        Stdio = 1,
        
        /// <summary>Named pipe</summary>
        Pipe = 2
    }
    
    /// <summary>
    /// Connection states
    /// </summary>
    public enum McpConnectionState : int
    {
        /// <summary>Connection is idle</summary>
        Idle = 0,
        
        /// <summary>Connection is being established</summary>
        Connecting = 1,
        
        /// <summary>Connection is established</summary>
        Connected = 2,
        
        /// <summary>Connection is closing</summary>
        Closing = 3,
        
        /// <summary>Connection is disconnected</summary>
        Disconnected = 4,
        
        /// <summary>Connection error occurred</summary>
        Error = 5
    }
    
    /// <summary>
    /// Type identifiers for collections and validation
    /// </summary>
    public enum McpTypeId : int
    {
        /// <summary>Unknown type</summary>
        Unknown = 0,
        
        /// <summary>String type</summary>
        String = 1,
        
        /// <summary>Number type</summary>
        Number = 2,
        
        /// <summary>Boolean type</summary>
        Bool = 3,
        
        /// <summary>JSON value type</summary>
        Json = 4,
        
        /// <summary>Resource type</summary>
        Resource = 5,
        
        /// <summary>Tool type</summary>
        Tool = 6,
        
        /// <summary>Prompt type</summary>
        Prompt = 7,
        
        /// <summary>Message type</summary>
        Message = 8,
        
        /// <summary>Content block type</summary>
        ContentBlock = 9,
        
        /// <summary>Error type</summary>
        Error = 10,
        
        /// <summary>Request type</summary>
        Request = 11,
        
        /// <summary>Response type</summary>
        Response = 12,
        
        /// <summary>Notification type</summary>
        Notification = 13
    }
    
    /// <summary>
    /// Request ID type enumeration
    /// </summary>
    public enum McpRequestIdType : int
    {
        /// <summary>String-based request ID</summary>
        String = 0,
        
        /// <summary>Number-based request ID</summary>
        Number = 1
    }
    
    /// <summary>
    /// Progress token type enumeration
    /// </summary>
    public enum McpProgressTokenType : int
    {
        /// <summary>String-based progress token</summary>
        String = 0,
        
        /// <summary>Number-based progress token</summary>
        Number = 1
    }
    
    /// <summary>
    /// Content block type enumeration
    /// </summary>
    public enum McpContentBlockType : int
    {
        /// <summary>Text content</summary>
        Text = 0,
        
        /// <summary>Image content</summary>
        Image = 1,
        
        /// <summary>Resource reference</summary>
        Resource = 2
    }
    
    /// <summary>
    /// Built-in filter types
    /// </summary>
    public enum McpBuiltinFilterType : int
    {
        /// <summary>Validation filter</summary>
        Validation = 0,
        
        /// <summary>Logging filter</summary>
        Logging = 1,
        
        /// <summary>Metrics filter</summary>
        Metrics = 2,
        
        /// <summary>Rate limiting filter</summary>
        RateLimit = 3,
        
        /// <summary>Compression filter</summary>
        Compression = 4,
        
        /// <summary>Encryption filter</summary>
        Encryption = 5,
        
        /// <summary>Authentication filter</summary>
        Authentication = 6,
        
        /// <summary>Caching filter</summary>
        Caching = 7
    }
    
    /// <summary>
    /// JSON value types
    /// </summary>
    public enum McpJsonType : int
    {
        /// <summary>Null value</summary>
        Null = 0,
        
        /// <summary>Boolean value</summary>
        Bool = 1,
        
        /// <summary>Number value</summary>
        Number = 2,
        
        /// <summary>String value</summary>
        String = 3,
        
        /// <summary>Array value</summary>
        Array = 4,
        
        /// <summary>Object value</summary>
        Object = 5
    }
    
    /// <summary>
    /// Address family enumeration
    /// </summary>
    public enum McpAddressFamily : int
    {
        /// <summary>IPv4 address</summary>
        Inet = 0,
        
        /// <summary>IPv6 address</summary>
        Inet6 = 1,
        
        /// <summary>Unix domain socket</summary>
        Unix = 2
    }
    
    /// <summary>
    /// String reference for zero-copy string passing
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpStringRef
    {
        /// <summary>Pointer to string data</summary>
        public IntPtr Data;
        
        /// <summary>Length of string in bytes</summary>
        public UIntPtr Length;
        
        /// <summary>
        /// Create a string reference from a managed string
        /// </summary>
        public static McpStringRef FromString(string value)
        {
            if (value == null)
            {
                return new McpStringRef { Data = IntPtr.Zero, Length = UIntPtr.Zero };
            }
            
            var bytes = System.Text.Encoding.UTF8.GetBytes(value);
            var ptr = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);
            
            return new McpStringRef
            {
                Data = ptr,
                Length = new UIntPtr((uint)bytes.Length)
            };
        }
        
        /// <summary>
        /// Convert to managed string
        /// </summary>
        public string ToString()
        {
            if (Data == IntPtr.Zero || Length == UIntPtr.Zero)
                return null;
            
            var bytes = new byte[(int)Length.ToUInt32()];
            Marshal.Copy(Data, bytes, 0, bytes.Length);
            return System.Text.Encoding.UTF8.GetString(bytes);
        }
        
        /// <summary>
        /// Free the allocated memory
        /// </summary>
        public void Free()
        {
            if (Data != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(Data);
                Data = IntPtr.Zero;
                Length = UIntPtr.Zero;
            }
        }
    }
    
    /// <summary>
    /// Error information structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct McpErrorInfo
    {
        /// <summary>Error code</summary>
        public McpResult Code;
        
        /// <summary>Error message (256 bytes max)</summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Message;
        
        /// <summary>Source file (256 bytes max)</summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string File;
        
        /// <summary>Line number in source file</summary>
        public int Line;
    }
    
    /// <summary>
    /// Memory allocator callbacks structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpAllocator
    {
        /// <summary>Allocation function pointer</summary>
        [MarshalAs(UnmanagedType.FunctionPtr)]
        public AllocDelegate Alloc;
        
        /// <summary>Reallocation function pointer</summary>
        [MarshalAs(UnmanagedType.FunctionPtr)]
        public ReallocDelegate Realloc;
        
        /// <summary>Free function pointer</summary>
        [MarshalAs(UnmanagedType.FunctionPtr)]
        public FreeDelegate Free;
        
        /// <summary>User data pointer</summary>
        public IntPtr UserData;
        
        /// <summary>Allocation delegate</summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate IntPtr AllocDelegate(UIntPtr size, IntPtr userData);
        
        /// <summary>Reallocation delegate</summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate IntPtr ReallocDelegate(IntPtr ptr, UIntPtr newSize, IntPtr userData);
        
        /// <summary>Free delegate</summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void FreeDelegate(IntPtr ptr, IntPtr userData);
    }
    
    /// <summary>
    /// Optional type for nullable values
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpOptional
    {
        /// <summary>Whether the value is present</summary>
        public McpBool HasValue;
        
        /// <summary>Pointer to the value</summary>
        public IntPtr Value;
        
        /// <summary>
        /// Create an optional with a value
        /// </summary>
        public static McpOptional WithValue(IntPtr value)
        {
            return new McpOptional
            {
                HasValue = McpBool.True,
                Value = value
            };
        }
        
        /// <summary>
        /// Create an empty optional
        /// </summary>
        public static McpOptional Empty()
        {
            return new McpOptional
            {
                HasValue = McpBool.False,
                Value = IntPtr.Zero
            };
        }
    }
    
    /// <summary>
    /// Socket options structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpSocketOptions
    {
        /// <summary>Enable address reuse</summary>
        public McpBool ReuseAddr;
        
        /// <summary>Enable keep-alive</summary>
        public McpBool KeepAlive;
        
        /// <summary>Enable TCP no-delay</summary>
        public McpBool TcpNoDelay;
        
        /// <summary>Send buffer size</summary>
        public uint SendBufferSize;
        
        /// <summary>Receive buffer size</summary>
        public uint RecvBufferSize;
        
        /// <summary>Connection timeout in milliseconds</summary>
        public uint ConnectTimeoutMs;
    }
    
    /// <summary>
    /// SSL configuration structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpSslConfig
    {
        /// <summary>CA certificate path</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string CaCertPath;
        
        /// <summary>Client certificate path</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string ClientCertPath;
        
        /// <summary>Client key path</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string ClientKeyPath;
        
        /// <summary>Whether to verify peer</summary>
        public McpBool VerifyPeer;
        
        /// <summary>Cipher list</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string CipherList;
        
        /// <summary>ALPN protocols array pointer</summary>
        public IntPtr AlpnProtocols;
        
        /// <summary>ALPN protocol count</summary>
        public UIntPtr AlpnCount;
    }
    
    /// <summary>
    /// Watermark configuration structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpWatermarkConfig
    {
        /// <summary>Low watermark threshold</summary>
        public uint LowWatermark;
        
        /// <summary>High watermark threshold</summary>
        public uint HighWatermark;
    }
    
    /// <summary>
    /// Address structure for network connections
    /// </summary>
    [StructLayout(LayoutKind.Explicit)]
    public struct McpAddress
    {
        /// <summary>Address family</summary>
        [FieldOffset(0)]
        public McpAddressFamily Family;
        
        /// <summary>IPv4/IPv6 host (256 bytes)</summary>
        [FieldOffset(4)]
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Host;
        
        /// <summary>Port number for IPv4/IPv6</summary>
        [FieldOffset(260)]
        public ushort Port;
        
        /// <summary>Unix socket path (256 bytes)</summary>
        [FieldOffset(4)]
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string UnixPath;
    }
    
    /// <summary>
    /// Client configuration structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpClientConfig
    {
        /// <summary>Client implementation info</summary>
        public ulong ClientInfo;
        
        /// <summary>Client capabilities</summary>
        public ulong Capabilities;
        
        /// <summary>Transport type</summary>
        public McpTransportType Transport;
        
        /// <summary>Server address pointer</summary>
        public IntPtr ServerAddress;
        
        /// <summary>SSL configuration pointer</summary>
        public IntPtr SslConfig;
        
        /// <summary>Watermark configuration</summary>
        public McpWatermarkConfig Watermarks;
        
        /// <summary>Reconnect delay in milliseconds</summary>
        public uint ReconnectDelayMs;
        
        /// <summary>Maximum reconnect attempts</summary>
        public uint MaxReconnectAttempts;
    }
    
    /// <summary>
    /// Filter configuration structure for P/Invoke
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpFilterConfig
    {
        /// <summary>Filter name</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Name;
        
        /// <summary>Filter type</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Type;
        
        /// <summary>Priority level</summary>
        public int Priority;
        
        /// <summary>Whether the filter is enabled</summary>
        public McpBool Enabled;
        
        /// <summary>Maximum buffer size</summary>
        public uint MaxBufferSize;
        
        /// <summary>Timeout in milliseconds</summary>
        public uint TimeoutMs;
        
        /// <summary>User data pointer</summary>
        public IntPtr UserData;
    }
    
    /// <summary>
    /// Server configuration structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpServerConfig
    {
        /// <summary>Server implementation info</summary>
        public ulong ServerInfo;
        
        /// <summary>Server capabilities</summary>
        public ulong Capabilities;
        
        /// <summary>Transport type</summary>
        public McpTransportType Transport;
        
        /// <summary>Bind address pointer</summary>
        public IntPtr BindAddress;
        
        /// <summary>SSL configuration pointer</summary>
        public IntPtr SslConfig;
        
        /// <summary>Watermark configuration</summary>
        public McpWatermarkConfig Watermarks;
        
        /// <summary>Maximum number of connections</summary>
        public uint MaxConnections;
        
        /// <summary>Instructions string</summary>
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Instructions;
    }
    
    // ============================================================================
    // Callback Delegates
    // ============================================================================
    
    /// <summary>
    /// Generic callback with user data
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpCallback(IntPtr userData);
    
    /// <summary>
    /// Timer callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpTimerCallback(IntPtr userData);
    
    /// <summary>
    /// Error callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpErrorCallback(
        McpResult error,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message,
        IntPtr userData);
    
    /// <summary>
    /// Data received callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpDataCallback(
        IntPtr connection,
        IntPtr data,
        UIntPtr length,
        IntPtr userData);
    
    /// <summary>
    /// Write complete callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpWriteCallback(
        IntPtr connection,
        McpResult result,
        UIntPtr bytesWritten,
        IntPtr userData);
    
    /// <summary>
    /// Connection state callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpConnectionStateCallback(
        IntPtr connection,
        int state,
        IntPtr userData);
    
    /// <summary>
    /// Accept callback for listeners
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpAcceptCallback(
        IntPtr listener,
        IntPtr connection,
        IntPtr userData);
    
    /// <summary>
    /// MCP request callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpRequestCallback(
        IntPtr client,
        IntPtr request,
        IntPtr userData);
    
    /// <summary>
    /// MCP response callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpResponseCallback(
        IntPtr client,
        IntPtr response,
        IntPtr userData);
    
    /// <summary>
    /// MCP notification callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpNotificationCallback(
        IntPtr client,
        IntPtr notification,
        IntPtr userData);
    
    /// <summary>
    /// Callback for logging messages
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpLogCallback(
        McpLogLevel level,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string message,
        IntPtr context);
    
    /// <summary>
    /// Callback for process completion
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpProcessCallback(
        McpResult result,
        IntPtr userData);
}