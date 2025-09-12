using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Filters;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    /// <summary>
    /// Filter chain functionality tests
    /// </summary>
    public class FilterChainTests
    {
        [Fact]
        public void FilterChain_Creation_InitializesCorrectly()
        {
            // Arrange & Act
            var chain = new FilterChain();

            // Assert
            Assert.NotNull(chain);
            Assert.NotNull(chain.GetFilters());
            Assert.Empty(chain.GetFilters());
        }

        [Fact]
        public void FilterChain_CreationWithConfig_UsesConfig()
        {
            // Arrange
            var config = new ChainConfig 
            { 
                Name = "TestChain",
                Mode = ExecutionMode.Sequential
            };

            // Act
            var chain = new FilterChain(config);

            // Assert
            Assert.NotNull(chain);
            Assert.Equal("TestChain", chain.Name);
        }

        [Fact]
        public void FilterChain_AddFilter_AddsToCollection()
        {
            // Arrange
            var chain = new FilterChain();
            var filter = new MockFilter("Filter1");

            // Act
            chain.AddFilter(filter);
            var filters = chain.GetFilters();

            // Assert
            Assert.Single(filters);
            Assert.Equal("Filter1", filters[0].Name);
        }

        [Fact]
        public void FilterChain_AddMultipleFilters_MaintainsOrder()
        {
            // Arrange
            var chain = new FilterChain();
            var filter1 = new MockFilter("Filter1");
            var filter2 = new MockFilter("Filter2");
            var filter3 = new MockFilter("Filter3");

            // Act
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);
            chain.AddFilter(filter3);
            var filters = chain.GetFilters();

            // Assert
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_RemoveFilterByObject_RemovesFromCollection()
        {
            // Arrange
            var chain = new FilterChain();
            var filter1 = new MockFilter("Filter1");
            var filter2 = new MockFilter("Filter2");
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);

            // Act
            var removed = chain.RemoveFilter(filter1);
            var filters = chain.GetFilters();

            // Assert
            Assert.True(removed);
            Assert.Single(filters);
            Assert.Equal("Filter2", filters[0].Name);
        }

        [Fact]
        public void FilterChain_RemoveFilterByName_RemovesFromCollection()
        {
            // Arrange
            var chain = new FilterChain();
            var filter1 = new MockFilter("Filter1");
            var filter2 = new MockFilter("Filter2");
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);

            // Act
            var removed = chain.RemoveFilter("Filter1");
            var filters = chain.GetFilters();

            // Assert
            Assert.True(removed);
            Assert.Single(filters);
            Assert.Equal("Filter2", filters[0].Name);
        }

        [Fact]
        public void FilterChain_RemoveNonExistentFilter_ReturnsFalse()
        {
            // Arrange
            var chain = new FilterChain();

            // Act
            var removed = chain.RemoveFilter("NonExistent");

            // Assert
            Assert.False(removed);
        }

        [Fact]
        public void FilterChain_ClearFilters_RemovesAllFilters()
        {
            // Arrange
            var chain = new FilterChain();
            chain.AddFilter(new MockFilter("Filter1"));
            chain.AddFilter(new MockFilter("Filter2"));
            chain.AddFilter(new MockFilter("Filter3"));

            // Act
            chain.ClearFilters();
            var filters = chain.GetFilters();

            // Assert
            Assert.Empty(filters);
        }

        [Fact]
        public void FilterChain_GetFilter_ReturnsCorrectFilter()
        {
            // Arrange
            var chain = new FilterChain();
            var filter1 = new MockFilter("Filter1");
            var filter2 = new MockFilter("Filter2");
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);

            // Act
            var retrieved = chain.GetFilter("Filter2");

            // Assert
            Assert.NotNull(retrieved);
            Assert.Equal("Filter2", retrieved.Name);
        }

        [Fact]
        public void FilterChain_GetFilter_NonExistent_ReturnsNull()
        {
            // Arrange
            var chain = new FilterChain();

            // Act
            var retrieved = chain.GetFilter("NonExistent");

            // Assert
            Assert.Null(retrieved);
        }

        [Fact]
        public void FilterChain_AddFilterBefore_InsertsAtCorrectPosition()
        {
            // Arrange
            var chain = new FilterChain();
            var filter1 = new MockFilter("Filter1");
            var filter2 = new MockFilter("Filter2");
            var filter3 = new MockFilter("Filter3");
            chain.AddFilter(filter1);
            chain.AddFilter(filter3);

            // Act
            chain.AddFilterBefore(filter2, "Filter3");
            var filters = chain.GetFilters();

            // Assert
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_AddFilterAfter_InsertsAtCorrectPosition()
        {
            // Arrange
            var chain = new FilterChain();
            var filter1 = new MockFilter("Filter1");
            var filter2 = new MockFilter("Filter2");
            var filter3 = new MockFilter("Filter3");
            chain.AddFilter(filter1);
            chain.AddFilter(filter3);

            // Act
            chain.AddFilterAfter(filter2, "Filter1");
            var filters = chain.GetFilters();

            // Assert
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_CreateWithFilters_InitializesWithFilters()
        {
            // Arrange
            var filters = new List<Filter>
            {
                new MockFilter("Filter1"),
                new MockFilter("Filter2"),
                new MockFilter("Filter3")
            };

            // Act
            var chain = new FilterChain(filters);
            var chainFilters = chain.GetFilters();

            // Assert
            Assert.Equal(3, chainFilters.Length);
            Assert.Equal("Filter1", chainFilters[0].Name);
            Assert.Equal("Filter2", chainFilters[1].Name);
            Assert.Equal("Filter3", chainFilters[2].Name);
        }

        [Fact]
        public void FilterChain_GetStatistics_ReturnsValidStats()
        {
            // Arrange
            var chain = new FilterChain();

            // Act
            var stats = chain.GetStatistics();

            // Assert
            Assert.NotNull(stats);
            // ChainStatistics properties depend on implementation
        }

        [Fact]
        public void FilterChain_Dispose_CanBeCalledMultipleTimes()
        {
            // Arrange
            var chain = new FilterChain();
            chain.AddFilter(new MockFilter("Filter1"));

            // Act & Assert - Should not throw
            chain.Dispose();
            chain.Dispose();
            chain.Dispose();
        }
    }

    /// <summary>
    /// Mock filter for testing
    /// </summary>
    internal class MockFilter : Filter
    {
        public MockFilter(string name) : base(new MockFilterConfig(name))
        {
        }

        protected override Task<FilterResult> ProcessInternal(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            return Task.FromResult(FilterResult.Success(data, 0, data.Length));
        }
    }

    /// <summary>
    /// Mock filter configuration
    /// </summary>
    internal class MockFilterConfig : FilterConfigBase
    {
        public override string Name { get; set; }
        public override string Type { get; set; }
        public override bool Enabled { get; set; } = true;
        public override int Priority { get; set; } = 0;
        public override TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(30);

        public MockFilterConfig(string name)
        {
            Name = name;
            Type = "Mock";
        }
    }
}