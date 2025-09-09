using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Transport;

namespace GopherMcp.Integration
{
    /// <summary>
    /// MCP server wrapper for handling requests
    /// </summary>
    public class McpServer : IDisposable
    {
        private readonly ITransport _transport;
        private readonly ConcurrentDictionary<string, MethodHandler> _methodHandlers;
        private readonly ConcurrentDictionary<string, ToolProvider> _toolProviders;
        private readonly ConcurrentDictionary<string, PromptProvider> _promptProviders;
        private readonly ConcurrentDictionary<string, ResourceProvider> _resourceProviders;
        private CancellationTokenSource? _receiveCancellationSource;
        private Task? _receiveTask;
        private bool _disposed;

        /// <summary>
        /// Event raised when an error occurs
        /// </summary>
        public event EventHandler<ErrorEventArgs>? ErrorOccurred;

        /// <summary>
        /// Gets whether the server is running
        /// </summary>
        public bool IsRunning => _transport?.IsConnected ?? false;

        /// <summary>
        /// Server information
        /// </summary>
        public ServerInfo Info { get; set; } = new();

        /// <summary>
        /// Creates a new MCP server
        /// </summary>
        public McpServer(ITransport transport)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
            _methodHandlers = new ConcurrentDictionary<string, MethodHandler>();
            _toolProviders = new ConcurrentDictionary<string, ToolProvider>();
            _promptProviders = new ConcurrentDictionary<string, PromptProvider>();
            _resourceProviders = new ConcurrentDictionary<string, ResourceProvider>();

            // Register built-in MCP methods
            RegisterBuiltInMethods();

            // Subscribe to transport events
            _transport.MessageReceived += OnTransportMessageReceived;
            _transport.Error += OnTransportError;
        }

        /// <summary>
        /// Starts the server
        /// </summary>
        public async Task StartAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            await _transport.StartAsync(cancellationToken);

            // Start receive loop
            _receiveCancellationSource = new CancellationTokenSource();
            _receiveTask = Task.Run(() => ReceiveLoop(_receiveCancellationSource.Token));
        }

        /// <summary>
        /// Stops the server
        /// </summary>
        public async Task StopAsync(CancellationToken cancellationToken = default)
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
        }

        /// <summary>
        /// Registers a method handler
        /// </summary>
        public void RegisterMethod(string method, Func<JsonRpcMessage, Task<object?>> handler)
        {
            ThrowIfDisposed();

            if (string.IsNullOrWhiteSpace(method))
                throw new ArgumentException("Method name cannot be empty", nameof(method));

            _methodHandlers[method] = new MethodHandler(handler);
        }

        /// <summary>
        /// Registers a typed method handler
        /// </summary>
        public void RegisterMethod<TParams, TResult>(string method, Func<TParams?, Task<TResult?>> handler)
        {
            RegisterMethod(method, async (message) =>
            {
                TParams? parameters = default;

                if (message.Params != null)
                {
                    if (message.Params is System.Text.Json.JsonElement jsonElement)
                    {
                        parameters = jsonElement.Deserialize<TParams>();
                    }
                    else
                    {
                        parameters = (TParams)message.Params;
                    }
                }

                var result = await handler(parameters);
                return result;
            });
        }

        /// <summary>
        /// Unregisters a method handler
        /// </summary>
        public bool UnregisterMethod(string method)
        {
            ThrowIfDisposed();
            return _methodHandlers.TryRemove(method, out _);
        }

        /// <summary>
        /// Registers a tool provider
        /// </summary>
        public void RegisterTool(string name, ToolProvider provider)
        {
            ThrowIfDisposed();

            if (string.IsNullOrWhiteSpace(name))
                throw new ArgumentException("Tool name cannot be empty", nameof(name));

            _toolProviders[name] = provider ?? throw new ArgumentNullException(nameof(provider));
        }

        /// <summary>
        /// Registers a simple tool
        /// </summary>
        public void RegisterTool<TArgs, TResult>(string name, string? description, Func<TArgs?, Task<TResult?>> handler)
        {
            var provider = new ToolProvider
            {
                Name = name,
                Description = description,
                InputSchema = typeof(TArgs),
                Handler = async (args) =>
                {
                    TArgs? typedArgs = default;
                    if (args != null)
                    {
                        if (args is System.Text.Json.JsonElement jsonElement)
                        {
                            typedArgs = jsonElement.Deserialize<TArgs>();
                        }
                        else
                        {
                            typedArgs = (TArgs)args;
                        }
                    }

                    var result = await handler(typedArgs);
                    return result;
                }
            };

            RegisterTool(name, provider);
        }

        /// <summary>
        /// Registers a prompt provider
        /// </summary>
        public void RegisterPrompt(string name, PromptProvider provider)
        {
            ThrowIfDisposed();

            if (string.IsNullOrWhiteSpace(name))
                throw new ArgumentException("Prompt name cannot be empty", nameof(name));

            _promptProviders[name] = provider ?? throw new ArgumentNullException(nameof(provider));
        }

        /// <summary>
        /// Registers a resource provider
        /// </summary>
        public void RegisterResource(string uri, ResourceProvider provider)
        {
            ThrowIfDisposed();

            if (string.IsNullOrWhiteSpace(uri))
                throw new ArgumentException("Resource URI cannot be empty", nameof(uri));

            _resourceProviders[uri] = provider ?? throw new ArgumentNullException(nameof(provider));
        }

        private void RegisterBuiltInMethods()
        {
            // Initialize method
            RegisterMethod("initialize", async (message) =>
            {
                var result = new
                {
                    protocolVersion = "2024-11-05",
                    capabilities = new
                    {
                        tools = new { },
                        prompts = new { },
                        resources = new { }
                    },
                    serverInfo = new
                    {
                        name = Info.Name,
                        version = Info.Version
                    }
                };

                return await Task.FromResult(result);
            });

            // Tool discovery
            RegisterMethod("tools/list", async (message) =>
            {
                var tools = _toolProviders.Values.Select(t => new
                {
                    name = t.Name,
                    description = t.Description,
                    inputSchema = t.InputSchema
                }).ToList();

                return await Task.FromResult(new { tools });
            });

            // Tool invocation
            RegisterMethod("tools/call", async (message) =>
            {
                var parameters = message.Params as dynamic;
                string? toolName = parameters?.name;

                if (string.IsNullOrEmpty(toolName))
                {
                    throw new JsonRpcException(JsonRpcError.InvalidParams("Tool name is required"));
                }

                if (!_toolProviders.TryGetValue(toolName, out var provider))
                {
                    throw new JsonRpcException(JsonRpcError.MethodNotFound($"Tool '{toolName}' not found"));
                }

                var arguments = parameters?.arguments;
                var result = await provider.Handler(arguments);

                return new
                {
                    content = new[]
                    {
                        new { type = "text", text = result?.ToString() ?? "" }
                    }
                };
            });

            // Prompt discovery
            RegisterMethod("prompts/list", async (message) =>
            {
                var prompts = _promptProviders.Values.Select(p => new
                {
                    name = p.Name,
                    description = p.Description,
                    arguments = p.Arguments?.Select(a => new
                    {
                        name = a.Name,
                        description = a.Description,
                        required = a.Required
                    })
                }).ToList();

                return await Task.FromResult(new { prompts });
            });

            // Prompt retrieval
            RegisterMethod("prompts/get", async (message) =>
            {
                var parameters = message.Params as dynamic;
                string? promptName = parameters?.name;

                if (string.IsNullOrEmpty(promptName))
                {
                    throw new JsonRpcException(JsonRpcError.InvalidParams("Prompt name is required"));
                }

                if (!_promptProviders.TryGetValue(promptName, out var provider))
                {
                    throw new JsonRpcException(JsonRpcError.MethodNotFound($"Prompt '{promptName}' not found"));
                }

                var arguments = parameters?.arguments;
                var result = await provider.Handler(arguments);

                return result;
            });

            // Resource discovery
            RegisterMethod("resources/list", async (message) =>
            {
                var resources = _resourceProviders.Values.Select(r => new
                {
                    uri = r.Uri,
                    name = r.Name,
                    description = r.Description,
                    mimeType = r.MimeType
                }).ToList();

                return await Task.FromResult(new { resources });
            });

            // Resource reading
            RegisterMethod("resources/read", async (message) =>
            {
                var parameters = message.Params as dynamic;
                string? uri = parameters?.uri;

                if (string.IsNullOrEmpty(uri))
                {
                    throw new JsonRpcException(JsonRpcError.InvalidParams("Resource URI is required"));
                }

                if (!_resourceProviders.TryGetValue(uri, out var provider))
                {
                    throw new JsonRpcException(new JsonRpcError(
                        JsonRpcErrorCodes.ResourceNotFound,
                        $"Resource '{uri}' not found"));
                }

                var result = await provider.Handler();

                return new
                {
                    contents = new[]
                    {
                        new
                        {
                            uri = uri,
                            mimeType = provider.MimeType,
                            text = result?.ToString()
                        }
                    }
                };
            });
        }

        private async Task ReceiveLoop(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested && IsRunning)
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
                if (message.IsRequest)
                {
                    await HandleRequest(message);
                }
                else if (message.IsNotification)
                {
                    await HandleNotification(message);
                }
                // Server shouldn't receive responses, but ignore gracefully
            }
            catch (Exception ex)
            {
                OnError(ex, "Error handling received message");

                // Send error response if it was a request
                if (message.IsRequest && message.Id != null)
                {
                    var errorResponse = JsonRpcMessage.CreateErrorResponse(
                        message.Id,
                        JsonRpcErrorCodes.InternalError,
                        ex.Message);

                    try
                    {
                        await _transport.SendAsync(errorResponse);
                    }
                    catch { }
                }
            }
        }

        private async Task HandleRequest(JsonRpcMessage request)
        {
            JsonRpcMessage response;

            try
            {
                if (string.IsNullOrEmpty(request.Method))
                {
                    throw new JsonRpcException(JsonRpcError.InvalidRequest("Method is required"));
                }

                if (!_methodHandlers.TryGetValue(request.Method, out var handler))
                {
                    throw new JsonRpcException(JsonRpcError.MethodNotFound(request.Method));
                }

                var result = await handler.Handler(request);
                response = JsonRpcMessage.CreateResponse(request.Id, result);
            }
            catch (JsonRpcException ex)
            {
                response = JsonRpcMessage.CreateErrorResponse(
                    request.Id,
                    ex.Error.Code,
                    ex.Error.Message,
                    ex.Error.Data);
            }
            catch (Exception ex)
            {
                response = JsonRpcMessage.CreateErrorResponse(
                    request.Id,
                    JsonRpcErrorCodes.InternalError,
                    ex.Message);
            }

            await _transport.SendAsync(response);
        }

        private async Task HandleNotification(JsonRpcMessage notification)
        {
            if (string.IsNullOrEmpty(notification.Method))
                return;

            if (_methodHandlers.TryGetValue(notification.Method, out var handler))
            {
                try
                {
                    await handler.Handler(notification);
                }
                catch (Exception ex)
                {
                    OnError(ex, $"Error handling notification '{notification.Method}'");
                }
            }
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

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(McpServer));
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                try
                {
                    StopAsync().GetAwaiter().GetResult();
                }
                catch { }

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

        private class MethodHandler
        {
            public Func<JsonRpcMessage, Task<object?>> Handler { get; }

            public MethodHandler(Func<JsonRpcMessage, Task<object?>> handler)
            {
                Handler = handler;
            }
        }
    }

    /// <summary>
    /// Server information
    /// </summary>
    public class ServerInfo
    {
        public string Name { get; set; } = "GopherMcp Server";
        public string Version { get; set; } = "1.0.0";
    }

    /// <summary>
    /// Tool provider
    /// </summary>
    public class ToolProvider
    {
        public string Name { get; set; } = string.Empty;
        public string? Description { get; set; }
        public object? InputSchema { get; set; }
        public Func<object?, Task<object?>> Handler { get; set; } = _ => Task.FromResult<object?>(null);
    }

    /// <summary>
    /// Prompt provider
    /// </summary>
    public class PromptProvider
    {
        public string Name { get; set; } = string.Empty;
        public string? Description { get; set; }
        public List<PromptArgument>? Arguments { get; set; }
        public Func<object?, Task<object?>> Handler { get; set; } = _ => Task.FromResult<object?>(null);
    }

    /// <summary>
    /// Resource provider
    /// </summary>
    public class ResourceProvider
    {
        public string Uri { get; set; } = string.Empty;
        public string Name { get; set; } = string.Empty;
        public string? Description { get; set; }
        public string MimeType { get; set; } = "text/plain";
        public Func<Task<object?>> Handler { get; set; } = () => Task.FromResult<object?>(null);
    }
}
