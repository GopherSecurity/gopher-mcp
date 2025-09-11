using System;
using System.Runtime.Serialization;

namespace GopherMcp.Types
{
    /// <summary>
    /// Base exception class for all MCP-related exceptions
    /// </summary>
    [Serializable]
    public class McpException : Exception
    {
        /// <summary>
        /// Gets the MCP error code associated with this exception
        /// </summary>
        public McpResult ErrorCode { get; }
        
        /// <summary>
        /// Gets additional context information about the error
        /// </summary>
        public string Context { get; }
        
        /// <summary>
        /// Gets the timestamp when the exception occurred
        /// </summary>
        public DateTime Timestamp { get; }
        
        /// <summary>
        /// Initializes a new instance of the McpException class
        /// </summary>
        public McpException() 
            : base("An MCP error occurred")
        {
            ErrorCode = McpResult.Unknown;
            Timestamp = DateTime.UtcNow;
        }
        
        /// <summary>
        /// Initializes a new instance of the McpException class with a specified error message
        /// </summary>
        /// <param name="message">The error message</param>
        public McpException(string message) 
            : base(message)
        {
            ErrorCode = McpResult.Unknown;
            Timestamp = DateTime.UtcNow;
        }
        
        /// <summary>
        /// Initializes a new instance of the McpException class with a specified error message and error code
        /// </summary>
        /// <param name="message">The error message</param>
        /// <param name="errorCode">The MCP error code</param>
        public McpException(string message, McpResult errorCode) 
            : base(message)
        {
            ErrorCode = errorCode;
            Timestamp = DateTime.UtcNow;
        }
        
        /// <summary>
        /// Initializes a new instance of the McpException class with a specified error message and inner exception
        /// </summary>
        /// <param name="message">The error message</param>
        /// <param name="innerException">The inner exception</param>
        public McpException(string message, Exception innerException) 
            : base(message, innerException)
        {
            ErrorCode = McpResult.Unknown;
            Timestamp = DateTime.UtcNow;
        }
        
        /// <summary>
        /// Initializes a new instance of the McpException class with a specified error message, error code, and inner exception
        /// </summary>
        /// <param name="message">The error message</param>
        /// <param name="errorCode">The MCP error code</param>
        /// <param name="innerException">The inner exception</param>
        public McpException(string message, McpResult errorCode, Exception innerException) 
            : base(message, innerException)
        {
            ErrorCode = errorCode;
            Timestamp = DateTime.UtcNow;
        }
        
        /// <summary>
        /// Initializes a new instance of the McpException class with full details
        /// </summary>
        /// <param name="message">The error message</param>
        /// <param name="errorCode">The MCP error code</param>
        /// <param name="context">Additional context information</param>
        /// <param name="innerException">The inner exception</param>
        public McpException(string message, McpResult errorCode, string context, Exception innerException = null)
            : base(message, innerException)
        {
            ErrorCode = errorCode;
            Context = context;
            Timestamp = DateTime.UtcNow;
        }
        
        /// <summary>
        /// Initializes a new instance of the McpException class with serialized data
        /// </summary>
        /// <param name="info">The serialization info</param>
        /// <param name="context">The streaming context</param>
        protected McpException(SerializationInfo info, StreamingContext context) 
            : base(info, context)
        {
            ErrorCode = (McpResult)info.GetInt32(nameof(ErrorCode));
            Context = info.GetString(nameof(Context));
            Timestamp = info.GetDateTime(nameof(Timestamp));
        }
        
        /// <summary>
        /// Sets the SerializationInfo with information about the exception
        /// </summary>
        public override void GetObjectData(SerializationInfo info, StreamingContext context)
        {
            base.GetObjectData(info, context);
            info.AddValue(nameof(ErrorCode), (int)ErrorCode);
            info.AddValue(nameof(Context), Context);
            info.AddValue(nameof(Timestamp), Timestamp);
        }
        
        /// <summary>
        /// Creates an McpException from an MCP error info structure
        /// </summary>
        public static McpException FromErrorInfo(McpErrorInfo errorInfo)
        {
            var message = $"{errorInfo.Message} (File: {errorInfo.File}, Line: {errorInfo.Line})";
            return new McpException(message, errorInfo.Code);
        }
        
        /// <summary>
        /// Creates an McpException from an MCP result code
        /// </summary>
        public static McpException FromResult(McpResult result, string message = null)
        {
            var errorMessage = message ?? GetDefaultMessage(result);
            return new McpException(errorMessage, result);
        }
        
        /// <summary>
        /// Gets the default error message for a result code
        /// </summary>
        private static string GetDefaultMessage(McpResult result)
        {
            return result switch
            {
                McpResult.Ok => "Operation completed successfully",
                McpResult.InvalidArgument => "Invalid argument provided",
                McpResult.NullPointer => "Null pointer encountered",
                McpResult.OutOfMemory => "Out of memory",
                McpResult.NotFound => "Resource not found",
                McpResult.AlreadyExists => "Resource already exists",
                McpResult.PermissionDenied => "Permission denied",
                McpResult.IoError => "I/O error occurred",
                McpResult.Timeout => "Operation timed out",
                McpResult.Cancelled => "Operation was cancelled",
                McpResult.NotImplemented => "Feature not implemented",
                McpResult.InvalidState => "Invalid state for operation",
                McpResult.BufferTooSmall => "Buffer too small",
                McpResult.ProtocolError => "Protocol error",
                McpResult.ConnectionFailed => "Connection failed",
                McpResult.ConnectionClosed => "Connection closed",
                McpResult.AlreadyInitialized => "Already initialized",
                McpResult.NotInitialized => "Not initialized",
                McpResult.ResourceExhausted => "Resource exhausted",
                McpResult.InvalidFormat => "Invalid format",
                McpResult.CleanupFailed => "Cleanup failed",
                McpResult.ResourceLimit => "Resource limit reached",
                McpResult.NoMemory => "No memory available",
                _ => "Unknown error occurred"
            };
        }
    }
    
    /// <summary>
    /// Exception for filter-specific errors
    /// </summary>
    [Serializable]
    public class FilterException : McpException
    {
        /// <summary>
        /// Gets the filter name associated with the error
        /// </summary>
        public string FilterName { get; }
        
        /// <summary>
        /// Gets the filter type associated with the error
        /// </summary>
        public string FilterType { get; }
        
        /// <summary>
        /// Gets the filter error code
        /// </summary>
        public FilterError FilterErrorCode { get; }
        
        /// <summary>
        /// Initializes a new instance of the FilterException class
        /// </summary>
        public FilterException() 
            : base("A filter error occurred")
        {
            FilterErrorCode = FilterError.ProcessingFailed;
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterException class with a specified error message
        /// </summary>
        public FilterException(string message) 
            : base(message)
        {
            FilterErrorCode = FilterError.ProcessingFailed;
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterException class with filter details
        /// </summary>
        public FilterException(string message, string filterName, string filterType) 
            : base(message)
        {
            FilterName = filterName;
            FilterType = filterType;
            FilterErrorCode = FilterError.ProcessingFailed;
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterException class with a specified error message and inner exception
        /// </summary>
        public FilterException(string message, Exception innerException) 
            : base(message, innerException)
        {
            FilterErrorCode = FilterError.ProcessingFailed;
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterException class with full details
        /// </summary>
        public FilterException(string message, FilterError filterErrorCode, string filterName = null, string filterType = null, Exception innerException = null)
            : base(message, ConvertFilterErrorToMcpResult(filterErrorCode), innerException)
        {
            FilterName = filterName;
            FilterType = filterType;
            FilterErrorCode = filterErrorCode;
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterException class with serialized data
        /// </summary>
        protected FilterException(SerializationInfo info, StreamingContext context) 
            : base(info, context)
        {
            FilterName = info.GetString(nameof(FilterName));
            FilterType = info.GetString(nameof(FilterType));
            FilterErrorCode = (FilterError)info.GetInt32(nameof(FilterErrorCode));
        }
        
        /// <summary>
        /// Sets the SerializationInfo with information about the exception
        /// </summary>
        public override void GetObjectData(SerializationInfo info, StreamingContext context)
        {
            base.GetObjectData(info, context);
            info.AddValue(nameof(FilterName), FilterName);
            info.AddValue(nameof(FilterType), FilterType);
            info.AddValue(nameof(FilterErrorCode), (int)FilterErrorCode);
        }
        
        /// <summary>
        /// Convert filter error to MCP result
        /// </summary>
        private static McpResult ConvertFilterErrorToMcpResult(FilterError filterError)
        {
            return filterError switch
            {
                FilterError.None => McpResult.Ok,
                FilterError.InvalidConfiguration => McpResult.InvalidArgument,
                FilterError.FilterNotFound => McpResult.NotFound,
                FilterError.FilterAlreadyExists => McpResult.AlreadyExists,
                FilterError.InitializationFailed => McpResult.NotInitialized,
                FilterError.Timeout => McpResult.Timeout,
                FilterError.ResourceExhausted => McpResult.ResourceExhausted,
                FilterError.NotSupported => McpResult.NotImplemented,
                FilterError.PermissionDenied => McpResult.PermissionDenied,
                FilterError.InvalidState => McpResult.InvalidState,
                _ => McpResult.Unknown
            };
        }
    }
    
    /// <summary>
    /// Exception for chain processing errors
    /// </summary>
    [Serializable]
    public class ChainException : McpException
    {
        /// <summary>
        /// Gets the chain name associated with the error
        /// </summary>
        public string ChainName { get; }
        
        /// <summary>
        /// Gets the current chain state
        /// </summary>
        public ChainState ChainState { get; }
        
        /// <summary>
        /// Gets the filter node where the error occurred
        /// </summary>
        public string FailedNodeName { get; }
        
        /// <summary>
        /// Gets the index of the failed node in the chain
        /// </summary>
        public int FailedNodeIndex { get; }
        
        /// <summary>
        /// Initializes a new instance of the ChainException class
        /// </summary>
        public ChainException() 
            : base("A chain processing error occurred")
        {
            ChainState = ChainState.Error;
            FailedNodeIndex = -1;
        }
        
        /// <summary>
        /// Initializes a new instance of the ChainException class with a specified error message
        /// </summary>
        public ChainException(string message) 
            : base(message)
        {
            ChainState = ChainState.Error;
            FailedNodeIndex = -1;
        }
        
        /// <summary>
        /// Initializes a new instance of the ChainException class with chain details
        /// </summary>
        public ChainException(string message, string chainName, ChainState chainState) 
            : base(message)
        {
            ChainName = chainName;
            ChainState = chainState;
            FailedNodeIndex = -1;
        }
        
        /// <summary>
        /// Initializes a new instance of the ChainException class with a specified error message and inner exception
        /// </summary>
        public ChainException(string message, Exception innerException) 
            : base(message, innerException)
        {
            ChainState = ChainState.Error;
            FailedNodeIndex = -1;
        }
        
        /// <summary>
        /// Initializes a new instance of the ChainException class with full details
        /// </summary>
        public ChainException(string message, string chainName, ChainState chainState, string failedNodeName, int failedNodeIndex, Exception innerException = null)
            : base(message, McpResult.Unknown, $"Chain: {chainName}, Node: {failedNodeName}[{failedNodeIndex}]", innerException)
        {
            ChainName = chainName;
            ChainState = chainState;
            FailedNodeName = failedNodeName;
            FailedNodeIndex = failedNodeIndex;
        }
        
        /// <summary>
        /// Initializes a new instance of the ChainException class with serialized data
        /// </summary>
        protected ChainException(SerializationInfo info, StreamingContext context) 
            : base(info, context)
        {
            ChainName = info.GetString(nameof(ChainName));
            ChainState = (ChainState)info.GetInt32(nameof(ChainState));
            FailedNodeName = info.GetString(nameof(FailedNodeName));
            FailedNodeIndex = info.GetInt32(nameof(FailedNodeIndex));
        }
        
        /// <summary>
        /// Sets the SerializationInfo with information about the exception
        /// </summary>
        public override void GetObjectData(SerializationInfo info, StreamingContext context)
        {
            base.GetObjectData(info, context);
            info.AddValue(nameof(ChainName), ChainName);
            info.AddValue(nameof(ChainState), (int)ChainState);
            info.AddValue(nameof(FailedNodeName), FailedNodeName);
            info.AddValue(nameof(FailedNodeIndex), FailedNodeIndex);
        }
    }
    
    /// <summary>
    /// Exception for transport layer errors
    /// </summary>
    [Serializable]
    public class TransportException : McpException
    {
        /// <summary>
        /// Gets the transport type
        /// </summary>
        public McpTransportType TransportType { get; }
        
        /// <summary>
        /// Gets the connection state when the error occurred
        /// </summary>
        public McpConnectionState ConnectionState { get; }
        
        /// <summary>
        /// Gets the endpoint associated with the error
        /// </summary>
        public string Endpoint { get; }
        
        /// <summary>
        /// Gets the socket error code if applicable
        /// </summary>
        public int? SocketErrorCode { get; }
        
        /// <summary>
        /// Initializes a new instance of the TransportException class
        /// </summary>
        public TransportException() 
            : base("A transport error occurred")
        {
            ConnectionState = McpConnectionState.Error;
        }
        
        /// <summary>
        /// Initializes a new instance of the TransportException class with a specified error message
        /// </summary>
        public TransportException(string message) 
            : base(message)
        {
            ConnectionState = McpConnectionState.Error;
        }
        
        /// <summary>
        /// Initializes a new instance of the TransportException class with transport details
        /// </summary>
        public TransportException(string message, McpTransportType transportType, McpConnectionState connectionState) 
            : base(message)
        {
            TransportType = transportType;
            ConnectionState = connectionState;
        }
        
        /// <summary>
        /// Initializes a new instance of the TransportException class with a specified error message and inner exception
        /// </summary>
        public TransportException(string message, Exception innerException) 
            : base(message, innerException)
        {
            ConnectionState = McpConnectionState.Error;
        }
        
        /// <summary>
        /// Initializes a new instance of the TransportException class with full details
        /// </summary>
        public TransportException(string message, McpTransportType transportType, McpConnectionState connectionState, 
            string endpoint, int? socketErrorCode = null, Exception innerException = null)
            : base(message, ConvertConnectionStateToMcpResult(connectionState), $"Transport: {transportType}, Endpoint: {endpoint}", innerException)
        {
            TransportType = transportType;
            ConnectionState = connectionState;
            Endpoint = endpoint;
            SocketErrorCode = socketErrorCode;
        }
        
        /// <summary>
        /// Initializes a new instance of the TransportException class with serialized data
        /// </summary>
        protected TransportException(SerializationInfo info, StreamingContext context) 
            : base(info, context)
        {
            TransportType = (McpTransportType)info.GetInt32(nameof(TransportType));
            ConnectionState = (McpConnectionState)info.GetInt32(nameof(ConnectionState));
            Endpoint = info.GetString(nameof(Endpoint));
            SocketErrorCode = (int?)info.GetValue(nameof(SocketErrorCode), typeof(int?));
        }
        
        /// <summary>
        /// Sets the SerializationInfo with information about the exception
        /// </summary>
        public override void GetObjectData(SerializationInfo info, StreamingContext context)
        {
            base.GetObjectData(info, context);
            info.AddValue(nameof(TransportType), (int)TransportType);
            info.AddValue(nameof(ConnectionState), (int)ConnectionState);
            info.AddValue(nameof(Endpoint), Endpoint);
            info.AddValue(nameof(SocketErrorCode), SocketErrorCode);
        }
        
        /// <summary>
        /// Convert connection state to MCP result
        /// </summary>
        private static McpResult ConvertConnectionStateToMcpResult(McpConnectionState state)
        {
            return state switch
            {
                McpConnectionState.Disconnected => McpResult.ConnectionClosed,
                McpConnectionState.Error => McpResult.ConnectionFailed,
                _ => McpResult.Unknown
            };
        }
    }
    
    /// <summary>
    /// Exception for configuration errors
    /// </summary>
    [Serializable]
    public class ConfigurationException : McpException
    {
        /// <summary>
        /// Gets the configuration section that caused the error
        /// </summary>
        public string ConfigSection { get; }
        
        /// <summary>
        /// Gets the configuration key that caused the error
        /// </summary>
        public string ConfigKey { get; }
        
        /// <summary>
        /// Gets the invalid value that was provided
        /// </summary>
        public object InvalidValue { get; }
        
        /// <summary>
        /// Gets the expected value or format description
        /// </summary>
        public string ExpectedValue { get; }
        
        /// <summary>
        /// Initializes a new instance of the ConfigurationException class
        /// </summary>
        public ConfigurationException() 
            : base("A configuration error occurred", McpResult.InvalidArgument)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the ConfigurationException class with a specified error message
        /// </summary>
        public ConfigurationException(string message) 
            : base(message, McpResult.InvalidArgument)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the ConfigurationException class with configuration details
        /// </summary>
        public ConfigurationException(string message, string configSection, string configKey) 
            : base(message, McpResult.InvalidArgument)
        {
            ConfigSection = configSection;
            ConfigKey = configKey;
        }
        
        /// <summary>
        /// Initializes a new instance of the ConfigurationException class with a specified error message and inner exception
        /// </summary>
        public ConfigurationException(string message, Exception innerException) 
            : base(message, McpResult.InvalidArgument, innerException)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the ConfigurationException class with full details
        /// </summary>
        public ConfigurationException(string message, string configSection, string configKey, 
            object invalidValue, string expectedValue = null, Exception innerException = null)
            : base(message, McpResult.InvalidArgument, $"Section: {configSection}, Key: {configKey}", innerException)
        {
            ConfigSection = configSection;
            ConfigKey = configKey;
            InvalidValue = invalidValue;
            ExpectedValue = expectedValue;
        }
        
        /// <summary>
        /// Initializes a new instance of the ConfigurationException class with serialized data
        /// </summary>
        protected ConfigurationException(SerializationInfo info, StreamingContext context) 
            : base(info, context)
        {
            ConfigSection = info.GetString(nameof(ConfigSection));
            ConfigKey = info.GetString(nameof(ConfigKey));
            InvalidValue = info.GetValue(nameof(InvalidValue), typeof(object));
            ExpectedValue = info.GetString(nameof(ExpectedValue));
        }
        
        /// <summary>
        /// Sets the SerializationInfo with information about the exception
        /// </summary>
        public override void GetObjectData(SerializationInfo info, StreamingContext context)
        {
            base.GetObjectData(info, context);
            info.AddValue(nameof(ConfigSection), ConfigSection);
            info.AddValue(nameof(ConfigKey), ConfigKey);
            info.AddValue(nameof(InvalidValue), InvalidValue);
            info.AddValue(nameof(ExpectedValue), ExpectedValue);
        }
        
        /// <summary>
        /// Creates a configuration exception for a missing required configuration
        /// </summary>
        public static ConfigurationException MissingRequired(string configSection, string configKey)
        {
            return new ConfigurationException(
                $"Required configuration '{configKey}' is missing from section '{configSection}'",
                configSection, 
                configKey,
                null,
                "Non-null value required");
        }
        
        /// <summary>
        /// Creates a configuration exception for an invalid value
        /// </summary>
        public static ConfigurationException CreateInvalidValueException(string configSection, string configKey, object invalidValue, string expectedFormat)
        {
            return new ConfigurationException(
                $"Invalid value '{invalidValue}' for configuration '{configKey}' in section '{configSection}'",
                configSection,
                configKey,
                invalidValue,
                expectedFormat);
        }
    }
}