using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Transport;

namespace GopherMcp.Integration
{
    /// <summary>
    /// Extension methods for MCP integration
    /// </summary>
    public static class McpExtensions
    {
        /// <summary>
        /// Creates an MCP client from a transport
        /// </summary>
        public static McpClient CreateClient(this ITransport transport, TimeSpan? requestTimeout = null)
        {
            return new McpClient(transport, requestTimeout);
        }

        /// <summary>
        /// Creates an MCP server from a transport
        /// </summary>
        public static McpServer CreateServer(this ITransport transport)
        {
            return new McpServer(transport);
        }

        /// <summary>
        /// Sends a request and waits for response
        /// </summary>
        public static async Task<JsonRpcMessage> RequestAsync(
            this ITransport transport,
            string method,
            object? parameters = null,
            CancellationToken cancellationToken = default)
        {
            var request = JsonRpcMessage.CreateRequest(method, parameters);
            await transport.SendAsync(request, cancellationToken);

            // Wait for response with matching ID
            var timeout = TimeSpan.FromSeconds(30);
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            cts.CancelAfter(timeout);

            while (!cts.Token.IsCancellationRequested)
            {
                var message = await transport.ReceiveAsync(cts.Token);
                if (message.Id?.ToString() == request.Id?.ToString())
                {
                    return message;
                }
            }

            throw new TimeoutException($"Timeout waiting for response to request {request.Id}");
        }

        /// <summary>
        /// Sends a notification
        /// </summary>
        public static Task NotifyAsync(
            this ITransport transport,
            string method,
            object? parameters = null,
            CancellationToken cancellationToken = default)
        {
            var notification = JsonRpcMessage.CreateNotification(method, parameters);
            return transport.SendAsync(notification, cancellationToken);
        }

        /// <summary>
        /// Sends a response
        /// </summary>
        public static Task RespondAsync(
            this ITransport transport,
            object? id,
            object? result,
            CancellationToken cancellationToken = default)
        {
            var response = JsonRpcMessage.CreateResponse(id, result);
            return transport.SendAsync(response, cancellationToken);
        }

        /// <summary>
        /// Sends an error response
        /// </summary>
        public static Task RespondErrorAsync(
            this ITransport transport,
            object? id,
            int code,
            string message,
            object? data = null,
            CancellationToken cancellationToken = default)
        {
            var response = JsonRpcMessage.CreateErrorResponse(id, code, message, data);
            return transport.SendAsync(response, cancellationToken);
        }

        /// <summary>
        /// Creates a request builder
        /// </summary>
        public static JsonRpcMessageBuilder BuildRequest(this ITransport transport, string method)
        {
            return JsonRpcMessageBuilder.Request(method);
        }

        /// <summary>
        /// Creates a notification builder
        /// </summary>
        public static JsonRpcMessageBuilder BuildNotification(this ITransport transport, string method)
        {
            return JsonRpcMessageBuilder.Notification(method);
        }

        /// <summary>
        /// Creates a response builder
        /// </summary>
        public static JsonRpcMessageBuilder BuildResponse(this ITransport transport, object? id)
        {
            return JsonRpcMessageBuilder.Response(id);
        }

        /// <summary>
        /// Validates a JSON-RPC message
        /// </summary>
        public static bool IsValid(this JsonRpcMessage message, out string? error)
        {
            return message.Validate(out error);
        }

        /// <summary>
        /// Converts message to JSON string
        /// </summary>
        public static string ToJson(this JsonRpcMessage message, bool indented = false)
        {
            var options = new JsonSerializerOptions
            {
                WriteIndented = indented,
                DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
            };
            return message.ToJson(options);
        }

        /// <summary>
        /// Creates a message from JSON string
        /// </summary>
        public static JsonRpcMessage? FromJson(this string json)
        {
            return JsonRpcMessage.FromJson(json);
        }

        /// <summary>
        /// Gets typed parameters from message
        /// </summary>
        public static T? GetParams<T>(this JsonRpcMessage message)
        {
            if (message.Params == null)
                return default;

            if (message.Params is JsonElement jsonElement)
            {
                return jsonElement.Deserialize<T>();
            }

            return (T)message.Params;
        }

        /// <summary>
        /// Gets typed result from message
        /// </summary>
        public static T? GetResult<T>(this JsonRpcMessage message)
        {
            if (message.Result == null)
                return default;

            if (message.Result is JsonElement jsonElement)
            {
                return jsonElement.Deserialize<T>();
            }

            return (T)message.Result;
        }

        /// <summary>
        /// Gets typed error data from message
        /// </summary>
        public static T? GetErrorData<T>(this JsonRpcMessage message)
        {
            if (message.Error?.Data == null)
                return default;

            if (message.Error.Data is JsonElement jsonElement)
            {
                return jsonElement.Deserialize<T>();
            }

            return (T)message.Error.Data;
        }

        /// <summary>
        /// Checks if message matches a method pattern
        /// </summary>
        public static bool MatchesMethod(this JsonRpcMessage message, string pattern)
        {
            if (string.IsNullOrEmpty(message.Method))
                return false;

            // Support wildcards
            if (pattern.EndsWith("*"))
            {
                var prefix = pattern.Substring(0, pattern.Length - 1);
                return message.Method.StartsWith(prefix);
            }

            return message.Method == pattern;
        }

        /// <summary>
        /// Creates a batch of messages
        /// </summary>
        public static List<JsonRpcMessage> CreateBatch(params JsonRpcMessage[] messages)
        {
            return messages.ToList();
        }

        /// <summary>
        /// Sends a batch of messages
        /// </summary>
        public static async Task SendBatchAsync(
            this ITransport transport,
            IEnumerable<JsonRpcMessage> messages,
            CancellationToken cancellationToken = default)
        {
            foreach (var message in messages)
            {
                await transport.SendAsync(message, cancellationToken);
            }
        }

        /// <summary>
        /// Waits for a specific message
        /// </summary>
        public static async Task<JsonRpcMessage> WaitForMessageAsync(
            this ITransport transport,
            Func<JsonRpcMessage, bool> predicate,
            TimeSpan timeout,
            CancellationToken cancellationToken = default)
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            cts.CancelAfter(timeout);

            while (!cts.Token.IsCancellationRequested)
            {
                var message = await transport.ReceiveAsync(cts.Token);
                if (predicate(message))
                {
                    return message;
                }
            }

            throw new TimeoutException("Timeout waiting for message");
        }

        /// <summary>
        /// Creates a transport configuration builder
        /// </summary>
        public static TransportConfigBuilder BuildConfig(this TransportProtocol protocol)
        {
            return new TransportConfigBuilder(protocol);
        }

        /// <summary>
        /// Performs MCP initialization handshake
        /// </summary>
        public static async Task<InitializeResult> InitializeAsync(
            this McpClient client,
            ClientInfo clientInfo,
            CancellationToken cancellationToken = default)
        {
            var parameters = new
            {
                protocolVersion = "2024-11-05",
                capabilities = new
                {
                    experimental = new { }
                },
                clientInfo = new
                {
                    name = clientInfo.Name,
                    version = clientInfo.Version
                }
            };

            var response = await client.InvokeAsync<InitializeResult>("initialize", parameters, cancellationToken);
            return response ?? new InitializeResult();
        }

        /// <summary>
        /// Initializes server with client connection
        /// </summary>
        public static async Task StartServerAsync(
            this McpServer server,
            ServerInfo? serverInfo = null,
            CancellationToken cancellationToken = default)
        {
            if (serverInfo != null)
            {
                server.Info = serverInfo;
            }

            await server.StartAsync(cancellationToken);
        }

        /// <summary>
        /// Registers a simple request handler
        /// </summary>
        public static void HandleRequest<TParams, TResult>(
            this McpServer server,
            string method,
            Func<TParams?, Task<TResult?>> handler)
        {
            server.RegisterMethod<TParams, TResult>(method, handler);
        }

        /// <summary>
        /// Registers a simple notification handler
        /// </summary>
        public static void HandleNotification<TParams>(
            this McpServer server,
            string method,
            Func<TParams?, Task> handler)
        {
            server.RegisterMethod(method, async (message) =>
            {
                var parameters = message.GetParams<TParams>();
                await handler(parameters);
                return null;
            });
        }

        /// <summary>
        /// Creates a tool provider helper
        /// </summary>
        public static ToolProvider CreateToolProvider(
            string name,
            string? description,
            Type inputType,
            Func<object?, Task<object?>> handler)
        {
            return new ToolProvider
            {
                Name = name,
                Description = description,
                InputSchema = inputType,
                Handler = handler
            };
        }

        /// <summary>
        /// Creates a prompt provider helper
        /// </summary>
        public static PromptProvider CreatePromptProvider(
            string name,
            string? description,
            Func<object?, Task<object?>> handler,
            params PromptArgument[] arguments)
        {
            return new PromptProvider
            {
                Name = name,
                Description = description,
                Arguments = arguments.ToList(),
                Handler = handler
            };
        }

        /// <summary>
        /// Creates a resource provider helper
        /// </summary>
        public static ResourceProvider CreateResourceProvider(
            string uri,
            string name,
            string? description,
            string mimeType,
            Func<Task<object?>> handler)
        {
            return new ResourceProvider
            {
                Uri = uri,
                Name = name,
                Description = description,
                MimeType = mimeType,
                Handler = handler
            };
        }

        /// <summary>
        /// Converts exception to JSON-RPC error
        /// </summary>
        public static JsonRpcError ToJsonRpcError(this Exception exception)
        {
            return exception switch
            {
                JsonRpcException jre => jre.Error,
                TimeoutException => new JsonRpcError(JsonRpcErrorCodes.Timeout, exception.Message),
                NotImplementedException => new JsonRpcError(JsonRpcErrorCodes.NotImplemented, exception.Message),
                UnauthorizedAccessException => new JsonRpcError(JsonRpcErrorCodes.ResourceAccessDenied, exception.Message),
                ArgumentException => new JsonRpcError(JsonRpcErrorCodes.InvalidParams, exception.Message),
                InvalidOperationException => new JsonRpcError(JsonRpcErrorCodes.InvalidRequest, exception.Message),
                _ => new JsonRpcError(JsonRpcErrorCodes.InternalError, exception.Message,
                    new { type = exception.GetType().Name, stackTrace = exception.StackTrace })
            };
        }

        /// <summary>
        /// Retries an async operation with exponential backoff
        /// </summary>
        public static async Task<T> RetryAsync<T>(
            Func<Task<T>> operation,
            int maxAttempts = 3,
            TimeSpan? initialDelay = null,
            CancellationToken cancellationToken = default)
        {
            var delay = initialDelay ?? TimeSpan.FromSeconds(1);
            Exception? lastException = null;

            for (int attempt = 0; attempt < maxAttempts; attempt++)
            {
                try
                {
                    return await operation();
                }
                catch (Exception ex)
                {
                    lastException = ex;

                    if (attempt < maxAttempts - 1)
                    {
                        await Task.Delay(delay, cancellationToken);
                        delay = TimeSpan.FromMilliseconds(delay.TotalMilliseconds * 2);
                    }
                }
            }

            throw new AggregateException($"Operation failed after {maxAttempts} attempts", lastException!);
        }

        /// <summary>
        /// Executes an operation with timeout
        /// </summary>
        public static async Task<T> WithTimeoutAsync<T>(
            this Task<T> task,
            TimeSpan timeout,
            string? timeoutMessage = null)
        {
            using var cts = new CancellationTokenSource(timeout);
            var completedTask = await Task.WhenAny(task, Task.Delay(Timeout.Infinite, cts.Token));

            if (completedTask == task)
            {
                return await task;
            }

            throw new TimeoutException(timeoutMessage ?? $"Operation timed out after {timeout}");
        }
    }

    /// <summary>
    /// Transport configuration builder
    /// </summary>
    public class TransportConfigBuilder
    {
        private readonly TransportConfig _config;

        public TransportConfigBuilder(TransportProtocol protocol)
        {
            _config = new TransportConfig { Protocol = protocol };
        }

        public TransportConfigBuilder WithHost(string host)
        {
            _config.Host = host;
            return this;
        }

        public TransportConfigBuilder WithPort(int port)
        {
            _config.Port = port;
            return this;
        }

        public TransportConfigBuilder WithTimeout(TimeSpan connectTimeout, TimeSpan sendTimeout, TimeSpan receiveTimeout)
        {
            _config.ConnectTimeout = connectTimeout;
            _config.SendTimeout = sendTimeout;
            _config.ReceiveTimeout = receiveTimeout;
            return this;
        }

        public TransportConfigBuilder WithBufferSize(int sendBuffer, int receiveBuffer)
        {
            _config.SendBufferSize = sendBuffer;
            _config.ReceiveBufferSize = receiveBuffer;
            return this;
        }

        public TransportConfigBuilder WithMaxMessageSize(int size)
        {
            _config.MaxMessageSize = size;
            return this;
        }

        public TransportConfigBuilder WithKeepAlive(bool enable, TimeSpan? interval = null)
        {
            _config.EnableKeepAlive = enable;
            if (interval.HasValue)
            {
                _config.KeepAliveInterval = interval.Value;
            }
            return this;
        }

        public TransportConfigBuilder WithCompression(bool enable, int level = 6)
        {
            _config.EnableCompression = enable;
            _config.CompressionLevel = level;
            return this;
        }

        public TransportConfigBuilder WithAutoReconnect(bool enable, int maxAttempts = 3, TimeSpan? delay = null)
        {
            _config.AutoReconnect = enable;
            _config.MaxReconnectAttempts = maxAttempts;
            if (delay.HasValue)
            {
                _config.ReconnectDelay = delay.Value;
            }
            return this;
        }

        public TransportConfigBuilder WithSsl(Action<SslConfig> configureSsl)
        {
            _config.SslConfig = new SslConfig();
            configureSsl(_config.SslConfig);
            return this;
        }

        public TransportConfigBuilder WithRetry(Action<ConnectionRetryConfig> configureRetry)
        {
            _config.RetryConfig = new ConnectionRetryConfig();
            configureRetry(_config.RetryConfig);
            return this;
        }

        public TransportConfigBuilder WithOption(string key, object value)
        {
            _config.ConnectionOptions[key] = value;
            return this;
        }

        public TransportConfig Build()
        {
            if (!_config.Validate(out var errors))
            {
                throw new InvalidOperationException($"Invalid configuration: {string.Join(", ", errors)}");
            }
            return _config;
        }
    }

    /// <summary>
    /// Client information for initialization
    /// </summary>
    public class ClientInfo
    {
        public string Name { get; set; } = "GopherMcp Client";
        public string Version { get; set; } = "1.0.0";
    }

    /// <summary>
    /// Initialize result from server
    /// </summary>
    public class InitializeResult
    {
        public string ProtocolVersion { get; set; } = "2024-11-05";
        public ServerCapabilities Capabilities { get; set; } = new();
        public ServerInfo ServerInfo { get; set; } = new();
    }

    /// <summary>
    /// Server capabilities
    /// </summary>
    public class ServerCapabilities
    {
        public object? Tools { get; set; }
        public object? Prompts { get; set; }
        public object? Resources { get; set; }
        public object? Experimental { get; set; }
    }
}
