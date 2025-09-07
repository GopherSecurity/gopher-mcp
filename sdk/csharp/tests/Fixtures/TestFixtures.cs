using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Filters;
using GopherMcp.Chain;
using GopherMcp.Integration;
using GopherMcp.Manager;
using GopherMcp.Transport;
using GopherMcp.Types;

namespace GopherMcp.Tests.Fixtures
{
    /// <summary>
    /// Test fixtures providing mock implementations and test data
    /// </summary>
    public static class TestFixtures
    {
        // Mock filter implementations
        public class MockFilter : FilterBase
        {
            private readonly Func<byte[], ProcessingContext, CancellationToken, Task<ProcessingResult>> _processor;
            public int ProcessCount { get; private set; }
            public List<byte[]> ProcessedData { get; } = new();

            public MockFilter(string name = "MockFilter") : base(new FilterConfig { Name = name })
            {
                _processor = (data, context, ct) => Task.FromResult(ProcessingResult.Success(data));
            }

            public MockFilter(string name, Func<byte[], ProcessingContext, CancellationToken, Task<ProcessingResult>> processor)
                : base(new FilterConfig { Name = name })
            {
                _processor = processor;
            }

            public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                ProcessCount++;
                ProcessedData.Add(data);
                return await _processor(data, context, cancellationToken);
            }

            public void Reset()
            {
                ProcessCount = 0;
                ProcessedData.Clear();
            }
        }

        public class PassthroughFilter : FilterBase
        {
            public PassthroughFilter(string name = "PassthroughFilter") : base(new FilterConfig { Name = name })
            {
            }

            public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                return Task.FromResult(ProcessingResult.Success(data));
            }
        }

        public class TransformingFilter : FilterBase
        {
            private readonly Func<byte[], byte[]> _transformer;

            public TransformingFilter(string name, Func<byte[], byte[]> transformer)
                : base(new FilterConfig { Name = name })
            {
                _transformer = transformer;
            }

            public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                var transformed = _transformer(data);
                return Task.FromResult(ProcessingResult.Success(transformed));
            }
        }

        public class DelayFilter : FilterBase
        {
            private readonly TimeSpan _delay;

            public DelayFilter(string name, TimeSpan delay) : base(new FilterConfig { Name = name })
            {
                _delay = delay;
            }

            public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                await Task.Delay(_delay, cancellationToken);
                return ProcessingResult.Success(data);
            }
        }

        public class FailingFilter : FilterBase
        {
            private readonly string _errorMessage;
            private readonly int _failAfter;
            private int _processCount;

            public FailingFilter(string name, string errorMessage, int failAfter = 0)
                : base(new FilterConfig { Name = name })
            {
                _errorMessage = errorMessage;
                _failAfter = failAfter;
            }

            public override Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                _processCount++;
                if (_failAfter == 0 || _processCount > _failAfter)
                {
                    return Task.FromResult(ProcessingResult.Failure(_errorMessage));
                }
                return Task.FromResult(ProcessingResult.Success(data));
            }
        }

        public class ConditionalFilter : FilterBase
        {
            private readonly Func<ProcessingContext, bool> _condition;
            private readonly IFilter _trueFilter;
            private readonly IFilter _falseFilter;

            public ConditionalFilter(string name, Func<ProcessingContext, bool> condition, IFilter trueFilter, IFilter falseFilter)
                : base(new FilterConfig { Name = name })
            {
                _condition = condition;
                _trueFilter = trueFilter;
                _falseFilter = falseFilter;
            }

            public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
            {
                var filter = _condition(context) ? _trueFilter : _falseFilter;
                return await filter.ProcessAsync(data, context, cancellationToken);
            }
        }

        // Test configurations
        public static class Configurations
        {
            public static FilterManagerConfig DefaultFilterManagerConfig()
            {
                return new FilterManagerConfig
                {
                    MaxConcurrency = 4,
                    DefaultTimeout = TimeSpan.FromSeconds(30),
                    EnableMetrics = true,
                    LogLevel = LogLevel.Info
                };
            }

            public static ChainConfig DefaultChainConfig(string name = "TestChain")
            {
                return new ChainConfig
                {
                    Name = name,
                    Mode = ExecutionMode.Sequential,
                    ContinueOnError = false,
                    MaxRetries = 3,
                    RetryDelay = TimeSpan.FromMilliseconds(100)
                };
            }

            public static TransportConfig TcpTransportConfig(int port)
            {
                return new TransportConfig
                {
                    Protocol = TransportProtocol.Tcp,
                    Host = "localhost",
                    Port = port,
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    SendTimeout = TimeSpan.FromSeconds(5),
                    ReceiveTimeout = TimeSpan.FromSeconds(5),
                    MaxMessageSize = 4 * 1024 * 1024,
                    SendBufferSize = 8192,
                    ReceiveBufferSize = 8192
                };
            }

            public static TransportConfig StdioTransportConfig()
            {
                return new TransportConfig
                {
                    Protocol = TransportProtocol.Stdio,
                    MaxMessageSize = 4 * 1024 * 1024
                };
            }
        }

        // Sample messages
        public static class SampleMessages
        {
            public static JsonRpcMessage SimpleRequest(string method = "test.method", object? id = null)
            {
                return JsonRpcMessage.CreateRequest(
                    method,
                    new { data = "test data", timestamp = DateTime.UtcNow },
                    id ?? Guid.NewGuid().ToString());
            }

            public static JsonRpcMessage SimpleNotification(string method = "test.notification")
            {
                return JsonRpcMessage.CreateNotification(
                    method,
                    new { message = "notification message" });
            }

            public static JsonRpcMessage SuccessResponse(object? id, object? result = null)
            {
                return JsonRpcMessage.CreateResponse(
                    id,
                    result ?? new { success = true, data = "response data" });
            }

            public static JsonRpcMessage ErrorResponse(object? id, int code = -32000, string message = "Error occurred")
            {
                return JsonRpcMessage.CreateErrorResponse(id, code, message);
            }

            public static byte[] BinaryMessage(int size = 1024)
            {
                var data = new byte[size];
                Random.Shared.NextBytes(data);
                return data;
            }

            public static string JsonMessage(object data)
            {
                return JsonSerializer.Serialize(data);
            }

            public static List<JsonRpcMessage> BatchRequest(int count)
            {
                var messages = new List<JsonRpcMessage>();
                for (int i = 0; i < count; i++)
                {
                    messages.Add(SimpleRequest($"test.method.{i}", $"msg-{i}"));
                }
                return messages;
            }
        }

        // Helper methods
        public static class Helpers
        {
            public static byte[] StringToBytes(string str)
            {
                return Encoding.UTF8.GetBytes(str);
            }

            public static string BytesToString(byte[] bytes)
            {
                return Encoding.UTF8.GetString(bytes);
            }

            public static ProcessingContext CreateContext(
                ProcessingDirection direction = ProcessingDirection.Inbound,
                string? chainName = null,
                Dictionary<string, object>? properties = null)
            {
                var context = new ProcessingContext
                {
                    Direction = direction,
                    ChainName = chainName,
                    CorrelationId = Guid.NewGuid().ToString()
                };

                if (properties != null)
                {
                    foreach (var kvp in properties)
                    {
                        context.SetProperty(kvp.Key, kvp.Value);
                    }
                }

                return context;
            }

            public static async Task<FilterChain> CreateFilterChain(
                FilterManager manager,
                string name,
                params string[] filterNames)
            {
                var config = Configurations.DefaultChainConfig(name);
                var chain = await manager.CreateChainAsync(config);

                foreach (var filterName in filterNames)
                {
                    chain.AddFilter(filterName);
                }

                return chain;
            }

            public static async Task RegisterMockFilters(
                FilterManager manager,
                params string[] filterNames)
            {
                foreach (var name in filterNames)
                {
                    var filter = new MockFilter(name);
                    await manager.RegisterFilterAsync(name, filter);
                }
            }

            public static T DeserializeJson<T>(byte[] data)
            {
                var json = BytesToString(data);
                return JsonSerializer.Deserialize<T>(json);
            }

            public static byte[] SerializeJson<T>(T obj)
            {
                var json = JsonSerializer.Serialize(obj);
                return StringToBytes(json);
            }
        }

        // Test data generators
        public static class Generators
        {
            private static readonly Random _random = new();

            public static byte[] RandomBytes(int minSize = 10, int maxSize = 1000)
            {
                var size = _random.Next(minSize, maxSize);
                var data = new byte[size];
                _random.NextBytes(data);
                return data;
            }

            public static string RandomString(int length = 10)
            {
                const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
                return new string(Enumerable.Repeat(chars, length)
                    .Select(s => s[_random.Next(s.Length)]).ToArray());
            }

            public static T RandomEnum<T>() where T : Enum
            {
                var values = Enum.GetValues(typeof(T));
                return (T)values.GetValue(_random.Next(values.Length));
            }

            public static Dictionary<string, object> RandomProperties(int count = 5)
            {
                var properties = new Dictionary<string, object>();
                for (int i = 0; i < count; i++)
                {
                    var key = $"prop_{i}";
                    var valueType = _random.Next(4);
                    object value = valueType switch
                    {
                        0 => _random.Next(100),
                        1 => RandomString(10),
                        2 => _random.NextDouble(),
                        _ => _random.Next(2) == 0
                    };
                    properties[key] = value;
                }
                return properties;
            }

            public static List<byte[]> GenerateMessageBatch(int count, int minSize = 100, int maxSize = 1000)
            {
                var messages = new List<byte[]>();
                for (int i = 0; i < count; i++)
                {
                    messages.Add(RandomBytes(minSize, maxSize));
                }
                return messages;
            }

            public static ComplexTestData GenerateComplexData()
            {
                return new ComplexTestData
                {
                    Id = Guid.NewGuid().ToString(),
                    Name = RandomString(20),
                    Value = _random.Next(1000),
                    Timestamp = DateTime.UtcNow,
                    Tags = Enumerable.Range(0, _random.Next(1, 10))
                        .Select(_ => RandomString(5))
                        .ToList(),
                    Nested = new NestedData
                    {
                        Field1 = RandomString(10),
                        Field2 = _random.Next(100),
                        Field3 = _random.NextDouble()
                    }
                };
            }
        }

        // Test data classes
        public class ComplexTestData
        {
            public string Id { get; set; } = string.Empty;
            public string Name { get; set; } = string.Empty;
            public int Value { get; set; }
            public DateTime Timestamp { get; set; }
            public List<string> Tags { get; set; } = new();
            public NestedData Nested { get; set; } = new();
        }

        public class NestedData
        {
            public string Field1 { get; set; } = string.Empty;
            public int Field2 { get; set; }
            public double Field3 { get; set; }
        }

        // Mock transport for testing
        public class MockTransport : ITransport
        {
            private readonly Queue<JsonRpcMessage> _receiveQueue = new();
            private readonly List<JsonRpcMessage> _sentMessages = new();
            private bool _isConnected;

            public bool IsConnected => _isConnected;
            public IReadOnlyList<JsonRpcMessage> SentMessages => _sentMessages;

            public event EventHandler<MessageReceivedEventArgs>? MessageReceived;
            public event EventHandler<TransportErrorEventArgs>? Error;
            public event EventHandler<ConnectionStateEventArgs>? Connected;
            public event EventHandler<ConnectionStateEventArgs>? Disconnected;

            public Task StartAsync(CancellationToken cancellationToken = default)
            {
                _isConnected = true;
                Connected?.Invoke(this, new ConnectionStateEventArgs(ConnectionState.Connected, ConnectionState.Disconnected));
                return Task.CompletedTask;
            }

            public Task StopAsync(CancellationToken cancellationToken = default)
            {
                _isConnected = false;
                Disconnected?.Invoke(this, new ConnectionStateEventArgs(ConnectionState.Disconnected, ConnectionState.Connected));
                return Task.CompletedTask;
            }

            public Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
            {
                _sentMessages.Add(message);
                return Task.CompletedTask;
            }

            public Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken = default)
            {
                if (_receiveQueue.Count > 0)
                {
                    var message = _receiveQueue.Dequeue();
                    MessageReceived?.Invoke(this, new MessageReceivedEventArgs(message));
                    return Task.FromResult(message);
                }

                return Task.FromCanceled<JsonRpcMessage>(cancellationToken);
            }

            public void EnqueueReceiveMessage(JsonRpcMessage message)
            {
                _receiveQueue.Enqueue(message);
            }

            public void SimulateError(Exception exception)
            {
                Error?.Invoke(this, new TransportErrorEventArgs(exception, "Simulated error"));
            }

            public void Dispose()
            {
                _isConnected = false;
                _receiveQueue.Clear();
                _sentMessages.Clear();
            }
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