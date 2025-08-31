using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for MCP JSON API functions (mcp_c_api_json.h)
    /// </summary>
    public static class McpJsonApi
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
         * JSON Value Operations
         * ============================================================================ */

        /// <summary>
        /// Parse JSON string to JSON value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_json_parse(
            [MarshalAs(UnmanagedType.LPStr)] string json_string);

        /// <summary>
        /// Convert JSON value to string
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_json_stringify(McpJsonValueHandle json);

        /// <summary>
        /// Free JSON value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_json_free(McpJsonValueHandle json);

        /* ============================================================================
         * Request ID JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert request ID to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_request_id_to_json(McpRequestIdHandle id);

        /// <summary>
        /// Create request ID from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_request_id_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Progress Token JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert progress token to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_progress_token_to_json(McpProgressTokenHandle token);

        /// <summary>
        /// Create progress token from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpProgressTokenHandle mcp_progress_token_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Content Block JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert content block to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_content_block_to_json(McpContentBlockHandle block);

        /// <summary>
        /// Create content block from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpContentBlockHandle mcp_content_block_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Tool JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert tool to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_tool_to_json(McpToolHandle tool);

        /// <summary>
        /// Create tool from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpToolHandle mcp_tool_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Prompt JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert prompt to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_prompt_to_json(McpPromptHandle prompt);

        /// <summary>
        /// Create prompt from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpPromptHandle mcp_prompt_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Message JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert message to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_message_to_json(McpMessageHandle message);

        /// <summary>
        /// Create message from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpMessageHandle mcp_message_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Error JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert JSON-RPC error to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_jsonrpc_error_to_json(McpErrorHandle error);

        /// <summary>
        /// Create JSON-RPC error from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpErrorHandle mcp_jsonrpc_error_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * JSON-RPC Request/Response/Notification JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert JSON-RPC request to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_jsonrpc_request_to_json(McpRequestHandle req);

        /// <summary>
        /// Create JSON-RPC request from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestHandle mcp_jsonrpc_request_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert JSON-RPC response to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_jsonrpc_response_to_json(McpResponseHandle resp);

        /// <summary>
        /// Create JSON-RPC response from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpResponseHandle mcp_jsonrpc_response_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert JSON-RPC notification to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_jsonrpc_notification_to_json(McpNotificationHandle notif);

        /// <summary>
        /// Create JSON-RPC notification from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpNotificationHandle mcp_jsonrpc_notification_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Initialize Request/Response JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert initialize request to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_initialize_request_to_json(IntPtr req);

        /// <summary>
        /// Create initialize request from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_request_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert initialize result to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_initialize_result_to_json(IntPtr result);

        /// <summary>
        /// Create initialize result from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_result_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Other Type JSON Conversions
         * ============================================================================ */

        /// <summary>
        /// Convert role to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_role_to_json(mcp_role_t role);

        /// <summary>
        /// Get role from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_role_t mcp_role_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert logging level to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_logging_level_to_json(mcp_logging_level_t level);

        /// <summary>
        /// Get logging level from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_logging_level_t mcp_logging_level_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert resource to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_resource_to_json(McpResourceHandle resource);

        /// <summary>
        /// Create resource from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpResourceHandle mcp_resource_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert implementation to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_implementation_to_json(IntPtr impl);

        /// <summary>
        /// Create implementation from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert client capabilities to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_client_capabilities_to_json(IntPtr caps);

        /// <summary>
        /// Create client capabilities from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_client_capabilities_from_json(McpJsonValueHandle json);

        /// <summary>
        /// Convert server capabilities to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_server_capabilities_to_json(IntPtr caps);

        /// <summary>
        /// Create server capabilities from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_server_capabilities_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * String JSON Conversion
         * ============================================================================ */

        /// <summary>
        /// Convert string to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_string_to_json(mcp_string_t str);

        /// <summary>
        /// Get string from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_string_t mcp_string_from_json(McpJsonValueHandle json);

        /* ============================================================================
         * Helper Methods
         * ============================================================================ */

        /// <summary>
        /// Helper to parse JSON string and return handle
        /// </summary>
        public static McpJsonValueHandle ParseJson(string jsonString)
        {
            if (string.IsNullOrEmpty(jsonString))
                return null;
                
            return mcp_json_parse(jsonString);
        }

        /// <summary>
        /// Helper to stringify JSON and return managed string
        /// </summary>
        public static string StringifyJson(McpJsonValueHandle json)
        {
            if (json == null || json.IsInvalid)
                return null;
                
            var ptr = mcp_json_stringify(json);
            if (ptr == IntPtr.Zero)
                return null;
                
            var result = Marshal.PtrToStringAnsi(ptr);
            // Free the native string (assuming mcp_string_free from memory API)
            McpMemoryApi.mcp_string_free(ptr);
            return result;
        }

        /// <summary>
        /// Helper to convert enum to JSON and get string representation
        /// </summary>
        public static string RoleToJsonString(mcp_role_t role)
        {
            using (var json = mcp_role_to_json(role))
            {
                return StringifyJson(json);
            }
        }

        /// <summary>
        /// Helper to convert enum to JSON and get string representation
        /// </summary>
        public static string LoggingLevelToJsonString(mcp_logging_level_t level)
        {
            using (var json = mcp_logging_level_to_json(level))
            {
                return StringifyJson(json);
            }
        }

        /// <summary>
        /// Helper to parse JSON string and get role
        /// </summary>
        public static mcp_role_t RoleFromJsonString(string jsonString)
        {
            using (var json = ParseJson(jsonString))
            {
                return mcp_role_from_json(json);
            }
        }

        /// <summary>
        /// Helper to parse JSON string and get logging level
        /// </summary>
        public static mcp_logging_level_t LoggingLevelFromJsonString(string jsonString)
        {
            using (var json = ParseJson(jsonString))
            {
                return mcp_logging_level_from_json(json);
            }
        }
    }
}