using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using Moq;
using GopherMcp.Chain;
using GopherMcp.Filters;
using GopherMcp.Manager;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    public class ChainTests
    {
        [Fact]
        public void ChainBuilding_AddFilters_MaintainsOrder()
        {
            // Arrange
            var chain = new FilterChain("TestChain");

            // Act
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2");
            chain.AddFilter("Filter3");

            var filters = chain.GetFilters();

            // Assert
            Assert.Equal(3, filters.Count);
            Assert.Equal("Filter1", filters[0]);
            Assert.Equal("Filter2", filters[1]);
            Assert.Equal("Filter3", filters[2]);
        }

        [Fact]
        public void ChainBuilding_RemoveFilter_UpdatesChain()
        {
            // Arrange
            var chain = new FilterChain("TestChain");
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2");
            chain.AddFilter("Filter3");

            // Act
            var removed = chain.RemoveFilter("Filter2");
            var filters = chain.GetFilters();

            // Assert
            Assert.True(removed);
            Assert.Equal(2, filters.Count);
            Assert.Equal("Filter1", filters[0]);
            Assert.Equal("Filter3", filters[1]);
        }

        [Fact]
        public void ChainBuilding_InsertFilter_AtSpecificPosition()
        {
            // Arrange
            var chain = new FilterChain("TestChain");
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter3");

            // Act
            chain.InsertFilter(1, "Filter2");
            var filters = chain.GetFilters();

            // Assert
            Assert.Equal(3, filters.Count);
            Assert.Equal("Filter1", filters[0]);
            Assert.Equal("Filter2", filters[1]);
            Assert.Equal("Filter3", filters[2]);
        }

        [Fact]
        public void FilterOrdering_MoveFilter_ChangesPosition()
        {
            // Arrange
            var chain = new FilterChain("TestChain");
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2");
            chain.AddFilter("Filter3");

            // Act
            chain.MoveFilter("Filter3", 0);
            var filters = chain.GetFilters();

            // Assert
            Assert.Equal("Filter3", filters[0]);
            Assert.Equal("Filter1", filters[1]);
            Assert.Equal("Filter2", filters[2]);
        }

        [Theory]
        [InlineData(ExecutionMode.Sequential)]
        [InlineData(ExecutionMode.Parallel)]
        [InlineData(ExecutionMode.Conditional)]
        public void ExecutionMode_SetMode_UpdatesChainBehavior(ExecutionMode mode)
        {
            // Arrange
            var config = new ChainConfig
            {
                Name = "TestChain",
                Mode = mode
            };

            // Act
            var chain = new FilterChain(config);

            // Assert
            Assert.Equal(mode, chain.Mode);
        }

        [Fact]
        public async Task ErrorPropagation_FilterThrows_ChainHandlesError()
        {
            // Arrange
            var mockManager = new Mock<IFilterManager>();
            var errorFilter = new Mock<IFilter>();
            var exception = new InvalidOperationException("Filter error");

            errorFilter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                       .ThrowsAsync(exception);

            mockManager.Setup(m => m.GetFilter("ErrorFilter"))
                       .Returns(errorFilter.Object);

            var chain = new FilterChain("TestChain");
            chain.AddFilter("ErrorFilter");

            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ExecuteAsync(data, context, mockManager.Object);

            // Assert
            Assert.False(result.IsSuccess);
            Assert.Contains("Filter error", result.ErrorMessage);
        }

        [Fact]
        public async Task ErrorPropagation_ContinueOnError_ProcessesRemainingFilters()
        {
            // Arrange
            var mockManager = new Mock<IFilterManager>();
            var successFilter = new Mock<IFilter>();
            var errorFilter = new Mock<IFilter>();

            successFilter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                         .ReturnsAsync(ProcessingResult.Success(new byte[] { 1, 2, 3 }));

            errorFilter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                       .ReturnsAsync(ProcessingResult.Failure("Error"));

            mockManager.Setup(m => m.GetFilter("SuccessFilter"))
                       .Returns(successFilter.Object);
            mockManager.Setup(m => m.GetFilter("ErrorFilter"))
                       .Returns(errorFilter.Object);

            var config = new ChainConfig
            {
                Name = "TestChain",
                ContinueOnError = true
            };

            var chain = new FilterChain(config);
            chain.AddFilter("ErrorFilter");
            chain.AddFilter("SuccessFilter");

            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ExecuteAsync(data, context, mockManager.Object);

            // Assert
            Assert.True(result.IsSuccess);
            successFilter.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Once);
        }

        [Fact]
        public async Task StatisticsAggregation_TracksChainMetrics()
        {
            // Arrange
            var mockManager = new Mock<IFilterManager>();
            var filter1 = new Mock<IFilter>();
            var filter2 = new Mock<IFilter>();

            filter1.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync(ProcessingResult.Success(new byte[] { 1, 2, 3 }));

            filter2.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync(ProcessingResult.Success(new byte[] { 1, 2, 3, 4 }));

            mockManager.Setup(m => m.GetFilter("Filter1")).Returns(filter1.Object);
            mockManager.Setup(m => m.GetFilter("Filter2")).Returns(filter2.Object);

            var chain = new FilterChain("TestChain");
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2");

            var data = new byte[] { 1, 2 };
            var context = new ProcessingContext();

            // Act
            for (int i = 0; i < 5; i++)
            {
                await chain.ExecuteAsync(data, context, mockManager.Object);
            }

            var stats = chain.GetStatistics();

            // Assert
            Assert.Equal(5, stats.ExecutionCount);
            Assert.Equal(5, stats.SuccessCount);
            Assert.Equal(0, stats.FailureCount);
            Assert.True(stats.AverageExecutionTime >= 0);
        }

        [Fact]
        public void ChainCloning_CreatesCopy_WithSameConfiguration()
        {
            // Arrange
            var config = new ChainConfig
            {
                Name = "OriginalChain",
                Mode = ExecutionMode.Parallel,
                ContinueOnError = true,
                MaxRetries = 3
            };

            var chain = new FilterChain(config);
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2");

            // Act
            var clone = chain.Clone("ClonedChain");

            // Assert
            Assert.Equal("ClonedChain", clone.Name);
            Assert.Equal(chain.Mode, clone.Mode);
            Assert.Equal(chain.ContinueOnError, clone.ContinueOnError);
            Assert.Equal(chain.MaxRetries, clone.MaxRetries);
            Assert.Equal(chain.GetFilters(), clone.GetFilters());
        }

        [Fact]
        public async Task ParallelExecution_ProcessesFiltersInParallel()
        {
            // Arrange
            var mockManager = new Mock<IFilterManager>();
            var executionOrder = new List<string>();
            var semaphore = new SemaphoreSlim(0);

            for (int i = 1; i <= 3; i++)
            {
                var filterName = $"Filter{i}";
                var filter = new Mock<IFilter>();
                
                filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                      .Returns(async (byte[] d, ProcessingContext c, CancellationToken ct) =>
                      {
                          executionOrder.Add(filterName);
                          await Task.Delay(10); // Simulate work
                          return ProcessingResult.Success(d);
                      });

                mockManager.Setup(m => m.GetFilter(filterName)).Returns(filter.Object);
            }

            var config = new ChainConfig
            {
                Name = "ParallelChain",
                Mode = ExecutionMode.Parallel
            };

            var chain = new FilterChain(config);
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2");
            chain.AddFilter("Filter3");

            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ExecuteAsync(data, context, mockManager.Object);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(3, executionOrder.Count);
            // In parallel mode, order is not guaranteed
        }

        [Fact]
        public async Task ConditionalExecution_SkipsFiltersBasedOnCondition()
        {
            // Arrange
            var mockManager = new Mock<IFilterManager>();
            var filter1 = new Mock<IFilter>();
            var filter2 = new Mock<IFilter>();
            var filter3 = new Mock<IFilter>();

            filter1.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync((byte[] d, ProcessingContext c, CancellationToken ct) =>
                   {
                       c.SetProperty("SkipFilter2", true);
                       return ProcessingResult.Success(d);
                   });

            filter2.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync(ProcessingResult.Success(new byte[] { 1 }));

            filter3.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                   .ReturnsAsync(ProcessingResult.Success(new byte[] { 1 }));

            mockManager.Setup(m => m.GetFilter("Filter1")).Returns(filter1.Object);
            mockManager.Setup(m => m.GetFilter("Filter2")).Returns(filter2.Object);
            mockManager.Setup(m => m.GetFilter("Filter3")).Returns(filter3.Object);

            var config = new ChainConfig
            {
                Name = "ConditionalChain",
                Mode = ExecutionMode.Conditional
            };

            var chain = new ConditionalFilterChain(config);
            chain.AddFilter("Filter1");
            chain.AddFilter("Filter2", c => !c.GetProperty<bool>("SkipFilter2"));
            chain.AddFilter("Filter3");

            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ExecuteAsync(data, context, mockManager.Object);

            // Assert
            Assert.True(result.IsSuccess);
            filter1.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Once);
            filter2.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Never);
            filter3.Verify(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()), Times.Once);
        }

        [Fact]
        public async Task ChainRetry_RetriesOnFailure()
        {
            // Arrange
            var mockManager = new Mock<IFilterManager>();
            var filter = new Mock<IFilter>();
            var attempts = 0;

            filter.Setup(f => f.ProcessAsync(It.IsAny<byte[]>(), It.IsAny<ProcessingContext>(), It.IsAny<CancellationToken>()))
                  .ReturnsAsync(() =>
                  {
                      attempts++;
                      if (attempts < 3)
                          return ProcessingResult.Failure("Temporary error");
                      return ProcessingResult.Success(new byte[] { 1, 2, 3 });
                  });

            mockManager.Setup(m => m.GetFilter("RetryFilter")).Returns(filter.Object);

            var config = new ChainConfig
            {
                Name = "RetryChain",
                MaxRetries = 3,
                RetryDelay = TimeSpan.FromMilliseconds(10)
            };

            var chain = new FilterChain(config);
            chain.AddFilter("RetryFilter");

            var data = new byte[] { 1, 2, 3 };
            var context = new ProcessingContext();

            // Act
            var result = await chain.ExecuteAsync(data, context, mockManager.Object);

            // Assert
            Assert.True(result.IsSuccess);
            Assert.Equal(3, attempts);
        }
    }

    // Test implementations
    public class ConditionalFilterChain : FilterChain
    {
        private readonly Dictionary<string, Func<ProcessingContext, bool>> _conditions = new();

        public ConditionalFilterChain(ChainConfig config) : base(config)
        {
        }

        public void AddFilter(string filterName, Func<ProcessingContext, bool> condition)
        {
            base.AddFilter(filterName);
            _conditions[filterName] = condition;
        }

        public override async Task<ProcessingResult> ExecuteAsync(byte[] data, ProcessingContext context, IFilterManager manager, CancellationToken cancellationToken = default)
        {
            var filters = GetFilters();
            var currentData = data;

            foreach (var filterName in filters)
            {
                if (_conditions.ContainsKey(filterName) && !_conditions[filterName](context))
                {
                    continue; // Skip this filter
                }

                var filter = manager.GetFilter(filterName);
                if (filter == null)
                {
                    return ProcessingResult.Failure($"Filter '{filterName}' not found");
                }

                var result = await filter.ProcessAsync(currentData, context, cancellationToken);
                if (!result.IsSuccess)
                {
                    return result;
                }

                currentData = result.Data;
            }

            return ProcessingResult.Success(currentData);
        }
    }

    public interface IFilterManager
    {
        IFilter GetFilter(string name);
    }
}