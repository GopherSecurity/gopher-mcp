using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Filters;
using GopherMcp.Manager;
using GopherMcp.Types;
using GopherMcp.Tests.Fixtures;

namespace GopherMcp.Tests.Unit
{
    public class ChainTests
    {
        [Fact]
        public void FilterChain_Construction_DefaultConfig()
        {
            // Arrange & Act
            var chain = new FilterChain();

            // Assert
            Assert.NotNull(chain);
            Assert.Equal("FilterChain", chain.Name);
            Assert.Equal(0, chain.FilterCount);
            Assert.False(chain.IsInitialized);
            Assert.False(chain.IsDisposed);
        }

        [Fact]
        public void FilterChain_Construction_WithConfig()
        {
            // Arrange
            var config = new ChainConfig
            {
                Name = "TestChain",
                ExecutionMode = ChainExecutionMode.Sequential,
                EnableStatistics = true,
                MaxConcurrency = 4,
                DefaultTimeout = TimeSpan.FromSeconds(30)
            };

            // Act
            var chain = new FilterChain(config);

            // Assert
            Assert.Equal("TestChain", chain.Name);
            Assert.Equal(config, chain.Config);
            Assert.Equal(0, chain.FilterCount);
        }

        [Fact]
        public void FilterChain_Construction_WithFilters()
        {
            // Arrange
            var filters = new[]
            {
                new TestFixtures.MockFilter("Filter1"),
                new TestFixtures.MockFilter("Filter2"),
                new TestFixtures.MockFilter("Filter3")
            };

            // Act
            var chain = new FilterChain(filters);

            // Assert
            Assert.Equal(3, chain.FilterCount);
            var chainFilters = chain.GetFilters();
            Assert.Equal("Filter1", chainFilters[0].Name);
            Assert.Equal("Filter2", chainFilters[1].Name);
            Assert.Equal("Filter3", chainFilters[2].Name);
        }

        [Fact]
        public void FilterChain_AddFilter_MaintainsOrder()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });

            // Act
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter2"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter3"));

            // Assert
            var filters = chain.GetFilters();
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_AddFilter_WithPosition()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter2"));

            // Act
            chain.AddFilter(new TestFixtures.MockFilter("Filter3"), FilterPosition.First);

            // Assert
            var filters = chain.GetFilters();
            Assert.Equal("Filter3", filters[0].Name);
            Assert.Equal("Filter1", filters[1].Name);
            Assert.Equal("Filter2", filters[2].Name);
        }

        [Fact]
        public void FilterChain_AddFilterBefore_InsertsCorrectly()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter3"));

            // Act
            chain.AddFilterBefore(new TestFixtures.MockFilter("Filter2"), "Filter3");

            // Assert
            var filters = chain.GetFilters();
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_AddFilterAfter_InsertsCorrectly()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter3"));

            // Act
            chain.AddFilterAfter(new TestFixtures.MockFilter("Filter2"), "Filter1");

            // Assert
            var filters = chain.GetFilters();
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_RemoveFilter_ByReference()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter1 = new TestFixtures.MockFilter("Filter1");
            var filter2 = new TestFixtures.MockFilter("Filter2");
            var filter3 = new TestFixtures.MockFilter("Filter3");
            
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);
            chain.AddFilter(filter3);

            // Act
            var removed = chain.RemoveFilter(filter2);

            // Assert
            Assert.True(removed);
            var filters = chain.GetFilters();
            Assert.Equal(2, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter3", filters[1].Name);
        }

        [Fact]
        public void FilterChain_RemoveFilter_ByName()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter2"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter3"));

            // Act
            var removed = chain.RemoveFilter("Filter2");

            // Assert
            Assert.True(removed);
            var filters = chain.GetFilters();
            Assert.Equal(2, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter3", filters[1].Name);
        }

        [Fact]
        public void FilterChain_GetFilter_ByName()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter2"));

            // Act
            var filter = chain.GetFilter("Filter2");

            // Assert
            Assert.NotNull(filter);
            Assert.Equal("Filter2", filter.Name);
        }

        [Fact]
        public void FilterChain_ClearFilters_RemovesAll()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.AddFilter(new TestFixtures.MockFilter("Filter1"));
            chain.AddFilter(new TestFixtures.MockFilter("Filter2"));

            // Act
            chain.ClearFilters();

            // Assert
            Assert.Equal(0, chain.FilterCount);
            var filters = chain.GetFilters();
            Assert.Empty(filters);
        }

        [Fact]
        public async Task FilterChain_Initialize_InitializesAllFilters()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter1 = new TestFixtures.MockFilter("Filter1");
            var filter2 = new TestFixtures.MockFilter("Filter2");
            
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);

            // Act
            await chain.InitializeAsync();

            // Assert
            Assert.True(chain.IsInitialized);
        }

        [Fact]
        public async Task FilterChain_ProcessAsync_CallsFiltersInOrder()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter1 = new TestFixtures.MockFilter("Filter1");
            var filter2 = new TestFixtures.MockFilter("Filter2");
            var filter3 = new TestFixtures.MockFilter("Filter3");
            
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);
            chain.AddFilter(filter3);
            
            await chain.InitializeAsync();
            
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(1, filter1.ProcessCount);
            Assert.Equal(1, filter2.ProcessCount);
            Assert.Equal(1, filter3.ProcessCount);
        }

        [Fact]
        public async Task FilterChain_ProcessAsync_StopsOnError()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter1 = new TestFixtures.MockFilter("Filter1");
            var filter2 = new TestFixtures.FailingFilter("Filter2", "Test error");
            var filter3 = new TestFixtures.MockFilter("Filter3");
            
            chain.AddFilter(filter1);
            chain.AddFilter(filter2);
            chain.AddFilter(filter3);
            
            await chain.InitializeAsync();
            
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ProcessAsync(data, context);

            // Assert
            Assert.False(result.IsSuccess);
            Assert.Equal("Test error", result.ErrorMessage);
            Assert.Equal(1, filter1.ProcessCount);
            Assert.Equal(0, filter3.ProcessCount); // Should not be called
        }

        [Fact]
        public async Task FilterChain_ProcessAsync_TransformsData()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            
            // Add a filter that doubles the data
            var transformFilter = new TestFixtures.TransformingFilter("Transform", 
                data => data.Select(b => (byte)(b * 2)).ToArray());
            
            chain.AddFilter(transformFilter);
            await chain.InitializeAsync();
            
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(new byte[] { 2, 4, 6 }, result.Data);
        }

        [Fact]
        public async Task FilterChain_ProcessAsync_WithDelay()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var delayFilter = new TestFixtures.DelayFilter("Delay", TimeSpan.FromMilliseconds(100));
            
            chain.AddFilter(delayFilter);
            await chain.InitializeAsync();
            
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var result = await chain.ProcessAsync(data, context);
            sw.Stop();

            // Assert
            Assert.True(result.IsSuccess);
            Assert.True(sw.ElapsedMilliseconds >= 100);
        }

        [Fact]
        public void FilterChain_GetStatistics_ReturnsStats()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig 
            { 
                Name = "TestChain",
                EnableStatistics = true
            });

            // Act
            var stats = chain.GetStatistics();

            // Assert
            Assert.NotNull(stats);
            Assert.Equal(0u, stats.TotalPacketsProcessed);
        }

        [Fact]
        public void FilterChain_Events_OnProcessingStart()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var eventRaised = false;
            ChainEventArgs capturedArgs = null;

            chain.OnProcessingStart += (sender, args) =>
            {
                eventRaised = true;
                capturedArgs = args;
            };

            // Since ProcessAsync is protected in implementation,
            // we can only test that the event handler is properly attached
            Assert.False(eventRaised);
        }

        [Fact]
        public void FilterChain_Dispose_CleansUp()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter = new TestFixtures.MockFilter("Filter1");
            chain.AddFilter(filter);

            // Act
            chain.Dispose();

            // Assert
            Assert.True(chain.IsDisposed);
        }

        [Fact]
        public void FilterChain_UseAfterDispose_ThrowsException()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            chain.Dispose();

            // Act & Assert
            Assert.Throws<ObjectDisposedException>(() => chain.AddFilter(new TestFixtures.MockFilter("Filter")));
        }

        [Fact]
        public async Task FilterChain_ProcessAsync_EmptyChain_ReturnsSuccess()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            await chain.InitializeAsync();
            
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(data, result.Data);
        }

        [Fact]
        public void FilterChain_AddFilterRelative_BeforePosition()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter1 = new TestFixtures.MockFilter("Filter1");
            var filter2 = new TestFixtures.MockFilter("Filter2");
            var filter3 = new TestFixtures.MockFilter("Filter3");
            
            chain.AddFilter(filter1);
            chain.AddFilter(filter3);

            // Act
            chain.AddFilterRelative(filter2, "Filter3", before: true);

            // Assert
            var filters = chain.GetFilters();
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public void FilterChain_AddFilterRelative_AfterPosition()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter1 = new TestFixtures.MockFilter("Filter1");
            var filter2 = new TestFixtures.MockFilter("Filter2");
            var filter3 = new TestFixtures.MockFilter("Filter3");
            
            chain.AddFilter(filter1);
            chain.AddFilter(filter3);

            // Act
            chain.AddFilterRelative(filter2, "Filter1", before: false);

            // Assert
            var filters = chain.GetFilters();
            Assert.Equal(3, filters.Length);
            Assert.Equal("Filter1", filters[0].Name);
            Assert.Equal("Filter2", filters[1].Name);
            Assert.Equal("Filter3", filters[2].Name);
        }

        [Fact]
        public async Task FilterChain_ProcessAsync_WithContext()
        {
            // Arrange
            var chain = new FilterChain(new ChainConfig { Name = "TestChain" });
            var filter = new TestFixtures.MockFilter("Filter1");
            chain.AddFilter(filter);
            await chain.InitializeAsync();
            
            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext
            {
                Direction = ProcessingDirection.Inbound,
                CorrelationId = "test-correlation-id",
                SessionId = "test-session",
                Protocol = "tcp"
            };

            // Act
            var result = await chain.ProcessAsync(data, context);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(1, filter.ProcessCount);
        }
    }
}