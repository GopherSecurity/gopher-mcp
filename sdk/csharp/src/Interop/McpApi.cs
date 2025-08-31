using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;
using static GopherMcp.Interop.McpCallbacks;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for main MCP C API functions (mcp_c_api.h)
    /// </summary>
    public static class McpApi
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
         * Library Version and Initialization
         * ============================================================================ */

        /// <summary>
        /// Initialize the MCP library
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_init(IntPtr allocator);

        /// <summary>
        /// Shutdown the MCP library
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_shutdown();

        /// <summary>
        /// Check if library is initialized
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_is_initialized();

        /// <summary>
        /// Get library version information
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_get_version();

        /* ============================================================================
         * RAII Guard Functions
         * ============================================================================ */

        /// <summary>
        /// Guard cleanup callback delegate
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_guard_cleanup_fn(IntPtr resource);

        /// <summary>
        /// Create a RAII guard for a handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpGuardHandle mcp_guard_create(IntPtr handle, mcp_type_id_t type);

        /// <summary>
        /// Create a RAII guard with custom cleanup
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpGuardHandle mcp_guard_create_custom(
            IntPtr handle,
            mcp_type_id_t type,
            mcp_guard_cleanup_fn cleanup);

        /// <summary>
        /// Release resource from guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_guard_release(ref McpGuardHandle guard);

        /// <summary>
        /// Destroy guard and cleanup resource
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_guard_destroy(ref McpGuardHandle guard);

        /// <summary>
        /// Check if guard is valid
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_guard_is_valid(McpGuardHandle guard);

        /// <summary>
        /// Get the guarded resource
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_guard_get(McpGuardHandle guard);

        /* ============================================================================
         * Transaction Management
         * ============================================================================ */

        /// <summary>
        /// Transaction options structure
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_transaction_opts_t
        {
            public mcp_bool_t auto_rollback;
            public mcp_bool_t strict_ordering;
            public uint max_resources;
        }

        /// <summary>
        /// Create a new transaction
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpTransactionHandle mcp_transaction_create();

        /// <summary>
        /// Create a new transaction with options
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpTransactionHandle mcp_transaction_create_ex(
            ref mcp_transaction_opts_t opts);

        /// <summary>
        /// Add resource to transaction
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_transaction_add(
            McpTransactionHandle txn,
            IntPtr handle,
            mcp_type_id_t type);

        /// <summary>
        /// Add resource with custom cleanup
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_transaction_add_custom(
            McpTransactionHandle txn,
            IntPtr handle,
            mcp_type_id_t type,
            mcp_guard_cleanup_fn cleanup);

        /// <summary>
        /// Get number of resources in transaction
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_transaction_size(McpTransactionHandle txn);

        /// <summary>
        /// Commit transaction
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_transaction_commit(ref McpTransactionHandle txn);

        /// <summary>
        /// Rollback transaction
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_transaction_rollback(ref McpTransactionHandle txn);

        /// <summary>
        /// Destroy transaction
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_transaction_destroy(ref McpTransactionHandle txn);

        /// <summary>
        /// Check if transaction is valid
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_transaction_is_valid(McpTransactionHandle txn);

        /* ============================================================================
         * Event Loop & Dispatcher
         * ============================================================================ */

        /// <summary>
        /// Create a new dispatcher
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpDispatcherHandle mcp_dispatcher_create();

        /// <summary>
        /// Create dispatcher with RAII guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpDispatcherHandle mcp_dispatcher_create_guarded(out McpGuardHandle guard);

        /// <summary>
        /// Run the dispatcher
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_dispatcher_run(McpDispatcherHandle dispatcher);

        /// <summary>
        /// Run dispatcher for specified duration
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_dispatcher_run_timeout(
            McpDispatcherHandle dispatcher,
            uint timeout_ms);

        /// <summary>
        /// Stop the dispatcher
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_dispatcher_stop(McpDispatcherHandle dispatcher);

        /// <summary>
        /// Post a callback to dispatcher thread
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_dispatcher_post(
            McpDispatcherHandle dispatcher,
            MCP_CALLBACK callback,
            IntPtr user_data);

        /// <summary>
        /// Check if current thread is dispatcher thread
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_dispatcher_is_thread(McpDispatcherHandle dispatcher);

        /// <summary>
        /// Create a timer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_dispatcher_create_timer(
            McpDispatcherHandle dispatcher,
            MCP_TIMER_CALLBACK callback,
            IntPtr user_data);

        /// <summary>
        /// Enable/arm a timer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_dispatcher_enable_timer(
            McpDispatcherHandle dispatcher,
            ulong timer_id,
            uint timeout_ms,
            mcp_bool_t repeat);

        /// <summary>
        /// Disable a timer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_dispatcher_disable_timer(
            McpDispatcherHandle dispatcher,
            ulong timer_id);

        /// <summary>
        /// Destroy a timer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_dispatcher_destroy_timer(
            McpDispatcherHandle dispatcher,
            ulong timer_id);

        /// <summary>
        /// Destroy dispatcher
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_dispatcher_destroy(McpDispatcherHandle dispatcher);

        /* ============================================================================
         * Connection Management
         * ============================================================================ */

        /// <summary>
        /// Extended transport configuration
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_transport_config_t
        {
            public mcp_transport_type_t type;
            
            // Transport-specific config would need proper union mapping
            // For now, using IntPtr for the union data
            public IntPtr config_data;
            
            public uint connect_timeout_ms;
            public uint idle_timeout_ms;
            public mcp_bool_t enable_keepalive;
            public uint keepalive_interval_ms;
        }

        /// <summary>
        /// Create a client connection
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpConnectionHandle mcp_connection_create_client(
            McpDispatcherHandle dispatcher,
            mcp_transport_type_t transport);

        /// <summary>
        /// Create a client connection with extended config
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpConnectionHandle mcp_connection_create_client_ex(
            McpDispatcherHandle dispatcher,
            ref mcp_transport_config_t transport_config);

        /// <summary>
        /// Create client connection with RAII guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpConnectionHandle mcp_connection_create_client_guarded(
            McpDispatcherHandle dispatcher,
            ref mcp_transport_config_t transport_config,
            out McpGuardHandle guard);

        /// <summary>
        /// Configure connection
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_configure(
            McpConnectionHandle connection,
            ref mcp_address_t address,
            ref mcp_socket_options_t options,
            ref mcp_ssl_config_t ssl_config);

        /// <summary>
        /// Set connection callbacks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_set_callbacks(
            McpConnectionHandle connection,
            MCP_CONNECTION_STATE_CALLBACK state_cb,
            MCP_DATA_CALLBACK data_cb,
            MCP_ERROR_CALLBACK error_cb,
            IntPtr user_data);

        /// <summary>
        /// Set watermarks for flow control
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_set_watermarks(
            McpConnectionHandle connection,
            ref mcp_watermark_config_t config);

        /// <summary>
        /// Connect (async)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_connect(McpConnectionHandle connection);

        /// <summary>
        /// Write data (async)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_write(
            McpConnectionHandle connection,
            IntPtr data,
            UIntPtr length,
            MCP_WRITE_CALLBACK callback,
            IntPtr user_data);

        /// <summary>
        /// Close connection
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_close(
            McpConnectionHandle connection,
            mcp_bool_t flush);

        /// <summary>
        /// Get connection state
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_connection_state_t mcp_connection_get_state(McpConnectionHandle connection);

        /// <summary>
        /// Get connection statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_connection_get_stats(
            McpConnectionHandle connection,
            out ulong bytesRead,
            out ulong bytesWritten);

        /// <summary>
        /// Destroy connection
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_connection_destroy(McpConnectionHandle connection);

        /* ============================================================================
         * Server & Listener
         * ============================================================================ */

        /// <summary>
        /// Create a listener
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpListenerHandle mcp_listener_create(
            McpDispatcherHandle dispatcher,
            mcp_transport_type_t transport);

        /// <summary>
        /// Create listener with RAII guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpListenerHandle mcp_listener_create_guarded(
            McpDispatcherHandle dispatcher,
            mcp_transport_type_t transport,
            out McpGuardHandle guard);

        /// <summary>
        /// Configure listener
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_listener_configure(
            McpListenerHandle listener,
            ref mcp_address_t address,
            ref mcp_socket_options_t options,
            ref mcp_ssl_config_t ssl_config);

        /// <summary>
        /// Set accept callback
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_listener_set_accept_callback(
            McpListenerHandle listener,
            MCP_ACCEPT_CALLBACK callback,
            IntPtr user_data);

        /// <summary>
        /// Start listening
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_listener_start(
            McpListenerHandle listener,
            int backlog);

        /// <summary>
        /// Stop listening
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_listener_stop(McpListenerHandle listener);

        /// <summary>
        /// Destroy listener
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_listener_destroy(McpListenerHandle listener);

        /* ============================================================================
         * MCP Client
         * ============================================================================ */

        /// <summary>
        /// Create MCP client
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpClientHandle mcp_client_create(
            McpDispatcherHandle dispatcher,
            ref mcp_client_config_t config);

        /// <summary>
        /// Create MCP client with RAII guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpClientHandle mcp_client_create_guarded(
            McpDispatcherHandle dispatcher,
            ref mcp_client_config_t config,
            out McpGuardHandle guard);

        /// <summary>
        /// Set MCP message callbacks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_client_set_callbacks(
            McpClientHandle client,
            MCP_REQUEST_CALLBACK request_cb,
            MCP_RESPONSE_CALLBACK response_cb,
            MCP_NOTIFICATION_CALLBACK notification_cb,
            IntPtr user_data);

        /// <summary>
        /// Connect client
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_client_connect(McpClientHandle client);

        /// <summary>
        /// Initialize protocol handshake
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_initialize(McpClientHandle client);

        /// <summary>
        /// Send request
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_send_request(
            McpClientHandle client,
            mcp_string_t method,
            McpJsonValueHandle parameters);

        /// <summary>
        /// Send notification
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_client_send_notification(
            McpClientHandle client,
            mcp_string_t method,
            McpJsonValueHandle parameters);

        /// <summary>
        /// List available tools
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_list_tools(McpClientHandle client);

        /// <summary>
        /// Call a tool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_call_tool(
            McpClientHandle client,
            mcp_string_t name,
            McpJsonValueHandle arguments);

        /// <summary>
        /// List resources
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_list_resources(McpClientHandle client);

        /// <summary>
        /// Read resource
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_read_resource(
            McpClientHandle client,
            mcp_string_t uri);

        /// <summary>
        /// List prompts
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_list_prompts(McpClientHandle client);

        /// <summary>
        /// Get prompt
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpRequestIdHandle mcp_client_get_prompt(
            McpClientHandle client,
            mcp_string_t name,
            McpMapHandle arguments);

        /// <summary>
        /// Disconnect client
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_client_disconnect(McpClientHandle client);

        /// <summary>
        /// Destroy client
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_client_destroy(McpClientHandle client);

        /* ============================================================================
         * MCP Server
         * ============================================================================ */

        /// <summary>
        /// Create MCP server
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpServerHandle mcp_server_create(
            McpDispatcherHandle dispatcher,
            ref mcp_server_config_t config);

        /// <summary>
        /// Create MCP server with RAII guard
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpServerHandle mcp_server_create_guarded(
            McpDispatcherHandle dispatcher,
            ref mcp_server_config_t config,
            out McpGuardHandle guard);

        /// <summary>
        /// Set MCP message callbacks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_set_callbacks(
            McpServerHandle server,
            MCP_REQUEST_CALLBACK request_cb,
            MCP_NOTIFICATION_CALLBACK notification_cb,
            IntPtr user_data);

        /// <summary>
        /// Register tool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_register_tool(
            McpServerHandle server,
            McpToolHandle tool);

        /// <summary>
        /// Register resource template
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_register_resource(
            McpServerHandle server,
            ref mcp_resource_template_t resource);

        /// <summary>
        /// Register prompt
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_register_prompt(
            McpServerHandle server,
            McpPromptHandle prompt);

        /// <summary>
        /// Start server
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_start(McpServerHandle server);

        /// <summary>
        /// Send response
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_send_response(
            McpServerHandle server,
            McpRequestIdHandle request_id,
            McpJsonValueHandle result);

        /// <summary>
        /// Send error response
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_send_error(
            McpServerHandle server,
            McpRequestIdHandle request_id,
            McpErrorHandle error);

        /// <summary>
        /// Send notification
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_server_send_notification(
            McpServerHandle server,
            mcp_string_t method,
            McpJsonValueHandle parameters);

        /// <summary>
        /// Stop server
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_stop(McpServerHandle server);

        /// <summary>
        /// Destroy server
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_server_destroy(McpServerHandle server);

        /* ============================================================================
         * JSON Value Management
         * ============================================================================ */

        /// <summary>
        /// Parse JSON from string
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_json_parse(mcp_string_t json);

        /// <summary>
        /// Serialize JSON to string
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_json_stringify(
            McpJsonValueHandle value,
            mcp_bool_t pretty);

        /// <summary>
        /// Clone JSON value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonValueHandle mcp_json_clone(McpJsonValueHandle value);

        /// <summary>
        /// Release JSON value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_json_release(McpJsonValueHandle value);

        /* ============================================================================
         * Utility Functions
         * ============================================================================ */

        /// <summary>
        /// Create string from C string
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_string_t mcp_string_from_cstr(
            [MarshalAs(UnmanagedType.LPStr)] string str);

        /// <summary>
        /// Create string with length
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_string_t mcp_string_from_data(
            IntPtr data,
            UIntPtr length);

        /// <summary>
        /// Duplicate string
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_string_dup(mcp_string_t str);

        /// <summary>
        /// Free string buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_string_buffer_free(IntPtr buffer);

        /// <summary>
        /// Create buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpBufferHandle mcp_buffer_create(UIntPtr capacity);

        /// <summary>
        /// Append to buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_append(
            McpBufferHandle buffer,
            IntPtr data,
            UIntPtr length);

        /// <summary>
        /// Get buffer data
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_buffer_get_data(
            McpBufferHandle buffer,
            out IntPtr outData,
            out UIntPtr outLength);

        /// <summary>
        /// Free buffer
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_buffer_free(McpBufferHandle buffer);

        /* ============================================================================
         * Resource Statistics and Debugging
         * ============================================================================ */

        /// <summary>
        /// Get resource statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_get_resource_stats(
            out UIntPtr activeCount,
            out UIntPtr totalCreated,
            out UIntPtr totalDestroyed);

        /// <summary>
        /// Check for resource leaks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_check_resource_leaks();

        /// <summary>
        /// Print resource leak report
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_print_leak_report();

        /* ============================================================================
         * Helper Methods
         * ============================================================================ */

        /// <summary>
        /// Helper to get version string
        /// </summary>
        public static string GetVersion()
        {
            var ptr = mcp_get_version();
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        /// <summary>
        /// Helper to initialize library with default allocator
        /// </summary>
        public static mcp_result_t Initialize()
        {
            return mcp_init(IntPtr.Zero);
        }

        /// <summary>
        /// Helper to check if initialized
        /// </summary>
        public static bool IsInitialized()
        {
            return mcp_is_initialized() != mcp_bool_t.False;
        }

        /// <summary>
        /// Helper to create string from managed string
        /// </summary>
        public static mcp_string_t CreateString(string str)
        {
            return str == null ? new mcp_string_t() : mcp_string_from_cstr(str);
        }

        /// <summary>
        /// Helper to duplicate string and get managed string
        /// </summary>
        public static string DuplicateString(mcp_string_t str)
        {
            var ptr = mcp_string_dup(str);
            if (ptr == IntPtr.Zero)
                return null;
                
            var result = Marshal.PtrToStringAnsi(ptr);
            mcp_string_buffer_free(ptr);
            return result;
        }

        /// <summary>
        /// Helper to write data to connection
        /// </summary>
        public static mcp_result_t ConnectionWrite(McpConnectionHandle connection, byte[] data)
        {
            if (data == null || data.Length == 0)
                return mcp_result_t.MCP_ERROR_INVALID_ARGUMENT;
                
            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return mcp_connection_write(
                    connection,
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

        /// <summary>
        /// Helper to append data to buffer
        /// </summary>
        public static mcp_result_t BufferAppend(McpBufferHandle buffer, byte[] data)
        {
            if (data == null || data.Length == 0)
                return mcp_result_t.MCP_ERROR_INVALID_ARGUMENT;
                
            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return mcp_buffer_append(
                    buffer,
                    handle.AddrOfPinnedObject(),
                    new UIntPtr((uint)data.Length));
            }
            finally
            {
                handle.Free();
            }
        }

        /// <summary>
        /// Helper to get buffer data as byte array
        /// </summary>
        public static byte[] BufferGetData(McpBufferHandle buffer)
        {
            if (buffer == null || buffer.IsInvalid)
                return null;
                
            IntPtr dataPtr;
            UIntPtr length;
            
            var result = mcp_buffer_get_data(buffer, out dataPtr, out length);
            if (result != mcp_result_t.MCP_OK || dataPtr == IntPtr.Zero)
                return null;
                
            var data = new byte[(int)length];
            Marshal.Copy(dataPtr, data, 0, data.Length);
            return data;
        }
    }
}