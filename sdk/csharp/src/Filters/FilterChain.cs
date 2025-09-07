using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
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
        internal McpChainHandle Handle
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
        /// Event raised when the chain is modified
        /// </summary>
        public event EventHandler<ChainModifiedEventArgs> OnChainModified;
        
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
        /// <param name="position">Position to add the filter (default: Last)</param>
        public void AddFilter(Filter filter, FilterPosition position = FilterPosition.Last)
        {
            ThrowIfDisposed();
            
            if (filter == null)
                throw new ArgumentNullException(nameof(filter));
            
            _filtersLock.EnterWriteLock();
            try
            {
                if (_filters.Contains(filter))
                    throw new InvalidOperationException($"Filter '{filter.Name}' is already in the chain");
                
                // Add filter based on position
                switch (position)
                {
                    case FilterPosition.First:
                        _filters.Insert(0, filter);
                        break;
                    
                    case FilterPosition.Last:
                        _filters.Add(filter);
                        break;
                    
                    default:
                        _filters.Add(filter);
                        break;
                }
                
                // Sort by priority if configured
                if (_config?.SortByPriority == true)
                {
                    _filters.Sort((a, b) => a.Config.Priority.CompareTo(b.Config.Priority));
                }
                
                // Update native chain via P/Invoke if handle is valid
                if (_handle != null && !_handle.IsInvalid)
                {
                    UpdateNativeChain();
                }
                
                // Raise chain modified event
                OnChainModified?.Invoke(this, new ChainModifiedEventArgs(
                    Name,
                    ChainModificationType.FilterAdded,
                    filter.Name));
            }
            finally
            {
                _filtersLock.ExitWriteLock();
            }
        }
        
        /// <summary>
        /// Adds a filter before another filter
        /// </summary>
        /// <param name="filter">Filter to add</param>
        /// <param name="beforeFilterName">Name of the filter to insert before</param>
        public void AddFilterBefore(Filter filter, string beforeFilterName)
        {
            ThrowIfDisposed();
            
            if (filter == null)
                throw new ArgumentNullException(nameof(filter));
            
            if (string.IsNullOrEmpty(beforeFilterName))
                throw new ArgumentNullException(nameof(beforeFilterName));
            
            _filtersLock.EnterWriteLock();
            try
            {
                if (_filters.Contains(filter))
                    throw new InvalidOperationException($"Filter '{filter.Name}' is already in the chain");
                
                var index = _filters.FindIndex(f => f.Name == beforeFilterName);
                if (index < 0)
                    throw new InvalidOperationException($"Filter '{beforeFilterName}' not found in chain");
                
                _filters.Insert(index, filter);
                
                // Update native chain via P/Invoke if handle is valid
                if (_handle != null && !_handle.IsInvalid)
                {
                    UpdateNativeChain();
                }
                
                // Raise chain modified event
                OnChainModified?.Invoke(this, new ChainModifiedEventArgs(
                    Name,
                    ChainModificationType.FilterAdded,
                    filter.Name));
            }
            finally
            {
                _filtersLock.ExitWriteLock();
            }
        }
        
        /// <summary>
        /// Adds a filter after another filter
        /// </summary>
        /// <param name="filter">Filter to add</param>
        /// <param name="afterFilterName">Name of the filter to insert after</param>
        public void AddFilterAfter(Filter filter, string afterFilterName)
        {
            ThrowIfDisposed();
            
            if (filter == null)
                throw new ArgumentNullException(nameof(filter));
            
            if (string.IsNullOrEmpty(afterFilterName))
                throw new ArgumentNullException(nameof(afterFilterName));
            
            _filtersLock.EnterWriteLock();
            try
            {
                if (_filters.Contains(filter))
                    throw new InvalidOperationException($"Filter '{filter.Name}' is already in the chain");
                
                var index = _filters.FindIndex(f => f.Name == afterFilterName);
                if (index < 0)
                    throw new InvalidOperationException($"Filter '{afterFilterName}' not found in chain");
                
                _filters.Insert(index + 1, filter);
                
                // Update native chain via P/Invoke if handle is valid
                if (_handle != null && !_handle.IsInvalid)
                {
                    UpdateNativeChain();
                }
                
                // Raise chain modified event
                OnChainModified?.Invoke(this, new ChainModifiedEventArgs(
                    Name,
                    ChainModificationType.FilterAdded,
                    filter.Name));
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
                bool removed = _filters.Remove(filter);
                
                if (removed)
                {
                    // Update native chain via P/Invoke if handle is valid
                    if (_handle != null && !_handle.IsInvalid)
                    {
                        UpdateNativeChain();
                    }
                    
                    // Raise chain modified event
                    OnChainModified?.Invoke(this, new ChainModifiedEventArgs(
                        Name,
                        ChainModificationType.FilterRemoved,
                        filter.Name));
                }
                
                return removed;
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
                
                // Update native chain via P/Invoke if handle is valid
                if (_handle != null && !_handle.IsInvalid)
                {
                    UpdateNativeChain();
                }
                
                // Raise chain modified event
                OnChainModified?.Invoke(this, new ChainModifiedEventArgs(
                    Name,
                    ChainModificationType.ChainCleared,
                    null));
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
                filters = _filters.Where(f => f.Config.Enabled).ToArray();
            }
            finally
            {
                _filtersLock.ExitReadLock();
            }
            
            FilterResult result;
            
            // Process based on execution mode
            var executionMode = _config?.ExecutionMode ?? ChainExecutionMode.Sequential;
            
            switch (executionMode)
            {
                case ChainExecutionMode.Sequential:
                    result = await ProcessSequentialAsync(filters, buffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    break;
                    
                case ChainExecutionMode.Parallel:
                    result = await ProcessParallelAsync(filters, buffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    break;
                    
                case ChainExecutionMode.Conditional:
                    result = await ProcessConditionalAsync(filters, buffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    break;
                    
                case ChainExecutionMode.Pipeline:
                    result = await ProcessPipelineAsync(filters, buffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    break;
                    
                default:
                    result = await ProcessSequentialAsync(filters, buffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    break;
            }
            
            // Raise processing complete event
            OnProcessingComplete?.Invoke(this, new ChainEventArgs(Name, FilterCount));
            
            return result;
        }
        
        /// <summary>
        /// Processes filters sequentially
        /// </summary>
        private async Task<FilterResult> ProcessSequentialAsync(
            Filter[] filters,
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
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
                        OnFilterError?.Invoke(this, new ChainFilterErrorEventArgs(
                            Name, filter.Name, result.ErrorCode, result.ErrorMessage));
                        
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
                    OnFilterError?.Invoke(this, new ChainFilterErrorEventArgs(
                        Name, filter.Name, FilterError.ProcessingFailed, ex.Message));
                    
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
            
            return lastResult ?? new FilterResult
            {
                Status = FilterStatus.Continue,
                Data = currentBuffer,
                Offset = 0,
                Length = currentBuffer.Length
            };
        }
        
        /// <summary>
        /// Processes filters in parallel
        /// </summary>
        private async Task<FilterResult> ProcessParallelAsync(
            Filter[] filters,
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            if (filters.Length == 0)
            {
                return new FilterResult
                {
                    Status = FilterStatus.Continue,
                    Data = buffer,
                    Offset = 0,
                    Length = buffer.Length
                };
            }
            
            // Process all filters in parallel
            var tasks = filters.Select(filter => 
                ProcessFilterSafelyAsync(filter, buffer, context, cancellationToken))
                .ToArray();
            
            var results = await Task.WhenAll(tasks).ConfigureAwait(false);
            
            // Aggregate results
            var errors = results.Where(r => r.IsError).ToArray();
            if (errors.Length > 0)
            {
                // Return first error
                return errors[0];
            }
            
            // Return the last successful result
            var successResults = results.Where(r => !r.IsError).ToArray();
            if (successResults.Length > 0)
            {
                return successResults[successResults.Length - 1];
            }
            
            return new FilterResult
            {
                Status = FilterStatus.Continue,
                Data = buffer,
                Offset = 0,
                Length = buffer.Length
            };
        }
        
        /// <summary>
        /// Processes filters conditionally based on conditions
        /// </summary>
        private async Task<FilterResult> ProcessConditionalAsync(
            Filter[] filters,
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
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
                
                // Evaluate condition (using metadata or context)
                bool shouldProcess = EvaluateFilterCondition(filter, context, lastResult);
                
                if (!shouldProcess)
                    continue;
                
                try
                {
                    var result = await filter.ProcessAsync(currentBuffer, context, cancellationToken)
                        .ConfigureAwait(false);
                    
                    lastResult = result;
                    
                    if (result.Status == FilterStatus.StopIteration)
                    {
                        break;
                    }
                    
                    if (result.IsError && !filter.Config.BypassOnError)
                    {
                        return result;
                    }
                    
                    if (result.Data != null && result.Length > 0)
                    {
                        currentBuffer = new byte[result.Length];
                        Array.Copy(result.Data, result.Offset, currentBuffer, 0, result.Length);
                    }
                }
                catch (Exception ex)
                {
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
            
            return lastResult ?? new FilterResult
            {
                Status = FilterStatus.Continue,
                Data = currentBuffer,
                Offset = 0,
                Length = currentBuffer.Length
            };
        }
        
        /// <summary>
        /// Processes filters using pipeline pattern with channels
        /// </summary>
        private async Task<FilterResult> ProcessPipelineAsync(
            Filter[] filters,
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            if (filters.Length == 0)
            {
                return new FilterResult
                {
                    Status = FilterStatus.Continue,
                    Data = buffer,
                    Offset = 0,
                    Length = buffer.Length
                };
            }
            
            // Create channels for pipeline
            var channels = new Channel<FilterData>[filters.Length + 1];
            for (int i = 0; i <= filters.Length; i++)
            {
                channels[i] = Channel.CreateUnbounded<FilterData>(new UnboundedChannelOptions
                {
                    SingleReader = true,
                    SingleWriter = true
                });
            }
            
            // Start pipeline stages
            var tasks = new Task[filters.Length];
            for (int i = 0; i < filters.Length; i++)
            {
                int filterIndex = i;
                var filter = filters[filterIndex];
                var inputChannel = channels[filterIndex];
                var outputChannel = channels[filterIndex + 1];
                
                tasks[filterIndex] = Task.Run(async () =>
                {
                    await foreach (var data in inputChannel.Reader.ReadAllAsync(cancellationToken))
                    {
                        try
                        {
                            var result = await filter.ProcessAsync(data.Buffer, context, cancellationToken)
                                .ConfigureAwait(false);
                            
                            if (result.IsError && !filter.Config.BypassOnError)
                            {
                                await outputChannel.Writer.WriteAsync(new FilterData
                                {
                                    Buffer = data.Buffer,
                                    Result = result,
                                    IsError = true
                                }, cancellationToken).ConfigureAwait(false);
                                break;
                            }
                            
                            var outputBuffer = result.Data != null && result.Length > 0
                                ? ExtractBuffer(result)
                                : data.Buffer;
                            
                            await outputChannel.Writer.WriteAsync(new FilterData
                            {
                                Buffer = outputBuffer,
                                Result = result,
                                IsError = false
                            }, cancellationToken).ConfigureAwait(false);
                        }
                        catch (Exception ex)
                        {
                            await outputChannel.Writer.WriteAsync(new FilterData
                            {
                                Buffer = data.Buffer,
                                Result = new FilterResult
                                {
                                    Status = FilterStatus.Error,
                                    ErrorCode = FilterError.ProcessingFailed,
                                    ErrorMessage = ex.Message
                                },
                                IsError = true
                            }, cancellationToken).ConfigureAwait(false);
                            break;
                        }
                    }
                    
                    outputChannel.Writer.Complete();
                }, cancellationToken);
            }
            
            // Write initial data
            await channels[0].Writer.WriteAsync(new FilterData { Buffer = buffer }, cancellationToken)
                .ConfigureAwait(false);
            channels[0].Writer.Complete();
            
            // Read final result
            FilterData finalData = null;
            await foreach (var data in channels[filters.Length].Reader.ReadAllAsync(cancellationToken))
            {
                finalData = data;
            }
            
            // Wait for all pipeline stages to complete
            await Task.WhenAll(tasks).ConfigureAwait(false);
            
            if (finalData?.Result != null)
            {
                return finalData.Result;
            }
            
            return new FilterResult
            {
                Status = FilterStatus.Continue,
                Data = finalData?.Buffer ?? buffer,
                Offset = 0,
                Length = finalData?.Buffer?.Length ?? buffer.Length
            };
        }
        
        /// <summary>
        /// Safely processes a single filter with exception handling
        /// </summary>
        private async Task<FilterResult> ProcessFilterSafelyAsync(
            Filter filter,
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            try
            {
                return await filter.ProcessAsync(buffer, context, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                OnFilterError?.Invoke(this, new ChainFilterErrorEventArgs(
                    Name, filter.Name, FilterError.ProcessingFailed, ex.Message));
                
                return new FilterResult
                {
                    Status = FilterStatus.Error,
                    ErrorCode = FilterError.ProcessingFailed,
                    ErrorMessage = $"Filter '{filter.Name}' failed: {ex.Message}"
                };
            }
        }
        
        /// <summary>
        /// Evaluates whether a filter should be processed based on conditions
        /// </summary>
        private bool EvaluateFilterCondition(Filter filter, ProcessingContext context, FilterResult previousResult)
        {
            // Check filter settings for condition
            if (filter.Config.Settings?.TryGetValue("Condition", out var condition) == true)
            {
                // Evaluate condition based on context or previous result
                if (condition is string conditionStr)
                {
                    // Simple condition evaluation
                    if (conditionStr == "OnError" && previousResult?.IsError == true)
                        return true;
                    if (conditionStr == "OnSuccess" && previousResult?.IsError == false)
                        return true;
                    if (conditionStr == "Always")
                        return true;
                    if (conditionStr == "Never")
                        return false;
                }
            }
            
            // Default: process the filter
            return true;
        }
        
        /// <summary>
        /// Extracts buffer from filter result
        /// </summary>
        private byte[] ExtractBuffer(FilterResult result)
        {
            if (result.Data == null || result.Length == 0)
                return new byte[0];
            
            var buffer = new byte[result.Length];
            Array.Copy(result.Data, result.Offset, buffer, 0, result.Length);
            return buffer;
        }
        
        /// <summary>
        /// Internal class for pipeline data
        /// </summary>
        private class FilterData
        {
            public byte[] Buffer { get; set; }
            public FilterResult Result { get; set; }
            public bool IsError { get; set; }
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
        /// Updates the native chain via P/Invoke
        /// </summary>
        private void UpdateNativeChain()
        {
            if (_handle == null || _handle.IsInvalid)
                return;
            
            // This would call the native API to update the chain
            // For now, it's a placeholder for when P/Invoke is fully implemented
            // Example: McpFilterChainApi.mcp_chain_update(_handle, ...);
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
    /// Event arguments for chain modification events
    /// </summary>
    public class ChainModifiedEventArgs : ChainEventArgs
    {
        /// <summary>
        /// Gets the type of modification
        /// </summary>
        public ChainModificationType ModificationType { get; }
        
        /// <summary>
        /// Gets the name of the affected filter
        /// </summary>
        public string FilterName { get; }
        
        /// <summary>
        /// Initializes a new instance of ChainModifiedEventArgs
        /// </summary>
        public ChainModifiedEventArgs(string chainName, ChainModificationType modificationType, string filterName)
            : base(chainName, 0)
        {
            ModificationType = modificationType;
            FilterName = filterName;
        }
    }
    
    /// <summary>
    /// Types of chain modifications
    /// </summary>
    public enum ChainModificationType
    {
        /// <summary>
        /// A filter was added to the chain
        /// </summary>
        FilterAdded,
        
        /// <summary>
        /// A filter was removed from the chain
        /// </summary>
        FilterRemoved,
        
        /// <summary>
        /// The chain was cleared
        /// </summary>
        ChainCleared,
        
        /// <summary>
        /// The chain order was modified
        /// </summary>
        ChainReordered
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