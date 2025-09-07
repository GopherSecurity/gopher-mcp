using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using Xunit.Abstractions;
using GopherMcp.Integration;
using GopherMcp.Transport;
using GopherMcp.Manager;
using GopherMcp.Filters;
using GopherMcp.Types;

namespace GopherMcp.Tests.Integration
{
    public class EndToEndTests : IDisposable
    {
        private readonly ITestOutputHelper _output;
        private readonly List<IDisposable> _disposables = new();

        public EndToEndTests(ITestOutputHelper output)
        {
            _output = output;
        }

        [Fact]
        public async Task CompleteMessageFlow_FilterProcessing_EndToEnd()
        {
            // Arrange - Create complete pipeline
            var port = GetAvailablePort();
            
            // Setup filter manager
            var filterConfig = new FilterManagerConfig
            {
                MaxConcurrency = 4,
                EnableMetrics = true
            };
            var filterManager = new FilterManager(filterConfig);
            _disposables.Add(filterManager);

            // Register filters
            await RegisterTestFilters(filterManager);

            // Create filter chain
            var chainConfig = new ChainConfig
            {
                Name = "E2EChain",
                Mode = ExecutionMode.Sequential
            };
            var chain = await filterManager.CreateChainAsync(chainConfig);
            chain.AddFilter("ValidationFilter");
            chain.AddFilter("TransformFilter");
            chain.AddFilter("CompressionFilter");

            // Setup server with filter integration
            var serverTransport = CreateTransport(port);
            var server = new McpServer(serverTransport);
            _disposables.Add(serverTransport);
            _disposables.Add(server);

            // Register tool that processes through filters
            server.RegisterTool<ProcessRequest, ProcessResult>("process.data",
                "Process data through filters",
                async (request) =>
                {
                    if (request == null)
                        return new ProcessResult { Success = false };

                    var data = Encoding.UTF8.GetBytes(request.Data);
                    var context = new ProcessingContext
                    {
                        ChainName = "E2EChain",
                        Direction = ProcessingDirection.Inbound
                    };

                    var result = await filterManager.ProcessAsync(data, context);
                    
                    return new ProcessResult
                    {
                        Success = result.IsSuccess,
                        ProcessedData = result.IsSuccess ? Encoding.UTF8.GetString(result.Data) : null,
                        Error = result.ErrorMessage
                    };
                });

            await server.StartAsync();

            // Setup client
            var clientTransport = CreateTransport(port);
            var client = new McpClient(clientTransport);
            _disposables.Add(clientTransport);
            _disposables.Add(client);

            await client.ConnectAsync();

            // Act - Send data through complete pipeline
            var testData = new { id = "123", message = "Test message", timestamp = DateTime.UtcNow };
            var jsonData = System.Text.Json.JsonSerializer.Serialize(testData);

            var result = await client.CallToolAsync<ProcessResult>(
                "process.data",
                new ProcessRequest { Data = jsonData });

            // Assert
            Assert.NotNull(result);
            Assert.True(result.Success);
            Assert.NotNull(result.ProcessedData);

            // Verify filters were applied
            var processedJson = System.Text.Json.JsonDocument.Parse(result.ProcessedData);
            Assert.True(processedJson.RootElement.TryGetProperty("validated", out _));
            Assert.True(processedJson.RootElement.TryGetProperty("transformed", out _));

            // Check metrics
            var stats = filterManager.GetStatistics();
            Assert.True(stats.TotalMessagesProcessed > 0);

            // Cleanup
            await client.DisconnectAsync();
            await server.StopAsync();
        }

        [Fact]
        public async Task PerformanceBenchmark_MeasureThroughput()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateOptimizedClientServerPair(port);

            // Register simple echo tool
            server.RegisterTool<EchoRequest, EchoResult>("echo",
                "Echo tool for benchmarking",
                async (request) =>
                {
                    return await Task.FromResult(new EchoResult { Data = request?.Data });
                });

            // Warmup
            for (int i = 0; i < 10; i++)
            {
                await client.CallToolAsync<EchoResult>("echo", new EchoRequest { Data = "warmup" });
            }

            // Act - Performance test
            var iterations = 1000;
            var sw = Stopwatch.StartNew();
            var tasks = new List<Task<EchoResult>>();

            for (int i = 0; i < iterations; i++)
            {
                var task = client.CallToolAsync<EchoResult>("echo", 
                    new EchoRequest { Data = $"test-{i}" });
                tasks.Add(task);
            }

            var results = await Task.WhenAll(tasks);
            sw.Stop();

            // Calculate metrics
            var totalTime = sw.Elapsed.TotalSeconds;
            var throughput = iterations / totalTime;
            var avgLatency = sw.Elapsed.TotalMilliseconds / iterations;

            _output.WriteLine($"Performance Metrics:");
            _output.WriteLine($"  Total iterations: {iterations}");
            _output.WriteLine($"  Total time: {totalTime:F2} seconds");
            _output.WriteLine($"  Throughput: {throughput:F2} ops/sec");
            _output.WriteLine($"  Average latency: {avgLatency:F2} ms");

            // Assert
            Assert.Equal(iterations, results.Length);
            Assert.All(results, r => Assert.NotNull(r));
            Assert.True(throughput > 100, $"Throughput {throughput:F2} is below expected threshold");

            // Cleanup
            await client.DisconnectAsync();
            await server.StopAsync();
        }

        [Fact]
        public async Task MemoryLeakDetection_NoMemoryLeaks()
        {
            // Skip on non-Windows platforms where GC behavior might differ
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return;

            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateOptimizedClientServerPair(port);

            server.RegisterTool<object, object>("memory.test",
                "Memory test tool",
                async (request) =>
                {
                    // Allocate some memory
                    var buffer = new byte[1024];
                    return await Task.FromResult(new { size = buffer.Length });
                });

            // Get initial memory
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            var initialMemory = GC.GetTotalMemory(false);

            // Act - Run many iterations
            for (int i = 0; i < 1000; i++)
            {
                await client.CallToolAsync<object>("memory.test", null);
                
                if (i % 100 == 0)
                {
                    GC.Collect();
                }
            }

            // Force collection
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            var finalMemory = GC.GetTotalMemory(false);

            var memoryIncrease = finalMemory - initialMemory;
            var memoryIncreaseMB = memoryIncrease / (1024.0 * 1024.0);

            _output.WriteLine($"Memory Usage:");
            _output.WriteLine($"  Initial: {initialMemory / 1024.0 / 1024.0:F2} MB");
            _output.WriteLine($"  Final: {finalMemory / 1024.0 / 1024.0:F2} MB");
            _output.WriteLine($"  Increase: {memoryIncreaseMB:F2} MB");

            // Assert - Memory increase should be minimal
            Assert.True(memoryIncreaseMB < 10, $"Memory increased by {memoryIncreaseMB:F2} MB");

            // Cleanup
            await client.DisconnectAsync();
            await server.StopAsync();
        }

        [Fact]
        public async Task StressTesting_HighLoad()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateOptimizedClientServerPair(port);

            var processedCount = 0;
            var errorCount = 0;

            server.RegisterTool<StressRequest, StressResult>("stress.test",
                "Stress test tool",
                async (request) =>
                {
                    Interlocked.Increment(ref processedCount);
                    
                    // Simulate varying processing times
                    var delay = Random.Shared.Next(1, 10);
                    await Task.Delay(delay);
                    
                    // Randomly fail some requests (5% failure rate)
                    if (Random.Shared.Next(100) < 5)
                    {
                        Interlocked.Increment(ref errorCount);
                        throw new InvalidOperationException("Simulated error");
                    }
                    
                    return new StressResult
                    {
                        ProcessedAt = DateTime.UtcNow,
                        ProcessingTime = delay
                    };
                });

            // Act - Generate high load
            var concurrentClients = 10;
            var requestsPerClient = 100;
            var sw = Stopwatch.StartNew();

            var clientTasks = new List<Task>();
            for (int c = 0; c < concurrentClients; c++)
            {
                var clientId = c;
                clientTasks.Add(Task.Run(async () =>
                {
                    // Create additional client
                    var stressClientTransport = CreateTransport(port);
                    var stressClient = new McpClient(stressClientTransport);
                    _disposables.Add(stressClientTransport);
                    _disposables.Add(stressClient);
                    
                    await stressClient.ConnectAsync();

                    var tasks = new List<Task>();
                    for (int i = 0; i < requestsPerClient; i++)
                    {
                        tasks.Add(CallWithRetry(stressClient, clientId, i));
                    }

                    await Task.WhenAll(tasks);
                    await stressClient.DisconnectAsync();
                }));
            }

            await Task.WhenAll(clientTasks);
            sw.Stop();

            // Calculate results
            var totalRequests = concurrentClients * requestsPerClient;
            var successRate = (processedCount - errorCount) / (double)totalRequests;
            var throughput = totalRequests / sw.Elapsed.TotalSeconds;

            _output.WriteLine($"Stress Test Results:");
            _output.WriteLine($"  Total requests: {totalRequests}");
            _output.WriteLine($"  Processed: {processedCount}");
            _output.WriteLine($"  Errors: {errorCount}");
            _output.WriteLine($"  Success rate: {successRate:P2}");
            _output.WriteLine($"  Throughput: {throughput:F2} req/sec");

            // Assert
            Assert.True(successRate > 0.90, $"Success rate {successRate:P2} is below 90%");
            Assert.True(throughput > 50, $"Throughput {throughput:F2} is below expected");

            // Cleanup
            await client.DisconnectAsync();
            await server.StopAsync();
        }

        [Fact]
        public async Task FilterChainProcessing_ComplexScenario()
        {
            // Arrange
            var port = GetAvailablePort();
            var filterManager = new FilterManager();
            _disposables.Add(filterManager);

            // Create complex filter setup
            await SetupComplexFilters(filterManager);

            var (server, client) = await CreateOptimizedClientServerPair(port);

            // Register tool with filter processing
            server.RegisterTool<ComplexRequest, ComplexResult>("complex.process",
                "Complex processing with filters",
                async (request) =>
                {
                    if (request == null)
                        return new ComplexResult { Success = false };

                    var results = new List<string>();
                    
                    // Process through different chains based on request type
                    foreach (var item in request.Items)
                    {
                        var chainName = item.Type switch
                        {
                            "A" => "ChainA",
                            "B" => "ChainB",
                            "C" => "ChainC",
                            _ => "DefaultChain"
                        };

                        var data = Encoding.UTF8.GetBytes(item.Data);
                        var context = new ProcessingContext
                        {
                            ChainName = chainName,
                            Direction = ProcessingDirection.Inbound
                        };
                        context.SetProperty("ItemId", item.Id);
                        context.SetProperty("Priority", item.Priority);

                        var result = await filterManager.ProcessAsync(data, context);
                        
                        if (result.IsSuccess)
                        {
                            results.Add($"{item.Id}:processed");
                        }
                        else
                        {
                            results.Add($"{item.Id}:failed:{result.ErrorMessage}");
                        }
                    }

                    return new ComplexResult
                    {
                        Success = true,
                        ProcessedItems = results.ToArray(),
                        Statistics = filterManager.GetStatistics()
                    };
                });

            // Act - Send complex request
            var complexRequest = new ComplexRequest
            {
                Items = new[]
                {
                    new RequestItem { Id = "1", Type = "A", Data = "Data A1", Priority = 1 },
                    new RequestItem { Id = "2", Type = "B", Data = "Data B1", Priority = 2 },
                    new RequestItem { Id = "3", Type = "C", Data = "Data C1", Priority = 3 },
                    new RequestItem { Id = "4", Type = "A", Data = "Data A2", Priority = 1 },
                    new RequestItem { Id = "5", Type = "B", Data = "Data B2", Priority = 2 }
                }
            };

            var result = await client.CallToolAsync<ComplexResult>("complex.process", complexRequest);

            // Assert
            Assert.NotNull(result);
            Assert.True(result.Success);
            Assert.Equal(5, result.ProcessedItems.Length);
            Assert.All(result.ProcessedItems, item => Assert.Contains("processed", item));
            Assert.NotNull(result.Statistics);
            Assert.True(result.Statistics.TotalMessagesProcessed >= 5);

            // Cleanup
            await client.DisconnectAsync();
            await server.StopAsync();
        }

        // Helper methods
        private async Task RegisterTestFilters(FilterManager manager)
        {
            // Validation filter
            var validationFilter = new TestFilter("ValidationFilter", data =>
            {
                var json = Encoding.UTF8.GetString(data);
                var doc = System.Text.Json.JsonDocument.Parse(json);
                
                using var stream = new System.IO.MemoryStream();
                using var writer = new System.Text.Json.Utf8JsonWriter(stream);
                
                writer.WriteStartObject();
                foreach (var prop in doc.RootElement.EnumerateObject())
                {
                    prop.WriteTo(writer);
                }
                writer.WriteBoolean("validated", true);
                writer.WriteEndObject();
                writer.Flush();
                
                return stream.ToArray();
            });
            await manager.RegisterFilterAsync("ValidationFilter", validationFilter);

            // Transform filter
            var transformFilter = new TestFilter("TransformFilter", data =>
            {
                var json = Encoding.UTF8.GetString(data);
                var doc = System.Text.Json.JsonDocument.Parse(json);
                
                using var stream = new System.IO.MemoryStream();
                using var writer = new System.Text.Json.Utf8JsonWriter(stream);
                
                writer.WriteStartObject();
                foreach (var prop in doc.RootElement.EnumerateObject())
                {
                    prop.WriteTo(writer);
                }
                writer.WriteBoolean("transformed", true);
                writer.WriteEndObject();
                writer.Flush();
                
                return stream.ToArray();
            });
            await manager.RegisterFilterAsync("TransformFilter", transformFilter);

            // Compression filter (simplified)
            var compressionFilter = new TestFilter("CompressionFilter", data =>
            {
                // In real scenario, would compress
                return data;
            });
            await manager.RegisterFilterAsync("CompressionFilter", compressionFilter);
        }

        private async Task SetupComplexFilters(FilterManager manager)
        {
            // Create multiple chains with different configurations
            var chains = new[] { "ChainA", "ChainB", "ChainC", "DefaultChain" };
            
            foreach (var chainName in chains)
            {
                var config = new ChainConfig
                {
                    Name = chainName,
                    Mode = chainName == "ChainB" ? ExecutionMode.Parallel : ExecutionMode.Sequential
                };
                
                var chain = await manager.CreateChainAsync(config);
                
                // Add chain-specific filters
                var filterName = $"Filter_{chainName}";
                var filter = new TestFilter(filterName, data => data);
                await manager.RegisterFilterAsync(filterName, filter);
                chain.AddFilter(filterName);
            }
            
            manager.SetDefaultChain("DefaultChain");
        }

        private async Task<(McpServer server, McpClient client)> CreateOptimizedClientServerPair(int port)
        {
            var serverConfig = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = port,
                SendBufferSize = 65536,
                ReceiveBufferSize = 65536,
                MaxMessageSize = 10 * 1024 * 1024
            };
            
            var serverTransport = new GopherTransport(serverConfig);
            var server = new McpServer(serverTransport);
            
            var clientTransport = new GopherTransport(serverConfig);
            var client = new McpClient(clientTransport, TimeSpan.FromSeconds(30));
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);
            _disposables.Add(server);
            _disposables.Add(client);

            await server.StartAsync();
            await client.ConnectAsync();

            return (server, client);
        }

        private async Task<StressResult> CallWithRetry(McpClient client, int clientId, int requestId)
        {
            for (int retry = 0; retry < 3; retry++)
            {
                try
                {
                    return await client.CallToolAsync<StressResult>(
                        "stress.test",
                        new StressRequest { ClientId = clientId, RequestId = requestId });
                }
                catch (JsonRpcException)
                {
                    if (retry == 2) // Last retry
                    {
                        return new StressResult { ProcessedAt = DateTime.UtcNow };
                    }
                    await Task.Delay(10);
                }
            }
            return new StressResult();
        }

        private ITransport CreateTransport(int port)
        {
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = port
            };
            return new GopherTransport(config);
        }

        private static int GetAvailablePort()
        {
            using var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            var port = ((IPEndPoint)listener.LocalEndpoint).Port;
            listener.Stop();
            return port;
        }

        public void Dispose()
        {
            foreach (var disposable in _disposables)
            {
                try
                {
                    disposable?.Dispose();
                }
                catch { }
            }
            _disposables.Clear();
        }

        // Test classes
        public class ProcessRequest
        {
            public string Data { get; set; } = string.Empty;
        }

        public class ProcessResult
        {
            public bool Success { get; set; }
            public string? ProcessedData { get; set; }
            public string? Error { get; set; }
        }

        public class EchoRequest
        {
            public string Data { get; set; } = string.Empty;
        }

        public class EchoResult
        {
            public string Data { get; set; } = string.Empty;
        }

        public class StressRequest
        {
            public int ClientId { get; set; }
            public int RequestId { get; set; }
        }

        public class StressResult
        {
            public DateTime ProcessedAt { get; set; }
            public int ProcessingTime { get; set; }
        }

        public class ComplexRequest
        {
            public RequestItem[] Items { get; set; } = Array.Empty<RequestItem>();
        }

        public class RequestItem
        {
            public string Id { get; set; } = string.Empty;
            public string Type { get; set; } = string.Empty;
            public string Data { get; set; } = string.Empty;
            public int Priority { get; set; }
        }

        public class ComplexResult
        {
            public bool Success { get; set; }
            public string[] ProcessedItems { get; set; } = Array.Empty<string>();
            public ManagerStatistics? Statistics { get; set; }
        }

        public class TestFilter : FilterBase
        {
            private readonly Func<byte[], byte[]> _processor;

            public TestFilter(string name, Func<byte[], byte[]> processor) 
                : base(new FilterConfig { Name = name })
            {
                _processor = processor;
            }

            public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                try
                {
                    var result = _processor(data);
                    return Task.FromResult(ProcessingResult.Success(result));
                }
                catch (Exception ex)
                {
                    return Task.FromResult(ProcessingResult.Failure(ex.Message));
                }
            }
        }
    }
}