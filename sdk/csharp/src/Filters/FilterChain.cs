using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Core;
using GopherMcp.Types;

namespace GopherMcp.Filters
{
    /// <summary>
    /// Represents a chain of filters for sequential processing
    /// </summary>
    public class FilterChain : IDisposable
    {
        private readonly List<Filter> _filters;
        private readonly ReaderWriterLockSlim _filtersLock;
        private McpChainHandle _handle;
        private ChainConfig _config;
        private bool _disposed;
        private bool _initialized;
        
        /// <summary>
        /// Gets the native chain handle
        /// </summary>
        protected McpChainHandle Handle
        {
            get
            {
                ThrowIfDisposed();
                return _handle;
            }
            set
            {
                _handle = value;
            }
        }
        
        /// <summary>
        /// Gets or sets the chain configuration
        /// </summary>
        public ChainConfig Config
        {
            get => _config;
            set
            {
                ThrowIfDisposed();
                _config = value ?? throw new ArgumentNullException(nameof(value));
            }
        }
        
        /// <summary>
        /// Gets the chain name
        /// </summary>
        public string Name => Config?.Name ?? "FilterChain";
        
        /// <summary>
        /// Gets the number of filters in the chain
        /// </summary>
        public int FilterCount
        {
            get
            {
                _filtersLock.EnterReadLock();
                try
                {
                    return _filters.Count;
                }
                finally
                {
                    _filtersLock.ExitReadLock();
                }
            }
        }
        
        /// <summary>
        /// Gets whether the chain is initialized
        /// </summary>
        public bool IsInitialized => _initialized;
        
        /// <summary>
        /// Gets whether the chain is disposed
        /// </summary>
        public bool IsDisposed => _disposed;
        
        /// <summary>
        /// Event raised when the chain starts processing
        /// </summary>
        public event EventHandler<ChainEventArgs> OnProcessingStart;
        
        /// <summary>
        /// Event raised when the chain completes processing
        /// </summary>
        public event EventHandler<ChainEventArgs> OnProcessingComplete;
        
        /// <summary>
        /// Event raised when a filter in the chain fails
        /// </summary>
        public event EventHandler<ChainFilterErrorEventArgs> OnFilterError;
        
        /// <summary>
        /// Initializes a new instance of the FilterChain class
        /// </summary>
        public FilterChain() : this(null)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterChain class with configuration
        /// </summary>
        /// <param name="config">Chain configuration</param>
        public FilterChain(ChainConfig config)
        {
            _filters = new List<Filter>();
            _filtersLock = new ReaderWriterLockSlim(LockRecursionPolicy.SupportsRecursion);
            _config = config ?? new ChainConfig { Name = "FilterChain" };
        }
        
        /// <summary>
        /// Initializes a new instance of the FilterChain class with filters
        /// </summary>
        /// <param name="filters">Initial filters for the chain</param>
        public FilterChain(IEnumerable<Filter> filters) : this()
        {
            if (filters != null)
            {
                foreach (var filter in filters)
                {
                    AddFilter(filter);
                }
            }
        }
        
        /// <summary>
        /// Adds a filter to the chain
        /// </summary>
        /// <param name="filter">Filter to add</param>
        public void AddFilter(Filter filter)
        {
            ThrowIfDisposed();
            
            if (filter == null)
                throw new ArgumentNullException(nameof(filter));
            
            _filtersLock.EnterWriteLock();
            try
            {
                if (_filters.Contains(filter))
                    throw new InvalidOperationException($"Filter '{filter.Name}' is already in the chain");
                
                _filters.Add(filter);
                
                // Sort by priority if configured
                if (_config?.SortByPriority == true)
                {
                    _filters.Sort((a, b) => a.Config.Priority.CompareTo(b.Config.Priority));
                }
            }
            finally
            {
                _filtersLock.ExitWriteLock();
            }
        }
        
        /// <summary>
        /// Removes a filter from the chain
        /// </summary>
        /// <param name="filter">Filter to remove</param>
        /// <returns>True if the filter was removed, false otherwise</returns>
        public bool RemoveFilter(Filter filter)
        {
            ThrowIfDisposed();
            
            if (filter == null)
                return false;
            
            _filtersLock.EnterWriteLock();
            try
            {
                return _filters.Remove(filter);
            }
            finally
            {
                _filtersLock.ExitWriteLock();
            }
        }
        
        /// <summary>
        /// Removes a filter by name
        /// </summary>
        /// <param name="filterName">Name of the filter to remove</param>
        /// <returns>True if a filter was removed, false otherwise</returns>
        public bool RemoveFilter(string filterName)
        {
            ThrowIfDisposed();
            
            if (string.IsNullOrEmpty(filterName))
                return false;
            
            _filtersLock.EnterWriteLock();
            try
            {
                var filter = _filters.FirstOrDefault(f => f.Name == filterName);
                if (filter != null)
                {
                    return _filters.Remove(filter);
                }
                return false;
            }
            finally
            {
                _filtersLock.ExitWriteLock();
            }
        }
        
        /// <summary>
        /// Clears all filters from the chain
        /// </summary>
        public void ClearFilters()
        {
            ThrowIfDisposed();
            
            _filtersLock.EnterWriteLock();
            try
            {
                _filters.Clear();
            }
            finally
            {
                _filtersLock.ExitWriteLock();
            }
        }
        
        /// <summary>
        /// Gets all filters in the chain
        /// </summary>
        /// <returns>Array of filters in the chain</returns>
        public Filter[] GetFilters()
        {
            ThrowIfDisposed();
            
            _filtersLock.EnterReadLock();
            try
            {
                return _filters.ToArray();
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
        }
        
        /// <summary>
        /// Gets a filter by name
        /// </summary>
        /// <param name="filterName">Name of the filter</param>
        /// <returns>The filter if found, null otherwise</returns>
        public Filter GetFilter(string filterName)
        {
            ThrowIfDisposed();
            
            if (string.IsNullOrEmpty(filterName))
                return null;
            
            _filtersLock.EnterReadLock();
            try
            {
                return _filters.FirstOrDefault(f => f.Name == filterName);
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
        }
        
        /// <summary>
        /// Initializes the filter chain
        /// </summary>
        public async Task InitializeAsync()
        {
            ThrowIfDisposed();
            
            if (_initialized)
                throw new InvalidOperationException("Chain is already initialized");
            
            _filtersLock.EnterReadLock();
            try
            {
                // Initialize all filters
                foreach (var filter in _filters)
                {
                    if (!filter.IsInitialized)
                    {
                        await filter.InitializeAsync().ConfigureAwait(false);
                    }
                }
                
                _initialized = true;
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
        }
        
        /// <summary>
        /// Processes data through the filter chain
        /// </summary>
        /// <param name="buffer">Input buffer</param>
        /// <param name="context">Processing context</param>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Chain processing result</returns>
        public async Task<FilterResult> ProcessAsync(
            byte[] buffer, 
            ProcessingContext context = null,
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            
            if (!_initialized)
                throw new InvalidOperationException("Chain is not initialized");
            
            if (buffer == null)
                throw new ArgumentNullException(nameof(buffer));
            
            // Raise processing start event
            OnProcessingStart?.Invoke(this, new ChainEventArgs(Name, FilterCount));
            
            Filter[] filters;
            _filtersLock.EnterReadLock();
            try
            {
                filters = _filters.ToArray();
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
            
            // Process through each filter sequentially
            var currentBuffer = buffer;
            FilterResult lastResult = null;
            
            foreach (var filter in filters)
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    return new FilterResult
                    {
                        Status = FilterStatus.Error,
                        ErrorCode = FilterError.Timeout,
                        ErrorMessage = "Chain processing cancelled"
                    };
                }
                
                try
                {
                    // Skip disabled filters
                    if (!filter.Config.Enabled)
                        continue;
                    
                    var result = await filter.ProcessAsync(currentBuffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    
                    lastResult = result;
                    
                    // Check if we should stop processing
                    if (result.Status == FilterStatus.StopIteration)
                    {
                        break;
                    }
                    
                    // Check for errors
                    if (result.IsError)
                    {
                        // Raise filter error event
                        OnFilterError?.Invoke(this, new ChainFilterErrorEventArgs(
                            Name, 
                            filter.Name, 
                            result.ErrorCode,
                            result.ErrorMessage));
                        
                        // Check if we should bypass on error
                        if (!filter.Config.BypassOnError)
                        {
                            return result;
                        }
                    }
                    
                    // Use output buffer for next filter if available
                    if (result.Data != null && result.Length > 0)
                    {
                        currentBuffer = new byte[result.Length];
                        Array.Copy(result.Data, result.Offset, currentBuffer, 0, result.Length);
                    }
                }
                catch (Exception ex)
                {
                    // Raise filter error event
                    OnFilterError?.Invoke(this, new ChainFilterErrorEventArgs(
                        Name,
                        filter.Name,
                        FilterError.ProcessingFailed,
                        ex.Message));
                    
                    if (!filter.Config.BypassOnError)
                    {
                        return new FilterResult
                        {
                            Status = FilterStatus.Error,
                            ErrorCode = FilterError.ProcessingFailed,
                            ErrorMessage = $"Filter '{filter.Name}' failed: {ex.Message}"
                        };
                    }
                }
            }
            
            // Raise processing complete event
            OnProcessingComplete?.Invoke(this, new ChainEventArgs(Name, FilterCount));
            
            // Return the last result or a success result
            return lastResult ?? new FilterResult
            {
                Status = FilterStatus.Continue,
                Data = currentBuffer,
                Offset = 0,
                Length = currentBuffer.Length
            };
        }
        
        /// <summary>
        /// Gets aggregated statistics from all filters in the chain
        /// </summary>
        /// <returns>Aggregated statistics</returns>
        public ChainStatistics GetStatistics()
        {
            ThrowIfDisposed();
            
            var stats = new ChainStatistics
            {
                ChainName = Name,
                FilterCount = 0,
                TotalBytesProcessed = 0,
                TotalPacketsProcessed = 0,
                TotalErrors = 0,
                TotalProcessingTimeUs = 0,
                FilterStatistics = new List<FilterStatistics>()
            };
            
            _filtersLock.EnterReadLock();
            try
            {
                stats.FilterCount = _filters.Count;
                
                foreach (var filter in _filters)
                {
                    var filterStats = filter.GetStatistics();
                    stats.FilterStatistics.Add(filterStats);
                    
                    // Aggregate statistics
                    stats.TotalBytesProcessed += filterStats.BytesProcessed;
                    stats.TotalPacketsProcessed += filterStats.PacketsProcessed;
                    stats.TotalErrors += filterStats.ErrorCount;
                    stats.TotalProcessingTimeUs += filterStats.ProcessingTimeUs;
                }
                
                // Calculate average processing time
                if (stats.TotalPacketsProcessed > 0)
                {
                    stats.AverageProcessingTimeUs = (double)stats.TotalProcessingTimeUs / stats.TotalPacketsProcessed;
                }
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
            
            return stats;
        }
        
        /// <summary>
        /// Resets statistics for all filters in the chain
        /// </summary>
        public void ResetStatistics()
        {
            ThrowIfDisposed();
            
            _filtersLock.EnterReadLock();
            try
            {
                foreach (var filter in _filters)
                {
                    filter.ResetStatistics();
                }
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
        }
        
        /// <summary>
        /// Throws if the chain has been disposed
        /// </summary>
        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(Name);
        }
        
        /// <summary>
        /// Disposes the filter chain and all its filters
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }
        
        /// <summary>
        /// Disposes the filter chain
        /// </summary>
        /// <param name="disposing">True if disposing managed resources</param>
        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
                return;
            
            if (disposing)
            {
                _filtersLock?.EnterWriteLock();
                try
                {
                    // Dispose all filters if owned by the chain
                    if (_config?.DisposeFilters == true)
                    {
                        foreach (var filter in _filters)
                        {
                            try
                            {
                                filter?.Dispose();
                            }
                            catch
                            {
                                // Ignore disposal errors
                            }
                        }
                    }
                    
                    _filters?.Clear();
                }
                finally
                {
                    _filtersLock?.ExitWriteLock();
                }
                
                _filtersLock?.Dispose();
                _handle?.Dispose();
            }
            
            _disposed = true;
        }
        
        /// <summary>
        /// Finalizer
        /// </summary>
        ~FilterChain()
        {
            Dispose(false);
        }
    }
    
    /// <summary>
    /// Event arguments for chain events
    /// </summary>
    public class ChainEventArgs : EventArgs
    {
        /// <summary>
        /// Gets the chain name
        /// </summary>
        public string ChainName { get; }
        
        /// <summary>
        /// Gets the number of filters in the chain
        /// </summary>
        public int FilterCount { get; }
        
        /// <summary>
        /// Gets the event timestamp
        /// </summary>
        public DateTime Timestamp { get; }
        
        /// <summary>
        /// Initializes a new instance of ChainEventArgs
        /// </summary>
        public ChainEventArgs(string chainName, int filterCount)
        {
            ChainName = chainName;
            FilterCount = filterCount;
            Timestamp = DateTime.UtcNow;
        }
    }
    
    /// <summary>
    /// Event arguments for chain filter errors
    /// </summary>
    public class ChainFilterErrorEventArgs : ChainEventArgs
    {
        /// <summary>
        /// Gets the filter name that caused the error
        /// </summary>
        public string FilterName { get; }
        
        /// <summary>
        /// Gets the error code
        /// </summary>
        public FilterError ErrorCode { get; }
        
        /// <summary>
        /// Gets the error message
        /// </summary>
        public string ErrorMessage { get; }
        
        /// <summary>
        /// Initializes a new instance of ChainFilterErrorEventArgs
        /// </summary>
        public ChainFilterErrorEventArgs(string chainName, string filterName, FilterError errorCode, string errorMessage)
            : base(chainName, 0)
        {
            FilterName = filterName;
            ErrorCode = errorCode;
            ErrorMessage = errorMessage;
        }
    }
    
    /// <summary>
    /// Chain statistics
    /// </summary>
    public class ChainStatistics
    {
        /// <summary>
        /// Gets or sets the chain name
        /// </summary>
        public string ChainName { get; set; }
        
        /// <summary>
        /// Gets or sets the number of filters
        /// </summary>
        public int FilterCount { get; set; }
        
        /// <summary>
        /// Gets or sets total bytes processed
        /// </summary>
        public ulong TotalBytesProcessed { get; set; }
        
        /// <summary>
        /// Gets or sets total packets processed
        /// </summary>
        public ulong TotalPacketsProcessed { get; set; }
        
        /// <summary>
        /// Gets or sets total errors
        /// </summary>
        public ulong TotalErrors { get; set; }
        
        /// <summary>
        /// Gets or sets total processing time in microseconds
        /// </summary>
        public ulong TotalProcessingTimeUs { get; set; }
        
        /// <summary>
        /// Gets or sets average processing time in microseconds
        /// </summary>
        public double AverageProcessingTimeUs { get; set; }
        
        /// <summary>
        /// Gets or sets individual filter statistics
        /// </summary>
        public List<FilterStatistics> FilterStatistics { get; set; }
    }
}