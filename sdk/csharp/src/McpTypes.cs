using System;
using System.Runtime.InteropServices;

namespace GopherMcp
{
    /// <summary>
    /// FFI-safe type definitions for Gopher MCP library
    /// Provides 1:1 mapping with C API types from mcp_c_types.h
    /// </summary>
    
    #region Enumerations

    /// <summary>
    /// Result codes for all API operations
    /// </summary>
    public enum McpResult : int
    {
        Ok = 0,
        ErrorInvalidArgument = -1,
        ErrorNullPointer = -2,
        ErrorOutOfMemory = -3,
        ErrorNotFound = -4,
        ErrorAlreadyExists = -5,
        ErrorPermissionDenied = -6,
        ErrorIoError = -7,
        ErrorTimeout = -8,
        ErrorCancelled = -9,
        ErrorNotImplemented = -10,
        ErrorInvalidState = -11,
        ErrorBufferTooSmall = -12,
        ErrorProtocolError = -13,
        ErrorConnectionFailed = -14,
        ErrorConnectionClosed = -15,
        ErrorAlreadyInitialized = -16,
        ErrorNotInitialized = -17,
        ErrorResourceExhausted = -18,
        ErrorInvalidFormat = -19,
        ErrorCleanupFailed = -20,
        ErrorResourceLimit = -21,
        ErrorNoMemory = -22,
        ErrorUnknown = -999
    }

    /// <summary>
    /// MCP Role enumeration
    /// </summary>
    public enum McpRole : int
    {
        User = 0,
        Assistant = 1
    }

    /// <summary>
    /// Logging levels
    /// </summary>
    public enum McpLoggingLevel : int
    {
        Debug = 0,
        Info = 1,
        Notice = 2,
        Warning = 3,
        Error = 4,
        Critical = 5,
        Alert = 6,
        Emergency = 7
    }

    /// <summary>
    /// Transport types
    /// </summary>
    public enum McpTransportType : int
    {
        HttpSse = 0,
        Stdio = 1,
        Pipe = 2
    }

    /// <summary>
    /// Connection states
    /// </summary>
    public enum McpConnectionState : int
    {
        Idle = 0,
        Connecting = 1,
        Connected = 2,
        Closing = 3,
        Disconnected = 4,
        Error = 5
    }

    /// <summary>
    /// Type identifiers for collections and validation
    /// </summary>
    public enum McpTypeId : int
    {
        Unknown = 0,
        String = 1,
        Number = 2,
        Bool = 3,
        Json = 4,
        Resource = 5,
        Tool = 6,
        Prompt = 7,
        Message = 8,
        ContentBlock = 9,
        Error = 10,
        Request = 11,
        Response = 12,
        Notification = 13
    }

    /// <summary>
    /// Request ID type enum
    /// </summary>
    public enum McpRequestIdType : int
    {
        String = 0,
        Number = 1
    }

    /// <summary>
    /// Progress token type enum
    /// </summary>
    public enum McpProgressTokenType : int
    {
        String = 0,
        Number = 1
    }

    /// <summary>
    /// Content block type enum
    /// </summary>
    public enum McpContentBlockType : int
    {
        Text = 0,
        Image = 1,
        Resource = 2
    }

    /// <summary>
    /// JSON value types
    /// </summary>
    public enum McpJsonType : int
    {
        Null = 0,
        Bool = 1,
        Number = 2,
        String = 3,
        Array = 4,
        Object = 5
    }

    #endregion

    #region Structs

    /// <summary>
    /// String reference for zero-copy string passing
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpStringRef
    {
        public IntPtr Data;
        public UIntPtr Length;

        public string ToManagedString()
        {
            if (Data == IntPtr.Zero || Length == UIntPtr.Zero)
                return string.Empty;
            
            return Marshal.PtrToStringUTF8(Data, (int)Length) ?? string.Empty;
        }

        public static McpStringRef FromString(string value)
        {
            if (string.IsNullOrEmpty(value))
                return new McpStringRef { Data = IntPtr.Zero, Length = UIntPtr.Zero };

            var bytes = System.Text.Encoding.UTF8.GetBytes(value);
            var ptr = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);
            
            return new McpStringRef
            {
                Data = ptr,
                Length = new UIntPtr((uint)bytes.Length)
            };
        }

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
        public McpResult Code;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Message;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string File;
        public int Line;
    }

    /// <summary>
    /// Memory allocator callbacks
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpAllocator
    {
        public IntPtr AllocFunc;
        public IntPtr ReallocFunc;
        public IntPtr FreeFunc;
        public IntPtr UserData;
    }

    /// <summary>
    /// Optional type for nullable values
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpOptional
    {
        [MarshalAs(UnmanagedType.U1)]
        public bool HasValue;
        public IntPtr Value;
    }

    /// <summary>
    /// Server configuration structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpServerConfig
    {
        public McpStringRef ServerName;
        public McpStringRef Version;
        public int WorkerThreads;
        public int MaxConnections;
        public int TimeoutMs;
        public McpLoggingLevel LogLevel;
        public IntPtr UserData;
    }

    /// <summary>
    /// Client configuration structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpClientConfig
    {
        public McpStringRef ClientName;
        public McpStringRef Version;
        public int MaxRetries;
        public int TimeoutMs;
        public int RetryDelayMs;
        public McpLoggingLevel LogLevel;
        public IntPtr UserData;
    }

    /// <summary>
    /// Tool structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpTool
    {
        public McpStringRef Name;
        public McpStringRef Description;
        public IntPtr InputSchema; // JSON schema
    }

    /// <summary>
    /// Resource structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpResource
    {
        public McpStringRef Uri;
        public McpStringRef Name;
        public McpStringRef Description;
        public McpStringRef MimeType;
    }

    /// <summary>
    /// Prompt structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpPrompt
    {
        public McpStringRef Name;
        public McpStringRef Description;
        public IntPtr Arguments; // Array of prompt arguments
        public UIntPtr ArgumentCount;
    }

    /// <summary>
    /// Implementation information structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpImplementation
    {
        public McpStringRef Name;
        public McpStringRef Version;
    }

    /// <summary>
    /// Initialize result structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpInitializeResult
    {
        public McpStringRef ProtocolVersion;
        public IntPtr ServerInfo;
        public IntPtr Capabilities;
    }

    /// <summary>
    /// Tool result structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpCallToolResult
    {
        public IntPtr Content; // Array of content blocks
        public UIntPtr ContentCount;
        [MarshalAs(UnmanagedType.U1)]
        public bool IsError;
    }

    /// <summary>
    /// Resource contents structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpResourceContents
    {
        public McpStringRef Uri;
        public McpStringRef Text;
        public IntPtr Blob; // Binary data
        public UIntPtr BlobSize;
        public McpStringRef MimeType;
    }

    #endregion

    #region Delegates (Callbacks)

    /// <summary>
    /// Guard cleanup callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpGuardCleanupFn(IntPtr resource);

    /// <summary>
    /// Tool handler callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate McpResult McpToolHandler(
        IntPtr server,
        ref McpStringRef toolName,
        IntPtr args,
        IntPtr userData,
        ref McpCallToolResult result);

    /// <summary>
    /// Resource handler callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate McpResult McpResourceHandler(
        IntPtr server,
        ref McpStringRef uri,
        IntPtr userData,
        ref McpResourceContents contents);

    /// <summary>
    /// Prompt handler callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate McpResult McpPromptHandler(
        IntPtr server,
        ref McpStringRef promptName,
        IntPtr args,
        IntPtr userData,
        IntPtr result);

    /// <summary>
    /// Initialize callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpInitializeCallback(
        McpResult result,
        ref McpInitializeResult initResult,
        IntPtr userData);

    /// <summary>
    /// Tool callback for client
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpToolCallback(
        McpResult result,
        ref McpCallToolResult toolResult,
        IntPtr userData);

    /// <summary>
    /// List tools callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpListToolsCallback(
        McpResult result,
        IntPtr tools,
        UIntPtr toolCount,
        IntPtr userData);

    /// <summary>
    /// Resource callback for client
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpResourceCallback(
        McpResult result,
        ref McpResourceContents contents,
        IntPtr userData);

    /// <summary>
    /// List resources callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpListResourcesCallback(
        McpResult result,
        IntPtr resources,
        UIntPtr resourceCount,
        IntPtr userData);

    /// <summary>
    /// Connection state callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpConnectionStateCallback(
        IntPtr connection,
        McpConnectionState oldState,
        McpConnectionState newState,
        IntPtr userData);

    /// <summary>
    /// Error callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpErrorCallback(
        McpResult error,
        ref McpStringRef message,
        IntPtr userData);

    /// <summary>
    /// Log callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpLogCallback(
        McpLoggingLevel level,
        ref McpStringRef message,
        ref McpStringRef file,
        int line,
        IntPtr userData);

    #endregion

    #region Handle Types

    /// <summary>
    /// Safe handle wrapper for MCP handles
    /// </summary>
    public abstract class McpSafeHandle : SafeHandle
    {
        protected McpSafeHandle() : base(IntPtr.Zero, true) { }

        public override bool IsInvalid => handle == IntPtr.Zero;
    }

    /// <summary>
    /// Server handle
    /// </summary>
    public class McpServerHandle : McpSafeHandle
    {
        protected override bool ReleaseHandle()
        {
            if (handle != IntPtr.Zero)
            {
                // Call native destroy function
                // NativeMethods.mcp_server_destroy(handle);
                handle = IntPtr.Zero;
            }
            return true;
        }
    }

    /// <summary>
    /// Client handle
    /// </summary>
    public class McpClientHandle : McpSafeHandle
    {
        protected override bool ReleaseHandle()
        {
            if (handle != IntPtr.Zero)
            {
                // Call native destroy function
                // NativeMethods.mcp_client_destroy(handle);
                handle = IntPtr.Zero;
            }
            return true;
        }
    }

    /// <summary>
    /// Guard handle for RAII
    /// </summary>
    public class McpGuardHandle : McpSafeHandle
    {
        protected override bool ReleaseHandle()
        {
            if (handle != IntPtr.Zero)
            {
                // Call native destroy function
                // NativeMethods.mcp_guard_destroy(handle);
                handle = IntPtr.Zero;
            }
            return true;
        }
    }

    #endregion
}