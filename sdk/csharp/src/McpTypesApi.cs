using System;
using System.Runtime.InteropServices;

namespace GopherMcp
{
    /// <summary>
    /// P/Invoke bindings for MCP types manipulation functions
    /// Provides access to functions for creating, accessing, and destroying MCP types
    /// </summary>
    internal static partial class McpTypesApi
    {
        #if WINDOWS
        private const string LibraryName = "gopher-mcp.0.1.0";
        #elif LINUX
        private const string LibraryName = "gopher-mcp.so.0.1.0";
        #elif OSX
        private const string LibraryName = "gopher-mcp.0.1.0";
        #else
        private const string LibraryName = "gopher-mcp";
        #endif

        #region Request ID Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_create_string([MarshalAs(UnmanagedType.LPUTF8Str)] string str);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_create_number(long num);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_request_id_free(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool mcp_request_id_is_string(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool mcp_request_id_is_number(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdType mcp_request_id_get_type(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_get_string(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long mcp_request_id_get_number(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_clone(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool mcp_request_id_is_valid(IntPtr id);

        #endregion

        #region Progress Token Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_progress_token_create_string([MarshalAs(UnmanagedType.LPUTF8Str)] string str);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_progress_token_create_number(long num);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_progress_token_free(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool mcp_progress_token_is_string(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool mcp_progress_token_is_number(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpProgressTokenType mcp_progress_token_get_type(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_progress_token_get_string(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long mcp_progress_token_get_number(IntPtr token);

        #endregion

        #region Cursor Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_cursor_create([MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_cursor_free(IntPtr cursor);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_cursor_get_value(IntPtr cursor);

        #endregion

        #region Content Block Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_create_text([MarshalAs(UnmanagedType.LPUTF8Str)] string text);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_create_image(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string data,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string mimeType);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_create_resource(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_content_block_free(IntPtr block);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpContentBlockType mcp_content_block_get_type(IntPtr block);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_get_text(IntPtr block);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool mcp_content_block_get_image(IntPtr block, out IntPtr outData, out IntPtr outMimeType);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_get_resource(IntPtr block);

        #endregion

        #region Tool Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string description);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_free(IntPtr tool);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_get_name(IntPtr tool);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_get_description(IntPtr tool);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_set_input_schema(IntPtr tool, IntPtr schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_get_input_schema(IntPtr tool);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_input_schema_create();

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_input_schema_free(IntPtr schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_input_schema_set_type(IntPtr schema, [MarshalAs(UnmanagedType.LPUTF8Str)] string type);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_input_schema_get_type(IntPtr schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_input_schema_add_property(
            IntPtr schema,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonSchema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_input_schema_add_required(IntPtr schema, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_tool_input_schema_get_property_count(IntPtr schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_tool_input_schema_get_required_count(IntPtr schema);

        #endregion

        #region Prompt Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_prompt_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string description);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_prompt_free(IntPtr prompt);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_prompt_get_name(IntPtr prompt);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_prompt_get_description(IntPtr prompt);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_prompt_add_argument(
            IntPtr prompt,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string description,
            [MarshalAs(UnmanagedType.U1)] bool required);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_prompt_get_argument_count(IntPtr prompt);

        #endregion

        #region Error Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_error_create(int code, [MarshalAs(UnmanagedType.LPUTF8Str)] string message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_error_free(IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_error_get_code(IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_error_get_message(IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_error_set_data(IntPtr error, [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonData);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_error_get_data(IntPtr error);

        #endregion

        #region Message Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_message_create([MarshalAs(UnmanagedType.LPUTF8Str)] string role);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_message_free(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_message_get_role(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_message_add_content(IntPtr message, IntPtr content);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_message_get_content_count(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_message_get_content(IntPtr message, UIntPtr index);

        #endregion

        #region Resource Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string uri,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_resource_free(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_uri(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_name(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_resource_set_description(IntPtr resource, [MarshalAs(UnmanagedType.LPUTF8Str)] string description);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_description(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_resource_set_mime_type(IntPtr resource, [MarshalAs(UnmanagedType.LPUTF8Str)] string mimeType);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_mime_type(IntPtr resource);

        #endregion

        #region JSON-RPC Request Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonrpc,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string method);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_request_free(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_get_jsonrpc(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_get_method(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_request_set_id(IntPtr request, IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_get_id(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_request_set_params(IntPtr request, [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonParams);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_get_params(IntPtr request);

        #endregion

        #region JSON-RPC Response Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_create([MarshalAs(UnmanagedType.LPUTF8Str)] string jsonrpc);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_free(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_jsonrpc(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_set_id(IntPtr response, IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_id(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_set_result(IntPtr response, [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonResult);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_result(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_set_error(IntPtr response, IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_error(IntPtr response);

        #endregion

        #region JSON-RPC Notification Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonrpc,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string method);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_notification_free(IntPtr notification);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_get_jsonrpc(IntPtr notification);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_get_method(IntPtr notification);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_notification_set_params(IntPtr notification, [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonParams);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_get_params(IntPtr notification);

        #endregion

        #region Implementation/Server Info Functions

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string version);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_implementation_free(IntPtr impl);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_get_name(IntPtr impl);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_get_version(IntPtr impl);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_implementation_set_title(IntPtr impl, [MarshalAs(UnmanagedType.LPUTF8Str)] string title);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_get_title(IntPtr impl);

        #endregion

        #region Additional Type Definitions

        // Type aliases for clarity
        internal static class TypeAliases
        {
            // Initialize types
            public static readonly IntPtr mcp_initialize_request_t = IntPtr.Zero;
            public static readonly IntPtr mcp_initialize_response_t = IntPtr.Zero;
            public static readonly IntPtr mcp_initialize_result_t = IntPtr.Zero;
            public static readonly IntPtr mcp_client_capabilities_t = IntPtr.Zero;
            public static readonly IntPtr mcp_server_capabilities_t = IntPtr.Zero;
        }

        #endregion
    }
}