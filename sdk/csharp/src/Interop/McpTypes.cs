using System;
using System.Runtime.InteropServices;

namespace GopherMcp.Interop
{
    /// <summary>
    /// FFI-safe type definitions matching mcp_c_types.h
    /// This file provides a 1:1 mapping with MCP C types for cross-language compatibility.
    /// </summary>
    public static class McpTypes
    {
        // Platform and Compiler Configuration
        public const string LibraryName = "mcp_c_api";
        
        // FFI-Safe Primitive Types
        
        /// <summary>
        /// FFI-safe boolean type (guaranteed 1 byte)
        /// </summary>
        [StructLayout(LayoutKind.Sequential, Size = 1)]
        public struct mcp_bool_t
        {
            private byte value;
            
            public static readonly mcp_bool_t True = new mcp_bool_t { value = 1 };
            public static readonly mcp_bool_t False = new mcp_bool_t { value = 0 };
            
            public static implicit operator bool(mcp_bool_t b) => b.value != 0;
            public static implicit operator mcp_bool_t(bool b) => b ? True : False;
        }
        
        // Result Codes
        
        /// <summary>
        /// Result codes for all API operations
        /// </summary>
        public enum mcp_result_t : int
        {
            MCP_OK = 0,
            MCP_ERROR_INVALID_ARGUMENT = -1,
            MCP_ERROR_NULL_POINTER = -2,
            MCP_ERROR_OUT_OF_MEMORY = -3,
            MCP_ERROR_NOT_FOUND = -4,
            MCP_ERROR_ALREADY_EXISTS = -5,
            MCP_ERROR_PERMISSION_DENIED = -6,
            MCP_ERROR_IO_ERROR = -7,
            MCP_ERROR_TIMEOUT = -8,
            MCP_ERROR_CANCELLED = -9,
            MCP_ERROR_NOT_IMPLEMENTED = -10,
            MCP_ERROR_INVALID_STATE = -11,
            MCP_ERROR_BUFFER_TOO_SMALL = -12,
            MCP_ERROR_PROTOCOL_ERROR = -13,
            MCP_ERROR_CONNECTION_FAILED = -14,
            MCP_ERROR_CONNECTION_CLOSED = -15,
            MCP_ERROR_ALREADY_INITIALIZED = -16,
            MCP_ERROR_NOT_INITIALIZED = -17,
            MCP_ERROR_RESOURCE_EXHAUSTED = -18,
            MCP_ERROR_INVALID_FORMAT = -19,
            MCP_ERROR_CLEANUP_FAILED = -20,
            MCP_ERROR_RESOURCE_LIMIT = -21,
            MCP_ERROR_NO_MEMORY = -22,
            MCP_ERROR_UNKNOWN = -999
        }
        
        // Basic Types
        
        /// <summary>
        /// String reference for zero-copy string passing
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_string_ref
        {
            public IntPtr data;    // const char*
            public UIntPtr length; // size_t
            
            public string ToManagedString()
            {
                if (data == IntPtr.Zero)
                    return null;
                return Marshal.PtrToStringUTF8(data, (int)length.ToUInt32());
            }
            
            public static mcp_string_ref FromString(string str)
            {
                if (str == null)
                    return new mcp_string_ref { data = IntPtr.Zero, length = UIntPtr.Zero };
                    
                var bytes = System.Text.Encoding.UTF8.GetBytes(str);
                var ptr = Marshal.AllocHGlobal(bytes.Length);
                Marshal.Copy(bytes, 0, ptr, bytes.Length);
                return new mcp_string_ref { data = ptr, length = new UIntPtr((uint)bytes.Length) };
            }
        }
        
        /// <summary>
        /// String type for API compatibility (alias for mcp_string_ref)
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_string_t
        {
            public IntPtr data;    // const char*
            public UIntPtr length; // size_t
            
            public static implicit operator mcp_string_ref(mcp_string_t str)
            {
                return new mcp_string_ref { data = str.data, length = str.length };
            }
        }
        
        /// <summary>
        /// Error information structure
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_error_info_t
        {
            public mcp_result_t code;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
            public byte[] message;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
            public byte[] file;
            public int line;
            
            public string GetMessage()
            {
                if (message == null) return string.Empty;
                int len = Array.IndexOf(message, (byte)0);
                if (len < 0) len = message.Length;
                return System.Text.Encoding.UTF8.GetString(message, 0, len);
            }
            
            public string GetFile()
            {
                if (file == null) return string.Empty;
                int len = Array.IndexOf(file, (byte)0);
                if (len < 0) len = file.Length;
                return System.Text.Encoding.UTF8.GetString(file, 0, len);
            }
        }
        
        /// <summary>
        /// Memory allocator callbacks
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_allocator_t
        {
            public IntPtr alloc;    // void* (*)(size_t size, void* user_data)
            public IntPtr realloc;  // void* (*)(void* ptr, size_t new_size, void* user_data)
            public IntPtr free;     // void (*)(void* ptr, void* user_data)
            public IntPtr user_data;
        }
        
        /// <summary>
        /// Optional type for nullable values
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_optional_t
        {
            public mcp_bool_t has_value;
            public IntPtr value; // void*
        }
        
        // Enumerations
        
        /// <summary>
        /// MCP Role
        /// </summary>
        public enum mcp_role_t : int
        {
            MCP_ROLE_USER = 0,
            MCP_ROLE_ASSISTANT = 1
        }
        
        /// <summary>
        /// Logging levels
        /// </summary>
        public enum mcp_logging_level_t : int
        {
            MCP_LOG_DEBUG = 0,
            MCP_LOG_INFO = 1,
            MCP_LOG_NOTICE = 2,
            MCP_LOG_WARNING = 3,
            MCP_LOG_ERROR = 4,
            MCP_LOG_CRITICAL = 5,
            MCP_LOG_ALERT = 6,
            MCP_LOG_EMERGENCY = 7
        }
        
        /// <summary>
        /// Transport types
        /// </summary>
        public enum mcp_transport_type_t : int
        {
            MCP_TRANSPORT_HTTP_SSE = 0,
            MCP_TRANSPORT_STDIO = 1,
            MCP_TRANSPORT_PIPE = 2
        }
        
        /// <summary>
        /// Connection states
        /// </summary>
        public enum mcp_connection_state_t : int
        {
            MCP_CONNECTION_STATE_IDLE = 0,
            MCP_CONNECTION_STATE_CONNECTING = 1,
            MCP_CONNECTION_STATE_CONNECTED = 2,
            MCP_CONNECTION_STATE_CLOSING = 3,
            MCP_CONNECTION_STATE_DISCONNECTED = 4,
            MCP_CONNECTION_STATE_ERROR = 5
        }
        
        /// <summary>
        /// Type identifiers for collections and validation
        /// </summary>
        public enum mcp_type_id_t : int
        {
            MCP_TYPE_UNKNOWN = 0,
            MCP_TYPE_STRING = 1,
            MCP_TYPE_NUMBER = 2,
            MCP_TYPE_BOOL = 3,
            MCP_TYPE_JSON = 4,
            MCP_TYPE_RESOURCE = 5,
            MCP_TYPE_TOOL = 6,
            MCP_TYPE_PROMPT = 7,
            MCP_TYPE_MESSAGE = 8,
            MCP_TYPE_CONTENT_BLOCK = 9,
            MCP_TYPE_ERROR = 10,
            MCP_TYPE_REQUEST = 11,
            MCP_TYPE_RESPONSE = 12,
            MCP_TYPE_NOTIFICATION = 13
        }
        
        /// <summary>
        /// Request ID type enum
        /// </summary>
        public enum mcp_request_id_type_t : int
        {
            MCP_REQUEST_ID_TYPE_STRING = 0,
            MCP_REQUEST_ID_TYPE_NUMBER = 1
        }
        
        /// <summary>
        /// Progress token type enum
        /// </summary>
        public enum mcp_progress_token_type_t : int
        {
            MCP_PROGRESS_TOKEN_TYPE_STRING = 0,
            MCP_PROGRESS_TOKEN_TYPE_NUMBER = 1
        }
        
        /// <summary>
        /// Content block type enum
        /// </summary>
        public enum mcp_content_block_type_t : int
        {
            MCP_CONTENT_BLOCK_TYPE_TEXT = 0,
            MCP_CONTENT_BLOCK_TYPE_IMAGE = 1,
            MCP_CONTENT_BLOCK_TYPE_RESOURCE = 2
        }
        
        /// <summary>
        /// JSON value types
        /// </summary>
        public enum mcp_json_type_t : int
        {
            MCP_JSON_TYPE_NULL = 0,
            MCP_JSON_TYPE_BOOL = 1,
            MCP_JSON_TYPE_NUMBER = 2,
            MCP_JSON_TYPE_STRING = 3,
            MCP_JSON_TYPE_ARRAY = 4,
            MCP_JSON_TYPE_OBJECT = 5
        }
        
        // Configuration Structures
        
        /// <summary>
        /// Address family for network connections
        /// </summary>
        public enum mcp_address_family_t : int
        {
            MCP_AF_INET = 0,
            MCP_AF_INET6 = 1,
            MCP_AF_UNIX = 2
        }
        
        /// <summary>
        /// Address structure for network connections
        /// </summary>
        [StructLayout(LayoutKind.Explicit)]
        public struct mcp_address_t
        {
            [FieldOffset(0)]
            public int family;  // enum { MCP_AF_INET, MCP_AF_INET6, MCP_AF_UNIX }
            
            // Union at offset 4 (after family enum)
            [FieldOffset(4)]
            public InetAddress inet;
            
            [FieldOffset(4)]
            public UnixAddress unix;
            
            [StructLayout(LayoutKind.Sequential)]
            public struct InetAddress
            {
                [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
                public string host;
                public ushort port;
            }
            
            [StructLayout(LayoutKind.Sequential)]
            public struct UnixAddress
            {
                [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
                public string path;
            }
        }
        
        /// <summary>
        /// Socket options
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_socket_options_t
        {
            public mcp_bool_t reuse_addr;
            public mcp_bool_t keep_alive;
            public mcp_bool_t tcp_nodelay;
            public uint send_buffer_size;
            public uint recv_buffer_size;
            public uint connect_timeout_ms;
        }
        
        /// <summary>
        /// SSL configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_ssl_config_t
        {
            public IntPtr ca_cert_path;      // const char*
            public IntPtr client_cert_path;  // const char*
            public IntPtr client_key_path;   // const char*
            public mcp_bool_t verify_peer;
            public IntPtr cipher_list;       // const char*
            public IntPtr alpn_protocols;    // const char**
            public UIntPtr alpn_count;       // size_t
        }
        
        /// <summary>
        /// Watermark configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_watermark_config_t
        {
            public uint low_watermark;
            public uint high_watermark;
        }
        
        /// <summary>
        /// Client configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_client_config_t
        {
            public IntPtr client_info;              // mcp_implementation_t
            public IntPtr capabilities;              // mcp_client_capabilities_t
            public mcp_transport_type_t transport;
            public IntPtr server_address;            // mcp_address_t*
            public IntPtr ssl_config;                // mcp_ssl_config_t*
            public mcp_watermark_config_t watermarks;
            public uint reconnect_delay_ms;
            public uint max_reconnect_attempts;
        }
        
        /// <summary>
        /// Server configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_server_config_t
        {
            public IntPtr server_info;               // mcp_implementation_t
            public IntPtr capabilities;              // mcp_server_capabilities_t
            public mcp_transport_type_t transport;
            public IntPtr bind_address;              // mcp_address_t*
            public IntPtr ssl_config;                // mcp_ssl_config_t*
            public mcp_watermark_config_t watermarks;
            public uint max_connections;
            public IntPtr instructions;              // const char*
        }
    }
}