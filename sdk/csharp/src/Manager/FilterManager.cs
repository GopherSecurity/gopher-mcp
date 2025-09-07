using System;
using System.Collections.Generic;
using System.Linq;
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
        private readonly DateTime _startTime;
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
            _startTime = DateTime.UtcNow;

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
                TotalErrors = 0,
                TotalBytesProcessed = 0,
                AverageLatency = TimeSpan.Zero,
                MinLatency = TimeSpan.MaxValue,
                MaxLatency = TimeSpan.Zero,
                MemoryUsage = GetMemoryUsage(),
                StartTime = _startTime,
                Uptime = DateTime.UtcNow - _startTime
            };

            // Aggregate statistics from all chains
            _chainsLock.EnterReadLock();
            try
            {
                long totalLatencyTicks = 0;
                int latencyCount = 0;

                foreach (var chain in _chains)
                {
                    var chainStats = chain.GetStatistics();
                    stats.TotalProcessed += (long)chainStats.TotalPacketsProcessed;
                    stats.TotalErrors += (long)chainStats.TotalErrors;
                    stats.TotalBytesProcessed += (long)chainStats.TotalBytesProcessed;
                    
                    // Convert microseconds to TimeSpan
                    var processingTime = TimeSpan.FromMicroseconds(chainStats.TotalProcessingTimeUs);
                    stats.TotalProcessingTime += processingTime;

                    // Track latency metrics
                    if (chainStats.AverageProcessingTimeUs > 0)
                    {
                        var avgLatency = TimeSpan.FromMicroseconds(chainStats.AverageProcessingTimeUs);
                        totalLatencyTicks += avgLatency.Ticks * (long)chainStats.TotalPacketsProcessed;
                        latencyCount += (int)chainStats.TotalPacketsProcessed;

                        // Use average as approximation for min/max
                        if (avgLatency < stats.MinLatency)
                            stats.MinLatency = avgLatency;
                        
                        if (avgLatency > stats.MaxLatency)
                            stats.MaxLatency = avgLatency;
                    }

                    // Add chain-specific stats
                    stats.ChainStatistics[chain.Name] = chainStats;
                }

                // Calculate average latency
                if (latencyCount > 0)
                {
                    stats.AverageLatency = new TimeSpan(totalLatencyTicks / latencyCount);
                }
                else
                {
                    stats.MinLatency = TimeSpan.Zero;
                }
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }

            // Aggregate statistics from all filters
            _registryLock.EnterReadLock();
            try
            {
                foreach (var kvp in _filterRegistry)
                {
                    var filterStats = kvp.Value.GetStatistics();
                    stats.FilterStatistics[kvp.Key] = filterStats;
                    
                    // Add filter processing counts
                    stats.TotalFilterInvocations += filterStats.ProcessCount;
                }
            }
            finally
            {
                _registryLock.ExitReadLock();
            }

            // Calculate throughput
            if (stats.Uptime.TotalSeconds > 0)
            {
                stats.Throughput = stats.TotalProcessed / stats.Uptime.TotalSeconds;
                stats.ByteThroughput = stats.TotalBytesProcessed / stats.Uptime.TotalSeconds;
            }

            // Calculate error rate
            if (stats.TotalProcessed > 0)
            {
                stats.ErrorRate = (double)stats.TotalErrors / stats.TotalProcessed;
            }

            return stats;
        }

        /// <summary>
        /// Gets the current memory usage.
        /// </summary>
        private long GetMemoryUsage()
        {
            // Get current process memory usage
            using (var process = System.Diagnostics.Process.GetCurrentProcess())
            {
                return process.WorkingSet64;
            }
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
        /// Processes a message through the appropriate filter chain.
        /// </summary>
        /// <param name="message">The JSON-RPC message to process.</param>
        /// <param name="chainName">Optional chain name to use.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        public async Task<ProcessingResult> ProcessAsync(
            JsonRpcMessage message,
            string? chainName = null,
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(message);

            var startTime = DateTime.UtcNow;
            var context = new ProcessingContext
            {
                SessionId = Guid.NewGuid().ToString(),
                Timestamp = startTime,
                Metadata = new Dictionary<string, object>
                {
                    ["MessageType"] = message.GetType().Name,
                    ["ManagerName"] = _config.Name
                }
            };

            // Select appropriate filter chain
            FilterChain? chain = null;
            if (!string.IsNullOrEmpty(chainName))
            {
                chain = FindChain(chainName);
                if (chain == null)
                {
                    var error = new ChainException($"Chain '{chainName}' not found");
                    OnProcessingError(new ProcessingErrorEventArgs(chainName ?? "default", context, error));
                    throw error;
                }
            }
            else
            {
                // Select default chain or first available
                _chainsLock.EnterReadLock();
                try
                {
                    chain = _chains.FirstOrDefault(c => c.Name == "default") ?? _chains.FirstOrDefault();
                }
                finally
                {
                    _chainsLock.ExitReadLock();
                }
            }

            if (chain == null)
            {
                var error = new ChainException("No filter chain available for processing");
                OnProcessingError(new ProcessingErrorEventArgs("none", context, error));
                throw error;
            }

            try
            {
                // Raise processing start event
                OnProcessingStart(new ProcessingStartEventArgs(chain.Name, context));

                // Serialize message to buffer
                var messageJson = await JsonSerializer.SerializeAsync(message, cancellationToken);
                var buffer = System.Text.Encoding.UTF8.GetBytes(messageJson);

                // Process through chain
                var result = await chain.ProcessAsync(buffer, context, cancellationToken);

                // Deserialize result if successful
                ProcessingResult processingResult;
                if (result.IsSuccess && result.Data != null)
                {
                    var resultJson = System.Text.Encoding.UTF8.GetString(result.Data);
                    var responseMessage = await JsonSerializer.DeserializeAsync<JsonRpcMessage>(
                        resultJson, cancellationToken);
                    
                    processingResult = new ProcessingResult
                    {
                        Success = true,
                        Message = responseMessage,
                        Context = context,
                        Duration = DateTime.UtcNow - startTime
                    };
                }
                else
                {
                    processingResult = new ProcessingResult
                    {
                        Success = false,
                        Error = result.Error,
                        Context = context,
                        Duration = DateTime.UtcNow - startTime
                    };
                }

                // Update statistics
                if (_config.EnableStatistics)
                {
                    // Statistics are updated by the chain
                }

                // Raise processing complete event
                var duration = DateTime.UtcNow - startTime;
                OnProcessingComplete(new ProcessingCompleteEventArgs(
                    chain.Name, context, result, duration));

                return processingResult;
            }
            catch (OperationCanceledException)
            {
                // Cancellation is not an error
                throw;
            }
            catch (Exception ex)
            {
                // Handle errors with fallback
                OnProcessingError(new ProcessingErrorEventArgs(
                    chain?.Name ?? "unknown", context, ex));

                if (_config.EnableFallback)
                {
                    return await ProcessWithFallbackAsync(message, context, cancellationToken);
                }

                throw;
            }
        }

        /// <summary>
        /// Creates a new filter chain with the specified name and configuration.
        /// </summary>
        /// <param name="chainName">The unique name for the chain.</param>
        /// <param name="config">Optional chain configuration.</param>
        /// <returns>The created filter chain.</returns>
        public FilterChain CreateChain(string chainName, ChainConfig? config = null)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(chainName);

            // Validate chain name is unique
            _chainsLock.EnterReadLock();
            try
            {
                if (_chains.Any(c => c.Name == chainName))
                {
                    throw new ArgumentException($"Chain with name '{chainName}' already exists");
                }
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }

            // Create new FilterChain instance
            var chainConfig = config ?? new ChainConfig
            {
                Name = chainName,
                ExecutionMode = ChainExecutionMode.Sequential,
                EnableStatistics = _config.EnableStatistics,
                MaxConcurrency = _config.MaxConcurrency,
                DefaultTimeout = _config.DefaultTimeout
            };

            // Ensure the name matches
            chainConfig.Name = chainName;

            var chain = new FilterChain(chainConfig);

            // Add to internal chain list
            _chainsLock.EnterWriteLock();
            try
            {
                _chains.Add(chain);
            }
            finally
            {
                _chainsLock.ExitWriteLock();
            }

            // Register with native manager if needed
            if (_handle != null && !_handle.IsInvalid)
            {
                var result = McpFilterApi.mcp_filter_manager_add_chain(
                    _handle.DangerousGetHandle().ToUInt64(),
                    chain.Handle.DangerousGetHandle().ToUInt64(),
                    chainName
                );

                if (result != 0)
                {
                    // Remove from list if native registration failed
                    _chainsLock.EnterWriteLock();
                    try
                    {
                        _chains.Remove(chain);
                    }
                    finally
                    {
                        _chainsLock.ExitWriteLock();
                    }

                    chain.Dispose();
                    throw new McpException($"Failed to register chain '{chainName}' with native manager");
                }
            }

            // Raise chain created event
            OnChainCreated(new ChainCreatedEventArgs(chainName, chain));

            return chain;
        }

        /// <summary>
        /// Registers a filter with the manager.
        /// </summary>
        /// <param name="filter">The filter to register.</param>
        /// <returns>The unique ID assigned to the filter.</returns>
        public Guid RegisterFilter(Filter filter)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(filter);

            // Generate unique ID for filter
            var filterId = Guid.NewGuid();

            // Add to filter registry
            _registryLock.EnterWriteLock();
            try
            {
                // Check if filter is already registered
                if (_filterRegistry.Values.Contains(filter))
                {
                    throw new ArgumentException("Filter is already registered");
                }

                _filterRegistry[filterId] = filter;
            }
            finally
            {
                _registryLock.ExitWriteLock();
            }

            try
            {
                // Set filter's manager reference
                SetFilterManagerReference(filter, this);

                // Configure filter with defaults from manager config
                ConfigureFilterDefaults(filter);

                // Initialize filter
                var initTask = InitializeFilterAsync(filter);
                initTask.Wait(_config.DefaultTimeout);

                // Register with native manager if available
                if (_handle != null && !_handle.IsInvalid)
                {
                    var result = McpFilterApi.mcp_filter_manager_add_filter(
                        _handle.DangerousGetHandle().ToUInt64(),
                        filter.Handle?.DangerousGetHandle().ToUInt64() ?? 0,
                        filterId.ToString()
                    );

                    if (result != 0)
                    {
                        // Rollback on native registration failure
                        _registryLock.EnterWriteLock();
                        try
                        {
                            _filterRegistry.Remove(filterId);
                        }
                        finally
                        {
                            _registryLock.ExitWriteLock();
                        }

                        throw new McpException($"Failed to register filter with native manager");
                    }
                }

                // Raise filter registered event
                OnFilterRegistered(new FilterRegisteredEventArgs(filterId, filter));

                return filterId;
            }
            catch
            {
                // Clean up on failure
                _registryLock.EnterWriteLock();
                try
                {
                    _filterRegistry.Remove(filterId);
                }
                finally
                {
                    _registryLock.ExitWriteLock();
                }
                throw;
            }
        }

        /// <summary>
        /// Sets the manager reference on a filter if supported.
        /// </summary>
        private void SetFilterManagerReference(Filter filter, FilterManager manager)
        {
            // Use reflection to set Manager property if it exists
            var managerProperty = filter.GetType().GetProperty("Manager", 
                System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance);
            
            if (managerProperty != null && managerProperty.CanWrite && 
                managerProperty.PropertyType.IsAssignableFrom(typeof(FilterManager)))
            {
                managerProperty.SetValue(filter, manager);
            }
        }

        /// <summary>
        /// Configures filter with default settings from manager config.
        /// </summary>
        private void ConfigureFilterDefaults(Filter filter)
        {
            // Apply default configuration
            if (_config.EnableStatistics && filter.Config != null)
            {
                filter.Config.EnableStatistics = true;
            }

            // Set default timeout
            if (filter.Config != null)
            {
                filter.Config.Timeout = _config.DefaultTimeout;
            }

            // Apply any additional default settings
            foreach (var setting in _config.Settings)
            {
                filter.Config?.SetSetting(setting.Key, setting.Value);
            }
        }

        /// <summary>
        /// Initializes a filter asynchronously.
        /// </summary>
        private async Task InitializeFilterAsync(Filter filter)
        {
            // Call Initialize if it exists
            var initMethod = filter.GetType().GetMethod("InitializeAsync", 
                System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance);
            
            if (initMethod != null && initMethod.ReturnType == typeof(Task))
            {
                var task = initMethod.Invoke(filter, null) as Task;
                if (task != null)
                {
                    await task.ConfigureAwait(false);
                }
            }
            else
            {
                // Try synchronous Initialize
                var syncInitMethod = filter.GetType().GetMethod("Initialize", 
                    System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance);
                
                if (syncInitMethod != null)
                {
                    syncInitMethod.Invoke(filter, null);
                }
            }
        }

        /// <summary>
        /// Updates the manager configuration dynamically.
        /// </summary>
        /// <param name="newConfig">The new configuration to apply.</param>
        /// <returns>A task representing the asynchronous operation.</returns>
        public async Task UpdateConfigurationAsync(FilterManagerConfig newConfig)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(newConfig);

            // Validate new configuration
            ValidateConfiguration(newConfig);

            var oldConfig = _config;
            
            try
            {
                // Update configuration
                _config = newConfig;

                // Apply changes to existing filters
                await ApplyConfigurationToFiltersAsync(newConfig);

                // Update chain configurations
                await UpdateChainConfigurationsAsync(newConfig);

                // Add/remove filters as needed
                await ReconfigureFiltersAsync(oldConfig, newConfig);

                // Update logging configuration
                if (newConfig.EnableLogging != oldConfig.EnableLogging || 
                    newConfig.LogLevel != oldConfig.LogLevel)
                {
                    ConfigureLogging();
                }

                // Trigger reconfiguration events
                OnConfigurationUpdated(new ConfigurationUpdatedEventArgs(oldConfig, newConfig));
            }
            catch (Exception ex)
            {
                // Rollback on failure
                _config = oldConfig;
                throw new ConfigurationException("Failed to update configuration", ex);
            }
        }

        /// <summary>
        /// Validates a configuration.
        /// </summary>
        private void ValidateConfiguration(FilterManagerConfig config)
        {
            if (string.IsNullOrWhiteSpace(config.Name))
            {
                throw new ArgumentException("Manager name is required", nameof(config));
            }

            if (config.MaxConcurrency <= 0)
            {
                throw new ArgumentException("MaxConcurrency must be positive", nameof(config));
            }

            if (config.DefaultTimeout <= TimeSpan.Zero)
            {
                throw new ArgumentException("DefaultTimeout must be positive", nameof(config));
            }
        }

        /// <summary>
        /// Applies configuration to existing filters.
        /// </summary>
        private async Task ApplyConfigurationToFiltersAsync(FilterManagerConfig config)
        {
            var filters = GetFilters();
            var tasks = new List<Task>();

            foreach (var filter in filters)
            {
                tasks.Add(Task.Run(() =>
                {
                    ConfigureFilterDefaults(filter);
                    
                    // Update filter-specific settings
                    if (filter.Config != null)
                    {
                        filter.Config.EnableStatistics = config.EnableStatistics;
                        filter.Config.Timeout = config.DefaultTimeout;
                    }
                }));
            }

            await Task.WhenAll(tasks);
        }

        /// <summary>
        /// Updates chain configurations.
        /// </summary>
        private async Task UpdateChainConfigurationsAsync(FilterManagerConfig config)
        {
            var chains = GetChains();
            var tasks = new List<Task>();

            foreach (var chain in chains)
            {
                tasks.Add(Task.Run(() =>
                {
                    if (chain.Config != null)
                    {
                        chain.Config.EnableStatistics = config.EnableStatistics;
                        chain.Config.MaxConcurrency = config.MaxConcurrency;
                        chain.Config.DefaultTimeout = config.DefaultTimeout;
                    }
                }));
            }

            await Task.WhenAll(tasks);
        }

        /// <summary>
        /// Reconfigures filters based on configuration changes.
        /// </summary>
        private async Task ReconfigureFiltersAsync(FilterManagerConfig oldConfig, FilterManagerConfig newConfig)
        {
            // Add new default filters if enabled
            if (!oldConfig.EnableDefaultFilters && newConfig.EnableDefaultFilters)
            {
                InitializeDefaultFilters();
            }

            // Remove default filters if disabled
            if (oldConfig.EnableDefaultFilters && !newConfig.EnableDefaultFilters)
            {
                await RemoveDefaultFiltersAsync();
            }

            // Handle other filter additions/removals based on config changes
            // This is extensible for custom filter management
        }

        /// <summary>
        /// Removes default filters.
        /// </summary>
        private async Task RemoveDefaultFiltersAsync()
        {
            // Find and remove default filters
            var filtersToRemove = new List<Guid>();
            
            _registryLock.EnterReadLock();
            try
            {
                foreach (var kvp in _filterRegistry)
                {
                    // Check if filter is a default filter (by name or type)
                    if (IsDefaultFilter(kvp.Value))
                    {
                        filtersToRemove.Add(kvp.Key);
                    }
                }
            }
            finally
            {
                _registryLock.ExitReadLock();
            }

            // Remove identified filters
            foreach (var filterId in filtersToRemove)
            {
                UnregisterFilter(filterId);
            }

            await Task.CompletedTask;
        }

        /// <summary>
        /// Checks if a filter is a default filter.
        /// </summary>
        private bool IsDefaultFilter(Filter filter)
        {
            // Check by filter name or type
            var filterName = filter.Config?.Name ?? "";
            return filterName.StartsWith("Default") || 
                   filterName.Contains("Authentication") ||
                   filterName.Contains("Metrics") ||
                   filterName.Contains("RateLimit");
        }

        /// <summary>
        /// Raises the ConfigurationUpdated event.
        /// </summary>
        protected virtual void OnConfigurationUpdated(ConfigurationUpdatedEventArgs e)
        {
            ConfigurationUpdated?.Invoke(this, e);
        }

        /// <summary>
        /// Occurs when configuration is updated.
        /// </summary>
        public event EventHandler<ConfigurationUpdatedEventArgs>? ConfigurationUpdated;

        /// <summary>
        /// Creates a new chain builder for fluent chain configuration.
        /// </summary>
        /// <param name="chainName">The name for the chain to build.</param>
        /// <returns>A new ChainBuilder instance.</returns>
        public ChainBuilder BuildChain(string chainName)
        {
            ThrowIfDisposed();
            ArgumentNullException.ThrowIfNull(chainName);

            // Return new ChainBuilder instance
            return new ChainBuilder(this, chainName);
        }

        /// <summary>
        /// Processes a message with fallback strategy.
        /// </summary>
        private async Task<ProcessingResult> ProcessWithFallbackAsync(
            JsonRpcMessage message,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            // Try to find a fallback chain
            FilterChain? fallbackChain = null;
            
            _chainsLock.EnterReadLock();
            try
            {
                fallbackChain = _chains.FirstOrDefault(c => c.Name == "fallback");
            }
            finally
            {
                _chainsLock.ExitReadLock();
            }

            if (fallbackChain != null)
            {
                try
                {
                    var messageJson = await JsonSerializer.SerializeAsync(message, cancellationToken);
                    var buffer = System.Text.Encoding.UTF8.GetBytes(messageJson);
                    var result = await fallbackChain.ProcessAsync(buffer, context, cancellationToken);

                    if (result.IsSuccess && result.Data != null)
                    {
                        var resultJson = System.Text.Encoding.UTF8.GetString(result.Data);
                        var responseMessage = await JsonSerializer.DeserializeAsync<JsonRpcMessage>(
                            resultJson, cancellationToken);
                        
                        return new ProcessingResult
                        {
                            Success = true,
                            Message = responseMessage,
                            Context = context,
                            Duration = DateTime.UtcNow - context.Timestamp,
                            WasFallback = true
                        };
                    }
                }
                catch
                {
                    // Fallback also failed
                }
            }

            // Return error result
            return new ProcessingResult
            {
                Success = false,
                Error = "All processing attempts failed",
                Context = context,
                Duration = DateTime.UtcNow - context.Timestamp,
                WasFallback = true
            };
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
        /// Gets or sets whether to enable fallback processing.
        /// </summary>
        public bool EnableFallback { get; set; } = true;

        /// <summary>
        /// Gets or sets additional settings.
        /// </summary>
        public Dictionary<string, object> Settings { get; set; } = new();
    }

    /// <summary>
    /// Manager statistics.
    /// </summary>
    public class ManagerStatistics
    {
        /// <summary>
        /// Gets or sets the number of filters.
        /// </summary>
        public int FilterCount { get; set; }

        /// <summary>
        /// Gets or sets the number of chains.
        /// </summary>
        public int ChainCount { get; set; }

        /// <summary>
        /// Gets or sets the total messages processed.
        /// </summary>
        public long TotalProcessed { get; set; }

        /// <summary>
        /// Gets or sets the total errors encountered.
        /// </summary>
        public long TotalErrors { get; set; }

        /// <summary>
        /// Gets or sets the total bytes processed.
        /// </summary>
        public long TotalBytesProcessed { get; set; }

        /// <summary>
        /// Gets or sets the total filter invocations.
        /// </summary>
        public long TotalFilterInvocations { get; set; }

        /// <summary>
        /// Gets or sets the total processing time.
        /// </summary>
        public TimeSpan TotalProcessingTime { get; set; }

        /// <summary>
        /// Gets or sets the average latency.
        /// </summary>
        public TimeSpan AverageLatency { get; set; }

        /// <summary>
        /// Gets or sets the minimum latency.
        /// </summary>
        public TimeSpan MinLatency { get; set; }

        /// <summary>
        /// Gets or sets the maximum latency.
        /// </summary>
        public TimeSpan MaxLatency { get; set; }

        /// <summary>
        /// Gets or sets the memory usage in bytes.
        /// </summary>
        public long MemoryUsage { get; set; }

        /// <summary>
        /// Gets or sets the manager start time.
        /// </summary>
        public DateTime StartTime { get; set; }

        /// <summary>
        /// Gets or sets the manager uptime.
        /// </summary>
        public TimeSpan Uptime { get; set; }

        /// <summary>
        /// Gets or sets the throughput (messages per second).
        /// </summary>
        public double Throughput { get; set; }

        /// <summary>
        /// Gets or sets the byte throughput (bytes per second).
        /// </summary>
        public double ByteThroughput { get; set; }

        /// <summary>
        /// Gets or sets the error rate (0.0 to 1.0).
        /// </summary>
        public double ErrorRate { get; set; }

        /// <summary>
        /// Gets the chain-specific statistics.
        /// </summary>
        public Dictionary<string, ChainStatistics> ChainStatistics { get; } = new();

        /// <summary>
        /// Gets the filter-specific statistics.
        /// </summary>
        public Dictionary<Guid, FilterStatistics> FilterStatistics { get; } = new();
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

    /// <summary>
    /// Result of message processing.
    /// </summary>
    public class ProcessingResult
    {
        /// <summary>
        /// Gets or sets whether processing was successful.
        /// </summary>
        public bool Success { get; set; }

        /// <summary>
        /// Gets or sets the processed message.
        /// </summary>
        public JsonRpcMessage? Message { get; set; }

        /// <summary>
        /// Gets or sets the error message if processing failed.
        /// </summary>
        public string? Error { get; set; }

        /// <summary>
        /// Gets or sets the processing context.
        /// </summary>
        public ProcessingContext Context { get; set; } = new();

        /// <summary>
        /// Gets or sets the processing duration.
        /// </summary>
        public TimeSpan Duration { get; set; }

        /// <summary>
        /// Gets or sets whether fallback was used.
        /// </summary>
        public bool WasFallback { get; set; }
    }

    /// <summary>
    /// Base class for JSON-RPC messages.
    /// </summary>
    public abstract class JsonRpcMessage
    {
        /// <summary>
        /// Gets or sets the JSON-RPC version.
        /// </summary>
        public string JsonRpc { get; set; } = "2.0";

        /// <summary>
        /// Gets or sets the message ID.
        /// </summary>
        public object? Id { get; set; }
    }

    /// <summary>
    /// JSON-RPC request message.
    /// </summary>
    public class JsonRpcRequest : JsonRpcMessage
    {
        /// <summary>
        /// Gets or sets the method name.
        /// </summary>
        public string Method { get; set; } = string.Empty;

        /// <summary>
        /// Gets or sets the method parameters.
        /// </summary>
        public object? Params { get; set; }
    }

    /// <summary>
    /// JSON-RPC response message.
    /// </summary>
    public class JsonRpcResponse : JsonRpcMessage
    {
        /// <summary>
        /// Gets or sets the result.
        /// </summary>
        public object? Result { get; set; }

        /// <summary>
        /// Gets or sets the error.
        /// </summary>
        public JsonRpcError? Error { get; set; }
    }

    /// <summary>
    /// JSON-RPC error.
    /// </summary>
    public class JsonRpcError
    {
        /// <summary>
        /// Gets or sets the error code.
        /// </summary>
        public int Code { get; set; }

        /// <summary>
        /// Gets or sets the error message.
        /// </summary>
        public string Message { get; set; } = string.Empty;

        /// <summary>
        /// Gets or sets additional error data.
        /// </summary>
        public object? Data { get; set; }
    }

    /// <summary>
    /// JSON-RPC notification message.
    /// </summary>
    public class JsonRpcNotification : JsonRpcMessage
    {
        /// <summary>
        /// Gets or sets the method name.
        /// </summary>
        public string Method { get; set; } = string.Empty;

        /// <summary>
        /// Gets or sets the method parameters.
        /// </summary>
        public object? Params { get; set; }

        /// <summary>
        /// Initializes a new instance of JsonRpcNotification.
        /// </summary>
        public JsonRpcNotification()
        {
            Id = null; // Notifications don't have IDs
        }
    }

    /// <summary>
    /// Event args for configuration updates.
    /// </summary>
    public class ConfigurationUpdatedEventArgs : EventArgs
    {
        /// <summary>
        /// Gets the old configuration.
        /// </summary>
        public FilterManagerConfig OldConfiguration { get; }

        /// <summary>
        /// Gets the new configuration.
        /// </summary>
        public FilterManagerConfig NewConfiguration { get; }

        /// <summary>
        /// Initializes a new instance of ConfigurationUpdatedEventArgs.
        /// </summary>
        public ConfigurationUpdatedEventArgs(FilterManagerConfig oldConfig, FilterManagerConfig newConfig)
        {
            OldConfiguration = oldConfig;
            NewConfiguration = newConfig;
        }
    }
}