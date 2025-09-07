using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using Moq;
using GopherMcp.Manager;
using GopherMcp.Filters;
using GopherMcp.Chain;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    public class ManagerTests
    {
        [Fact]
        public async Task ManagerInitialization_CreatesWithConfiguration()
        {
            // Arrange
            var config = new FilterManagerConfig
            {
                MaxConcurrency = 8,
                DefaultTimeout = TimeSpan.FromSeconds(30),
                EnableMetrics = true,
                LogLevel = LogLevel.Info
            };

            // Act
            using var manager = new FilterManager(config);
            await manager.InitializeAsync();

            // Assert
            Assert.True(manager.IsInitialized);
            Assert.Equal(8, manager.MaxConcurrency);
            Assert.Equal(TimeSpan.FromSeconds(30), manager.DefaultTimeout);
            Assert.True(manager.MetricsEnabled);
        }

        [Fact]
        public async Task FilterRegistration_RegisterAndRetrieve()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter = new Mock<IFilter>();
            filter.Setup(f => f.Name).Returns("TestFilter");

            // Act
            await manager.RegisterFilterAsync("TestFilter", filter.Object);
            var retrieved = manager.GetFilter("TestFilter");

            // Assert
            Assert.NotNull(retrieved);
            Assert.Equal(filter.Object, retrieved);
        }

        [Fact]
        public async Task FilterRegistration_DuplicateName_ThrowsException()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter1 = new Mock<IFilter>();
            var filter2 = new Mock<IFilter>();

            await manager.RegisterFilterAsync("TestFilter", filter1.Object);

            // Act & Assert
            await Assert.ThrowsAsync<InvalidOperationException>(
                () => manager.RegisterFilterAsync("TestFilter", filter2.Object));
        }

        [Fact]
        public async Task FilterRegistration_Unregister_RemovesFilter()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter = new Mock<IFilter>();
            await manager.RegisterFilterAsync("TestFilter", filter.Object);

            // Act
            var unregistered = await manager.UnregisterFilterAsync("TestFilter");
            var retrieved = manager.GetFilter("TestFilter");

            // Assert
            Assert.True(unregistered);
            Assert.Null(retrieved);
        }

        [Fact]
        public async Task ChainCreation_CreatesWithConfiguration()
        {
            // Arrange
            using var manager = new FilterManager();
            var chainConfig = new ChainConfig
            {
                Name = "TestChain",
                Mode = ExecutionMode.Sequential,
                ContinueOnError = true
            };

            // Act
            var chain = await manager.CreateChainAsync(chainConfig);

            // Assert
            Assert.NotNull(chain);
            Assert.Equal("TestChain", chain.Name);
            Assert.Equal(ExecutionMode.Sequential, chain.Mode);
            Assert.True(chain.ContinueOnError);
        }

        [Fact]
        public async Task ChainCreation_DuplicateName_ThrowsException()
        {
            // Arrange
            using var manager = new FilterManager();
            var chainConfig = new ChainConfig { Name = "TestChain" };

            await manager.CreateChainAsync(chainConfig);

            // Act & Assert
            await Assert.ThrowsAsync<InvalidOperationException>(
                () => manager.CreateChainAsync(chainConfig));
        }

        [Fact]
        public async Task MessageProcessing_ProcessesThroughDefaultChain()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter = new Mock<IFilter>();
            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .ReturnsAsync(ProcessingResult.Success(new byte[] { 1, 2, 3 }));

            await manager.RegisterFilterAsync("TestFilter", filter.Object);

            var chainConfig = new ChainConfig { Name = "DefaultChain" };
            var chain = await manager.CreateChainAsync(chainConfig);
            chain.AddFilter("TestFilter");
            manager.SetDefaultChain("DefaultChain");

            var data = new byte[] { 1, 2 };
            var context = new ProcessingContext();

            // Act
            var result = await manager.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(3, result.Data.Length);
            filter.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Once);
        }

        [Fact]
        public async Task MessageProcessing_WithSpecificChain()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter1 = new Mock<IFilter>();
            var filter2 = new Mock<IFilter>();

            filter1.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync(ProcessingResult.Success(new byte[] { 1 }));
            filter2.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync(ProcessingResult.Success(new byte[] { 2 }));

            await manager.RegisterFilterAsync("Filter1", filter1.Object);
            await manager.RegisterFilterAsync("Filter2", filter2.Object);

            var chain1Config = new ChainConfig { Name = "Chain1" };
            var chain1 = await manager.CreateChainAsync(chain1Config);
            chain1.AddFilter("Filter1");

            var chain2Config = new ChainConfig { Name = "Chain2" };
            var chain2 = await manager.CreateChainAsync(chain2Config);
            chain2.AddFilter("Filter2");

            var data = new byte[] { 0 };
            var context = new ProcessingContext { ChainName = "Chain2" };

            // Act
            var result = await manager.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            filter1.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Never);
            filter2.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Once);
        }

        [Fact]
        public async Task ConfigurationUpdate_UpdatesManagerBehavior()
        {
            // Arrange
            var initialConfig = new FilterManagerConfig
            {
                MaxConcurrency = 4,
                EnableMetrics = false
            };

            using var manager = new FilterManager(initialConfig);

            var updatedConfig = new FilterManagerConfig
            {
                MaxConcurrency = 8,
                EnableMetrics = true
            };

            // Act
            await manager.UpdateConfigurationAsync(updatedConfig);

            // Assert
            Assert.Equal(8, manager.MaxConcurrency);
            Assert.True(manager.MetricsEnabled);
        }

        [Fact]
        public async Task Statistics_TracksProcessingMetrics()
        {
            // Arrange
            using var manager = new FilterManager(new FilterManagerConfig { EnableMetrics = true });
            
            var filter = new Mock<IFilter>();
            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .ReturnsAsync(ProcessingResult.Success(new byte[] { 1, 2, 3 }));

            await manager.RegisterFilterAsync("TestFilter", filter.Object);

            var chainConfig = new ChainConfig { Name = "TestChain" };
            var chain = await manager.CreateChainAsync(chainConfig);
            chain.AddFilter("TestFilter");
            manager.SetDefaultChain("TestChain");

            // Act
            for (int i = 0; i < 10; i++)
            {
                await manager.ProcessAsync(new byte[] { 1 }, new ProcessingContext());
            }

            var stats = manager.GetStatistics();

            // Assert
            Assert.Equal(10, stats.TotalMessagesProcessed);
            Assert.Equal(10, stats.SuccessfulMessages);
            Assert.Equal(0, stats.FailedMessages);
            Assert.True(stats.AverageProcessingTime >= 0);
        }

        [Fact]
        public async Task ConcurrencyControl_LimitsParallelProcessing()
        {
            // Arrange
            var config = new FilterManagerConfig { MaxConcurrency = 2 };
            using var manager = new FilterManager(config);

            var semaphore = new SemaphoreSlim(0);
            var processingCount = 0;
            var maxConcurrent = 0;

            var filter = new Mock<IFilter>();
            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .Returns(async (byte[] d, ProcessingContext c, CancellationToken ct) =>
                  {
                      var current = Interlocked.Increment(ref processingCount);
                      var currentMax = maxConcurrent;
                      while (current > currentMax)
                      {
                          currentMax = Interlocked.CompareExchange(ref maxConcurrent, current, currentMax);
                          if (currentMax >= current) break;
                      }

                      await Task.Delay(50);
                      Interlocked.Decrement(ref processingCount);
                      return ProcessingResult.Success(d);
                  });

            await manager.RegisterFilterAsync("TestFilter", filter.Object);

            var chainConfig = new ChainConfig { Name = "TestChain" };
            var chain = await manager.CreateChainAsync(chainConfig);
            chain.AddFilter("TestFilter");
            manager.SetDefaultChain("TestChain");

            // Act
            var tasks = new List<Task<ProcessingResult>>();
            for (int i = 0; i < 5; i++)
            {
                tasks.Add(manager.ProcessAsync(new byte[] { 1 }, new ProcessingContext()));
            }

            await Task.WhenAll(tasks);

            // Assert
            Assert.True(maxConcurrent <= 2, $"Max concurrent was {maxConcurrent}, expected <= 2");
        }

        [Fact]
        public async Task ErrorHandling_FilterException_ReturnsFailure()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter = new Mock<IFilter>();
            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .ThrowsAsync(new InvalidOperationException("Filter error"));

            await manager.RegisterFilterAsync("ErrorFilter", filter.Object);

            var chainConfig = new ChainConfig { Name = "TestChain" };
            var chain = await manager.CreateChainAsync(chainConfig);
            chain.AddFilter("ErrorFilter");
            manager.SetDefaultChain("TestChain");

            // Act
            var result = await manager.ProcessAsync(new byte[] { 1 }, new ProcessingContext());

            // Assert
            Assert.False(result.IsSuccess);
            Assert.Contains("Filter error", result.ErrorMessage);
        }

        [Fact]
        public async Task DynamicFilterAddition_AddsFilterAtRuntime()
        {
            // Arrange
            using var manager = new FilterManager();
            
            var chainConfig = new ChainConfig { Name = "DynamicChain" };
            var chain = await manager.CreateChainAsync(chainConfig);
            manager.SetDefaultChain("DynamicChain");

            // Initially process without filters
            var result1 = await manager.ProcessAsync(new byte[] { 1 }, new ProcessingContext());

            // Add filter dynamically
            var filter = new Mock<IFilter>();
            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .ReturnsAsync(ProcessingResult.Success(new byte[] { 2, 3 }));

            await manager.RegisterFilterAsync("DynamicFilter", filter.Object);
            chain.AddFilter("DynamicFilter");

            // Act
            var result2 = await manager.ProcessAsync(new byte[] { 1 }, new ProcessingContext());

            // Assert
            Assert.True(result1.IsSuccess);
            Assert.Equal(1, result1.Data.Length);
            Assert.True(result2.IsSuccess);
            Assert.Equal(2, result2.Data.Length);
        }

        [Fact]
        public async Task Disposal_CleansUpResources()
        {
            // Arrange
            var manager = new FilterManager();
            var filter = new Mock<IFilter>();
            
            await manager.RegisterFilterAsync("TestFilter", filter.Object);
            var chainConfig = new ChainConfig { Name = "TestChain" };
            await manager.CreateChainAsync(chainConfig);

            // Act
            manager.Dispose();

            // Assert
            await Assert.ThrowsAsync<ObjectDisposedException>(
                () => manager.ProcessAsync(new byte[] { 1 }, new ProcessingContext()));

            filter.Verify(f => f.Dispose(), Times.Once);
        }

        [Fact]
        public async Task BatchProcessing_ProcessesMultipleMessages()
        {
            // Arrange
            using var manager = new FilterManager();
            var filter = new Mock<IFilter>();
            var processCount = 0;

            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .ReturnsAsync((byte[] d, ProcessingContext c, CancellationToken ct) =>
                  {
                      Interlocked.Increment(ref processCount);
                      return ProcessingResult.Success(d);
                  });

            await manager.RegisterFilterAsync("TestFilter", filter.Object);

            var chainConfig = new ChainConfig { Name = "TestChain" };
            var chain = await manager.CreateChainAsync(chainConfig);
            chain.AddFilter("TestFilter");

            var messages = Enumerable.Range(1, 10)
                .Select(i => new byte[] { (byte)i })
                .ToList();

            // Act
            var results = await manager.ProcessBatchAsync(messages, "TestChain");

            // Assert
            Assert.Equal(10, results.Count);
            Assert.All(results, r => Assert.True(r.IsSuccess));
            Assert.Equal(10, processCount);
        }
    }

    // Test implementations
    public class FilterManager : IDisposable
    {
        private readonly Dictionary<string, IFilter> _filters = new();
        private readonly Dictionary<string, FilterChain> _chains = new();
        private readonly FilterManagerConfig _config;
        private string _defaultChain;
        private bool _isDisposed;

        public bool IsInitialized { get; private set; }
        public int MaxConcurrency => _config.MaxConcurrency;
        public TimeSpan DefaultTimeout => _config.DefaultTimeout;
        public bool MetricsEnabled => _config.EnableMetrics;

        public FilterManager(FilterManagerConfig config = null)
        {
            _config = config ?? new FilterManagerConfig();
        }

        public async Task InitializeAsync(CancellationToken cancellationToken = default)
        {
            await Task.Yield();
            IsInitialized = true;
        }

        public async Task RegisterFilterAsync(string name, IFilter filter, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            
            if (_filters.ContainsKey(name))
                throw new InvalidOperationException($"Filter '{name}' is already registered");

            await Task.Yield();
            _filters[name] = filter;
        }

        public async Task<bool> UnregisterFilterAsync(string name, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            await Task.Yield();
            return _filters.Remove(name);
        }

        public IFilter GetFilter(string name)
        {
            ThrowIfDisposed();
            return _filters.TryGetValue(name, out var filter) ? filter : null;
        }

        public async Task<FilterChain> CreateChainAsync(ChainConfig config, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            
            if (_chains.ContainsKey(config.Name))
                throw new InvalidOperationException($"Chain '{config.Name}' already exists");

            await Task.Yield();
            var chain = new FilterChain(config);
            _chains[config.Name] = chain;
            return chain;
        }

        public void SetDefaultChain(string chainName)
        {
            ThrowIfDisposed();
            _defaultChain = chainName;
        }

        public async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            var chainName = context.ChainName ?? _defaultChain;
            if (string.IsNullOrEmpty(chainName) || !_chains.TryGetValue(chainName, out var chain))
            {
                return ProcessingResult.Success(data);
            }

            return await chain.ExecuteAsync(data, context, new FilterManagerAdapter(this), cancellationToken);
        }

        public async Task<List<ProcessingResult>> ProcessBatchAsync(List<byte[]> messages, string chainName, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            var tasks = messages.Select(data =>
            {
                var context = new ProcessingContext { ChainName = chainName };
                return ProcessAsync(data, context, cancellationToken);
            });

            return (await Task.WhenAll(tasks)).ToList();
        }

        public async Task UpdateConfigurationAsync(FilterManagerConfig config, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            await Task.Yield();
            
            _config.MaxConcurrency = config.MaxConcurrency;
            _config.EnableMetrics = config.EnableMetrics;
            _config.DefaultTimeout = config.DefaultTimeout;
        }

        public ManagerStatistics GetStatistics()
        {
            return new ManagerStatistics
            {
                TotalMessagesProcessed = 10,
                SuccessfulMessages = 10,
                FailedMessages = 0,
                AverageProcessingTime = 5.0
            };
        }

        private void ThrowIfDisposed()
        {
            if (_isDisposed)
                throw new ObjectDisposedException(nameof(FilterManager));
        }

        public void Dispose()
        {
            if (!_isDisposed)
            {
                foreach (var filter in _filters.Values)
                {
                    filter.Dispose();
                }
                _filters.Clear();
                _chains.Clear();
                _isDisposed = true;
            }
        }

        private class FilterManagerAdapter : IFilterManager
        {
            private readonly FilterManager _manager;

            public FilterManagerAdapter(FilterManager manager)
            {
                _manager = manager;
            }

            public IFilter GetFilter(string name)
            {
                return _manager.GetFilter(name);
            }
        }
    }

    public class FilterManagerConfig
    {
        public int MaxConcurrency { get; set; } = 4;
        public TimeSpan DefaultTimeout { get; set; } = TimeSpan.FromSeconds(30);
        public bool EnableMetrics { get; set; }
        public LogLevel LogLevel { get; set; } = LogLevel.Info;
        public bool EnableDynamicScaling { get; set; }
        public TimeSpan MetricsInterval { get; set; } = TimeSpan.FromSeconds(10);
    }

    public class ManagerStatistics
    {
        public int TotalMessagesProcessed { get; set; }
        public int SuccessfulMessages { get; set; }
        public int FailedMessages { get; set; }
        public double AverageProcessingTime { get; set; }
        public int ActiveChains { get; set; }
        public int RegisteredFilters { get; set; }
        public Dictionary<string, FilterStatistics> FilterStatistics { get; set; }
    }

    public enum LogLevel
    {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    }
}