using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Core;
using GopherMcp.Types;
using GopherMcp.Utils;

namespace GopherMcp.Filters
{
    /// <summary>
    /// Abstract base class for all MCP filters
    /// </summary>
    public abstract class Filter : IDisposable
    {
        private readonly object _syncLock = new object();
        private McpFilterHandle _handle;
        private FilterConfigBase _config;
        private FilterStatistics _statistics;
        private bool _disposed;
        private bool _initialized;
        private CancellationTokenSource _cancellationTokenSource;

        /// <summary>
        /// Gets the native filter handle
        /// </summary>
        internal McpFilterHandle Handle
        {
            get
            {
                ThrowIfDisposed();
                return _handle;
            }
            set
            {
                lock (_syncLock)
                {
                    _handle = value;
                }
            }
        }

        /// <summary>
        /// Gets or sets the filter configuration
        /// </summary>
        public FilterConfigBase Config
        {
            get => _config;
            protected set
            {
                ThrowIfDisposed();
                _config = value ?? throw new ArgumentNullException(nameof(value));
            }
        }

        /// <summary>
        /// Gets the filter name
        /// </summary>
        public string Name => Config?.Name ?? GetType().Name;

        /// <summary>
        /// Gets the filter type
        /// </summary>
        public string Type => Config?.Type ?? GetType().FullName;

        /// <summary>
        /// Gets whether the filter is initialized
        /// </summary>
        public bool IsInitialized => _initialized;

        /// <summary>
        /// Gets whether the filter is disposed
        /// </summary>
        public bool IsDisposed => _disposed;

        /// <summary>
        /// Gets the cancellation token for this filter
        /// </summary>
        protected CancellationToken CancellationToken => _cancellationTokenSource?.Token ?? CancellationToken.None;

        /// <summary>
        /// Event raised when the filter is initialized
        /// </summary>
        public event EventHandler<FilterEventArgs> OnInitialize;

        /// <summary>
        /// Event raised when the filter is destroyed
        /// </summary>
        public event EventHandler<FilterEventArgs> OnDestroy;

        /// <summary>
        /// Event raised when data is processed
        /// </summary>
        public event EventHandler<FilterDataEventArgs> OnData;

        /// <summary>
        /// Event raised when an error occurs
        /// </summary>
        public event EventHandler<FilterErrorEventArgs> OnError;

        /// <summary>
        /// Initializes a new instance of the Filter class
        /// </summary>
        protected Filter() : this(null)
        {
        }

        /// <summary>
        /// Initializes a new instance of the Filter class with configuration
        /// </summary>
        /// <param name="config">Filter configuration</param>
        protected Filter(FilterConfigBase config)
        {
            _config = config;
            if (_config == null)
            {
                // Create a minimal config if none provided
                _config = new MinimalFilterConfig
                {
                    Name = GetType().Name,
                    Type = GetType().FullName
                };
            }
            _statistics = new FilterStatistics();
            _cancellationTokenSource = new CancellationTokenSource();
        }

        /// <summary>
        /// Initializes the filter
        /// </summary>
        public virtual async Task InitializeAsync()
        {
            ThrowIfDisposed();

            if (_initialized)
                throw new InvalidOperationException($"Filter '{Name}' is already initialized");

            try
            {
                // Call derived class initialization
                await OnInitializeAsync().ConfigureAwait(false);

                _initialized = true;

                // Raise initialization event
                RaiseOnInitialize();
            }
            catch (Exception ex)
            {
                RaiseOnError(new FilterException($"Failed to initialize filter '{Name}'", ex));
                throw;
            }
        }

        /// <summary>
        /// Process a JsonRpcMessage through the filter
        /// </summary>
        /// <param name="message">The JSON-RPC message to process</param>
        /// <param name="context">Processing context</param>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Filter processing result</returns>
        public virtual async Task<FilterResult> ProcessAsync(
            GopherMcp.Integration.JsonRpcMessage message,
            ProcessingContext context = null,
            CancellationToken cancellationToken = default)
        {
            if (message == null)
                throw new ArgumentNullException(nameof(message));

            // Serialize message to bytes
            var messageJson = System.Text.Json.JsonSerializer.Serialize(message);
            var messageBytes = System.Text.Encoding.UTF8.GetBytes(messageJson);

            // Ensure context has the original message
            context ??= new ProcessingContext();
            context.SetProperty("JsonRpcMessage", message);

            // Process through the byte array pipeline
            return await ProcessAsync(messageBytes, context, cancellationToken);
        }

        /// <summary>
        /// Processes data through the filter
        /// </summary>
        /// <param name="buffer">Input buffer</param>
        /// <param name="context">Processing context</param>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Filter processing result</returns>
        public virtual async Task<FilterResult> ProcessAsync(
            byte[] buffer,
            ProcessingContext context = null,
            CancellationToken cancellationToken = default)
        {
            // Validate input parameters
            if (buffer == null)
                throw new ArgumentNullException(nameof(buffer));

            // Check disposed state
            ThrowIfDisposed();

            if (!_initialized)
                throw new InvalidOperationException($"Filter '{Name}' is not initialized");

            var stopwatch = Stopwatch.StartNew();
            FilterResult result = null;
            bool success = false;

            try
            {
                // Merge cancellation tokens
                using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(
                    cancellationToken,
                    CancellationToken);

                // Call ProcessInternal on thread pool
                result = await Task.Run(async () =>
                {
                    return await ProcessInternal(buffer, context, linkedCts.Token).ConfigureAwait(false);
                }, linkedCts.Token).ConfigureAwait(false);

                success = result?.IsSuccess == true;

                // Raise OnData event
                if (result != null && result.Data != null)
                {
                    RaiseOnData(result.Data, result.Offset, result.Length, result.Status);
                }
            }
            catch (OperationCanceledException)
            {
                // Handle cancellation
                lock (_syncLock)
                {
                    _statistics.TimeoutCount++;
                }

                result = new FilterResult
                {
                    Status = FilterStatus.Error,
                    ErrorCode = FilterError.Timeout,
                    ErrorMessage = "Processing cancelled or timed out"
                };
            }
            catch (Exception ex)
            {
                // Handle exceptions and convert to FilterResult
                RaiseOnError(ex);

                result = new FilterResult
                {
                    Status = FilterStatus.Error,
                    ErrorCode = FilterError.ProcessingFailed,
                    ErrorMessage = ex.Message
                };

                // Store exception details in metadata
                if (result.Metadata == null)
                    result.Metadata = new Dictionary<string, object>();

                result.Metadata["Exception"] = ex;
                result.Metadata["ExceptionType"] = ex.GetType().Name;
                result.Metadata["StackTrace"] = ex.StackTrace;
            }
            finally
            {
                stopwatch.Stop();

                // Update statistics
                UpdateStatistics(
                    buffer.Length,
                    stopwatch.ElapsedTicks * 1000000 / Stopwatch.Frequency,
                    success);

                if (!success && result?.ErrorCode == FilterError.Bypass)
                {
                    lock (_syncLock)
                    {
                        _statistics.BypassCount++;
                    }
                }
            }

            return result ?? new FilterResult
            {
                Status = FilterStatus.Error,
                ErrorCode = FilterError.ProcessingFailed,
                ErrorMessage = "Processing failed without result"
            };
        }

        /// <summary>
        /// Gets the current filter statistics
        /// </summary>
        /// <returns>Current statistics snapshot</returns>
        public virtual FilterStatistics GetStatistics()
        {
            ThrowIfDisposed();

            lock (_syncLock)
            {
                // Return a copy to prevent external modification
                return new FilterStatistics
                {
                    BytesProcessed = _statistics.BytesProcessed,
                    PacketsProcessed = _statistics.PacketsProcessed,
                    ErrorCount = _statistics.ErrorCount,
                    ProcessingTimeUs = _statistics.ProcessingTimeUs,
                    AverageProcessingTimeUs = _statistics.AverageProcessingTimeUs,
                    MaxProcessingTimeUs = _statistics.MaxProcessingTimeUs,
                    MinProcessingTimeUs = _statistics.MinProcessingTimeUs,
                    CurrentBufferUsage = _statistics.CurrentBufferUsage,
                    PeakBufferUsage = _statistics.PeakBufferUsage,
                    BypassCount = _statistics.BypassCount,
                    TimeoutCount = _statistics.TimeoutCount,
                    ThroughputBps = _statistics.ThroughputBps
                };
            }
        }

        /// <summary>
        /// Resets the filter statistics
        /// </summary>
        public virtual void ResetStatistics()
        {
            ThrowIfDisposed();

            lock (_syncLock)
            {
                _statistics = new FilterStatistics();
            }
        }

        /// <summary>
        /// Updates the filter configuration
        /// </summary>
        /// <param name="config">New configuration</param>
        public virtual async Task UpdateConfigAsync(FilterConfigBase config)
        {
            ThrowIfDisposed();

            if (config == null)
                throw new ArgumentNullException(nameof(config));

            var oldConfig = _config;
            _config = config;

            try
            {
                // Call derived class configuration update
                await OnConfigurationUpdateAsync(oldConfig, config).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                // Rollback on failure
                _config = oldConfig;
                RaiseOnError(new ConfigurationException(
                    $"Failed to update configuration for filter '{Name}'",
                    "Filter",
                    Name,
                    config,
                    null,
                    ex));
                throw;
            }
        }

        /// <summary>
        /// Validates the filter configuration
        /// </summary>
        /// <param name="config">Configuration to validate</param>
        /// <returns>True if valid, false otherwise</returns>
        public virtual bool ValidateConfig(FilterConfigBase config)
        {
            if (config == null)
                return false;

            if (string.IsNullOrWhiteSpace(config.Name))
                return false;

            if (string.IsNullOrWhiteSpace(config.Type))
                return false;

            if (config.MaxBufferSize <= 0)
                return false;

            if (config.TimeoutMs < 0)
                return false;

            // Call derived class validation
            return OnValidateConfig(config);
        }

        /// <summary>
        /// Updates filter statistics after processing
        /// </summary>
        /// <param name="bytesProcessed">Number of bytes processed</param>
        /// <param name="processingTimeUs">Processing time in microseconds</param>
        /// <param name="success">Whether processing was successful</param>
        protected void UpdateStatistics(long bytesProcessed, long processingTimeUs, bool success)
        {
            lock (_syncLock)
            {
                _statistics.BytesProcessed += (ulong)bytesProcessed;
                _statistics.PacketsProcessed++;

                if (!success)
                {
                    _statistics.ErrorCount++;
                }

                _statistics.ProcessingTimeUs += (ulong)processingTimeUs;

                if (_statistics.PacketsProcessed > 0)
                {
                    _statistics.AverageProcessingTimeUs =
                        (double)_statistics.ProcessingTimeUs / _statistics.PacketsProcessed;
                }

                if (processingTimeUs > (long)_statistics.MaxProcessingTimeUs)
                {
                    _statistics.MaxProcessingTimeUs = (ulong)processingTimeUs;
                }

                if (_statistics.MinProcessingTimeUs == 0 || processingTimeUs < (long)_statistics.MinProcessingTimeUs)
                {
                    _statistics.MinProcessingTimeUs = (ulong)processingTimeUs;
                }

                // Calculate throughput (bytes per second)
                if (_statistics.ProcessingTimeUs > 0)
                {
                    _statistics.ThroughputBps =
                        (_statistics.BytesProcessed * 1_000_000.0) / _statistics.ProcessingTimeUs;
                }
            }
        }

        /// <summary>
        /// Raises the OnInitialize event with thread safety and exception handling
        /// </summary>
        protected virtual void RaiseOnInitialize()
        {
            var handler = OnInitialize;
            if (handler != null)
            {
                var args = new FilterEventArgs(Name, Type);
                var exceptions = new List<Exception>();

                foreach (EventHandler<FilterEventArgs> singleHandler in handler.GetInvocationList())
                {
                    try
                    {
                        singleHandler(this, args);
                    }
                    catch (Exception ex)
                    {
                        exceptions.Add(ex);
                    }
                }

                if (exceptions.Count > 0)
                {
                    throw new AggregateException("One or more event handlers threw exceptions", exceptions);
                }
            }
        }

        /// <summary>
        /// Raises the OnInitialize event asynchronously
        /// </summary>
        protected virtual async Task RaiseOnInitializeAsync()
        {
            var handler = OnInitialize;
            if (handler != null)
            {
                var args = new FilterEventArgs(Name, Type);
                var tasks = new List<Task>();

                foreach (EventHandler<FilterEventArgs> singleHandler in handler.GetInvocationList())
                {
                    tasks.Add(Task.Run(() => singleHandler(this, args)));
                }

                await Task.WhenAll(tasks).ConfigureAwait(false);
            }
        }

        /// <summary>
        /// Raises the OnDestroy event with thread safety and exception handling
        /// </summary>
        protected virtual void RaiseOnDestroy()
        {
            var handler = OnDestroy;
            if (handler != null)
            {
                var args = new FilterEventArgs(Name, Type);
                var exceptions = new List<Exception>();

                foreach (EventHandler<FilterEventArgs> singleHandler in handler.GetInvocationList())
                {
                    try
                    {
                        singleHandler(this, args);
                    }
                    catch (Exception ex)
                    {
                        exceptions.Add(ex);
                    }
                }

                // Don't throw during disposal
                if (exceptions.Count > 0 && !_disposed)
                {
                    throw new AggregateException("One or more event handlers threw exceptions", exceptions);
                }
            }
        }

        /// <summary>
        /// Raises the OnDestroy event asynchronously
        /// </summary>
        protected virtual async Task RaiseOnDestroyAsync()
        {
            var handler = OnDestroy;
            if (handler != null)
            {
                var args = new FilterEventArgs(Name, Type);
                var tasks = new List<Task>();

                foreach (EventHandler<FilterEventArgs> singleHandler in handler.GetInvocationList())
                {
                    tasks.Add(Task.Run(() => singleHandler(this, args)));
                }

                try
                {
                    await Task.WhenAll(tasks).ConfigureAwait(false);
                }
                catch
                {
                    // Ignore exceptions during disposal
                }
            }
        }

        /// <summary>
        /// Raises the OnData event with thread safety and exception handling
        /// </summary>
        protected virtual void RaiseOnData(byte[] buffer, int offset, int length, FilterStatus status)
        {
            var handler = OnData;
            if (handler != null)
            {
                var args = new FilterDataEventArgs(Name, Type, buffer, offset, length)
                {
                    Status = status
                };
                var exceptions = new List<Exception>();

                foreach (EventHandler<FilterDataEventArgs> singleHandler in handler.GetInvocationList())
                {
                    try
                    {
                        singleHandler(this, args);
                    }
                    catch (Exception ex)
                    {
                        exceptions.Add(ex);
                    }
                }

                if (exceptions.Count > 0)
                {
                    throw new AggregateException("One or more event handlers threw exceptions", exceptions);
                }
            }
        }

        /// <summary>
        /// Raises the OnData event asynchronously
        /// </summary>
        protected virtual async Task RaiseOnDataAsync(byte[] buffer, int offset, int length, FilterStatus status)
        {
            var handler = OnData;
            if (handler != null)
            {
                var args = new FilterDataEventArgs(Name, Type, buffer, offset, length)
                {
                    Status = status
                };
                var tasks = new List<Task>();

                foreach (EventHandler<FilterDataEventArgs> singleHandler in handler.GetInvocationList())
                {
                    tasks.Add(Task.Run(() => singleHandler(this, args)));
                }

                await Task.WhenAll(tasks).ConfigureAwait(false);
            }
        }

        /// <summary>
        /// Raises the OnError event with thread safety and exception handling
        /// </summary>
        protected virtual void RaiseOnError(Exception exception)
        {
            var handler = OnError;
            if (handler != null)
            {
                var args = new FilterErrorEventArgs(Name, Type, exception);
                var exceptions = new List<Exception>();

                foreach (EventHandler<FilterErrorEventArgs> singleHandler in handler.GetInvocationList())
                {
                    try
                    {
                        singleHandler(this, args);
                    }
                    catch (Exception ex)
                    {
                        exceptions.Add(ex);
                    }
                }

                // Log exceptions but don't throw during error handling
                if (exceptions.Count > 0)
                {
                    Debug.WriteLine($"Error handlers threw {exceptions.Count} exceptions");
                }
            }
        }

        /// <summary>
        /// Raises the OnError event asynchronously
        /// </summary>
        protected virtual async Task RaiseOnErrorAsync(Exception exception)
        {
            var handler = OnError;
            if (handler != null)
            {
                var args = new FilterErrorEventArgs(Name, Type, exception);
                var tasks = new List<Task>();

                foreach (EventHandler<FilterErrorEventArgs> singleHandler in handler.GetInvocationList())
                {
                    tasks.Add(Task.Run(() => singleHandler(this, args)));
                }

                try
                {
                    await Task.WhenAll(tasks).ConfigureAwait(false);
                }
                catch
                {
                    // Ignore exceptions during error handling
                }
            }
        }

        /// <summary>
        /// Throws if the filter has been disposed
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        protected void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(Name);
        }

        /// <summary>
        /// Clears all event subscriptions
        /// </summary>
        public void ClearEventSubscriptions()
        {
            OnInitialize = null;
            OnDestroy = null;
            OnData = null;
            OnError = null;
        }

        /// <summary>
        /// Gets the number of Initialize event subscribers
        /// </summary>
        public int InitializeEventSubscriberCount => OnInitialize?.GetInvocationList().Length ?? 0;

        /// <summary>
        /// Gets the number of Destroy event subscribers
        /// </summary>
        public int DestroyEventSubscriberCount => OnDestroy?.GetInvocationList().Length ?? 0;

        /// <summary>
        /// Gets the number of Data event subscribers
        /// </summary>
        public int DataEventSubscriberCount => OnData?.GetInvocationList().Length ?? 0;

        /// <summary>
        /// Gets the number of Error event subscribers
        /// </summary>
        public int ErrorEventSubscriberCount => OnError?.GetInvocationList().Length ?? 0;

        // ============================================================================
        // Abstract methods for derived classes
        // ============================================================================

        /// <summary>
        /// Internal processing method to be implemented by derived classes
        /// </summary>
        /// <param name="buffer">Input buffer</param>
        /// <param name="context">Processing context</param>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Filter processing result</returns>
        protected virtual Task<FilterResult> ProcessInternal(
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            // Default implementation - pass through
            return Task.FromResult(new FilterResult
            {
                Status = FilterStatus.Continue,
                Data = buffer,
                Offset = 0,
                Length = buffer?.Length ?? 0
            });
        }

        /// <summary>
        /// Called when the filter is being initialized
        /// </summary>
        protected virtual Task OnInitializeAsync()
        {
            return Task.CompletedTask;
        }

        /// <summary>
        /// Updates the filter configuration
        /// </summary>
        /// <param name="config">New configuration</param>
        public virtual void UpdateConfig(FilterConfigBase config)
        {
            if (config == null)
                throw new ArgumentNullException(nameof(config));

            _config = config;
        }

        /// <summary>
        /// Called when the filter configuration is being updated
        /// </summary>
        /// <param name="oldConfig">Previous configuration</param>
        /// <param name="newConfig">New configuration</param>
        protected virtual Task OnConfigurationUpdateAsync(FilterConfigBase oldConfig, FilterConfigBase newConfig)
        {
            return Task.CompletedTask;
        }

        /// <summary>
        /// Called to validate filter configuration
        /// </summary>
        /// <param name="config">Configuration to validate</param>
        /// <returns>True if valid, false otherwise</returns>
        protected virtual bool OnValidateConfig(FilterConfigBase config)
        {
            return config != null;
        }

        /// <summary>
        /// Called when the filter is being disposed
        /// </summary>
        /// <param name="disposing">True if disposing managed resources</param>
        protected virtual void OnDispose(bool disposing)
        {
            // Default cleanup
        }

        // ============================================================================
        // IDisposable implementation
        // ============================================================================

        /// <summary>
        /// Disposes the filter and releases all resources
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        /// <summary>
        /// Disposes the filter
        /// </summary>
        /// <param name="disposing">True if disposing managed resources</param>
        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
                return;

            if (disposing)
            {
                try
                {
                    // Raise destroy event before disposal
                    RaiseOnDestroy();
                }
                catch
                {
                    // Ignore exceptions during disposal
                }

                // Cancel any pending operations
                _cancellationTokenSource?.Cancel();
                _cancellationTokenSource?.Dispose();
                _cancellationTokenSource = null;

                // Call derived class disposal
                try
                {
                    OnDispose(true);
                }
                catch
                {
                    // Ignore exceptions from derived disposal
                }

                // Dispose native handle
                _handle?.Dispose();
                _handle = null;
            }

            _disposed = true;
        }

        /// <summary>
        /// Finalizer
        /// </summary>
        ~Filter()
        {
            Dispose(false);
        }
    }

    /// <summary>
    /// Event arguments for filter errors
    /// </summary>
    public class FilterErrorEventArgs : FilterEventArgs
    {
        /// <summary>
        /// Gets the exception that occurred
        /// </summary>
        public Exception Exception { get; }

        /// <summary>
        /// Gets the error code if available
        /// </summary>
        public FilterError ErrorCode { get; }

        /// <summary>
        /// Initializes a new instance of FilterErrorEventArgs
        /// </summary>
        public FilterErrorEventArgs(string filterName, string filterType, Exception exception)
            : base(filterName, filterType)
        {
            Exception = exception;

            // Extract error code if it's a FilterException
            if (exception is FilterException filterEx)
            {
                ErrorCode = filterEx.FilterErrorCode;
            }
            else
            {
                ErrorCode = FilterError.ProcessingFailed;
            }
        }
    }
}
