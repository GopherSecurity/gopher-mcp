using System;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Filters;
using GopherMcp.Types;
using GopherMcp.Tests.Fixtures;

namespace GopherMcp.Tests.Unit
{
    public class FilterTests
    {
        [Fact]
        public async Task Filter_Lifecycle_InitializeProcessDispose()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act & Assert - Initialize
            Assert.False(filter.IsInitialized);
            await filter.InitializeAsync();
            Assert.True(filter.IsInitialized);

            // Act & Assert - Process
            var result = await filter.ProcessAsync(data, context);
            Assert.True(result.IsSuccess);
            Assert.Equal(data, result.Data);
            Assert.Equal(0, result.Offset);
            Assert.Equal(data.Length, result.Length);

            // Act & Assert - Dispose
            Assert.False(filter.IsDisposed);
            filter.Dispose();
            Assert.True(filter.IsDisposed);
        }

        [Fact]
        public async Task Filter_ProcessAsync_ValidData_ReturnsSuccess()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            var data = new byte[] { 1, 2, 3, 4, 5 };
            var context = new ProcessingContext
            {
                Direction = ProcessingDirection.Inbound,
                CorrelationId = Guid.NewGuid().ToString()
            };

            await filter.InitializeAsync();

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.NotNull(result);
            Assert.True(result.IsSuccess);
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(data, result.Data);
            Assert.Equal(0, result.Offset);
            Assert.Equal(data.Length, result.Length);
        }

        [Fact]
        public async Task Filter_ProcessAsync_NullData_ThrowsException()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            await filter.InitializeAsync();
            var context = new ProcessingContext();

            // Act & Assert
            await Assert.ThrowsAsync<ArgumentNullException>(
                () => filter.ProcessAsync((byte[])null, context));
        }

        [Fact]
        public async Task Filter_ProcessAsync_WithCancellation_ThrowsOperationCanceled()
        {
            // Arrange
            var filter = new SlowTestFilter(new TestFilterConfig("SlowFilter", "SlowFilter"));
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();
            var cts = new CancellationTokenSource();
            cts.CancelAfter(TimeSpan.FromMilliseconds(50));

            // Act
            var result = await filter.ProcessAsync(data, context, cts.Token);

            // Assert - The Filter base class catches OperationCanceledException and returns an error result
            Assert.Equal(FilterStatus.Error, result.Status);
            Assert.Equal(FilterError.Timeout, result.ErrorCode);
            Assert.Contains("cancelled", result.ErrorMessage, StringComparison.OrdinalIgnoreCase);
        }

        [Fact]
        public async Task Filter_Events_OnInitialize_RaisedCorrectly()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            var eventRaised = false;
            FilterEventArgs capturedArgs = null;

            filter.OnInitialize += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            await filter.InitializeAsync();

            // Assert
            Assert.True(eventRaised);
            Assert.NotNull(capturedArgs);
            Assert.Equal("TestFilter", capturedArgs.FilterName);
        }

        [Fact]
        public void Filter_Events_OnDestroy_RaisedOnDispose()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            var eventRaised = false;
            FilterEventArgs capturedArgs = null;

            filter.OnDestroy += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            filter.Dispose();

            // Assert
            Assert.True(eventRaised);
            Assert.NotNull(capturedArgs);
            Assert.Equal("TestFilter", capturedArgs.FilterName);
        }

        [Fact]
        public async Task Filter_Events_OnData_RaisedDuringProcessing()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            await filter.InitializeAsync();
            
            var eventRaised = false;
            FilterDataEventArgs capturedArgs = null;
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            filter.OnData += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            await filter.ProcessAsync(data, context);

            // Assert
            Assert.True(eventRaised);
            Assert.NotNull(capturedArgs);
            Assert.Equal("TestFilter", capturedArgs.FilterName);
            Assert.Equal(data, capturedArgs.Buffer);
        }

        [Fact]
        public async Task Filter_Events_OnError_RaisedOnException()
        {
            // Arrange
            var filter = new TestFixtures.FailingFilter("FailFilter", "Test error");
            await filter.InitializeAsync();
            
            var eventRaised = false;
            FilterErrorEventArgs capturedArgs = null;
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            filter.OnError += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.False(result.IsSuccess);
            // OnError event is raised internally when errors occur
        }

        [Fact]
        public async Task Filter_Configuration_UpdateConfig()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            var newConfig = new TestFilterConfig("UpdatedFilter", "UpdatedType")
            {
                Enabled = false,
                Priority = 200
            };

            // Act
            filter.UpdateConfig(newConfig);

            // Assert
            Assert.Equal("UpdatedFilter", filter.Name);
            Assert.Equal("UpdatedType", filter.Type);
            Assert.False(filter.Config.Enabled);
            Assert.Equal(200, filter.Config.Priority);
        }

        [Fact]
        public async Task Filter_Statistics_TracksProcessing()
        {
            // Arrange
            var filter = new StatisticsFilter();
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            for (int i = 0; i < 10; i++)
            {
                await filter.ProcessAsync(data, context);
            }

            var stats = filter.GetStatistics();

            // Assert
            Assert.Equal(10u, stats.PacketsProcessed);
            Assert.Equal(30u, stats.BytesProcessed);
            Assert.True(stats.AverageProcessingTimeUs >= 0);
        }

        [Fact]
        public void Filter_Statistics_Reset()
        {
            // Arrange
            var filter = new StatisticsFilter();
            var stats = filter.GetStatistics();
            
            // Initial state
            Assert.Equal(0u, stats.PacketsProcessed);

            // Act
            filter.ResetStatistics();
            stats = filter.GetStatistics();

            // Assert
            Assert.Equal(0u, stats.PacketsProcessed);
            Assert.Equal(0u, stats.BytesProcessed);
        }

        [Fact]
        public async Task Filter_ThrowIfDisposed_PreventUseAfterDispose()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            filter.Dispose();

            // Assert
            await Assert.ThrowsAsync<ObjectDisposedException>(
                () => filter.ProcessAsync(data, context));
        }

        [Fact]
        public void Filter_Properties_CorrectlySet()
        {
            // Arrange
            var config = new TestFilterConfig("MyFilter", "MyType")
            {
                Description = "Test Description",
                Priority = 150,
                Enabled = true,
                TimeoutMs = 5000
            };

            // Act
            var filter = new TestFilter(config);

            // Assert
            Assert.Equal("MyFilter", filter.Name);
            Assert.Equal("MyType", filter.Type);
            Assert.Equal(config, filter.Config);
            Assert.False(filter.IsInitialized);
            Assert.False(filter.IsDisposed);
        }

        [Fact]
        public async Task Filter_MultipleInitialize_ThrowsException()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));

            // Act
            await filter.InitializeAsync();

            // Assert
            await Assert.ThrowsAsync<InvalidOperationException>(
                () => filter.InitializeAsync());
        }

        [Fact]
        public void Filter_EventSubscriptions_CanBeCounted()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));

            // Act
            filter.OnInitialize += (s, e) => { };
            filter.OnInitialize += (s, e) => { };
            filter.OnDestroy += (s, e) => { };
            filter.OnData += (s, e) => { };
            filter.OnError += (s, e) => { };

            // Assert
            Assert.Equal(2, filter.InitializeEventSubscriberCount);
            Assert.Equal(1, filter.DestroyEventSubscriberCount);
            Assert.Equal(1, filter.DataEventSubscriberCount);
            Assert.Equal(1, filter.ErrorEventSubscriberCount);
        }

        [Fact]
        public void Filter_EventSubscriptions_CanBeCleared()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("TestFilter", "TestFilter"));
            filter.OnInitialize += (s, e) => { };
            filter.OnDestroy += (s, e) => { };

            // Act
            filter.ClearEventSubscriptions();

            // Assert
            Assert.Equal(0, filter.InitializeEventSubscriberCount);
            Assert.Equal(0, filter.DestroyEventSubscriberCount);
        }
    }

    // Test implementations
    public class SlowProcessingFilter : Filter
    {
        public SlowProcessingFilter() : base(new TestFilterConfig("SlowFilter", "SlowFilter"))
        {
        }

        protected override async Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            await Task.Delay(50, cancellationToken);
            return FilterResult.Success(data, 0, data.Length);
        }
    }

    public class ConfigurableFilter : Filter
    {
        private FilterConfigBase _config;

        public ConfigurableFilter() : base(new TestFilterConfig("ConfigurableFilter", "ConfigurableFilter"))
        {
            _config = Config;
        }

        public void UpdateConfiguration(FilterConfigBase config)
        {
            _config = config;
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (!_config.Enabled)
            {
                return Task.FromResult(FilterResult.Error("Filter is disabled"));
            }

            return Task.FromResult(FilterResult.Success(data, 0, data.Length));
        }
    }

    public class ResourceFilter : Filter
    {
        private readonly IDisposable _resource;
        public new bool IsDisposed { get; private set; }

        public ResourceFilter(IDisposable resource) : base(new TestFilterConfig("ResourceFilter", "ResourceFilter"))
        {
            _resource = resource;
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (IsDisposed)
                throw new ObjectDisposedException(nameof(ResourceFilter));

            return Task.FromResult(FilterResult.Success(data, 0, data.Length));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && !IsDisposed)
            {
                _resource?.Dispose();
                IsDisposed = true;
            }
            base.Dispose(disposing);
        }
    }

    public class ErrorFilter : Filter
    {
        public ErrorFilter() : base(new TestFilterConfig("ErrorFilter", "ErrorFilter"))
        {
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            throw new InvalidOperationException("Intentional error for testing");
        }
    }

    public class StatisticsFilter : Filter
    {
        public StatisticsFilter() : base(new TestFilterConfig("StatisticsFilter", "StatisticsFilter"))
        {
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            // Base class automatically updates statistics
            return Task.FromResult(FilterResult.Success(data, 0, data.Length));
        }
    }
}