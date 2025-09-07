using System;
using System.Collections.Generic;
using GopherMcp.Filters;
using GopherMcp.Types;

namespace GopherMcp.Manager
{
    /// <summary>
    /// Fluent builder for creating filter chains.
    /// </summary>
    public class ChainBuilder
    {
        private readonly FilterManager _manager;
        private readonly string _chainName;
        private readonly List<Filter> _filters;
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
            _filters = new List<Filter>();
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
            
            if (_filters.Contains(filter))
            {
                throw new ArgumentException("Filter is already in the chain", nameof(filter));
            }

            _filters.Add(filter);
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

            // Add filters to the chain
            foreach (var filter in _filters)
            {
                chain.AddFilter(filter, FilterPosition.Last);
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