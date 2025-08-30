using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// Opaque handle types for FFI safety (1:1 mapping with types.h)
    /// These are designed to work with the C API's RAII system
    /// </summary>
    public static class McpHandles
    {
        // Core runtime types (SDK infrastructure)
        
        /// <summary>
        /// Base class for all MCP handles with automatic cleanup
        /// </summary>
        public abstract class McpSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
        {
            protected McpSafeHandle() : base(true) { }
            
            protected McpSafeHandle(IntPtr handle, bool ownsHandle) : base(ownsHandle)
            {
                SetHandle(handle);
            }
        }
        
        /// <summary>
        /// Dispatcher handle - manages event loop
        /// </summary>
        public sealed class McpDispatcherHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_dispatcher_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Connection handle - represents a network connection
        /// </summary>
        public sealed class McpConnectionHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_connection_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Listener handle - accepts incoming connections
        /// </summary>
        public sealed class McpListenerHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_listener_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Filter handle - uses uint64 for FFI safety
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_filter_t
        {
            public ulong handle;
            
            public bool IsValid => handle != 0;
            public static readonly mcp_filter_t Invalid = new mcp_filter_t { handle = 0 };
        }
        
        /// <summary>
        /// Client handle - MCP client instance
        /// </summary>
        public sealed class McpClientHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_client_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Server handle - MCP server instance
        /// </summary>
        public sealed class McpServerHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_server_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Transport socket handle
        /// </summary>
        public sealed class McpTransportSocketHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_transport_socket_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// State machine handle
        /// </summary>
        public sealed class McpStateMachineHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_state_machine_free when implemented
                return true;
            }
        }
        
        // Core MCP protocol types
        
        /// <summary>
        /// Request ID handle
        /// </summary>
        public sealed class McpRequestIdHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_request_id_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Progress token handle
        /// </summary>
        public sealed class McpProgressTokenHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_progress_token_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Cursor handle for pagination
        /// </summary>
        public sealed class McpCursorHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_cursor_free when implemented
                return true;
            }
        }
        
        // Content types
        
        /// <summary>
        /// Annotations handle
        /// </summary>
        public sealed class McpAnnotationsHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_annotations_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Text content handle
        /// </summary>
        public sealed class McpTextContentHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_text_content_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Image content handle
        /// </summary>
        public sealed class McpImageContentHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_image_content_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Audio content handle
        /// </summary>
        public sealed class McpAudioContentHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_audio_content_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Resource handle
        /// </summary>
        public sealed class McpResourceHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_resource_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Content block handle
        /// </summary>
        public sealed class McpContentBlockHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_content_block_free when implemented
                return true;
            }
        }
        
        // Tool and Prompt types
        
        /// <summary>
        /// Tool parameter handle
        /// </summary>
        public sealed class McpToolParameterHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_tool_parameter_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Tool handle
        /// </summary>
        public sealed class McpToolHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_tool_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Prompt argument handle
        /// </summary>
        public sealed class McpPromptArgumentHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_prompt_argument_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Prompt handle
        /// </summary>
        public sealed class McpPromptHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_prompt_free when implemented
                return true;
            }
        }
        
        // Error and Message types
        
        /// <summary>
        /// Error data handle
        /// </summary>
        public sealed class McpErrorDataHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_error_data_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Error handle
        /// </summary>
        public sealed class McpErrorHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_error_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Message handle
        /// </summary>
        public sealed class McpMessageHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_message_free when implemented
                return true;
            }
        }
        
        // JSON-RPC types
        
        /// <summary>
        /// Request handle
        /// </summary>
        public sealed class McpRequestHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_request_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Response handle
        /// </summary>
        public sealed class McpResponseHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_response_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Notification handle
        /// </summary>
        public sealed class McpNotificationHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_notification_free when implemented
                return true;
            }
        }
        
        // Collections and utilities
        
        /// <summary>
        /// List handle
        /// </summary>
        public sealed class McpListHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_list_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Map handle
        /// </summary>
        public sealed class McpMapHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_map_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Buffer handle
        /// </summary>
        public sealed class McpBufferHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_buffer_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// JSON value handle
        /// </summary>
        public sealed class McpJsonValueHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_json_free when implemented
                return true;
            }
        }
        
        // Additional handle types for testing compatibility
        
        /// <summary>
        /// JSON handle (alias for McpJsonValueHandle)
        /// </summary>
        public sealed class McpJsonHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_json_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Timer handle
        /// </summary>
        public sealed class McpTimerHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_timer_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Call handle for RPC calls
        /// </summary>
        public sealed class McpCallHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_call_free when implemented
                return true;
            }
        }
        
        /// <summary>
        /// Argument handle for function arguments
        /// </summary>
        public sealed class McpArgumentHandle : McpSafeHandle
        {
            protected override bool ReleaseHandle()
            {
                // Call mcp_argument_free when implemented
                return true;
            }
        }
    }
}