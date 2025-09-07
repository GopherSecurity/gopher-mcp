using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Filters;
using GopherMcp.Integration;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Manager
{
    /// <summary>
    /// Processes JSON-RPC messages through filter chains.
    /// </summary>
    public class MessageProcessor
    {
        private readonly FilterManager _filterManager;
        private readonly ILogger<MessageProcessor> _logger;
        private readonly Dictionary<string, FilterChain> _routeTable;
        private readonly JsonSerializerOptions _jsonOptions;
        private long _messageIdCounter;

        /// <summary>
        /// Initializes a new instance of the MessageProcessor class.
        /// </summary>
        /// <param name="filterManager">The filter manager to use.</param>
        /// <param name="logger">Optional logger instance.</param>
        public MessageProcessor(FilterManager filterManager, ILogger<MessageProcessor> logger = null)
        {
            _filterManager = filterManager ?? throw new ArgumentNullException(nameof(filterManager));
            _logger = logger;
            _routeTable = new Dictionary<string, FilterChain>();
            _jsonOptions = new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                WriteIndented = false
            };
        }

        /// <summary>
        /// Processes a JSON-RPC message through the appropriate chain.
        /// </summary>
        /// <param name="message">The message to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The response message.</returns>
        public async Task<JsonRpcMessage> ProcessAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(message);

            try
            {
                // Validate message
                ValidateMessage(message);

                // Route to appropriate chain
                var chain = SelectChain(message);
                if (chain == null)
                {
                    _logger?.LogWarning("No chain found for method: {Method}", message.Method);
                    return CreateErrorResponse(message.Id, -32601, "Method not found");
                }

                // Process through chain
                var result = await chain.ProcessAsync(message, cancellationToken);

                // Generate response
                return GenerateResponse(message, result);
            }
            catch (OperationCanceledException)
            {
                _logger?.LogInformation("Request cancelled: {Method}", message.Method);
                return CreateErrorResponse(message.Id, -32800, "Request cancelled");
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error processing message: {Method}", message.Method);
                return CreateErrorResponse(message.Id, -32603, "Internal error");
            }
        }

        /// <summary>
        /// Processes a batch of JSON-RPC messages.
        /// </summary>
        /// <param name="messages">The messages to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The response messages.</returns>
        public async Task<List<JsonRpcMessage>> ProcessBatchAsync(
            IEnumerable<JsonRpcMessage> messages, 
            CancellationToken cancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(messages);

            var tasks = messages.Select(msg => ProcessAsync(msg, cancellationToken));
            var results = await Task.WhenAll(tasks);
            
            // Filter out null responses (notifications don't generate responses)
            return results.Where(r => r != null).ToList();
        }

        /// <summary>
        /// Registers a route for a specific method pattern.
        /// </summary>
        /// <param name="methodPattern">The method pattern (supports wildcards).</param>
        /// <param name="chain">The chain to handle this route.</param>
        public void RegisterRoute(string methodPattern, FilterChain chain)
        {
            ArgumentNullException.ThrowIfNull(methodPattern);
            ArgumentNullException.ThrowIfNull(chain);

            _routeTable[methodPattern] = chain;
            _logger?.LogInformation("Registered route: {Pattern} -> {Chain}", methodPattern, chain.Name);
        }

        /// <summary>
        /// Validates a JSON-RPC message.
        /// </summary>
        /// <param name="message">The message to validate.</param>
        private void ValidateMessage(JsonRpcMessage message)
        {
            if (string.IsNullOrEmpty(message.JsonRpc))
            {
                throw new ArgumentException("JSON-RPC version is required");
            }

            if (message.JsonRpc != "2.0")
            {
                throw new ArgumentException($"Unsupported JSON-RPC version: {message.JsonRpc}");
            }

            if (string.IsNullOrEmpty(message.Method))
            {
                throw new ArgumentException("Method is required");
            }
        }

        /// <summary>
        /// Selects the appropriate chain for a message.
        /// </summary>
        /// <param name="message">The message to route.</param>
        /// <returns>The selected chain, or null if no match.</returns>
        private FilterChain SelectChain(JsonRpcMessage message)
        {
            // Exact match
            if (_routeTable.TryGetValue(message.Method, out var chain))
            {
                return chain;
            }

            // Wildcard match
            foreach (var (pattern, routeChain) in _routeTable)
            {
                if (MatchesPattern(message.Method, pattern))
                {
                    return routeChain;
                }
            }

            // Default chain
            return _filterManager.GetDefaultChain();
        }

        /// <summary>
        /// Checks if a method matches a pattern.
        /// </summary>
        /// <param name="method">The method name.</param>
        /// <param name="pattern">The pattern (supports * wildcard).</param>
        /// <returns>True if matches.</returns>
        private bool MatchesPattern(string method, string pattern)
        {
            if (pattern.Contains('*'))
            {
                var regex = "^" + pattern.Replace("*", ".*") + "$";
                return System.Text.RegularExpressions.Regex.IsMatch(method, regex);
            }

            return method == pattern;
        }

        /// <summary>
        /// Generates a response message from a filter result.
        /// </summary>
        /// <param name="request">The original request.</param>
        /// <param name="result">The filter result.</param>
        /// <returns>The response message.</returns>
        private JsonRpcMessage GenerateResponse(JsonRpcMessage request, FilterResult result)
        {
            // Notification - no response
            if (request.Id == null)
            {
                return null;
            }

            if (result.IsSuccess)
            {
                return new JsonRpcMessage
                {
                    JsonRpc = "2.0",
                    Id = request.Id,
                    Result = result.Data
                };
            }
            else
            {
                return CreateErrorResponse(request.Id, result.ErrorCode, result.ErrorMessage);
            }
        }

        /// <summary>
        /// Creates an error response message.
        /// </summary>
        /// <param name="id">The request ID.</param>
        /// <param name="code">The error code.</param>
        /// <param name="message">The error message.</param>
        /// <returns>The error response.</returns>
        private JsonRpcMessage CreateErrorResponse(object id, int code, string message)
        {
            return new JsonRpcMessage
            {
                JsonRpc = "2.0",
                Id = id,
                Error = new JsonRpcError
                {
                    Code = code,
                    Message = message
                }
            };
        }

        /// <summary>
        /// Generates the next message ID.
        /// </summary>
        /// <returns>The next message ID.</returns>
        public string GenerateMessageId()
        {
            var id = Interlocked.Increment(ref _messageIdCounter);
            return $"msg_{id}";
        }
    }
}