using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace GopherMcp.Integration
{
    /// <summary>
    /// Represents a JSON-RPC 2.0 message
    /// </summary>
    public class JsonRpcMessage
    {
        /// <summary>
        /// JSON-RPC version (should be "2.0")
        /// </summary>
        [JsonPropertyName("jsonrpc")]
        public string JsonRpc { get; set; } = "2.0";

        /// <summary>
        /// Request/Response ID
        /// </summary>
        [JsonPropertyName("id")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public object? Id { get; set; }

        /// <summary>
        /// Method name for requests
        /// </summary>
        [JsonPropertyName("method")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public string? Method { get; set; }

        /// <summary>
        /// Parameters for requests
        /// </summary>
        [JsonPropertyName("params")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public object? Params { get; set; }

        /// <summary>
        /// Result for successful responses
        /// </summary>
        [JsonPropertyName("result")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public object? Result { get; set; }

        /// <summary>
        /// Error for failed responses
        /// </summary>
        [JsonPropertyName("error")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public JsonRpcError? Error { get; set; }

        /// <summary>
        /// Checks if this is a request message
        /// </summary>
        [JsonIgnore]
        public bool IsRequest => Method != null && Id != null;

        /// <summary>
        /// Checks if this is a notification message
        /// </summary>
        [JsonIgnore]
        public bool IsNotification => Method != null && Id == null;

        /// <summary>
        /// Checks if this is a response message
        /// </summary>
        [JsonIgnore]
        public bool IsResponse => Method == null && (Result != null || Error != null);

        /// <summary>
        /// Checks if this is an error response
        /// </summary>
        [JsonIgnore]
        public bool IsError => Error != null;

        /// <summary>
        /// Validates the message according to JSON-RPC 2.0 specification
        /// </summary>
        public bool Validate(out string? validationError)
        {
            validationError = null;

            // Check JSON-RPC version
            if (JsonRpc != "2.0")
            {
                validationError = $"Invalid JSON-RPC version: {JsonRpc}";
                return false;
            }

            // Check message type validity
            bool hasMethod = !string.IsNullOrEmpty(Method);
            bool hasResult = Result != null;
            bool hasError = Error != null;

            if (hasMethod && (hasResult || hasError))
            {
                validationError = "Request/notification cannot have result or error";
                return false;
            }

            if (!hasMethod && !hasResult && !hasError)
            {
                validationError = "Response must have either result or error";
                return false;
            }

            if (hasResult && hasError)
            {
                validationError = "Response cannot have both result and error";
                return false;
            }

            // Validate request
            if (hasMethod)
            {
                if (string.IsNullOrWhiteSpace(Method))
                {
                    validationError = "Method name cannot be empty";
                    return false;
                }

                if (Method.StartsWith("rpc."))
                {
                    validationError = "Method names starting with 'rpc.' are reserved";
                    return false;
                }
            }

            // Validate error
            if (hasError)
            {
                if (Error!.Code == 0)
                {
                    validationError = "Error code cannot be 0";
                    return false;
                }

                if (string.IsNullOrWhiteSpace(Error.Message))
                {
                    validationError = "Error message cannot be empty";
                    return false;
                }
            }

            return true;
        }

        /// <summary>
        /// Creates a new request message
        /// </summary>
        public static JsonRpcMessage CreateRequest(string method, object? parameters = null, object? id = null)
        {
            return new JsonRpcMessage
            {
                Id = id ?? Guid.NewGuid().ToString(),
                Method = method,
                Params = parameters
            };
        }

        /// <summary>
        /// Creates a new notification message
        /// </summary>
        public static JsonRpcMessage CreateNotification(string method, object? parameters = null)
        {
            return new JsonRpcMessage
            {
                Method = method,
                Params = parameters
            };
        }

        /// <summary>
        /// Creates a successful response message
        /// </summary>
        public static JsonRpcMessage CreateResponse(object? id, object? result)
        {
            return new JsonRpcMessage
            {
                Id = id,
                Result = result
            };
        }

        /// <summary>
        /// Creates an error response message
        /// </summary>
        public static JsonRpcMessage CreateErrorResponse(object? id, int code, string message, object? data = null)
        {
            return new JsonRpcMessage
            {
                Id = id,
                Error = new JsonRpcError
                {
                    Code = code,
                    Message = message,
                    Data = data
                }
            };
        }

        /// <summary>
        /// Creates an error response message from an exception
        /// </summary>
        public static JsonRpcMessage CreateErrorResponse(object? id, Exception exception)
        {
            return CreateErrorResponse(
                id,
                JsonRpcErrorCodes.InternalError,
                exception.Message,
                new { type = exception.GetType().Name, stackTrace = exception.StackTrace });
        }

        /// <summary>
        /// Serializes the message to JSON
        /// </summary>
        public string ToJson(JsonSerializerOptions? options = null)
        {
            options ??= new JsonSerializerOptions
            {
                WriteIndented = false,
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
            };

            return JsonSerializer.Serialize(this, options);
        }

        /// <summary>
        /// Deserializes a message from JSON
        /// </summary>
        public static JsonRpcMessage? FromJson(string json, JsonSerializerOptions? options = null)
        {
            options ??= new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true
            };

            return JsonSerializer.Deserialize<JsonRpcMessage>(json, options);
        }
    }

    /// <summary>
    /// Represents a JSON-RPC error
    /// </summary>
    public class JsonRpcError
    {
        /// <summary>
        /// Error code
        /// </summary>
        [JsonPropertyName("code")]
        public int Code { get; set; }

        /// <summary>
        /// Error message
        /// </summary>
        [JsonPropertyName("message")]
        public string Message { get; set; } = string.Empty;

        /// <summary>
        /// Additional error data
        /// </summary>
        [JsonPropertyName("data")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public object? Data { get; set; }

        /// <summary>
        /// Creates a new JSON-RPC error
        /// </summary>
        public JsonRpcError() { }

        /// <summary>
        /// Creates a new JSON-RPC error with specified code and message
        /// </summary>
        public JsonRpcError(int code, string message, object? data = null)
        {
            Code = code;
            Message = message;
            Data = data;
        }

        /// <summary>
        /// Creates a parse error
        /// </summary>
        public static JsonRpcError ParseError(string? details = null)
        {
            return new JsonRpcError(JsonRpcErrorCodes.ParseError, "Parse error", details);
        }

        /// <summary>
        /// Creates an invalid request error
        /// </summary>
        public static JsonRpcError InvalidRequest(string? details = null)
        {
            return new JsonRpcError(JsonRpcErrorCodes.InvalidRequest, "Invalid request", details);
        }

        /// <summary>
        /// Creates a method not found error
        /// </summary>
        public static JsonRpcError MethodNotFound(string? methodName = null)
        {
            return new JsonRpcError(JsonRpcErrorCodes.MethodNotFound,
                $"Method not found{(methodName != null ? $": {methodName}" : "")}");
        }

        /// <summary>
        /// Creates an invalid params error
        /// </summary>
        public static JsonRpcError InvalidParams(string? details = null)
        {
            return new JsonRpcError(JsonRpcErrorCodes.InvalidParams, "Invalid params", details);
        }

        /// <summary>
        /// Creates an internal error
        /// </summary>
        public static JsonRpcError InternalError(string? details = null)
        {
            return new JsonRpcError(JsonRpcErrorCodes.InternalError, "Internal error", details);
        }
    }

    /// <summary>
    /// Standard JSON-RPC error codes
    /// </summary>
    public static class JsonRpcErrorCodes
    {
        /// <summary>
        /// Invalid JSON was received by the server
        /// </summary>
        public const int ParseError = -32700;

        /// <summary>
        /// The JSON sent is not a valid Request object
        /// </summary>
        public const int InvalidRequest = -32600;

        /// <summary>
        /// The method does not exist or is not available
        /// </summary>
        public const int MethodNotFound = -32601;

        /// <summary>
        /// Invalid method parameters
        /// </summary>
        public const int InvalidParams = -32602;

        /// <summary>
        /// Internal JSON-RPC error
        /// </summary>
        public const int InternalError = -32603;

        /// <summary>
        /// Reserved for implementation-defined server errors (-32000 to -32099)
        /// </summary>
        public const int ServerErrorStart = -32099;
        public const int ServerErrorEnd = -32000;

        // MCP-specific error codes
        public const int NotImplemented = -32001;
        public const int Timeout = -32002;
        public const int ResourceNotFound = -32003;
        public const int ResourceAccessDenied = -32004;
        public const int RateLimitExceeded = -32005;
    }

    /// <summary>
    /// Builder for creating JSON-RPC messages
    /// </summary>
    public class JsonRpcMessageBuilder
    {
        private readonly JsonRpcMessage _message;

        private JsonRpcMessageBuilder()
        {
            _message = new JsonRpcMessage();
        }

        /// <summary>
        /// Starts building a request
        /// </summary>
        public static JsonRpcMessageBuilder Request(string method)
        {
            var builder = new JsonRpcMessageBuilder();
            builder._message.Method = method;
            builder._message.Id = Guid.NewGuid().ToString();
            return builder;
        }

        /// <summary>
        /// Starts building a notification
        /// </summary>
        public static JsonRpcMessageBuilder Notification(string method)
        {
            var builder = new JsonRpcMessageBuilder();
            builder._message.Method = method;
            return builder;
        }

        /// <summary>
        /// Starts building a response
        /// </summary>
        public static JsonRpcMessageBuilder Response(object? id)
        {
            var builder = new JsonRpcMessageBuilder();
            builder._message.Id = id;
            return builder;
        }

        /// <summary>
        /// Sets the ID
        /// </summary>
        public JsonRpcMessageBuilder WithId(object id)
        {
            _message.Id = id;
            return this;
        }

        /// <summary>
        /// Sets the parameters
        /// </summary>
        public JsonRpcMessageBuilder WithParams(object parameters)
        {
            _message.Params = parameters;
            return this;
        }

        /// <summary>
        /// Sets the result
        /// </summary>
        public JsonRpcMessageBuilder WithResult(object result)
        {
            _message.Result = result;
            return this;
        }

        /// <summary>
        /// Sets the error
        /// </summary>
        public JsonRpcMessageBuilder WithError(int code, string message, object? data = null)
        {
            _message.Error = new JsonRpcError(code, message, data);
            return this;
        }

        /// <summary>
        /// Sets the error from an exception
        /// </summary>
        public JsonRpcMessageBuilder WithError(Exception exception)
        {
            _message.Error = new JsonRpcError(
                JsonRpcErrorCodes.InternalError,
                exception.Message,
                new { type = exception.GetType().Name, stackTrace = exception.StackTrace });
            return this;
        }

        /// <summary>
        /// Builds the message
        /// </summary>
        public JsonRpcMessage Build()
        {
            if (!_message.Validate(out var error))
            {
                throw new InvalidOperationException($"Invalid JSON-RPC message: {error}");
            }
            return _message;
        }
    }
}
