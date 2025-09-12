using System;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Filters;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    public class MinimalTests
    {
        [Fact]
        public void Filter_CanBeCreated()
        {
            // Arrange & Act
            var config = new MinimalTestFilterConfig("TestFilter", "TestFilter");
            var filter = new TestMinimalFilter(config);

            // Assert
            Assert.NotNull(filter);
            Assert.Equal("TestFilter", filter.Name);
        }

        [Fact]
        public async Task Filter_ProcessesData()
        {
            // Arrange
            var config = new MinimalTestFilterConfig("TestFilter", "TestFilter");
            var filter = new TestMinimalFilter(config);
            await filter.InitializeAsync();
            var data = new byte[] { 1, 2, 3, 4, 5 };
            var context = new ProcessingContext();

            // Act
            var result = await filter.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(data.Length, result.Data.Length);
        }
    }

    internal class MinimalTestFilterConfig : FilterConfigBase
    {
        public override string Name { get; set; }
        public override string Type { get; set; }
        public override bool Enabled { get; set; } = true;
        public override int Priority { get; set; } = 0;
        public override TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(30);

        public MinimalTestFilterConfig(string name, string type)
        {
            Name = name;
            Type = type;
        }
    }

    internal class TestMinimalFilter : Filter
    {
        public TestMinimalFilter(FilterConfigBase config) : base(config)
        {
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, System.Threading.CancellationToken cancellationToken = default)
        {
            return Task.FromResult(FilterResult.Success(data, 0, data.Length));
        }
    }
}