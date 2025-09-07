using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp;
using GopherMcp.Filters;
using GopherMcp.Manager;
using GopherMcp.Types;

namespace AdvancedFiltering
{
    class Program
    {
        static async Task Main(string[] args)
        {
            Console.WriteLine("=== Advanced Filtering Example ===\n");

            try
            {
                // Create FilterManager with advanced configuration
                var config = new FilterManagerConfig
                {
                    MaxConcurrency = 8,
                    DefaultTimeout = TimeSpan.FromSeconds(60),
                    EnableMetrics = true,
                    LogLevel = LogLevel.Info,
                    EnableDynamicScaling = true,
                    MetricsInterval = TimeSpan.FromSeconds(5)
                };

                using var manager = new FilterManager(config);

                // Demonstrate complex filter chains
                await DemonstrateComplexFilterChains(manager);

                // Demonstrate parallel processing
                await DemonstrateParallelProcessing(manager);

                // Demonstrate conditional filtering
                await DemonstrateConditionalFiltering(manager);

                // Demonstrate custom filters
                await DemonstrateCustomFilters(manager);

                // Demonstrate performance monitoring
                await DemonstratePerformanceMonitoring(manager);

                Console.WriteLine("\n=== Advanced Filtering Example Completed ===");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"\n[ERROR] {ex.GetType().Name}: {ex.Message}");
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }

        static async Task DemonstrateComplexFilterChains(FilterManager manager)
        {
            Console.WriteLine("=== Complex Filter Chains ===\n");

            // Register multiple filters
            await RegisterAdvancedFilters(manager);

            // Create a complex multi-stage processing chain
            var complexChainConfig = new ChainConfig
            {
                Name = "ComplexProcessingPipeline",
                Mode = ExecutionMode.Sequential,
                ContinueOnError = false,
                MaxRetries = 2,
                RetryDelay = TimeSpan.FromMilliseconds(500)
            };

            var complexChain = await manager.CreateChainAsync(complexChainConfig);

            // Stage 1: Input validation and normalization
            complexChain.AddFilter("InputValidator");
            complexChain.AddFilter("DataNormalizer");

            // Stage 2: Content processing
            complexChain.AddFilter("ContentEnricher");
            complexChain.AddFilter("DataTransformer");

            // Stage 3: Security and compliance
            complexChain.AddFilter("SecurityScanner");
            complexChain.AddFilter("ComplianceChecker");

            // Stage 4: Optimization
            complexChain.AddFilter("PerformanceOptimizer");
            complexChain.AddFilter("CacheManager");

            // Stage 5: Output preparation
            complexChain.AddFilter("OutputFormatter");
            complexChain.AddFilter("CompressionFilter");

            // Process sample data through the complex chain
            var testData = GenerateTestData();
            var context = new ProcessingContext
            {
                Direction = ProcessingDirection.Outbound,
                ChainName = "ComplexProcessingPipeline",
                CorrelationId = Guid.NewGuid().ToString()
            };

            Console.WriteLine($"Processing {testData.Length} bytes through complex chain...");
            var sw = Stopwatch.StartNew();
            
            var result = await manager.ProcessAsync(testData, context);
            
            sw.Stop();
            Console.WriteLine($"Processing completed in {sw.ElapsedMilliseconds}ms");
            Console.WriteLine($"Success: {result.IsSuccess}");
            Console.WriteLine($"Output size: {result.Data.Length} bytes\n");
        }

        static async Task DemonstrateParallelProcessing(FilterManager manager)
        {
            Console.WriteLine("=== Parallel Processing ===\n");

            // Create parallel execution chain
            var parallelChainConfig = new ChainConfig
            {
                Name = "ParallelProcessingChain",
                Mode = ExecutionMode.Parallel,
                ContinueOnError = true,
                MaxConcurrency = 4
            };

            var parallelChain = await manager.CreateChainAsync(parallelChainConfig);

            // Add filters that can run in parallel
            parallelChain.AddFilter("AsyncProcessor1");
            parallelChain.AddFilter("AsyncProcessor2");
            parallelChain.AddFilter("AsyncProcessor3");
            parallelChain.AddFilter("AsyncProcessor4");

            // Process multiple messages concurrently
            var tasks = new List<Task<ProcessingResult>>();
            var messageCount = 10;

            Console.WriteLine($"Processing {messageCount} messages in parallel...");
            var sw = Stopwatch.StartNew();

            for (int i = 0; i < messageCount; i++)
            {
                var data = Encoding.UTF8.GetBytes($"Message {i + 1}");
                var context = new ProcessingContext
                {
                    Direction = ProcessingDirection.Inbound,
                    ChainName = "ParallelProcessingChain",
                    CorrelationId = $"msg-{i + 1}"
                };
                
                tasks.Add(manager.ProcessAsync(data, context));
            }

            var results = await Task.WhenAll(tasks);
            sw.Stop();

            var successCount = results.Count(r => r.IsSuccess);
            Console.WriteLine($"Completed in {sw.ElapsedMilliseconds}ms");
            Console.WriteLine($"Success rate: {successCount}/{messageCount}");
            Console.WriteLine($"Average time per message: {sw.ElapsedMilliseconds / (double)messageCount:F2}ms\n");
        }

        static async Task DemonstrateConditionalFiltering(FilterManager manager)
        {
            Console.WriteLine("=== Conditional Filtering ===\n");

            // Create conditional chain with branching logic
            var conditionalChainConfig = new ChainConfig
            {
                Name = "ConditionalProcessingChain",
                Mode = ExecutionMode.Sequential,
                ContinueOnError = false
            };

            var conditionalChain = await manager.CreateChainAsync(conditionalChainConfig);

            // Add conditional filter
            var conditionalFilter = new ConditionalFilter();
            await manager.RegisterFilterAsync("ConditionalRouter", conditionalFilter);
            conditionalChain.AddFilter("ConditionalRouter");

            // Process different message types
            var messageTypes = new[] { "TypeA", "TypeB", "TypeC", "Unknown" };

            foreach (var messageType in messageTypes)
            {
                var data = Encoding.UTF8.GetBytes($"{{\"type\":\"{messageType}\",\"data\":\"test\"}}");
                var context = new ProcessingContext
                {
                    Direction = ProcessingDirection.Inbound,
                    ChainName = "ConditionalProcessingChain"
                };
                context.SetProperty("MessageType", messageType);

                Console.WriteLine($"Processing message of type: {messageType}");
                var result = await manager.ProcessAsync(data, context);
                
                if (result.IsSuccess)
                {
                    var processedData = Encoding.UTF8.GetString(result.Data);
                    Console.WriteLine($"  Result: {processedData}");
                }
                else
                {
                    Console.WriteLine($"  Error: {result.ErrorMessage}");
                }
            }
            Console.WriteLine();
        }

        static async Task DemonstrateCustomFilters(FilterManager manager)
        {
            Console.WriteLine("=== Custom Filters ===\n");

            // Register custom filters
            var rateLimiter = new RateLimitingFilter(10, TimeSpan.FromSeconds(1));
            await manager.RegisterFilterAsync("RateLimiter", rateLimiter);

            var circuitBreaker = new CircuitBreakerFilter(3, TimeSpan.FromSeconds(30));
            await manager.RegisterFilterAsync("CircuitBreaker", circuitBreaker);

            var retryFilter = new RetryFilter(3, TimeSpan.FromMilliseconds(100));
            await manager.RegisterFilterAsync("RetryFilter", retryFilter);

            var bulkheadFilter = new BulkheadFilter(5);
            await manager.RegisterFilterAsync("BulkheadFilter", bulkheadFilter);

            // Create resilience chain
            var resilienceChainConfig = new ChainConfig
            {
                Name = "ResilienceChain",
                Mode = ExecutionMode.Sequential
            };

            var resilienceChain = await manager.CreateChainAsync(resilienceChainConfig);
            resilienceChain.AddFilter("RateLimiter");
            resilienceChain.AddFilter("CircuitBreaker");
            resilienceChain.AddFilter("BulkheadFilter");
            resilienceChain.AddFilter("RetryFilter");

            // Test resilience patterns
            Console.WriteLine("Testing resilience patterns...");

            // Test rate limiting
            Console.WriteLine("\n1. Rate Limiting Test:");
            for (int i = 0; i < 15; i++)
            {
                var data = Encoding.UTF8.GetBytes($"Request {i + 1}");
                var context = new ProcessingContext
                {
                    ChainName = "ResilienceChain"
                };

                try
                {
                    var result = await manager.ProcessAsync(data, context);
                    Console.WriteLine($"  Request {i + 1}: {(result.IsSuccess ? "Success" : "Rate limited")}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  Request {i + 1}: {ex.Message}");
                }

                if (i == 9)
                {
                    Console.WriteLine("  [Waiting 1 second for rate limit reset...]");
                    await Task.Delay(1000);
                }
            }

            Console.WriteLine();
        }

        static async Task DemonstratePerformanceMonitoring(FilterManager manager)
        {
            Console.WriteLine("=== Performance Monitoring ===\n");

            // Create performance test chain
            var perfChainConfig = new ChainConfig
            {
                Name = "PerformanceTestChain",
                Mode = ExecutionMode.Sequential,
                EnableMetrics = true
            };

            var perfChain = await manager.CreateChainAsync(perfChainConfig);
            
            // Add performance-intensive filters
            perfChain.AddFilter("CpuIntensiveFilter");
            perfChain.AddFilter("MemoryIntensiveFilter");
            perfChain.AddFilter("IoIntensiveFilter");

            // Register performance filters
            await manager.RegisterFilterAsync("CpuIntensiveFilter", new CpuIntensiveFilter());
            await manager.RegisterFilterAsync("MemoryIntensiveFilter", new MemoryIntensiveFilter());
            await manager.RegisterFilterAsync("IoIntensiveFilter", new IoIntensiveFilter());

            // Run performance test
            var iterations = 100;
            var dataSizes = new[] { 1024, 10240, 102400 }; // 1KB, 10KB, 100KB

            foreach (var dataSize in dataSizes)
            {
                Console.WriteLine($"\nTesting with {dataSize / 1024}KB data:");

                var times = new List<long>();
                var data = GenerateTestData(dataSize);

                for (int i = 0; i < iterations; i++)
                {
                    var context = new ProcessingContext
                    {
                        ChainName = "PerformanceTestChain"
                    };

                    var sw = Stopwatch.StartNew();
                    await manager.ProcessAsync(data, context);
                    sw.Stop();

                    times.Add(sw.ElapsedMilliseconds);
                }

                // Calculate statistics
                var avgTime = times.Average();
                var minTime = times.Min();
                var maxTime = times.Max();
                var p50 = GetPercentile(times, 50);
                var p95 = GetPercentile(times, 95);
                var p99 = GetPercentile(times, 99);

                Console.WriteLine($"  Iterations: {iterations}");
                Console.WriteLine($"  Average: {avgTime:F2}ms");
                Console.WriteLine($"  Min: {minTime}ms");
                Console.WriteLine($"  Max: {maxTime}ms");
                Console.WriteLine($"  P50: {p50}ms");
                Console.WriteLine($"  P95: {p95}ms");
                Console.WriteLine($"  P99: {p99}ms");
                Console.WriteLine($"  Throughput: {1000.0 / avgTime:F2} ops/sec");
            }

            // Display overall statistics
            Console.WriteLine("\n=== Overall Statistics ===");
            var stats = manager.GetStatistics();
            Console.WriteLine($"Total Messages: {stats.TotalMessagesProcessed}");
            Console.WriteLine($"Success Rate: {(double)stats.SuccessfulMessages / stats.TotalMessagesProcessed:P2}");
            Console.WriteLine($"Total Bytes: {stats.TotalBytesProcessed:N0}");
            Console.WriteLine($"Average Time: {stats.AverageProcessingTime:F2}ms");
        }

        static async Task RegisterAdvancedFilters(FilterManager manager)
        {
            // Register all required filters for the complex chain
            await manager.RegisterFilterAsync("InputValidator", new InputValidatorFilter());
            await manager.RegisterFilterAsync("DataNormalizer", new DataNormalizerFilter());
            await manager.RegisterFilterAsync("ContentEnricher", new ContentEnricherFilter());
            await manager.RegisterFilterAsync("DataTransformer", new DataTransformerFilter());
            await manager.RegisterFilterAsync("SecurityScanner", new SecurityScannerFilter());
            await manager.RegisterFilterAsync("ComplianceChecker", new ComplianceCheckerFilter());
            await manager.RegisterFilterAsync("PerformanceOptimizer", new PerformanceOptimizerFilter());
            await manager.RegisterFilterAsync("CacheManager", new CacheManagerFilter());
            await manager.RegisterFilterAsync("OutputFormatter", new OutputFormatterFilter());
            await manager.RegisterFilterAsync("CompressionFilter", new AdvancedCompressionFilter());

            // Register async processors
            for (int i = 1; i <= 4; i++)
            {
                await manager.RegisterFilterAsync($"AsyncProcessor{i}", new AsyncProcessorFilter(i));
            }
        }

        static byte[] GenerateTestData(int size = 1024)
        {
            var random = new Random();
            var data = new byte[size];
            random.NextBytes(data);
            return data;
        }

        static long GetPercentile(List<long> values, int percentile)
        {
            var sorted = values.OrderBy(v => v).ToList();
            var index = (int)Math.Ceiling(percentile / 100.0 * sorted.Count) - 1;
            return sorted[Math.Max(0, index)];
        }
    }

    // Custom filter implementations
    public class ConditionalFilter : FilterBase
    {
        public ConditionalFilter() : base(new FilterConfig { Name = "ConditionalRouter" }) { }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            var messageType = context.GetProperty<string>("MessageType");

            switch (messageType)
            {
                case "TypeA":
                    return ProcessingResult.Success(Encoding.UTF8.GetBytes("Processed as Type A"));
                case "TypeB":
                    return ProcessingResult.Success(Encoding.UTF8.GetBytes("Processed as Type B"));
                case "TypeC":
                    return ProcessingResult.Success(Encoding.UTF8.GetBytes("Processed as Type C"));
                default:
                    return ProcessingResult.Failure($"Unknown message type: {messageType}");
            }
        }
    }

    public class RateLimitingFilter : FilterBase
    {
        private readonly SemaphoreSlim _semaphore;
        private readonly int _limit;
        private readonly TimeSpan _period;
        private readonly Queue<DateTime> _requestTimes;

        public RateLimitingFilter(int limit, TimeSpan period) : base(new FilterConfig { Name = "RateLimiter" })
        {
            _limit = limit;
            _period = period;
            _semaphore = new SemaphoreSlim(1, 1);
            _requestTimes = new Queue<DateTime>();
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            await _semaphore.WaitAsync(cancellationToken);
            try
            {
                var now = DateTime.UtcNow;
                
                // Remove old requests outside the time window
                while (_requestTimes.Count > 0 && now - _requestTimes.Peek() > _period)
                {
                    _requestTimes.Dequeue();
                }

                if (_requestTimes.Count >= _limit)
                {
                    return ProcessingResult.Failure("Rate limit exceeded");
                }

                _requestTimes.Enqueue(now);
                return ProcessingResult.Success(data);
            }
            finally
            {
                _semaphore.Release();
            }
        }
    }

    public class CircuitBreakerFilter : FilterBase
    {
        private readonly int _threshold;
        private readonly TimeSpan _timeout;
        private int _failureCount;
        private DateTime _lastFailureTime;
        private CircuitState _state = CircuitState.Closed;

        public CircuitBreakerFilter(int threshold, TimeSpan timeout) : base(new FilterConfig { Name = "CircuitBreaker" })
        {
            _threshold = threshold;
            _timeout = timeout;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (_state == CircuitState.Open)
            {
                if (DateTime.UtcNow - _lastFailureTime > _timeout)
                {
                    _state = CircuitState.HalfOpen;
                }
                else
                {
                    return ProcessingResult.Failure("Circuit breaker is open");
                }
            }

            try
            {
                // Simulate processing
                if (_state == CircuitState.HalfOpen)
                {
                    _state = CircuitState.Closed;
                    _failureCount = 0;
                }

                return ProcessingResult.Success(data);
            }
            catch
            {
                _failureCount++;
                _lastFailureTime = DateTime.UtcNow;

                if (_failureCount >= _threshold)
                {
                    _state = CircuitState.Open;
                }

                throw;
            }
        }

        private enum CircuitState
        {
            Closed,
            Open,
            HalfOpen
        }
    }

    public class RetryFilter : FilterBase
    {
        private readonly int _maxRetries;
        private readonly TimeSpan _delay;

        public RetryFilter(int maxRetries, TimeSpan delay) : base(new FilterConfig { Name = "RetryFilter" })
        {
            _maxRetries = maxRetries;
            _delay = delay;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            for (int i = 0; i <= _maxRetries; i++)
            {
                try
                {
                    // Simulate processing that might fail
                    return ProcessingResult.Success(data);
                }
                catch when (i < _maxRetries)
                {
                    await Task.Delay(_delay, cancellationToken);
                }
            }

            return ProcessingResult.Failure("Max retries exceeded");
        }
    }

    public class BulkheadFilter : FilterBase
    {
        private readonly SemaphoreSlim _semaphore;

        public BulkheadFilter(int maxConcurrency) : base(new FilterConfig { Name = "BulkheadFilter" })
        {
            _semaphore = new SemaphoreSlim(maxConcurrency, maxConcurrency);
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            if (!await _semaphore.WaitAsync(0, cancellationToken))
            {
                return ProcessingResult.Failure("Bulkhead limit reached");
            }

            try
            {
                // Process within bulkhead
                return ProcessingResult.Success(data);
            }
            finally
            {
                _semaphore.Release();
            }
        }
    }

    // Additional filter implementations for the complex chain
    public class InputValidatorFilter : FilterBase
    {
        public InputValidatorFilter() : base(new FilterConfig { Name = "InputValidator" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class DataNormalizerFilter : FilterBase
    {
        public DataNormalizerFilter() : base(new FilterConfig { Name = "DataNormalizer" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class ContentEnricherFilter : FilterBase
    {
        public ContentEnricherFilter() : base(new FilterConfig { Name = "ContentEnricher" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class DataTransformerFilter : FilterBase
    {
        public DataTransformerFilter() : base(new FilterConfig { Name = "DataTransformer" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class SecurityScannerFilter : FilterBase
    {
        public SecurityScannerFilter() : base(new FilterConfig { Name = "SecurityScanner" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class ComplianceCheckerFilter : FilterBase
    {
        public ComplianceCheckerFilter() : base(new FilterConfig { Name = "ComplianceChecker" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class PerformanceOptimizerFilter : FilterBase
    {
        public PerformanceOptimizerFilter() : base(new FilterConfig { Name = "PerformanceOptimizer" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class CacheManagerFilter : FilterBase
    {
        public CacheManagerFilter() : base(new FilterConfig { Name = "CacheManager" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class OutputFormatterFilter : FilterBase
    {
        public OutputFormatterFilter() : base(new FilterConfig { Name = "OutputFormatter" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class AdvancedCompressionFilter : FilterBase
    {
        public AdvancedCompressionFilter() : base(new FilterConfig { Name = "CompressionFilter" }) { }
        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            => Task.FromResult(ProcessingResult.Success(data));
    }

    public class AsyncProcessorFilter : FilterBase
    {
        private readonly int _id;

        public AsyncProcessorFilter(int id) : base(new FilterConfig { Name = $"AsyncProcessor{id}" })
        {
            _id = id;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            await Task.Delay(Random.Shared.Next(10, 100), cancellationToken);
            return ProcessingResult.Success(data);
        }
    }

    public class CpuIntensiveFilter : FilterBase
    {
        public CpuIntensiveFilter() : base(new FilterConfig { Name = "CpuIntensiveFilter" }) { }

        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            // Simulate CPU-intensive work
            var result = 0;
            for (int i = 0; i < 1000000; i++)
            {
                result += i;
            }
            context.SetProperty("CpuResult", result);
            return Task.FromResult(ProcessingResult.Success(data));
        }
    }

    public class MemoryIntensiveFilter : FilterBase
    {
        public MemoryIntensiveFilter() : base(new FilterConfig { Name = "MemoryIntensiveFilter" }) { }

        public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            // Simulate memory-intensive work
            var buffer = new byte[data.Length * 2];
            Buffer.BlockCopy(data, 0, buffer, 0, data.Length);
            Buffer.BlockCopy(data, 0, buffer, data.Length, data.Length);
            return Task.FromResult(ProcessingResult.Success(data));
        }
    }

    public class IoIntensiveFilter : FilterBase
    {
        public IoIntensiveFilter() : base(new FilterConfig { Name = "IoIntensiveFilter" }) { }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            // Simulate I/O-intensive work
            await Task.Delay(10, cancellationToken);
            return ProcessingResult.Success(data);
        }
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