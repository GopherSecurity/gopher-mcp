using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Core;
using GopherMcp.Filters;
using GopherMcp.Types;
using GopherMcp.Utils;

namespace GopherMcp.Manager
{
    /// <summary>
    /// Manages filters and filter chains for message processing.
    /// Provides centralized filter registry and chain management.
    /// </summary>
    public class FilterManager : IDisposable
    {
        private readonly McpManagerHandle _handle;
        private readonly FilterManagerConfig _config;
        private readonly Dictionary<Guid, Filter> _filterRegistry;
        private readonly List<FilterChain> _chains;
        private readonly ReaderWriterLockSlim _registryLock;
        private readonly ReaderWriterLockSlim _chainsLock;
        private readonly CallbackManager _callbackManager;
        private bool _disposed;

        /// <summary>
        /// Initializes a new instance of the FilterManager class with default configuration.
        /// </summary>
        public FilterManager() : this(new FilterManagerConfig())
        {
        }

        /// <summary>
        /// Initializes a new instance of the FilterManager class with specified configuration.
        /// </summary>
        /// <param name="config">The manager configuration.</param>
        public FilterManager(FilterManagerConfig? config)
        {
            _config = config ?? new FilterManagerConfig();
            _filterRegistry = new Dictionary<Guid, Filter>();
            _chains = new List<FilterChain>();
            _registryLock = new ReaderWriterLockSlim();
            _chainsLock = new ReaderWriterLockSlim();
            _callbackManager = new CallbackManager();

            // Create native manager handle via P/Invoke
            var handle = McpFilterApi.mcp_filter_manager_create(
                _config.Name,
                _config.MaxConcurrency,
                _config.EnableStatistics ? McpBool.True : McpBool.False
            );

            if (handle == 0)
            {
                throw new McpException("Failed to create native filter manager");
            }

            _handle = McpManagerHandle.FromULong(handle);

            // Set up logging if configured
            if (_config.EnableLogging)
            {
                ConfigureLogging();
            }

            // Initialize default filters if configured
            if (_config.EnableDefaultFilters)
            {
                InitializeDefaultFilters();
            }
        }

        /// <summary>
        /// Gets the manager configuration.
        /// </summary>
        public FilterManagerConfig Configuration => _config;

        /// <summary>
        /// Gets the number of registered filters.
        /// </summary>
        public int FilterCount
        {
            get
            {
                _registryLock.EnterReadLock();
                try
                {
                    return _filterRegistry.Count;
                }
                finally
                {
                    _registryLock.ExitReadLock();
                }
            }
        }

        /// <summary>
        /// Gets the number of filter chains.
        /// </summary>
        public int ChainCount
        {
            get
            {
                _chainsLock.EnterReadLock();
                try
                {
                    return _chains.Count;
                }
                finally
                {
                    _chainsLock.ExitReadLock();
                }
            }
        }

        /// <summary>
        /// Occurs when a filter is registered.
        /// </summary>
        public event EventHandler<FilterRegisteredEventArgs>? FilterRegistered;

        /// <summary>
        /// Occurs when a filter is unregistered.
        /// </summary>
        public event EventHandler<FilterUnregisteredEventArgs>? FilterUnregistered;

        /// <summary>
        /// Occurs when a chain is created.
        /// </summary>
        public event EventHandler<ChainCreatedEventArgs>? ChainCreated;

        /// <summary>
        /// Occurs when a chain is removed.
        /// </summary>
        public event EventHandler<ChainRemovedEventArgs>? ChainRemoved;

        /// <summary>
        /// Occurs when processing starts.
        /// </summary>
        public event EventHandler<ProcessingStartEventArgs>? ProcessingStart;

        /// <summary>
        /// Occurs when processing completes.
        /// </summary>
        public event EventHandler<ProcessingCompleteEventArgs>? ProcessingComplete;

        /// <summary>
        /// Occurs when a processing error happens.
        /// </summary>
        public event EventHandler<ProcessingErrorEventArgs>? ProcessingError;

        /// <summary>
        /// Gets the manager statistics.
        /// </summary>
        public ManagerStatistics GetStatistics()
        {
            ThrowIfDisposed();
            
            var stats = new ManagerStatistics
            {
                FilterCount = FilterCount,
                ChainCount = ChainCount,
                TotalProcessed = 0,
                TotalErrors = 0
            };

            // Aggregate statistics from all chains
            _chainsLock.EnterReadLock();
            try
            {
                foreach (var chain in _chains)
                {
                    var chainStats = chain.GetStatistics();
                    stats.TotalProcessed += chainStats.TotalProcessed;
                    stats.TotalErrors += chainStats.TotalErrors;
                    stats.TotalProcessingTime += chainStats.TotalProcessingTime;
                }
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }

            return stats;
        }

        /// <summary>
        /// Resets all statistics.
        /// </summary>
        public void ResetStatistics()
        {
            ThrowIfDisposed();

            _chainsLock.EnterReadLock();
            try
            {
                foreach (var chain in _chains)
                {
                    chain.ResetStatistics();
                }
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }
        }

        /// <summary>
        /// Gets all registered filters.
        /// </summary>
        public IReadOnlyList<Filter> GetFilters()
        {
            ThrowIfDisposed();

            _registryLock.EnterReadLock();
            try
            {
                return new List<Filter>(_filterRegistry.Values);
            }
            finally
            {
                _registryLock.ExitReadLock();
            }
        }

        /// <summary>
        /// Gets all filter chains.
        /// </summary>
        public IReadOnlyList<FilterChain> GetChains()
        {
            ThrowIfDisposed();

            _chainsLock.EnterReadLock();
            try
            {
                return new List<FilterChain>(_chains);
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }
        }

        /// <summary>
        /// Finds a filter by ID.
        /// </summary>
        public Filter? FindFilter(Guid filterId)
        {
            ThrowIfDisposed();

            _registryLock.EnterReadLock();
            try
            {
                return _filterRegistry.TryGetValue(filterId, out var filter) ? filter : null;
            }
            finally
            {
                _registryLock.ExitReadLock();
            }
        }

        /// <summary>
        /// Finds a chain by name.
        /// </summary>
        public FilterChain? FindChain(string chainName)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(chainName);

            _chainsLock.EnterReadLock();
            try
            {
                return _chains.Find(c => c.Name == chainName);
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }
        }

        /// <summary>
        /// Removes a filter from the registry.
        /// </summary>
        public bool UnregisterFilter(Guid filterId)
        {
            ThrowIfDisposed();

            Filter? removedFilter = null;
            
            _registryLock.EnterWriteLock();
            try
            {
                if (_filterRegistry.TryGetValue(filterId, out removedFilter))
                {
                    _filterRegistry.Remove(filterId);
                }
            }
            finally
            {
                _registryLock.ExitWriteLock();
            }

            if (removedFilter != null)
            {
                OnFilterUnregistered(new FilterUnregisteredEventArgs(filterId, removedFilter));
                return true;
            }

            return false;
        }

        /// <summary>
        /// Removes a chain from the manager.
        /// </summary>
        public bool RemoveChain(string chainName)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(chainName);

            FilterChain? removedChain = null;

            _chainsLock.EnterWriteLock();
            try
            {
                removedChain = _chains.Find(c => c.Name == chainName);
                if (removedChain != null)
                {
                    _chains.Remove(removedChain);
                }
            }
            finally
            {
                _chainsLock.ExitWriteLock();
            }

            if (removedChain != null)
            {
                OnChainRemoved(new ChainRemovedEventArgs(chainName, removedChain));
                removedChain.Dispose();
                return true;
            }

            return false;
        }

        /// <summary>
        /// Disposes the manager and all managed resources.
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        /// <summary>
        /// Disposes managed and unmanaged resources.
        /// </summary>
        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    // Dispose all chains
                    _chainsLock.EnterWriteLock();
                    try
                    {
                        foreach (var chain in _chains)
                        {
                            chain.Dispose();
                        }
                        _chains.Clear();
                    }
                    finally
                    {
                        _chainsLock.ExitWriteLock();
                    }

                    // Clear filter registry
                    _registryLock.EnterWriteLock();
                    try
                    {
                        foreach (var filter in _filterRegistry.Values)
                        {
                            filter.Dispose();
                        }
                        _filterRegistry.Clear();
                    }
                    finally
                    {
                        _registryLock.ExitWriteLock();
                    }

                    // Dispose locks
                    _registryLock.Dispose();
                    _chainsLock.Dispose();

                    // Dispose callback manager
                    _callbackManager.Dispose();

                    // Dispose native handle
                    _handle?.Dispose();
                }

                _disposed = true;
            }
        }

        /// <summary>
        /// Throws if the manager has been disposed.
        /// </summary>
        protected void ThrowIfDisposed()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(FilterManager));
            }
        }

        /// <summary>
        /// Raises the FilterRegistered event.
        /// </summary>
        protected virtual void OnFilterRegistered(FilterRegisteredEventArgs e)
        {
            FilterRegistered?.Invoke(this, e);
        }

        /// <summary>
        /// Raises the FilterUnregistered event.
        /// </summary>
        protected virtual void OnFilterUnregistered(FilterUnregisteredEventArgs e)
        {
            FilterUnregistered?.Invoke(this, e);
        }

        /// <summary>
        /// Raises the ChainCreated event.
        /// </summary>
        protected virtual void OnChainCreated(ChainCreatedEventArgs e)
        {
            ChainCreated?.Invoke(this, e);
        }

        /// <summary>
        /// Raises the ChainRemoved event.
        /// </summary>
        protected virtual void OnChainRemoved(ChainRemovedEventArgs e)
        {
            ChainRemoved?.Invoke(this, e);
        }

        /// <summary>
        /// Raises the ProcessingStart event.
        /// </summary>
        protected virtual void OnProcessingStart(ProcessingStartEventArgs e)
        {
            ProcessingStart?.Invoke(this, e);
        }

        /// <summary>
        /// Raises the ProcessingComplete event.
        /// </summary>
        protected virtual void OnProcessingComplete(ProcessingCompleteEventArgs e)
        {
            ProcessingComplete?.Invoke(this, e);
        }

        /// <summary>
        /// Raises the ProcessingError event.
        /// </summary>
        protected virtual void OnProcessingError(ProcessingErrorEventArgs e)
        {
            ProcessingError?.Invoke(this, e);
        }

        /// <summary>
        /// Configures logging for the manager.
        /// </summary>
        private void ConfigureLogging()
        {
            // Set log level via P/Invoke
            McpFilterApi.mcp_filter_manager_set_log_level(
                _handle.DangerousGetHandle().ToUInt64(),
                _config.LogLevel
            );

            // Register log callback if needed
            if (_config.LogLevel != McpLogLevel.None)
            {
                var logCallback = new McpLogCallback((level, message, context) =>
                {
                    // Forward to .NET logging system
                    Console.WriteLine($"[{level}] {message}");
                });

                _callbackManager.RegisterCallback("log", logCallback);
                
                McpFilterApi.mcp_filter_manager_set_log_callback(
                    _handle.DangerousGetHandle().ToUInt64(),
                    logCallback,
                    IntPtr.Zero
                );
            }
        }

        /// <summary>
        /// Initializes default filters based on configuration.
        /// </summary>
        private void InitializeDefaultFilters()
        {
            // Create and register default filters
            // This would typically include:
            // - Validation filter
            // - Logging filter
            // - Metrics filter
            // - Error handling filter
            
            // For now, this is a placeholder for future implementation
            // when we have specific filter implementations
        }
    }

    /// <summary>
    /// Configuration for FilterManager.
    /// </summary>
    public class FilterManagerConfig
    {
        /// <summary>
        /// Gets or sets the manager name.
        /// </summary>
        public string Name { get; set; } = "DefaultManager";

        /// <summary>
        /// Gets or sets whether to enable statistics collection.
        /// </summary>
        public bool EnableStatistics { get; set; } = true;

        /// <summary>
        /// Gets or sets the maximum number of concurrent operations.
        /// </summary>
        public int MaxConcurrency { get; set; } = Environment.ProcessorCount;

        /// <summary>
        /// Gets or sets the default timeout for operations.
        /// </summary>
        public TimeSpan DefaultTimeout { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Gets or sets whether to enable logging.
        /// </summary>
        public bool EnableLogging { get; set; } = false;

        /// <summary>
        /// Gets or sets the log level.
        /// </summary>
        public McpLogLevel LogLevel { get; set; } = McpLogLevel.Info;

        /// <summary>
        /// Gets or sets whether to enable default filters.
        /// </summary>
        public bool EnableDefaultFilters { get; set; } = true;

        /// <summary>
        /// Gets or sets additional settings.
        /// </summary>
        public Dictionary<string, object> Settings { get; set; } = new();
    }

    /// <summary>
    /// Manager statistics.
    /// </summary>
    public struct ManagerStatistics
    {
        public int FilterCount { get; set; }
        public int ChainCount { get; set; }
        public long TotalProcessed { get; set; }
        public long TotalErrors { get; set; }
        public TimeSpan TotalProcessingTime { get; set; }
    }

    /// <summary>
    /// Event args for filter registration.
    /// </summary>
    public class FilterRegisteredEventArgs : EventArgs
    {
        public Guid FilterId { get; }
        public Filter Filter { get; }

        public FilterRegisteredEventArgs(Guid filterId, Filter filter)
        {
            FilterId = filterId;
            Filter = filter;
        }
    }

    /// <summary>
    /// Event args for filter unregistration.
    /// </summary>
    public class FilterUnregisteredEventArgs : EventArgs
    {
        public Guid FilterId { get; }
        public Filter Filter { get; }

        public FilterUnregisteredEventArgs(Guid filterId, Filter filter)
        {
            FilterId = filterId;
            Filter = filter;
        }
    }

    /// <summary>
    /// Event args for chain creation.
    /// </summary>
    public class ChainCreatedEventArgs : EventArgs
    {
        public string ChainName { get; }
        public FilterChain Chain { get; }

        public ChainCreatedEventArgs(string chainName, FilterChain chain)
        {
            ChainName = chainName;
            Chain = chain;
        }
    }

    /// <summary>
    /// Event args for chain removal.
    /// </summary>
    public class ChainRemovedEventArgs : EventArgs
    {
        public string ChainName { get; }
        public FilterChain Chain { get; }

        public ChainRemovedEventArgs(string chainName, FilterChain chain)
        {
            ChainName = chainName;
            Chain = chain;
        }
    }

    /// <summary>
    /// Event args for processing start.
    /// </summary>
    public class ProcessingStartEventArgs : EventArgs
    {
        public string ChainName { get; }
        public ProcessingContext Context { get; }

        public ProcessingStartEventArgs(string chainName, ProcessingContext context)
        {
            ChainName = chainName;
            Context = context;
        }
    }

    /// <summary>
    /// Event args for processing complete.
    /// </summary>
    public class ProcessingCompleteEventArgs : EventArgs
    {
        public string ChainName { get; }
        public ProcessingContext Context { get; }
        public FilterResult Result { get; }
        public TimeSpan Duration { get; }

        public ProcessingCompleteEventArgs(string chainName, ProcessingContext context, FilterResult result, TimeSpan duration)
        {
            ChainName = chainName;
            Context = context;
            Result = result;
            Duration = duration;
        }
    }

    /// <summary>
    /// Event args for processing error.
    /// </summary>
    public class ProcessingErrorEventArgs : EventArgs
    {
        public string ChainName { get; }
        public ProcessingContext Context { get; }
        public Exception Error { get; }

        public ProcessingErrorEventArgs(string chainName, ProcessingContext context, Exception error)
        {
            ChainName = chainName;
            Context = context;
            Error = error;
        }
    }
}