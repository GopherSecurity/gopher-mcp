using System;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using Moq;
using GopherMcp.Filters;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    public class FilterTests
    {
        [Fact]
        public async Task Filter_Lifecycle_InitializeProcessDispose()
        {
            // Arrange
            var filter = new TestFilter();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            await filter.InitializeAsync();
            var result = await filter.ProcessAsync(data, context);
            filter.Dispose();

            // Assert
            Assert.True(filter.IsInitialized);
            Assert.True(result.IsSuccess);
            Assert.True(filter.IsDisposed);
        }

        [Fact]
        public async Task ProcessAsync_ValidData_ReturnsSuccess()
        {
            // Arrange
            var filter = new TestFilter();
            var data = new byte[] { 1, 2, 3, 4, 5 };
            var context = new ProcessingContext
            {
                Direction = ProcessingDirection.Inbound
            };

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(data, result.Data);
            Assert.Null(result.ErrorMessage);
        }

        [Fact]
        public async Task ProcessAsync_NullData_ThrowsArgumentNullException()
        {
            // Arrange
            var filter = new TestFilter();
            byte[] data = null;
            var context = new ProcessingContext();

            // Act & Assert
            await Assert.ThrowsAsync<ArgumentNullException>(
                () => filter.ProcessAsync(data, context));
        }

        [Fact]
        public async Task ProcessAsync_WithCancellation_ThrowsOperationCanceledException()
        {
            // Arrange
            var filter = new SlowProcessingFilter();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();
            var cts = new CancellationTokenSource();
            cts.Cancel();

            // Act & Assert
            await Assert.ThrowsAsync<OperationCanceledException>(
                () => filter.ProcessAsync(data, context, cts.Token));
        }

        [Fact]
        public void EventRaising_ProcessingStarted_RaisesEvent()
        {
            // Arrange
            var filter = new TestFilter();
            var eventRaised = false;
            FilterEventArgs capturedArgs = null;

            filter.ProcessingStarted += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            filter.RaiseProcessingStarted();

            // Assert
            Assert.True(eventRaised);
            Assert.NotNull(capturedArgs);
            Assert.Equal("TestFilter", capturedArgs.FilterName);
        }

        [Fact]
        public void EventRaising_ProcessingCompleted_RaisesEventWithDuration()
        {
            // Arrange
            var filter = new TestFilter();
            var eventRaised = false;
            FilterCompletedEventArgs capturedArgs = null;

            filter.ProcessingCompleted += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            filter.RaiseProcessingCompleted(100);

            // Assert
            Assert.True(eventRaised);
            Assert.NotNull(capturedArgs);
            Assert.Equal(100, capturedArgs.Duration);
            Assert.True(capturedArgs.IsSuccess);
        }

        [Fact]
        public void EventRaising_ErrorOccurred_RaisesEventWithException()
        {
            // Arrange
            var filter = new TestFilter();
            var eventRaised = false;
            FilterErrorEventArgs capturedArgs = null;
            var exception = new InvalidOperationException("Test error");

            filter.ErrorOccurred += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Act
            filter.RaiseError(exception);

            // Assert
            Assert.True(eventRaised);
            Assert.NotNull(capturedArgs);
            Assert.Equal(exception, capturedArgs.Exception);
        }

        [Fact]
        public async Task ConfigurationUpdate_UpdatesFilterBehavior()
        {
            // Arrange
            var filter = new ConfigurableFilter();
            var initialConfig = new FilterConfig { Name = "Initial", Enabled = true };
            var updatedConfig = new FilterConfig { Name = "Updated", Enabled = false };

            filter.UpdateConfiguration(initialConfig);
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result1 = await filter.ProcessAsync(data, context);
            filter.UpdateConfiguration(updatedConfig);
            var result2 = await filter.ProcessAsync(data, context);

            // Assert
            Assert.True(result1.IsSuccess);
            Assert.False(result2.IsSuccess);
            Assert.Equal("Filter is disabled", result2.ErrorMessage);
        }

        [Fact]
        public void Disposal_DisposesResources()
        {
            // Arrange
            var mockResource = new Mock<IDisposable>();
            var filter = new ResourceFilter(mockResource.Object);

            // Act
            filter.Dispose();

            // Assert
            mockResource.Verify(r => r.Dispose(), Times.Once);
            Assert.True(filter.IsDisposed);
        }

        [Fact]
        public void Disposal_MultipleCalls_DoesNotThrow()
        {
            // Arrange
            var filter = new TestFilter();

            // Act & Assert (should not throw)
            filter.Dispose();
            filter.Dispose();
            filter.Dispose();
        }

        [Fact]
        public async Task MockNativeHandle_SimulatesNativeInteraction()
        {
            // Arrange
            var mockHandle = new Mock<INativeFilterHandle>();
            mockHandle.Setup(h => h.Process(It.IsAny<byte[]>()))
                      .Returns<byte[]>(data => Task.FromResult(data));

            var filter = new NativeFilter(mockHandle.Object);
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            mockHandle.Verify(h => h.Process(data), Times.Once);
            Assert.True(result.IsSuccess);
            Assert.Equal(data, result.Data);
        }

        [Theory]
        [InlineData(ProcessingDirection.Inbound)]
        [InlineData(ProcessingDirection.Outbound)]
        public async Task ProcessAsync_DifferentDirections_ProcessesCorrectly(ProcessingDirection direction)
        {
            // Arrange
            var filter = new DirectionalFilter();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext { Direction = direction };

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            var expectedPrefix = direction == ProcessingDirection.Inbound ? "IN:" : "OUT:";
            Assert.Contains(expectedPrefix, System.Text.Encoding.UTF8.GetString(result.Data));
        }

        [Fact]
        public async Task Filter_Statistics_TracksProcessingMetrics()
        {
            // Arrange
            var filter = new StatisticsFilter();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            for (int i = 0; i < 10; i++)
            {
                await filter.ProcessAsync(data, context);
            }

            var stats = filter.GetStatistics();

            // Assert
            Assert.Equal(10, stats.ProcessCount);
            Assert.Equal(30, stats.TotalBytesProcessed);
            Assert.True(stats.AverageProcessingTime > 0);
        }
    }

    // Test filter implementations
    public class TestFilter : FilterBase
    {
        public bool IsInitialized { get; private set; }
        public bool IsDisposed { get; private set; }

        public TestFilter() : base(new FilterConfig { Name = "TestFilter" })
        {
        }

        public override async Task InitializeAsync(CancellationToken cancellationToken = default)
        {
            await base.InitializeAsync(cancellationToken);
            IsInitialized = true;
        }

        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (data == null)
                throw new ArgumentNullException(nameof(data));

            return Task.FromResult(ProcessingResult.Success(data));
        }

        public void RaiseProcessingStarted()
        {
            OnProcessingStarted();
        }

        public void RaiseProcessingCompleted(long duration)
        {
            OnProcessingCompleted(true, duration);
        }

        public void RaiseError(Exception exception)
        {
            OnError(exception);
        }

        protected override void Dispose(bool disposing)
        {
            base.Dispose(disposing);
            IsDisposed = true;
        }
    }

    public class SlowProcessingFilter : FilterBase
    {
        public SlowProcessingFilter() : base(new FilterConfig { Name = "SlowFilter" })
        {
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            await Task.Delay(1000, cancellationToken);
            return ProcessingResult.Success(data);
        }
    }

    public class ConfigurableFilter : FilterBase
    {
        private FilterConfig _config;

        public ConfigurableFilter() : base(new FilterConfig { Name = "ConfigurableFilter" })
        {
            _config = Config;
        }

        public void UpdateConfiguration(FilterConfig config)
        {
            _config = config;
        }

        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (!_config.Enabled)
            {
                return Task.FromResult(ProcessingResult.Failure("Filter is disabled"));
            }

            return Task.FromResult(ProcessingResult.Success(data));
        }
    }

    public class ResourceFilter : FilterBase
    {
        private readonly IDisposable _resource;
        public bool IsDisposed { get; private set; }

        public ResourceFilter(IDisposable resource) : base(new FilterConfig { Name = "ResourceFilter" })
        {
            _resource = resource;
        }

        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            return Task.FromResult(ProcessingResult.Success(data));
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

    public interface INativeFilterHandle
    {
        Task<byte[]> Process(byte[] data);
    }

    public class NativeFilter : FilterBase
    {
        private readonly INativeFilterHandle _nativeHandle;

        public NativeFilter(INativeFilterHandle nativeHandle) : base(new FilterConfig { Name = "NativeFilter" })
        {
            _nativeHandle = nativeHandle;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            var processedData = await _nativeHandle.Process(data);
            return ProcessingResult.Success(processedData);
        }
    }

    public class DirectionalFilter : FilterBase
    {
        public DirectionalFilter() : base(new FilterConfig { Name = "DirectionalFilter" })
        {
        }

        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            var prefix = context.Direction == ProcessingDirection.Inbound ? "IN:" : "OUT:";
            var prefixBytes = System.Text.Encoding.UTF8.GetBytes(prefix);
            var result = new byte[prefixBytes.Length + data.Length];
            
            Buffer.BlockCopy(prefixBytes, 0, result, 0, prefixBytes.Length);
            Buffer.BlockCopy(data, 0, result, prefixBytes.Length, data.Length);

            return Task.FromResult(ProcessingResult.Success(result));
        }
    }

    public class StatisticsFilter : FilterBase
    {
        private int _processCount;
        private long _totalBytes;
        private double _totalTime;

        public StatisticsFilter() : base(new FilterConfig { Name = "StatisticsFilter" })
        {
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            
            await Task.Delay(10, cancellationToken); // Simulate work
            
            sw.Stop();
            
            Interlocked.Increment(ref _processCount);
            Interlocked.Add(ref _totalBytes, data.Length);
            
            var time = sw.Elapsed.TotalMilliseconds;
            var currentTotal = _totalTime;
            while (true)
            {
                var newTotal = currentTotal + time;
                var oldTotal = Interlocked.CompareExchange(ref _totalTime, newTotal, currentTotal);
                if (oldTotal == currentTotal)
                    break;
                currentTotal = oldTotal;
            }

            return ProcessingResult.Success(data);
        }

        public FilterStatistics GetStatistics()
        {
            return new FilterStatistics
            {
                ProcessCount = _processCount,
                TotalBytesProcessed = _totalBytes,
                AverageProcessingTime = _processCount > 0 ? _totalTime / _processCount : 0
            };
        }
    }

    public class FilterStatistics
    {
        public int ProcessCount { get; set; }
        public long TotalBytesProcessed { get; set; }
        public double AverageProcessingTime { get; set; }
    }
}