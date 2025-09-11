using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using GopherMcp.Core;

namespace GopherMcp.Types
{
    /// <summary>
    /// Execution mode for backward compatibility
    /// </summary>
    public enum ExecutionMode
    {
        /// <summary>
        /// Sequential execution
        /// </summary>
        Sequential = 0,
        
        /// <summary>
        /// Parallel execution
        /// </summary>
        Parallel = 1
    }
    
    /// <summary>
    /// Chain execution mode determining how filters are processed
    /// </summary>
    public enum ChainExecutionMode
    {
        /// <summary>
        /// Execute filters in sequential order
        /// </summary>
        Sequential = 0,
        
        /// <summary>
        /// Execute filters in parallel
        /// </summary>
        Parallel = 1,
        
        /// <summary>
        /// Execute filters based on conditions
        /// </summary>
        Conditional = 2,
        
        /// <summary>
        /// Pipeline mode with buffering between filters
        /// </summary>
        Pipeline = 3,
        
        /// <summary>
        /// Fan-out mode - duplicate data to multiple filters
        /// </summary>
        FanOut = 4,
        
        /// <summary>
        /// Fan-in mode - merge data from multiple filters
        /// </summary>
        FanIn = 5,
        
        /// <summary>
        /// Custom execution mode
        /// </summary>
        Custom = 99
    }
    
    /// <summary>
    /// Routing strategy for filter chain load balancing
    /// </summary>
    public enum RoutingStrategy
    {
        /// <summary>
        /// Round-robin distribution
        /// </summary>
        RoundRobin = 0,
        
        /// <summary>
        /// Route to least loaded filter
        /// </summary>
        LeastLoaded = 1,
        
        /// <summary>
        /// Hash-based routing for session affinity
        /// </summary>
        HashBased = 2,
        
        /// <summary>
        /// Priority-based routing
        /// </summary>
        Priority = 3,
        
        /// <summary>
        /// Random distribution
        /// </summary>
        Random = 4,
        
        /// <summary>
        /// Weighted round-robin
        /// </summary>
        WeightedRoundRobin = 5,
        
        /// <summary>
        /// Least connections routing
        /// </summary>
        LeastConnections = 6,
        
        /// <summary>
        /// Custom routing function
        /// </summary>
        Custom = 99
    }
    
    /// <summary>
    /// Processing direction through the chain
    /// </summary>
    public enum ProcessingDirection
    {
        /// <summary>
        /// Forward direction (request path)
        /// </summary>
        Forward = 0,
        
        /// <summary>
        /// Reverse direction (response path)
        /// </summary>
        Reverse = 1,
        
        /// <summary>
        /// Bidirectional processing
        /// </summary>
        Bidirectional = 2,
        
        /// <summary>
        /// Inbound direction
        /// </summary>
        Inbound = 3,
        
        /// <summary>
        /// Outbound direction
        /// </summary>
        Outbound = 4
    }
    
    /// <summary>
    /// Chain state
    /// </summary>
    public enum ChainState
    {
        /// <summary>
        /// Chain is idle
        /// </summary>
        Idle = 0,
        
        /// <summary>
        /// Chain is processing data
        /// </summary>
        Processing = 1,
        
        /// <summary>
        /// Chain is paused
        /// </summary>
        Paused = 2,
        
        /// <summary>
        /// Chain encountered an error
        /// </summary>
        Error = 3,
        
        /// <summary>
        /// Chain processing completed
        /// </summary>
        Completed = 4,
        
        /// <summary>
        /// Chain is stopped
        /// </summary>
        Stopped = 5
    }
    
    /// <summary>
    /// Filter node in a chain
    /// </summary>
    public class FilterNode
    {
        /// <summary>
        /// Gets or sets the filter handle
        /// </summary>
        public McpFilterHandle Filter { get; set; }
        
        /// <summary>
        /// Gets or sets the filter name
        /// </summary>
        public string Name { get; set; }
        
        /// <summary>
        /// Gets or sets the filter type
        /// </summary>
        public string Type { get; set; }
        
        /// <summary>
        /// Gets or sets the node priority (lower values = higher priority)
        /// </summary>
        public uint Priority { get; set; } = 100;
        
        /// <summary>
        /// Gets or sets whether the filter is enabled
        /// </summary>
        public bool Enabled { get; set; } = true;
        
        /// <summary>
        /// Gets or sets whether to bypass this filter on error
        /// </summary>
        public bool BypassOnError { get; set; } = false;
        
        /// <summary>
        /// Gets or sets the filter configuration
        /// </summary>
        public FilterConfig Config { get; set; }
        
        /// <summary>
        /// Gets or sets metadata associated with this node
        /// </summary>
        public Dictionary<string, object> Metadata { get; set; }
        
        /// <summary>
        /// Gets or sets the next node in the chain
        /// </summary>
        public FilterNode Next { get; set; }
        
        /// <summary>
        /// Gets or sets the previous node in the chain
        /// </summary>
        public FilterNode Previous { get; set; }
        
        /// <summary>
        /// Gets or sets conditional execution criteria
        /// </summary>
        public Func<ProcessingContext, bool> Condition { get; set; }
        
        /// <summary>
        /// Gets or sets the weight for weighted routing
        /// </summary>
        public int Weight { get; set; } = 1;
        
        /// <summary>
        /// Initializes a new instance of FilterNode
        /// </summary>
        public FilterNode()
        {
            Metadata = new Dictionary<string, object>();
        }
        
        /// <summary>
        /// Initializes a new instance of FilterNode with name and type
        /// </summary>
        public FilterNode(string name, string type) : this()
        {
            Name = name;
            Type = type;
        }
        
        /// <summary>
        /// Initializes a new instance of FilterNode with filter handle
        /// </summary>
        public FilterNode(McpFilterHandle filter, string name, string type) : this(name, type)
        {
            Filter = filter;
        }
        
        /// <summary>
        /// Clone the filter node
        /// </summary>
        public FilterNode Clone()
        {
            return new FilterNode
            {
                Filter = Filter,
                Name = Name,
                Type = Type,
                Priority = Priority,
                Enabled = Enabled,
                BypassOnError = BypassOnError,
                Config = Config?.Clone(),
                Metadata = Metadata != null ? new Dictionary<string, object>(Metadata) : null,
                Condition = Condition,
                Weight = Weight
            };
        }
    }
    
    /// <summary>
    /// Chain configuration
    /// </summary>
    public class ChainConfig
    {
        /// <summary>
        /// Gets or sets the chain name
        /// </summary>
        public string Name { get; set; }
        
        /// <summary>
        /// Gets or sets the execution mode
        /// </summary>
        public ChainExecutionMode ExecutionMode { get; set; } = ChainExecutionMode.Sequential;
        
        /// <summary>
        /// Gets or sets the routing strategy
        /// </summary>
        public RoutingStrategy RoutingStrategy { get; set; } = RoutingStrategy.RoundRobin;
        
        /// <summary>
        /// Gets or sets the maximum number of parallel filters
        /// </summary>
        public uint MaxParallel { get; set; } = 4;
        
        /// <summary>
        /// Gets or sets the buffer size between filters
        /// </summary>
        public uint BufferSize { get; set; } = 65536;
        
        /// <summary>
        /// Gets or sets the chain timeout in milliseconds
        /// </summary>
        public uint TimeoutMs { get; set; } = 30000;
        
        /// <summary>
        /// Gets or sets whether to stop on first error
        /// </summary>
        public bool StopOnError { get; set; } = true;
        
        /// <summary>
        /// Gets or sets whether to enable auto-retry
        /// </summary>
        public bool EnableRetry { get; set; } = false;
        
        /// <summary>
        /// Gets or sets the maximum retry attempts
        /// </summary>
        public int MaxRetryAttempts { get; set; } = 3;
        
        /// <summary>
        /// Gets or sets the retry delay in milliseconds
        /// </summary>
        public int RetryDelayMs { get; set; } = 1000;
        
        /// <summary>
        /// Gets or sets whether to enable circuit breaker
        /// </summary>
        public bool EnableCircuitBreaker { get; set; } = false;
        
        /// <summary>
        /// Gets or sets the circuit breaker threshold
        /// </summary>
        public int CircuitBreakerThreshold { get; set; } = 5;
        
        /// <summary>
        /// Gets or sets the circuit breaker timeout in milliseconds
        /// </summary>
        public int CircuitBreakerTimeoutMs { get; set; } = 60000;
        
        /// <summary>
        /// Gets or sets additional configuration settings
        /// </summary>
        public Dictionary<string, object> Settings { get; set; }
        
        /// <summary>
        /// Gets or sets the default timeout for the chain
        /// </summary>
        public TimeSpan DefaultTimeout { get; set; } = TimeSpan.FromSeconds(30);
        
        /// <summary>
        /// Gets or sets the maximum concurrency level
        /// </summary>
        public int MaxConcurrency { get; set; } = 10;
        
        /// <summary>
        /// Gets or sets whether to enable statistics collection
        /// </summary>
        public bool EnableStatistics { get; set; } = true;
        
        /// <summary>
        /// Gets or sets whether to dispose filters when chain is disposed
        /// </summary>
        public bool DisposeFilters { get; set; } = true;
        
        /// <summary>
        /// Gets or sets the execution mode (backward compatibility)
        /// </summary>
        public ExecutionMode Mode 
        { 
            get => this.ExecutionMode == ChainExecutionMode.Sequential ? Types.ExecutionMode.Sequential : Types.ExecutionMode.Parallel;
            set => this.ExecutionMode = value == Types.ExecutionMode.Sequential ? ChainExecutionMode.Sequential : ChainExecutionMode.Parallel;
        }
        
        /// <summary>
        /// Gets or sets whether to continue on error (backward compatibility)
        /// </summary>
        public bool ContinueOnError 
        { 
            get => !StopOnError;
            set => StopOnError = !value;
        }
        
        /// <summary>
        /// Gets or sets the maximum retry attempts (backward compatibility)
        /// </summary>
        public int MaxRetries 
        { 
            get => MaxRetryAttempts;
            set => MaxRetryAttempts = value;
        }
        
        /// <summary>
        /// Gets or sets the retry delay (backward compatibility)
        /// </summary>
        public TimeSpan RetryDelay 
        { 
            get => TimeSpan.FromMilliseconds(RetryDelayMs);
            set => RetryDelayMs = (int)value.TotalMilliseconds;
        }
        
        /// <summary>
        /// Initializes a new instance of ChainConfig
        /// </summary>
        public ChainConfig()
        {
            Settings = new Dictionary<string, object>();
        }
        
        /// <summary>
        /// Initializes a new instance of ChainConfig with a name
        /// </summary>
        public ChainConfig(string name) : this()
        {
            Name = name;
        }
        
        /// <summary>
        /// Clone the configuration
        /// </summary>
        public ChainConfig Clone()
        {
            return new ChainConfig
            {
                Name = Name,
                ExecutionMode = ExecutionMode,
                RoutingStrategy = RoutingStrategy,
                MaxParallel = MaxParallel,
                BufferSize = BufferSize,
                TimeoutMs = TimeoutMs,
                StopOnError = StopOnError,
                EnableRetry = EnableRetry,
                MaxRetryAttempts = MaxRetryAttempts,
                RetryDelayMs = RetryDelayMs,
                EnableCircuitBreaker = EnableCircuitBreaker,
                CircuitBreakerThreshold = CircuitBreakerThreshold,
                CircuitBreakerTimeoutMs = CircuitBreakerTimeoutMs,
                Settings = Settings != null ? new Dictionary<string, object>(Settings) : null,
                Metadata = Metadata != null ? new Dictionary<string, object>(Metadata) : null,
                ConditionPredicate = ConditionPredicate
            };
        }
        
        /// <summary>
        /// Gets or sets the metadata for the chain
        /// </summary>
        public Dictionary<string, object> Metadata { get; set; } = new Dictionary<string, object>();
        
        /// <summary>
        /// Gets or sets the condition predicate for conditional execution
        /// </summary>
        public Func<ProcessingContext, bool> ConditionPredicate { get; set; }
        
        /// <summary>
        /// Gets or sets whether to sort filters by priority
        /// </summary>
        public bool SortByPriority { get; set; } = false;
    }
    
    /// <summary>
    /// Chain statistics
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ChainStatistics
    {
        /// <summary>
        /// Total number of packets processed
        /// </summary>
        public ulong TotalProcessed;
        
        /// <summary>
        /// Total packets processed (alias for TotalProcessed)
        /// </summary>
        public ulong TotalPacketsProcessed;
        
        /// <summary>
        /// Total number of errors
        /// </summary>
        public ulong TotalErrors;
        
        /// <summary>
        /// Total number of bypassed filters
        /// </summary>
        public ulong TotalBypassed;
        
        /// <summary>
        /// Total bytes processed
        /// </summary>
        public ulong TotalBytesProcessed;
        
        /// <summary>
        /// Total processing time in microseconds
        /// </summary>
        public ulong TotalProcessingTimeUs;
        
        /// <summary>
        /// Average processing time in microseconds
        /// </summary>
        public double AverageProcessingTimeUs;
        
        /// <summary>
        /// Average latency in milliseconds
        /// </summary>
        public double AverageLatencyMs;
        
        /// <summary>
        /// Maximum latency in milliseconds
        /// </summary>
        public double MaxLatencyMs;
        
        /// <summary>
        /// Minimum latency in milliseconds
        /// </summary>
        public double MinLatencyMs;
        
        /// <summary>
        /// Throughput in megabits per second
        /// </summary>
        public double ThroughputMbps;
        
        /// <summary>
        /// Number of active filters in the chain
        /// </summary>
        public uint ActiveFilters;
        
        /// <summary>
        /// Total number of filters in the chain
        /// </summary>
        public uint TotalFilters;
        
        /// <summary>
        /// Number of filters currently processing
        /// </summary>
        public uint ProcessingFilters;
        
        /// <summary>
        /// Total bytes processed
        /// </summary>
        public ulong BytesProcessed;
        
        /// <summary>
        /// Current queue depth
        /// </summary>
        public uint QueueDepth;
        
        /// <summary>
        /// Peak queue depth
        /// </summary>
        public uint PeakQueueDepth;
        
        /// <summary>
        /// Number of retries
        /// </summary>
        public ulong RetryCount;
        
        /// <summary>
        /// Number of circuit breaker trips
        /// </summary>
        public ulong CircuitBreakerTrips;
        
        /// <summary>
        /// Get a string representation of the statistics
        /// </summary>
        public override string ToString()
        {
            return $"ChainStatistics: Processed={TotalProcessed}, Errors={TotalErrors}, " +
                   $"AvgLatency={AverageLatencyMs:F2}ms, Throughput={ThroughputMbps:F2}Mbps, " +
                   $"ActiveFilters={ActiveFilters}/{TotalFilters}";
        }
    }
    
    /// <summary>
    /// Processing context for filter chain execution
    /// </summary>
    public class ProcessingContext
    {
        /// <summary>
        /// Gets or sets the processing direction
        /// </summary>
        public ProcessingDirection Direction { get; set; }
        
        /// <summary>
        /// Gets or sets the session ID
        /// </summary>
        public string SessionId { get; set; }
        
        /// <summary>
        /// Gets or sets the correlation ID
        /// </summary>
        public string CorrelationId { get; set; }
        
        /// <summary>
        /// Gets or sets the source endpoint
        /// </summary>
        public string SourceEndpoint { get; set; }
        
        /// <summary>
        /// Gets or sets the destination endpoint
        /// </summary>
        public string DestinationEndpoint { get; set; }
        
        /// <summary>
        /// Gets or sets the protocol
        /// </summary>
        public string Protocol { get; set; }
        
        /// <summary>
        /// Gets or sets the timestamp
        /// </summary>
        public DateTime Timestamp { get; set; }
        
        /// <summary>
        /// Gets or sets context metadata
        /// </summary>
        public Dictionary<string, object> Metadata { get; set; }
        
        /// <summary>
        /// Gets or sets user-defined properties
        /// </summary>
        public Dictionary<string, object> Properties { get; set; }
        
        /// <summary>
        /// Gets or sets the current filter node
        /// </summary>
        public FilterNode CurrentNode { get; set; }
        
        /// <summary>
        /// Gets or sets the chain configuration
        /// </summary>
        public ChainConfig ChainConfig { get; set; }
        
        /// <summary>
        /// Gets or sets whether to skip remaining filters
        /// </summary>
        public bool SkipRemaining { get; set; }
        
        /// <summary>
        /// Gets or sets the cancellation token
        /// </summary>
        public System.Threading.CancellationToken CancellationToken { get; set; }
        
        /// <summary>
        /// Gets the elapsed time since context creation
        /// </summary>
        public TimeSpan Elapsed => DateTime.UtcNow - Timestamp;
        
        /// <summary>
        /// Initializes a new instance of ProcessingContext
        /// </summary>
        public ProcessingContext()
        {
            Timestamp = DateTime.UtcNow;
            Metadata = new Dictionary<string, object>();
            Properties = new Dictionary<string, object>();
            Direction = ProcessingDirection.Forward;
            SessionId = Guid.NewGuid().ToString();
            CorrelationId = Guid.NewGuid().ToString();
        }
        
        /// <summary>
        /// Initializes a new instance of ProcessingContext with direction
        /// </summary>
        public ProcessingContext(ProcessingDirection direction) : this()
        {
            Direction = direction;
        }
        
        /// <summary>
        /// Clone the processing context
        /// </summary>
        public ProcessingContext Clone()
        {
            return new ProcessingContext
            {
                Direction = Direction,
                SessionId = SessionId,
                CorrelationId = CorrelationId,
                SourceEndpoint = SourceEndpoint,
                DestinationEndpoint = DestinationEndpoint,
                Protocol = Protocol,
                Timestamp = Timestamp,
                Metadata = new Dictionary<string, object>(Metadata),
                Properties = new Dictionary<string, object>(Properties),
                CurrentNode = CurrentNode,
                ChainConfig = ChainConfig?.Clone(),
                SkipRemaining = SkipRemaining,
                CancellationToken = CancellationToken
            };
        }
        
        /// <summary>
        /// Add or update metadata value
        /// </summary>
        public void SetMetadata(string key, object value)
        {
            Metadata[key] = value;
        }
        
        /// <summary>
        /// Get metadata value
        /// </summary>
        public T GetMetadata<T>(string key, T defaultValue = default)
        {
            if (Metadata.TryGetValue(key, out var value) && value is T typedValue)
            {
                return typedValue;
            }
            return defaultValue;
        }
        
        /// <summary>
        /// Add or update property value
        /// </summary>
        public void SetProperty(string key, object value)
        {
            Properties[key] = value;
        }
        
        /// <summary>
        /// Get property value
        /// </summary>
        public T GetProperty<T>(string key, T defaultValue = default)
        {
            if (Properties.TryGetValue(key, out var value) && value is T typedValue)
            {
                return typedValue;
            }
            return defaultValue;
        }
    }
}