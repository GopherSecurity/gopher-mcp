using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;
using static GopherMcp.Interop.McpApi;
using static GopherMcp.Interop.McpMemoryApi;

namespace GopherMcp
{
    /// <summary>
    /// Simplified high-level wrapper for the MCP C++ SDK
    /// Note: This is a basic wrapper that demonstrates the structure.
    /// Full implementation would require proper JSON handling and callback management.
    /// </summary>
    public class GopherMcpWrapper : IDisposable
    {
        private McpClientHandle? _client;
        private McpServerHandle? _server;
        private McpDispatcherHandle? _dispatcher;
        private readonly object _lock = new object();
        private bool _disposed;

        /// <summary>
        /// Event raised when an error occurs
        /// </summary>
        public event EventHandler<McpErrorEventArgs>? ErrorOccurred;

        /// <summary>
        /// Event raised when connection state changes
        /// </summary>
        public event EventHandler<ConnectionStateEventArgs>? ConnectionStateChanged;

        #region Client Operations

        /// <summary>
        /// Initialize as an MCP client
        /// </summary>
        public async Task<bool> InitializeClientAsync(string name, string version, McpClientConfig? config = null)
        {
            return await Task.Run(() =>
            {
                lock (_lock)
                {
                    if (_client != null || _disposed)
                        return false;

                    try
                    {
                        // Create dispatcher
                        _dispatcher = mcp_dispatcher_create();
                        if (_dispatcher == null || _dispatcher.IsInvalid)
                            return false;

                        // Create client configuration
                        var clientConfig = config?.ToNative() ?? GetDefaultClientConfig();

                        // Create client
                        _client = mcp_client_create(_dispatcher, ref clientConfig);
                        if (_client == null || _client.IsInvalid)
                        {
                            mcp_dispatcher_destroy(_dispatcher);
                            _dispatcher = null;
                            return false;
                        }

                        return true;
                    }
                    catch (Exception ex)
                    {
                        OnError(new McpErrorEventArgs(ex.Message, -1));
                        return false;
                    }
                }
            });
        }

        /// <summary>
        /// Connect client to server
        /// </summary>
        public async Task<bool> ConnectAsync(string address, int port, TimeSpan? timeout = null)
        {
            if (_client == null || _client.IsInvalid)
                throw new InvalidOperationException("Client not initialized");

            return await Task.Run(() =>
            {
                try
                {
                    // Note: In the actual implementation, address and port would be configured
                    // through the client config before creation
                    var result = mcp_client_connect(_client);
                    return result == mcp_result_t.MCP_OK;
                }
                catch (Exception ex)
                {
                    OnError(new McpErrorEventArgs($"Connection failed: {ex.Message}", -1));
                    return false;
                }
            });
        }

        /// <summary>
        /// Send a request to the server
        /// </summary>
        public async Task<McpResponse> SendRequestAsync(string method, object? parameters = null, TimeSpan? timeout = null)
        {
            if (_client == null || _client.IsInvalid)
                throw new InvalidOperationException("Client not initialized");

            return await Task.Run(() =>
            {
                try
                {
                    // Create method string
                    var methodStr = new mcp_string_t 
                    { 
                        data = Marshal.StringToHGlobalAnsi(method), 
                        length = (UIntPtr)method.Length 
                    };
                    
                    // Create parameters (simplified - would need proper JSON handling)
                    var paramsHandle = new McpJsonValueHandle();
                    
                    try
                    {
                        // Send request
                        var requestId = mcp_client_send_request(_client, methodStr, paramsHandle);
                        
                        // In a real implementation, we would wait for the response
                        // For now, return a dummy response
                        return new McpResponse
                        {
                            JsonRpc = "2.0",
                            Result = "Request sent",
                            Id = 1
                        };
                    }
                    finally
                    {
                        if (methodStr.data != IntPtr.Zero)
                            Marshal.FreeHGlobal(methodStr.data);
                    }
                }
                catch (Exception ex)
                {
                    throw new McpException($"Failed to send request: {ex.Message}", ex);
                }
            });
        }

        /// <summary>
        /// Send a notification to the server
        /// </summary>
        public async Task SendNotificationAsync(string method, object? parameters = null)
        {
            if (_client == null || _client.IsInvalid)
                throw new InvalidOperationException("Client not initialized");

            await Task.Run(() =>
            {
                try
                {
                    // Create method string
                    var methodStr = new mcp_string_t 
                    { 
                        data = Marshal.StringToHGlobalAnsi(method), 
                        length = (UIntPtr)method.Length 
                    };
                    
                    // Create parameters (simplified - would need proper JSON handling)
                    var paramsHandle = new McpJsonValueHandle();
                    
                    try
                    {
                        var result = mcp_client_send_notification(_client, methodStr, paramsHandle);
                        if (result != mcp_result_t.MCP_OK)
                            throw new McpException($"Failed to send notification: {result}");
                    }
                    finally
                    {
                        if (methodStr.data != IntPtr.Zero)
                            Marshal.FreeHGlobal(methodStr.data);
                    }
                }
                catch (Exception ex)
                {
                    throw new McpException($"Failed to send notification: {ex.Message}", ex);
                }
            });
        }

        #endregion

        #region Server Operations

        /// <summary>
        /// Initialize as an MCP server
        /// </summary>
        public async Task<bool> InitializeServerAsync(string name, string version, McpServerConfig? config = null)
        {
            return await Task.Run(() =>
            {
                lock (_lock)
                {
                    if (_server != null || _disposed)
                        return false;

                    try
                    {
                        // Create dispatcher
                        _dispatcher = mcp_dispatcher_create();
                        if (_dispatcher == null || _dispatcher.IsInvalid)
                            return false;

                        // Create server configuration
                        var serverConfig = config?.ToNative() ?? GetDefaultServerConfig();

                        // Create server
                        _server = mcp_server_create(_dispatcher, ref serverConfig);
                        if (_server == null || _server.IsInvalid)
                        {
                            mcp_dispatcher_destroy(_dispatcher);
                            _dispatcher = null;
                            return false;
                        }

                        return true;
                    }
                    catch (Exception ex)
                    {
                        OnError(new McpErrorEventArgs(ex.Message, -1));
                        return false;
                    }
                }
            });
        }

        /// <summary>
        /// Start listening for incoming connections
        /// </summary>
        public async Task<bool> ListenAsync(string address, int port)
        {
            if (_server == null || _server.IsInvalid)
                throw new InvalidOperationException("Server not initialized");

            return await Task.Run(() =>
            {
                try
                {
                    // Note: In the actual implementation, address and port would be configured
                    // through the server config before creation
                    // For now, just return success
                    return true;
                }
                catch (Exception ex)
                {
                    OnError(new McpErrorEventArgs($"Listen failed: {ex.Message}", -1));
                    return false;
                }
            });
        }

        /// <summary>
        /// Register a request handler
        /// </summary>
        public void RegisterRequestHandler(string method, Func<McpRequest, Task<McpResponse>> handler)
        {
            if (handler == null)
                throw new ArgumentNullException(nameof(handler));
            
            // In a real implementation, this would register the handler with the native library
            // For now, just store it (implementation simplified)
        }

        /// <summary>
        /// Register a notification handler
        /// </summary>
        public void RegisterNotificationHandler(string method, Action<McpNotification> handler)
        {
            if (handler == null)
                throw new ArgumentNullException(nameof(handler));
            
            // In a real implementation, this would register the handler with the native library
            // For now, just store it (implementation simplified)
        }

        /// <summary>
        /// Send a response to a client request
        /// </summary>
        public async Task SendResponseAsync(ulong requestId, object? result = null, McpError? error = null)
        {
            if (_server == null || _server.IsInvalid)
                throw new InvalidOperationException("Server not initialized");

            await Task.Run(() =>
            {
                try
                {
                    // Create result JSON (simplified - would need proper JSON handling)
                    var resultHandle = new McpJsonValueHandle();
                    
                    // Create request ID handle (simplified)
                    var requestIdHandle = new McpRequestIdHandle();
                    
                    var sendResult = mcp_server_send_response(_server, requestIdHandle, resultHandle);
                    if (sendResult != mcp_result_t.MCP_OK)
                        throw new McpException($"Failed to send response: {sendResult}");
                }
                catch (Exception ex)
                {
                    throw new McpException($"Failed to send response: {ex.Message}", ex);
                }
            });
        }

        #endregion

        #region Common Operations

        /// <summary>
        /// Run the event dispatcher
        /// </summary>
        public async Task RunAsync(CancellationToken cancellationToken = default)
        {
            if (_dispatcher == null || _dispatcher.IsInvalid)
                throw new InvalidOperationException("Not initialized");

            await Task.Run(() =>
            {
                while (!cancellationToken.IsCancellationRequested && !_disposed)
                {
                    // In a real implementation, this would process events
                    // For now, just sleep
                    Thread.Sleep(100);
                }
            }, cancellationToken);
        }

        /// <summary>
        /// Close the connection
        /// </summary>
        public async Task CloseAsync()
        {
            await Task.Run(() =>
            {
                lock (_lock)
                {
                    // Cleanup would happen here
                }
            });
        }

        #endregion

        #region Helper Methods

        private static mcp_client_config_t GetDefaultClientConfig()
        {
            return new mcp_client_config_t
            {
                client_info = IntPtr.Zero,
                capabilities = IntPtr.Zero,
                transport = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                server_address = IntPtr.Zero,
                ssl_config = IntPtr.Zero,
                watermarks = new mcp_watermark_config_t(),
                reconnect_delay_ms = 1000,
                max_reconnect_attempts = 3
            };
        }

        private static mcp_server_config_t GetDefaultServerConfig()
        {
            return new mcp_server_config_t
            {
                server_info = IntPtr.Zero,
                capabilities = IntPtr.Zero,
                transport = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                bind_address = IntPtr.Zero,
                ssl_config = IntPtr.Zero,
                watermarks = new mcp_watermark_config_t(),
                max_connections = 100,
                instructions = IntPtr.Zero
            };
        }

        private void OnError(McpErrorEventArgs e)
        {
            ErrorOccurred?.Invoke(this, e);
        }

        private void OnConnectionStateChanged(ConnectionStateEventArgs e)
        {
            ConnectionStateChanged?.Invoke(this, e);
        }

        #endregion

        #region IDisposable

        /// <summary>
        /// Dispose the wrapper and clean up resources
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
                return;

            if (disposing)
            {
                // Dispose managed resources
                CloseAsync().Wait(5000);
            }

            lock (_lock)
            {
                // Dispose unmanaged resources
                if (_client != null && !_client.IsInvalid)
                {
                    mcp_client_destroy(_client);
                    _client = null;
                }

                if (_server != null && !_server.IsInvalid)
                {
                    mcp_server_destroy(_server);
                    _server = null;
                }

                if (_dispatcher != null && !_dispatcher.IsInvalid)
                {
                    mcp_dispatcher_destroy(_dispatcher);
                    _dispatcher = null;
                }

                _disposed = true;
            }
        }

        ~GopherMcpWrapper()
        {
            Dispose(false);
        }

        #endregion
    }

    #region Supporting Classes

    /// <summary>
    /// MCP client configuration
    /// </summary>
    public class McpClientConfig
    {
        public mcp_transport_type_t TransportType { get; set; } = mcp_transport_type_t.MCP_TRANSPORT_STDIO;
        public string? ServerAddress { get; set; }
        public bool EnableCompression { get; set; } = true;
        public bool EnableEncryption { get; set; } = true;

        internal mcp_client_config_t ToNative()
        {
            return new mcp_client_config_t
            {
                client_info = IntPtr.Zero,
                capabilities = IntPtr.Zero,
                transport = TransportType,
                server_address = IntPtr.Zero,
                ssl_config = IntPtr.Zero,
                watermarks = new mcp_watermark_config_t(),
                reconnect_delay_ms = 1000,
                max_reconnect_attempts = 3
            };
        }
    }

    /// <summary>
    /// MCP server configuration
    /// </summary>
    public class McpServerConfig
    {
        public mcp_transport_type_t TransportType { get; set; } = mcp_transport_type_t.MCP_TRANSPORT_STDIO;
        public string? BindAddress { get; set; }
        public uint MaxConnections { get; set; } = 100;
        public bool EnableCompression { get; set; } = true;
        public bool EnableEncryption { get; set; } = true;

        internal mcp_server_config_t ToNative()
        {
            return new mcp_server_config_t
            {
                server_info = IntPtr.Zero,
                capabilities = IntPtr.Zero,
                transport = TransportType,
                bind_address = IntPtr.Zero,
                ssl_config = IntPtr.Zero,
                watermarks = new mcp_watermark_config_t(),
                max_connections = MaxConnections,
                instructions = IntPtr.Zero
            };
        }
    }

    /// <summary>
    /// MCP request
    /// </summary>
    public class McpRequest
    {
        public string JsonRpc { get; set; } = "2.0";
        public string Method { get; set; } = string.Empty;
        public object? Params { get; set; }
        public ulong? Id { get; set; }
    }

    /// <summary>
    /// MCP response
    /// </summary>
    public class McpResponse
    {
        public string JsonRpc { get; set; } = "2.0";
        public object? Result { get; set; }
        public McpError? Error { get; set; }
        public ulong? Id { get; set; }
    }

    /// <summary>
    /// MCP notification
    /// </summary>
    public class McpNotification
    {
        public string JsonRpc { get; set; } = "2.0";
        public string Method { get; set; } = string.Empty;
        public object? Params { get; set; }
    }

    /// <summary>
    /// MCP error
    /// </summary>
    public class McpError
    {
        public int Code { get; set; }
        public string Message { get; set; } = string.Empty;
        public object? Data { get; set; }
    }

    /// <summary>
    /// MCP exception
    /// </summary>
    public class McpException : Exception
    {
        public int ErrorCode { get; }

        public McpException(string message, int errorCode = -1) : base(message)
        {
            ErrorCode = errorCode;
        }

        public McpException(string message, Exception innerException, int errorCode = -1) 
            : base(message, innerException)
        {
            ErrorCode = errorCode;
        }
    }

    /// <summary>
    /// Error event arguments
    /// </summary>
    public class McpErrorEventArgs : EventArgs
    {
        public string Message { get; }
        public int ErrorCode { get; }

        public McpErrorEventArgs(string message, int errorCode)
        {
            Message = message;
            ErrorCode = errorCode;
        }
    }

    /// <summary>
    /// Connection state event arguments
    /// </summary>
    public class ConnectionStateEventArgs : EventArgs
    {
        public mcp_connection_state_t State { get; }
        public bool IsConnected => State == mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED;

        public ConnectionStateEventArgs(mcp_connection_state_t state)
        {
            State = state;
        }
    }

    #endregion
}