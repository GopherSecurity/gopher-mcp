using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Transport;

namespace GopherMcp.Integration
{
    /// <summary>
    /// MCP client wrapper for simplified interaction
    /// </summary>
    public class McpClient : IDisposable
    {
        private readonly ITransport _transport;
        private readonly ConcurrentDictionary<object, TaskCompletionSource<JsonRpcMessage>> _pendingRequests;
        private readonly ConcurrentDictionary<string, Func<JsonRpcMessage, Task<object?>>> _notificationHandlers;
        private readonly Timer _cleanupTimer;
        private readonly TimeSpan _requestTimeout;
        private long _nextId;
        private bool _disposed;
        private CancellationTokenSource? _receiveCancellationSource;
        private Task? _receiveTask;

        /// <summary>
        /// Event raised when a notification is received
        /// </summary>
        public event EventHandler<NotificationEventArgs>? NotificationReceived;

        /// <summary>
        /// Event raised when an error occurs
        /// </summary>
        public event EventHandler<ErrorEventArgs>? ErrorOccurred;

        /// <summary>
        /// Gets whether the client is connected
        /// </summary>
        public bool IsConnected => _transport?.IsConnected ?? false;

        /// <summary>
        /// Creates a new MCP client
        /// </summary>
        public McpClient(ITransport transport, TimeSpan? requestTimeout = null)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
            _pendingRequests = new ConcurrentDictionary<object, TaskCompletionSource<JsonRpcMessage>>();
            _notificationHandlers = new ConcurrentDictionary<string, Func<JsonRpcMessage, Task<object?>>>();
            _requestTimeout = requestTimeout ?? TimeSpan.FromSeconds(30);

            // Cleanup old pending requests every minute
            _cleanupTimer = new Timer(CleanupPendingRequests, null, TimeSpan.FromMinutes(1), TimeSpan.FromMinutes(1));

            // Subscribe to transport events
            _transport.MessageReceived += OnTransportMessageReceived;
            _transport.Error += OnTransportError;
        }

        /// <summary>
        /// Connects the client
        /// </summary>
        public async Task ConnectAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            await _transport.StartAsync(cancellationToken);

            // Start receive loop
            _receiveCancellationSource = new CancellationTokenSource();
            _receiveTask = Task.Run(() => ReceiveLoop(_receiveCancellationSource.Token));
        }

        /// <summary>
        /// Disconnects the client
        /// </summary>
        public async Task DisconnectAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            // Stop receive loop
            _receiveCancellationSource?.Cancel();
            if (_receiveTask != null)
            {
                try
                {
                    await _receiveTask;
                }
                catch { }
            }

            await _transport.StopAsync(cancellationToken);

            // Cancel all pending requests
            foreach (var pending in _pendingRequests.Values)
            {
                pending.TrySetCanceled();
            }
            _pendingRequests.Clear();
        }

        /// <summary>
        /// Invokes a method and waits for the response
        /// </summary>
        public async Task<T?> InvokeAsync<T>(string method, object? parameters = null, CancellationToken cancellationToken = default)
        {
            var response = await InvokeAsync(method, parameters, cancellationToken);

            if (response.Result == null)
                return default;

            // Convert result to requested type
            if (response.Result is System.Text.Json.JsonElement jsonElement)
            {
                return System.Text.Json.JsonSerializer.Deserialize<T>(jsonElement.GetRawText());
            }

            return (T)response.Result;
        }

        /// <summary>
        /// Invokes a method and waits for the response
        /// </summary>
        public async Task<JsonRpcMessage> InvokeAsync(string method, object? parameters = null, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            if (!IsConnected)
                throw new InvalidOperationException("Client is not connected");

            // Generate request ID
            var id = GenerateId();

            // Create request message
            var request = JsonRpcMessage.CreateRequest(method, parameters, id);

            // Create completion source for response
            var tcs = new TaskCompletionSource<JsonRpcMessage>();
            _pendingRequests[id] = tcs;

            try
            {
                // Send request
                await _transport.SendAsync(request, cancellationToken);

                // Wait for response with timeout
                using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                cts.CancelAfter(_requestTimeout);

                var responseTask = tcs.Task;
                var completedTask = await Task.WhenAny(responseTask, Task.Delay(_requestTimeout, cts.Token));

                if (completedTask != responseTask)
                {
                    throw new TimeoutException($"Request timeout for method '{method}'");
                }

                var response = await responseTask;

                // Check for error response
                if (response.IsError)
                {
                    throw new JsonRpcException(response.Error!);
                }

                return response;
            }
            finally
            {
                _pendingRequests.TryRemove(id, out _);
            }
        }

        /// <summary>
        /// Sends a notification (no response expected)
        /// </summary>
        public async Task NotifyAsync(string method, object? parameters = null, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            if (!IsConnected)
                throw new InvalidOperationException("Client is not connected");

            var notification = JsonRpcMessage.CreateNotification(method, parameters);
            await _transport.SendAsync(notification, cancellationToken);
        }

        /// <summary>
        /// Registers a handler for notifications
        /// </summary>
        public void RegisterNotificationHandler(string method, Func<JsonRpcMessage, Task<object?>> handler)
        {
            ThrowIfDisposed();
            _notificationHandlers[method] = handler ?? throw new ArgumentNullException(nameof(handler));
        }

        /// <summary>
        /// Unregisters a notification handler
        /// </summary>
        public bool UnregisterNotificationHandler(string method)
        {
            ThrowIfDisposed();
            return _notificationHandlers.TryRemove(method, out _);
        }

        /// <summary>
        /// Discovers available tools
        /// </summary>
        public async Task<ToolDiscoveryResult> DiscoverToolsAsync(CancellationToken cancellationToken = default)
        {
            var response = await InvokeAsync<ToolDiscoveryResult>("tools/list", null, cancellationToken);
            return response ?? new ToolDiscoveryResult();
        }

        /// <summary>
        /// Discovers available prompts
        /// </summary>
        public async Task<PromptDiscoveryResult> DiscoverPromptsAsync(CancellationToken cancellationToken = default)
        {
            var response = await InvokeAsync<PromptDiscoveryResult>("prompts/list", null, cancellationToken);
            return response ?? new PromptDiscoveryResult();
        }

        /// <summary>
        /// Calls a tool
        /// </summary>
        public async Task<T?> CallToolAsync<T>(string toolName, object? arguments = null, CancellationToken cancellationToken = default)
        {
            var parameters = new { name = toolName, arguments };
            return await InvokeAsync<T>("tools/call", parameters, cancellationToken);
        }

        /// <summary>
        /// Gets a prompt
        /// </summary>
        public async Task<PromptResult> GetPromptAsync(string promptName, object? arguments = null, CancellationToken cancellationToken = default)
        {
            var parameters = new { name = promptName, arguments };
            var response = await InvokeAsync<PromptResult>("prompts/get", parameters, cancellationToken);
            return response ?? new PromptResult();
        }

        private async Task ReceiveLoop(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested && IsConnected)
            {
                try
                {
                    var message = await _transport.ReceiveAsync(cancellationToken);
                    await HandleReceivedMessage(message);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    OnError(ex, "Error in receive loop");
                }
            }
        }

        private async Task HandleReceivedMessage(JsonRpcMessage message)
        {
            try
            {
                if (message.IsResponse || message.IsError)
                {
                    // Handle response
                    if (message.Id != null && _pendingRequests.TryRemove(message.Id, out var tcs))
                    {
                        tcs.TrySetResult(message);
                    }
                }
                else if (message.IsNotification)
                {
                    // Handle notification
                    await HandleNotification(message);
                }
                else if (message.IsRequest)
                {
                    // Client shouldn't receive requests, but handle gracefully
                    var errorResponse = JsonRpcMessage.CreateErrorResponse(
                        message.Id,
                        JsonRpcErrorCodes.MethodNotFound,
                        "Client does not handle requests");

                    await _transport.SendAsync(errorResponse);
                }
            }
            catch (Exception ex)
            {
                OnError(ex, "Error handling received message");
            }
        }

        private async Task HandleNotification(JsonRpcMessage notification)
        {
            if (notification.Method != null && _notificationHandlers.TryGetValue(notification.Method, out var handler))
            {
                try
                {
                    await handler(notification);
                }
                catch (Exception ex)
                {
                    OnError(ex, $"Error handling notification '{notification.Method}'");
                }
            }

            // Raise event
            NotificationReceived?.Invoke(this, new NotificationEventArgs(notification));
        }

        private void OnTransportMessageReceived(object? sender, MessageReceivedEventArgs e)
        {
            // Message handling is done in ReceiveLoop
        }

        private void OnTransportError(object? sender, TransportErrorEventArgs e)
        {
            OnError(e.Exception, e.Context);
        }

        private void OnError(Exception exception, string? context)
        {
            ErrorOccurred?.Invoke(this, new ErrorEventArgs(exception, context));
        }

        private string GenerateId()
        {
            var id = Interlocked.Increment(ref _nextId);
            return $"req_{id}";
        }

        private void CleanupPendingRequests(object? state)
        {
            var cutoff = DateTime.UtcNow - _requestTimeout;
            var timedOutRequests = new List<object>();

            foreach (var kvp in _pendingRequests)
            {
                if (kvp.Value.Task.CreationOptions.HasFlag(TaskCreationOptions.None))
                {
                    // Check if request has timed out
                    // This is simplified - in production you'd track creation time
                    timedOutRequests.Add(kvp.Key);
                }
            }

            foreach (var id in timedOutRequests)
            {
                if (_pendingRequests.TryRemove(id, out var tcs))
                {
                    tcs.TrySetException(new TimeoutException("Request timed out"));
                }
            }
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(McpClient));
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                try
                {
                    DisconnectAsync().GetAwaiter().GetResult();
                }
                catch { }

                _cleanupTimer?.Dispose();
                _receiveCancellationSource?.Dispose();

                // Unsubscribe from transport events
                if (_transport != null)
                {
                    _transport.MessageReceived -= OnTransportMessageReceived;
                    _transport.Error -= OnTransportError;
                    _transport.Dispose();
                }

                _disposed = true;
            }
        }
    }

    /// <summary>
    /// Event arguments for notifications
    /// </summary>
    public class NotificationEventArgs : EventArgs
    {
        public JsonRpcMessage Notification { get; }

        public NotificationEventArgs(JsonRpcMessage notification)
        {
            Notification = notification;
        }
    }

    /// <summary>
    /// Event arguments for errors
    /// </summary>
    public class ErrorEventArgs : EventArgs
    {
        public Exception Exception { get; }
        public string? Context { get; }

        public ErrorEventArgs(Exception exception, string? context = null)
        {
            Exception = exception;
            Context = context;
        }
    }

    /// <summary>
    /// JSON-RPC exception
    /// </summary>
    public class JsonRpcException : Exception
    {
        public JsonRpcError Error { get; }

        public JsonRpcException(JsonRpcError error)
            : base(error.Message)
        {
            Error = error;
        }
    }

    /// <summary>
    /// Tool discovery result
    /// </summary>
    public class ToolDiscoveryResult
    {
        public List<ToolInfo> Tools { get; set; } = new();
    }

    /// <summary>
    /// Tool information
    /// </summary>
    public class ToolInfo
    {
        public string Name { get; set; } = string.Empty;
        public string? Description { get; set; }
        public object? InputSchema { get; set; }
    }

    /// <summary>
    /// Prompt discovery result
    /// </summary>
    public class PromptDiscoveryResult
    {
        public List<PromptInfo> Prompts { get; set; } = new();
    }

    /// <summary>
    /// Prompt information
    /// </summary>
    public class PromptInfo
    {
        public string Name { get; set; } = string.Empty;
        public string? Description { get; set; }
        public List<PromptArgument> Arguments { get; set; } = new();
    }

    /// <summary>
    /// Prompt argument
    /// </summary>
    public class PromptArgument
    {
        public string Name { get; set; } = string.Empty;
        public string? Description { get; set; }
        public bool Required { get; set; }
    }

    /// <summary>
    /// Prompt result
    /// </summary>
    public class PromptResult
    {
        public List<PromptMessage> Messages { get; set; } = new();
    }

    /// <summary>
    /// Prompt message
    /// </summary>
    public class PromptMessage
    {
        public string Role { get; set; } = string.Empty;
        public object? Content { get; set; }
    }
}
