using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// Callback delegate types for MCP C API
    /// All callbacks use platform-appropriate calling conventions
    /// </summary>
    public static class McpCallbacks
    {
        // Determine platform-specific calling convention
#if WINDOWS
        private const CallingConvention CallbackConvention = CallingConvention.StdCall;
#else
        private const CallingConvention CallbackConvention = CallingConvention.Cdecl;
#endif
        
        /// <summary>
        /// Generic callback with user data
        /// void (*mcp_callback_t)(void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_CALLBACK(IntPtr user_data);
        
        /// <summary>
        /// Timer callback
        /// void (*mcp_timer_callback_t)(void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_TIMER_CALLBACK(IntPtr user_data);
        
        /// <summary>
        /// Error callback
        /// void (*mcp_error_callback_t)(mcp_result_t error, const char* message, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_ERROR_CALLBACK(
            mcp_result_t error,
            [MarshalAs(UnmanagedType.LPStr)] string message,
            IntPtr user_data);
        
        /// <summary>
        /// Data received callback
        /// void (*mcp_data_callback_t)(mcp_connection_t connection, const uint8_t* data, size_t length, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_DATA_CALLBACK(
            IntPtr connection,  // mcp_connection_t
            IntPtr data,        // const uint8_t*
            UIntPtr length,     // size_t
            IntPtr user_data);
        
        /// <summary>
        /// Write complete callback
        /// void (*mcp_write_callback_t)(mcp_connection_t connection, mcp_result_t result, size_t bytes_written, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_WRITE_CALLBACK(
            IntPtr connection,  // mcp_connection_t
            mcp_result_t result,
            UIntPtr bytes_written, // size_t
            IntPtr user_data);
        
        /// <summary>
        /// Connection state callback
        /// void (*mcp_connection_state_callback_t)(mcp_connection_t connection, int state, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_CONNECTION_STATE_CALLBACK(
            IntPtr connection,  // mcp_connection_t
            mcp_connection_state_t state,
            IntPtr user_data);
        
        /// <summary>
        /// Accept callback for listeners
        /// void (*mcp_accept_callback_t)(mcp_listener_t listener, mcp_connection_t connection, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_ACCEPT_CALLBACK(
            IntPtr listener,    // mcp_listener_t
            IntPtr connection,  // mcp_connection_t
            IntPtr user_data);
        
        /// <summary>
        /// MCP request callback
        /// void (*mcp_request_callback_t)(mcp_client_t client, mcp_request_t request, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_REQUEST_CALLBACK(
            IntPtr client,      // mcp_client_t
            IntPtr request,     // mcp_request_t
            IntPtr user_data);
        
        /// <summary>
        /// MCP response callback
        /// void (*mcp_response_callback_t)(mcp_client_t client, mcp_response_t response, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_RESPONSE_CALLBACK(
            IntPtr client,      // mcp_client_t
            IntPtr response,    // mcp_response_t
            IntPtr user_data);
        
        /// <summary>
        /// MCP notification callback
        /// void (*mcp_notification_callback_t)(mcp_client_t client, mcp_notification_t notification, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_NOTIFICATION_CALLBACK(
            IntPtr client,          // mcp_client_t
            IntPtr notification,    // mcp_notification_t
            IntPtr user_data);
        
        /// <summary>
        /// Memory allocator function
        /// void* (*alloc)(size_t size, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate IntPtr MCP_ALLOC_FUNC(UIntPtr size, IntPtr user_data);
        
        /// <summary>
        /// Memory reallocator function
        /// void* (*realloc)(void* ptr, size_t new_size, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate IntPtr MCP_REALLOC_FUNC(IntPtr ptr, UIntPtr new_size, IntPtr user_data);
        
        /// <summary>
        /// Memory free function
        /// void (*free)(void* ptr, void* user_data)
        /// </summary>
        [UnmanagedFunctionPointer(CallbackConvention)]
        public delegate void MCP_FREE_FUNC(IntPtr ptr, IntPtr user_data);
        
        /// <summary>
        /// Helper class to manage callback lifetime and prevent GC collection
        /// </summary>
        public class CallbackHolder : IDisposable
        {
            private readonly GCHandle _handle;
            private readonly Delegate _callback;
            
            public CallbackHolder(Delegate callback)
            {
                _callback = callback ?? throw new ArgumentNullException(nameof(callback));
                _handle = GCHandle.Alloc(callback);
            }
            
            public IntPtr GetFunctionPointer()
            {
                return Marshal.GetFunctionPointerForDelegate(_callback);
            }
            
            public void Dispose()
            {
                Dispose(true);
                GC.SuppressFinalize(this);
            }
            
            protected virtual void Dispose(bool disposing)
            {
                if (_handle.IsAllocated)
                {
                    _handle.Free();
                }
            }
            
            ~CallbackHolder()
            {
                Dispose(false);
            }
        }
        
        /// <summary>
        /// Utility to create a callback holder with automatic lifetime management
        /// </summary>
        public static CallbackHolder CreateCallback<T>(T callback) where T : Delegate
        {
            return new CallbackHolder(callback);
        }
    }
}