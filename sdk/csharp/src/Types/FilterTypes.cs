using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace GopherMcp.Types
{
    /// <summary>
    /// Filter processing status indicating how to continue processing
    /// </summary>
    public enum FilterStatus
    {
        /// <summary>
        /// Continue processing with next filter
        /// </summary>
        Continue = 0,
        
        /// <summary>
        /// Stop iteration and return
        /// </summary>
        StopIteration = 1,
        
        /// <summary>
        /// Error occurred during processing
        /// </summary>
        Error = 2,
        
        /// <summary>
        /// Filter needs more data to proceed
        /// </summary>
        NeedMoreData = 3,
        
        /// <summary>
        /// Filter has buffered data for later processing
        /// </summary>
        Buffered = 4
    }
    
    /// <summary>
    /// Filter position in chain
    /// </summary>
    public enum FilterPosition
    {
        /// <summary>
        /// Add filter at the beginning of the chain
        /// </summary>
        First = 0,
        
        /// <summary>
        /// Add filter at the end of the chain
        /// </summary>
        Last = 1,
        
        /// <summary>
        /// Add filter before a specific filter
        /// </summary>
        Before = 2,
        
        /// <summary>
        /// Add filter after a specific filter
        /// </summary>
        After = 3
    }
    
    /// <summary>
    /// Filter-specific error codes
    /// </summary>
    public enum FilterError
    {
        /// <summary>
        /// No error
        /// </summary>
        None = 0,
        
        /// <summary>
        /// Invalid filter configuration
        /// </summary>
        InvalidConfiguration = 1001,
        
        /// <summary>
        /// Filter not found
        /// </summary>
        FilterNotFound = 1002,
        
        /// <summary>
        /// Filter already exists
        /// </summary>
        FilterAlreadyExists = 1003,
        
        /// <summary>
        /// Filter initialization failed
        /// </summary>
        InitializationFailed = 1004,
        
        /// <summary>
        /// Filter processing failed
        /// </summary>
        ProcessingFailed = 1005,
        
        /// <summary>
        /// Filter chain error
        /// </summary>
        ChainError = 1006,
        
        /// <summary>
        /// Buffer overflow
        /// </summary>
        BufferOverflow = 1007,
        
        /// <summary>
        /// Buffer underflow
        /// </summary>
        BufferUnderflow = 1008,
        
        /// <summary>
        /// Invalid buffer state
        /// </summary>
        InvalidBufferState = 1009,
        
        /// <summary>
        /// Filter timeout
        /// </summary>
        Timeout = 1010,
        
        /// <summary>
        /// Resource exhausted
        /// </summary>
        ResourceExhausted = 1011,
        
        /// <summary>
        /// Not supported
        /// </summary>
        NotSupported = 1012,
        
        /// <summary>
        /// Permission denied
        /// </summary>
        PermissionDenied = 1013,
        
        /// <summary>
        /// Filter is in invalid state for operation
        /// </summary>
        InvalidState = 1014,
        
        /// <summary>
        /// Dependency not met
        /// </summary>
        DependencyNotMet = 1015
    }
    
    /// <summary>
    /// Filter layer in the processing stack
    /// </summary>
    public enum FilterLayer
    {
        /// <summary>
        /// Transport layer (L4)
        /// </summary>
        Transport = 0,
        
        /// <summary>
        /// Session layer (L5)
        /// </summary>
        Session = 1,
        
        /// <summary>
        /// Presentation layer (L6)
        /// </summary>
        Presentation = 2,
        
        /// <summary>
        /// Application layer (L7)
        /// </summary>
        Application = 3,
        
        /// <summary>
        /// Custom layer
        /// </summary>
        Custom = 99
    }
    
    /// <summary>
    /// Filter configuration
    /// </summary>
    public class FilterConfig
    {
        /// <summary>
        /// Gets or sets the filter name
        /// </summary>
        public string Name { get; set; }
        
        /// <summary>
        /// Gets or sets the filter type
        /// </summary>
        public string Type { get; set; }
        
        /// <summary>
        /// Gets or sets the filter settings as key-value pairs
        /// </summary>
        public Dictionary<string, object> Settings { get; set; }
        
        /// <summary>
        /// Gets or sets the filter layer
        /// </summary>
        public FilterLayer Layer { get; set; }
        
        /// <summary>
        /// Gets or sets the memory pool name for this filter
        /// </summary>
        public string MemoryPool { get; set; }
        
        /// <summary>
        /// Gets or sets whether the filter is enabled
        /// </summary>
        public bool Enabled { get; set; } = true;
        
        /// <summary>
        /// Gets or sets the filter priority (lower values = higher priority)
        /// </summary>
        public int Priority { get; set; } = 100;
        
        /// <summary>
        /// Gets or sets the filter timeout in milliseconds
        /// </summary>
        public int TimeoutMs { get; set; } = 30000;
        
        /// <summary>
        /// Gets or sets whether to bypass this filter on error
        /// </summary>
        public bool BypassOnError { get; set; } = false;
        
        /// <summary>
        /// Gets or sets the maximum buffer size for this filter
        /// </summary>
        public int MaxBufferSize { get; set; } = 65536;
        
        /// <summary>
        /// Initializes a new instance of FilterConfig
        /// </summary>
        public FilterConfig()
        {
            Settings = new Dictionary<string, object>();
        }
        
        /// <summary>
        /// Initializes a new instance of FilterConfig with a name and type
        /// </summary>
        public FilterConfig(string name, string type) : this()
        {
            Name = name;
            Type = type;
        }
        
        /// <summary>
        /// Clone the configuration
        /// </summary>
        public FilterConfig Clone()
        {
            return new FilterConfig
            {
                Name = Name,
                Type = Type,
                Settings = new Dictionary<string, object>(Settings),
                Layer = Layer,
                MemoryPool = MemoryPool,
                Enabled = Enabled,
                Priority = Priority,
                TimeoutMs = TimeoutMs,
                BypassOnError = BypassOnError,
                MaxBufferSize = MaxBufferSize
            };
        }
    }
    
    /// <summary>
    /// Filter statistics
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct FilterStatistics
    {
        /// <summary>
        /// Total bytes processed
        /// </summary>
        public ulong BytesProcessed;
        
        /// <summary>
        /// Total packets processed
        /// </summary>
        public ulong PacketsProcessed;
        
        /// <summary>
        /// Total errors encountered
        /// </summary>
        public ulong ErrorCount;
        
        /// <summary>
        /// Total processing time in microseconds
        /// </summary>
        public ulong ProcessingTimeUs;
        
        /// <summary>
        /// Average processing time per packet in microseconds
        /// </summary>
        public double AverageProcessingTimeUs;
        
        /// <summary>
        /// Maximum processing time in microseconds
        /// </summary>
        public ulong MaxProcessingTimeUs;
        
        /// <summary>
        /// Minimum processing time in microseconds
        /// </summary>
        public ulong MinProcessingTimeUs;
        
        /// <summary>
        /// Current buffer usage in bytes
        /// </summary>
        public ulong CurrentBufferUsage;
        
        /// <summary>
        /// Peak buffer usage in bytes
        /// </summary>
        public ulong PeakBufferUsage;
        
        /// <summary>
        /// Number of times the filter was bypassed
        /// </summary>
        public ulong BypassCount;
        
        /// <summary>
        /// Number of timeouts
        /// </summary>
        public ulong TimeoutCount;
        
        /// <summary>
        /// Throughput in bytes per second
        /// </summary>
        public double ThroughputBps;
        
        /// <summary>
        /// Get a string representation of the statistics
        /// </summary>
        public override string ToString()
        {
            return $"FilterStatistics: BytesProcessed={BytesProcessed}, PacketsProcessed={PacketsProcessed}, " +
                   $"Errors={ErrorCount}, AvgTime={AverageProcessingTimeUs}μs, Throughput={ThroughputBps:F2}Bps";
        }
    }
    
    /// <summary>
    /// Base event arguments for filter events
    /// </summary>
    public class FilterEventArgs : EventArgs
    {
        /// <summary>
        /// Gets the filter name
        /// </summary>
        public string FilterName { get; }
        
        /// <summary>
        /// Gets the filter type
        /// </summary>
        public string FilterType { get; }
        
        /// <summary>
        /// Gets the timestamp of the event
        /// </summary>
        public DateTime Timestamp { get; }
        
        /// <summary>
        /// Gets additional event data
        /// </summary>
        public Dictionary<string, object> Data { get; }
        
        /// <summary>
        /// Initializes a new instance of FilterEventArgs
        /// </summary>
        public FilterEventArgs(string filterName, string filterType)
        {
            FilterName = filterName;
            FilterType = filterType;
            Timestamp = DateTime.UtcNow;
            Data = new Dictionary<string, object>();
        }
        
        /// <summary>
        /// Initializes a new instance of FilterEventArgs with additional data
        /// </summary>
        public FilterEventArgs(string filterName, string filterType, Dictionary<string, object> data)
            : this(filterName, filterType)
        {
            if (data != null)
            {
                Data = new Dictionary<string, object>(data);
            }
        }
    }
    
    /// <summary>
    /// Event arguments for filter data events
    /// </summary>
    public class FilterDataEventArgs : FilterEventArgs
    {
        /// <summary>
        /// Gets the data buffer
        /// </summary>
        public byte[] Buffer { get; }
        
        /// <summary>
        /// Gets the data offset
        /// </summary>
        public int Offset { get; }
        
        /// <summary>
        /// Gets the data length
        /// </summary>
        public int Length { get; }
        
        /// <summary>
        /// Gets or sets the processing status
        /// </summary>
        public FilterStatus Status { get; set; }
        
        /// <summary>
        /// Gets or sets whether the event was handled
        /// </summary>
        public bool Handled { get; set; }
        
        /// <summary>
        /// Initializes a new instance of FilterDataEventArgs
        /// </summary>
        public FilterDataEventArgs(string filterName, string filterType, byte[] buffer, int offset, int length)
            : base(filterName, filterType)
        {
            Buffer = buffer;
            Offset = offset;
            Length = length;
            Status = FilterStatus.Continue;
            Handled = false;
        }
        
        /// <summary>
        /// Get the data as a span
        /// </summary>
        public ReadOnlySpan<byte> GetData()
        {
            return new ReadOnlySpan<byte>(Buffer, Offset, Length);
        }
    }
    
    /// <summary>
    /// Filter processing result
    /// </summary>
    public class FilterResult
    {
        /// <summary>
        /// Gets or sets the processing status
        /// </summary>
        public FilterStatus Status { get; set; }
        
        /// <summary>
        /// Gets or sets the output data
        /// </summary>
        public byte[] Data { get; set; }
        
        /// <summary>
        /// Gets or sets the output data offset
        /// </summary>
        public int Offset { get; set; }
        
        /// <summary>
        /// Gets or sets the output data length
        /// </summary>
        public int Length { get; set; }
        
        /// <summary>
        /// Gets or sets the error code if Status is Error
        /// </summary>
        public FilterError ErrorCode { get; set; }
        
        /// <summary>
        /// Gets or sets the error message if Status is Error
        /// </summary>
        public string ErrorMessage { get; set; }
        
        /// <summary>
        /// Gets or sets additional result metadata
        /// </summary>
        public Dictionary<string, object> Metadata { get; set; }
        
        /// <summary>
        /// Gets whether the result indicates success
        /// </summary>
        public bool IsSuccess => Status == FilterStatus.Continue || Status == FilterStatus.StopIteration;
        
        /// <summary>
        /// Gets whether the result indicates an error
        /// </summary>
        public bool IsError => Status == FilterStatus.Error;
        
        /// <summary>
        /// Initializes a new instance of FilterResult
        /// </summary>
        public FilterResult()
        {
            Status = FilterStatus.Continue;
            Metadata = new Dictionary<string, object>();
        }
        
        /// <summary>
        /// Initializes a new instance of FilterResult with status
        /// </summary>
        public FilterResult(FilterStatus status) : this()
        {
            Status = status;
        }
        
        /// <summary>
        /// Initializes a new instance of FilterResult with status and data
        /// </summary>
        public FilterResult(FilterStatus status, byte[] data, int offset, int length) : this(status)
        {
            Data = data;
            Offset = offset;
            Length = length;
        }
        
        /// <summary>
        /// Create a success result
        /// </summary>
        public static FilterResult Success()
        {
            return new FilterResult(FilterStatus.Continue);
        }
        
        /// <summary>
        /// Create a success result with data
        /// </summary>
        public static FilterResult Success(byte[] data, int offset, int length)
        {
            return new FilterResult(FilterStatus.Continue, data, offset, length);
        }
        
        /// <summary>
        /// Create an error result
        /// </summary>
        public static FilterResult Error(FilterError errorCode, string message = null)
        {
            return new FilterResult(FilterStatus.Error)
            {
                ErrorCode = errorCode,
                ErrorMessage = message
            };
        }
        
        /// <summary>
        /// Create a stop iteration result
        /// </summary>
        public static FilterResult StopIteration()
        {
            return new FilterResult(FilterStatus.StopIteration);
        }
    }
}