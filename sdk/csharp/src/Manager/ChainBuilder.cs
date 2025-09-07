using System;
using System.Collections.Generic;
using GopherMcp.Filters;
using GopherMcp.Filters.BuiltinFilters;
using GopherMcp.Types;

namespace GopherMcp.Manager
{
    /// <summary>
    /// Descriptor for a filter to be added to a chain.
    /// </summary>
    public class FilterDescriptor
    {
        /// <summary>
        /// Gets or sets the filter instance.
        /// </summary>
        public Filter Filter { get; set; }

        /// <summary>
        /// Gets or sets the filter position.
        /// </summary>
        public FilterPosition Position { get; set; } = FilterPosition.Last;

        /// <summary>
        /// Gets or sets the reference filter ID for relative positioning.
        /// </summary>
        public Guid? ReferenceFilterId { get; set; }

        /// <summary>
        /// Gets or sets the filter configuration.
        /// </summary>
        public FilterConfig Configuration { get; set; }

        /// <summary>
        /// Gets or sets whether the filter is enabled.
        /// </summary>
        public bool Enabled { get; set; } = true;
    }

    /// <summary>
    /// Fluent builder for creating filter chains.
    /// </summary>
    public class ChainBuilder
    {
        private readonly FilterManager _manager;
        private readonly string _chainName;
        private readonly List<FilterDescriptor> _filterDescriptors;
        private ChainConfig _config;

        /// <summary>
        /// Initializes a new instance of the ChainBuilder class.
        /// </summary>
        /// <param name="manager">The filter manager.</param>
        /// <param name="chainName">The name for the chain being built.</param>
        internal ChainBuilder(FilterManager manager, string chainName)
        {
            _manager = manager ?? throw new ArgumentNullException(nameof(manager));
            _chainName = chainName ?? throw new ArgumentNullException(nameof(chainName));
            _filterDescriptors = new List<FilterDescriptor>();
            _config = new ChainConfig
            {
                Name = chainName,
                ExecutionMode = ChainExecutionMode.Sequential
            };
        }

        /// <summary>
        /// Sets the execution mode for the chain.
        /// </summary>
        /// <param name="mode">The execution mode.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithExecutionMode(ChainExecutionMode mode)
        {
            _config.ExecutionMode = mode;
            return this;
        }

        /// <summary>
        /// Sets whether to enable statistics for the chain.
        /// </summary>
        /// <param name="enable">Whether to enable statistics.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithStatistics(bool enable = true)
        {
            _config.EnableStatistics = enable;
            return this;
        }

        /// <summary>
        /// Sets the maximum concurrency for parallel execution.
        /// </summary>
        /// <param name="maxConcurrency">The maximum concurrency.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithMaxConcurrency(int maxConcurrency)
        {
            if (maxConcurrency <= 0)
            {
                throw new ArgumentException("Max concurrency must be positive", nameof(maxConcurrency));
            }

            _config.MaxConcurrency = maxConcurrency;
            return this;
        }

        /// <summary>
        /// Sets the default timeout for chain operations.
        /// </summary>
        /// <param name="timeout">The timeout duration.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithTimeout(TimeSpan timeout)
        {
            if (timeout <= TimeSpan.Zero)
            {
                throw new ArgumentException("Timeout must be positive", nameof(timeout));
            }

            _config.DefaultTimeout = timeout;
            return this;
        }

        /// <summary>
        /// Sets the routing strategy for the chain.
        /// </summary>
        /// <param name="strategy">The routing strategy.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithRoutingStrategy(RoutingStrategy strategy)
        {
            _config.RoutingStrategy = strategy;
            return this;
        }

        /// <summary>
        /// Adds a filter to the chain.
        /// </summary>
        /// <param name="filter">The filter to add.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilter(Filter filter)
        {
            ArgumentNullException.ThrowIfNull(filter);
            
            var descriptor = new FilterDescriptor
            {
                Filter = filter,
                Position = FilterPosition.Last,
                Enabled = true
            };

            _filterDescriptors.Add(descriptor);
            return this;
        }

        /// <summary>
        /// Adds a filter with a descriptor.
        /// </summary>
        /// <param name="descriptor">The filter descriptor.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilterDescriptor(FilterDescriptor descriptor)
        {
            ArgumentNullException.ThrowIfNull(descriptor);
            ArgumentNullException.ThrowIfNull(descriptor.Filter);

            _filterDescriptors.Add(descriptor);
            return this;
        }

        /// <summary>
        /// Creates and adds a filter of type T with configuration.
        /// </summary>
        /// <typeparam name="T">The filter type.</typeparam>
        /// <param name="configAction">Optional configuration action.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilter<T>(Action<T> configAction = null) where T : Filter, new()
        {
            // Create new instance of filter type T
            var filter = new T();

            // Apply configuration if provided
            configAction?.Invoke(filter);

            // Add to filter descriptor list
            var descriptor = new FilterDescriptor
            {
                Filter = filter,
                Position = FilterPosition.Last,
                Enabled = true
            };

            _filterDescriptors.Add(descriptor);
            return this;
        }

        /// <summary>
        /// Adds a TCP proxy filter with configuration.
        /// </summary>
        /// <param name="configAction">Configuration action for TCP proxy.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddTcpProxy(Action<TcpProxyConfig> configAction)
        {
            ArgumentNullException.ThrowIfNull(configAction);

            // Create and configure TcpProxyConfig
            var config = new TcpProxyConfig();
            configAction(config);

            // Create TcpProxyFilter with config
            var filter = new TcpProxyFilter(config);

            // Add to filter list
            var descriptor = new FilterDescriptor
            {
                Filter = filter,
                Position = FilterPosition.Last,
                Configuration = config,
                Enabled = true
            };

            _filterDescriptors.Add(descriptor);
            return this;
        }

        /// <summary>
        /// Adds an authentication filter with specified method and secret.
        /// </summary>
        /// <param name="method">The authentication method.</param>
        /// <param name="secret">The authentication secret or key.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddAuthentication(AuthenticationMethod method, string secret)
        {
            ArgumentNullException.ThrowIfNull(secret);

            // Create AuthenticationConfig
            var config = new AuthenticationConfig
            {
                Method = method,
                Secret = secret,
                Enabled = true,
                AllowAnonymous = false
            };

            // Create AuthenticationFilter
            var filter = new AuthenticationFilter(config);

            // Add to filter list
            var descriptor = new FilterDescriptor
            {
                Filter = filter,
                Position = FilterPosition.Last,
                Configuration = config,
                Enabled = true
            };

            _filterDescriptors.Add(descriptor);
            return this;
        }

        /// <summary>
        /// Adds a rate limit filter with specified parameters.
        /// </summary>
        /// <param name="requestsPerMinute">Maximum requests per minute.</param>
        /// <param name="burstSize">Maximum burst size.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddRateLimit(int requestsPerMinute, int burstSize)
        {
            if (requestsPerMinute <= 0)
                throw new ArgumentException("Requests per minute must be positive", nameof(requestsPerMinute));
            if (burstSize <= 0)
                throw new ArgumentException("Burst size must be positive", nameof(burstSize));

            // Create RateLimitConfig
            var config = new RateLimitConfig
            {
                RequestsPerMinute = requestsPerMinute,
                BurstSize = burstSize,
                Enabled = true,
                Algorithm = RateLimitAlgorithm.TokenBucket
            };

            // Create RateLimitFilter
            var filter = new RateLimitFilter(config);

            // Add to filter list
            var descriptor = new FilterDescriptor
            {
                Filter = filter,
                Position = FilterPosition.Last,
                Configuration = config,
                Enabled = true
            };

            _filterDescriptors.Add(descriptor);
            return this;
        }

        /// <summary>
        /// Adds a filter by ID from the manager's registry.
        /// </summary>
        /// <param name="filterId">The filter ID.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilterById(Guid filterId)
        {
            var filter = _manager.FindFilter(filterId);
            if (filter == null)
            {
                throw new ArgumentException($"Filter with ID {filterId} not found", nameof(filterId));
            }

            return AddFilter(filter);
        }

        /// <summary>
        /// Adds multiple filters to the chain.
        /// </summary>
        /// <param name="filters">The filters to add.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilters(params Filter[] filters)
        {
            ArgumentNullException.ThrowIfNull(filters);

            foreach (var filter in filters)
            {
                AddFilter(filter);
            }

            return this;
        }

        /// <summary>
        /// Adds a filter conditionally based on a predicate.
        /// </summary>
        /// <param name="condition">The condition to evaluate.</param>
        /// <param name="filter">The filter to add if condition is true.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilterIf(bool condition, Filter filter)
        {
            if (condition)
            {
                AddFilter(filter);
            }

            return this;
        }

        /// <summary>
        /// Adds a filter conditionally based on a function.
        /// </summary>
        /// <param name="condition">The condition function.</param>
        /// <param name="filterFactory">The filter factory function.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder AddFilterIf(Func<bool> condition, Func<Filter> filterFactory)
        {
            ArgumentNullException.ThrowIfNull(condition);
            ArgumentNullException.ThrowIfNull(filterFactory);

            if (condition())
            {
                AddFilter(filterFactory());
            }

            return this;
        }

        /// <summary>
        /// Configures the chain with a custom configuration action.
        /// </summary>
        /// <param name="configAction">The configuration action.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder Configure(Action<ChainConfig> configAction)
        {
            ArgumentNullException.ThrowIfNull(configAction);
            configAction(_config);
            return this;
        }

        /// <summary>
        /// Sets a metadata value for the chain.
        /// </summary>
        /// <param name="key">The metadata key.</param>
        /// <param name="value">The metadata value.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithMetadata(string key, object value)
        {
            ArgumentNullException.ThrowIfNull(key);
            _config.Metadata[key] = value;
            return this;
        }

        /// <summary>
        /// Sets multiple metadata values for the chain.
        /// </summary>
        /// <param name="metadata">The metadata dictionary.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder WithMetadata(IDictionary<string, object> metadata)
        {
            ArgumentNullException.ThrowIfNull(metadata);

            foreach (var kvp in metadata)
            {
                _config.Metadata[kvp.Key] = kvp.Value;
            }

            return this;
        }

        /// <summary>
        /// Validates the chain configuration.
        /// </summary>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder Validate()
        {
            if (string.IsNullOrWhiteSpace(_chainName))
            {
                throw new InvalidOperationException("Chain name is required");
            }

            if (_config.ExecutionMode == ChainExecutionMode.Parallel && _config.MaxConcurrency <= 0)
            {
                throw new InvalidOperationException("Max concurrency must be positive for parallel execution");
            }

            // Additional validation as needed
            return this;
        }

        /// <summary>
        /// Builds and registers the chain with the manager.
        /// </summary>
        /// <returns>The created filter chain.</returns>
        public FilterChain Build()
        {
            // Validate configuration
            Validate();

            // Create the chain
            var chain = _manager.CreateChain(_chainName, _config);

            // Add filters to the chain based on descriptors
            foreach (var descriptor in _filterDescriptors)
            {
                if (!descriptor.Enabled)
                    continue;

                // Apply configuration if provided
                if (descriptor.Configuration != null)
                {
                    descriptor.Filter.UpdateConfig(descriptor.Configuration);
                }

                // Add filter with specified position
                if (descriptor.ReferenceFilterId.HasValue)
                {
                    chain.AddFilterRelative(descriptor.Filter, descriptor.Position, descriptor.ReferenceFilterId.Value);
                }
                else
                {
                    chain.AddFilter(descriptor.Filter, descriptor.Position);
                }
            }

            return chain;
        }

        /// <summary>
        /// Builds the chain and returns the builder for further operations.
        /// </summary>
        /// <param name="chain">The created chain.</param>
        /// <returns>The builder for method chaining.</returns>
        public ChainBuilder BuildAndContinue(out FilterChain chain)
        {
            chain = Build();
            return this;
        }

        /// <summary>
        /// Creates a new builder for another chain.
        /// </summary>
        /// <param name="chainName">The name for the new chain.</param>
        /// <returns>A new chain builder.</returns>
        public ChainBuilder NewChain(string chainName)
        {
            return new ChainBuilder(_manager, chainName);
        }
    }
}