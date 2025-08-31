using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for MCP C API functions (mcp_c_types_api.h)
    /// </summary>
    public static class McpTypesApi
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
         * Request ID Functions (variant<string, int>)
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_create_string([MarshalAs(UnmanagedType.LPStr)] string str);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_create_number(long num);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_request_id_free(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_request_id_is_string(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_request_id_is_number(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_request_id_type_t mcp_request_id_get_type(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_get_string(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long mcp_request_id_get_number(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_request_id_clone(IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_request_id_is_valid(IntPtr id);

        /* ============================================================================
         * Progress Token Functions (variant<string, int>)
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_progress_token_create_string([MarshalAs(UnmanagedType.LPStr)] string str);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_progress_token_create_number(long num);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_progress_token_free(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_progress_token_is_string(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_progress_token_is_number(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_progress_token_type_t mcp_progress_token_get_type(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_progress_token_get_string(IntPtr token);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long mcp_progress_token_get_number(IntPtr token);

        /* ============================================================================
         * Cursor Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_cursor_create([MarshalAs(UnmanagedType.LPStr)] string value);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_cursor_free(IntPtr cursor);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_cursor_get_value(IntPtr cursor);

        /* ============================================================================
         * Content Block Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_create_text([MarshalAs(UnmanagedType.LPStr)] string text);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_create_image(
            [MarshalAs(UnmanagedType.LPStr)] string data,
            [MarshalAs(UnmanagedType.LPStr)] string mime_type);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_create_resource(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_content_block_free(IntPtr block);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_content_block_type_t mcp_content_block_get_type(IntPtr block);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_get_text(IntPtr block);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_content_block_get_image(
            IntPtr block,
            out IntPtr out_data,
            out IntPtr out_mime_type);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_content_block_get_resource(IntPtr block);

        /* ============================================================================
         * Tool Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_create(
            [MarshalAs(UnmanagedType.LPStr)] string name,
            [MarshalAs(UnmanagedType.LPStr)] string description);

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
        public static extern void mcp_tool_input_schema_set_type(
            IntPtr schema,
            [MarshalAs(UnmanagedType.LPStr)] string type);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_tool_input_schema_get_type(IntPtr schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_input_schema_add_property(
            IntPtr schema,
            [MarshalAs(UnmanagedType.LPStr)] string name,
            [MarshalAs(UnmanagedType.LPStr)] string json_schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_tool_input_schema_add_required(
            IntPtr schema,
            [MarshalAs(UnmanagedType.LPStr)] string name);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_tool_input_schema_get_property_count(IntPtr schema);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_tool_input_schema_get_required_count(IntPtr schema);

        /* ============================================================================
         * Prompt Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_prompt_create(
            [MarshalAs(UnmanagedType.LPStr)] string name,
            [MarshalAs(UnmanagedType.LPStr)] string description);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_prompt_free(IntPtr prompt);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_prompt_get_name(IntPtr prompt);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_prompt_get_description(IntPtr prompt);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_prompt_add_argument(
            IntPtr prompt,
            [MarshalAs(UnmanagedType.LPStr)] string name,
            [MarshalAs(UnmanagedType.LPStr)] string description,
            mcp_bool_t required);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_prompt_get_argument_count(IntPtr prompt);

        /* ============================================================================
         * Error Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_error_create(
            int code,
            [MarshalAs(UnmanagedType.LPStr)] string message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_error_free(IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_error_get_code(IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_error_get_message(IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_error_set_data(
            IntPtr error,
            [MarshalAs(UnmanagedType.LPStr)] string json_data);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_error_get_data(IntPtr error);

        /* ============================================================================
         * Message Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_message_create([MarshalAs(UnmanagedType.LPStr)] string role);

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

        /* ============================================================================
         * Resource Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_create(
            [MarshalAs(UnmanagedType.LPStr)] string uri,
            [MarshalAs(UnmanagedType.LPStr)] string name);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_resource_free(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_uri(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_name(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_resource_set_description(
            IntPtr resource,
            [MarshalAs(UnmanagedType.LPStr)] string description);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_description(IntPtr resource);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_resource_set_mime_type(
            IntPtr resource,
            [MarshalAs(UnmanagedType.LPStr)] string mime_type);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_resource_get_mime_type(IntPtr resource);

        /* ============================================================================
         * JSON-RPC Request Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_create(
            [MarshalAs(UnmanagedType.LPStr)] string jsonrpc,
            [MarshalAs(UnmanagedType.LPStr)] string method);

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
        public static extern void mcp_jsonrpc_request_set_params(
            IntPtr request,
            [MarshalAs(UnmanagedType.LPStr)] string json_params);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_request_get_params(IntPtr request);

        /* ============================================================================
         * JSON-RPC Response Functions
         * ============================================================================ */

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_create([MarshalAs(UnmanagedType.LPStr)] string jsonrpc);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_free(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_jsonrpc(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_set_id(IntPtr response, IntPtr id);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_id(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_set_result(
            IntPtr response,
            [MarshalAs(UnmanagedType.LPStr)] string json_result);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_result(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_response_set_error(IntPtr response, IntPtr error);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_response_get_error(IntPtr response);

        /* ============================================================================
         * Protocol Message Functions
         * ============================================================================ */

        // Initialize Request/Response
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_request_create(
            [MarshalAs(UnmanagedType.LPStr)] string protocol_version,
            [MarshalAs(UnmanagedType.LPStr)] string client_name,
            [MarshalAs(UnmanagedType.LPStr)] string client_version);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_initialize_request_free(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_request_get_protocol_version(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_request_get_client_name(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_request_get_client_version(IntPtr request);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_response_create(
            [MarshalAs(UnmanagedType.LPStr)] string protocol_version,
            [MarshalAs(UnmanagedType.LPStr)] string server_name,
            [MarshalAs(UnmanagedType.LPStr)] string server_version);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_initialize_response_free(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_response_get_protocol_version(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_response_get_server_name(IntPtr response);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_response_get_server_version(IntPtr response);

        // Implementation/Server Info Functions
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_create(
            [MarshalAs(UnmanagedType.LPStr)] string name,
            [MarshalAs(UnmanagedType.LPStr)] string version);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_implementation_free(IntPtr impl);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_get_name(IntPtr impl);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_get_version(IntPtr impl);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_implementation_set_title(
            IntPtr impl,
            [MarshalAs(UnmanagedType.LPStr)] string title);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_implementation_get_title(IntPtr impl);

        // Client Capabilities Functions
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_client_capabilities_create();

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_client_capabilities_free(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_client_capabilities_has_roots(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_client_capabilities_set_roots(IntPtr caps, mcp_bool_t enabled);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_client_capabilities_has_sampling(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_client_capabilities_set_sampling(IntPtr caps, mcp_bool_t enabled);

        // Server Capabilities Functions
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_server_capabilities_create();

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_capabilities_free(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_server_capabilities_has_tools(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_capabilities_set_tools(IntPtr caps, mcp_bool_t enabled);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_server_capabilities_has_prompts(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_capabilities_set_prompts(IntPtr caps, mcp_bool_t enabled);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_server_capabilities_has_resources(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_capabilities_set_resources(IntPtr caps, mcp_bool_t enabled);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_server_capabilities_has_logging(IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_capabilities_set_logging(IntPtr caps, mcp_bool_t enabled);

        // Initialize Result Functions
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_result_create([MarshalAs(UnmanagedType.LPStr)] string protocol_version);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_initialize_result_free(IntPtr result);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_result_get_protocol_version(IntPtr result);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_initialize_result_set_server_info(IntPtr result, IntPtr info);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_result_get_server_info(IntPtr result);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_initialize_result_set_capabilities(IntPtr result, IntPtr caps);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_initialize_result_get_capabilities(IntPtr result);

        // JSON-RPC Notification Functions
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_create(
            [MarshalAs(UnmanagedType.LPStr)] string jsonrpc,
            [MarshalAs(UnmanagedType.LPStr)] string method);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_notification_free(IntPtr notification);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_get_jsonrpc(IntPtr notification);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_get_method(IntPtr notification);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_jsonrpc_notification_set_params(
            IntPtr notification,
            [MarshalAs(UnmanagedType.LPStr)] string json_params);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_jsonrpc_notification_get_params(IntPtr notification);

        /* ============================================================================
         * Helper Methods for String Marshalling
         * ============================================================================ */

        /// <summary>
        /// Helper to convert IntPtr (const char*) to managed string
        /// </summary>
        public static string PtrToString(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
                return null;
            return Marshal.PtrToStringAnsi(ptr);
        }

        /// <summary>
        /// Helper to convert IntPtr (const char*) to managed string with UTF-8 support
        /// </summary>
        public static string PtrToStringUtf8(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
                return null;
            return Marshal.PtrToStringUTF8(ptr);
        }
    }
}