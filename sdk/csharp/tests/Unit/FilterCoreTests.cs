using System;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Filters;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    /// <summary>
    /// Core filter functionality tests
    /// </summary>
    public class FilterCoreTests
    {
        [Fact]
        public void Filter_Creation_SetsPropertiesCorrectly()
        {
            // Arrange
            var config = new TestFilterConfig("TestFilter", "TestType")
            {
                Enabled = true,
                Priority = 10,
                Timeout = TimeSpan.FromSeconds(30)
            };

            // Act
            var filter = new TestFilter(config);

            // Assert
            Assert.Equal("TestFilter", filter.Name);
            Assert.Equal("TestType", filter.Type);
            Assert.True(filter.Config.Enabled);
            Assert.Equal(10, filter.Config.Priority);
            Assert.Equal(TimeSpan.FromSeconds(30), filter.Config.Timeout);
        }

        [Fact]
        public async Task Filter_ProcessAsync_WithValidData_ReturnsSuccess()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("Test", "Test"));
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3, 4, 5 };
            var context = new ProcessingContext();

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.NotNull(result.Data);
            Assert.Equal(data.Length, result.Length);
        }

        [Fact]
        public async Task Filter_ProcessAsync_WithNullData_ThrowsArgumentNullException()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("Test", "Test"));
            await filter.InitializeAsync();
            byte[]? data = null;
            var context = new ProcessingContext();

            // Act & Assert
            await Assert.ThrowsAsync<ArgumentNullException>(
                () => filter.ProcessAsync(data!, context));
        }

        [Fact]
        public async Task Filter_ProcessAsync_WithCancellation_ReturnsTimeoutError()
        {
            // Arrange
            var filter = new SlowTestFilter(new TestFilterConfig("Slow", "Test"));
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();
            using var cts = new CancellationTokenSource();
            cts.Cancel();

            // Act
            var result = await filter.ProcessAsync(data, context, cts.Token);

            // Assert
            Assert.Equal(FilterStatus.Error, result.Status);
            Assert.Equal(FilterError.Timeout, result.ErrorCode);
            Assert.Contains("cancelled", result.ErrorMessage, StringComparison.OrdinalIgnoreCase);
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(100)]
        [InlineData(1024)]
        [InlineData(65536)]
        public async Task Filter_ProcessAsync_WithVariousDataSizes_HandlesCorrectly(int size)
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("Test", "Test"));
            await filter.InitializeAsync();
            var data = new byte[size];
            if (size > 0) Random.Shared.NextBytes(data);
            var context = new ProcessingContext();

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(size, result.Length);
        }

        [Fact]
        public void Filter_Dispose_CanBeCalledMultipleTimes()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("Test", "Test"));

            // Act & Assert - Should not throw
            filter.Dispose();
            filter.Dispose();
            filter.Dispose();
        }

        [Fact]
        public async Task Filter_GetStatistics_ReturnsCorrectMetrics()
        {
            // Arrange
            var filter = new TestFilter(new TestFilterConfig("Test", "Test"));
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            for (int i = 0; i < 5; i++)
            {
                await filter.ProcessAsync(data, context);
            }
            var stats = filter.GetStatistics();

            // Assert
            Assert.True(stats.PacketsProcessed >= 5);
        }
    }

    /// <summary>
    /// Test filter implementation
    /// </summary>
    internal class TestFilter : Filter
    {
        public TestFilter(FilterConfigBase config) : base(config)
        {
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (data == null)
                throw new ArgumentNullException(nameof(data));

            // Simple pass-through
            return Task.FromResult(FilterResult.Success(data, 0, data.Length));
        }
    }

    /// <summary>
    /// Slow filter for testing cancellation
    /// </summary>
    internal class SlowTestFilter : Filter
    {
        public SlowTestFilter(FilterConfigBase config) : base(config)
        {
        }

        protected override async Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            await Task.Delay(100, cancellationToken);
            return FilterResult.Success(data, 0, data.Length);
        }
    }

    /// <summary>
    /// Test configuration implementation
    /// </summary>
    internal class TestFilterConfig : FilterConfigBase
    {
        public override string Name { get; set; }
        public override string Type { get; set; }
        public override bool Enabled { get; set; } = true;
        public override int Priority { get; set; } = 0;
        public override TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(30);

        public TestFilterConfig(string name, string type)
        {
            Name = name;
            Type = type;
        }
    }
}