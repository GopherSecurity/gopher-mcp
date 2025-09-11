using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Filters;
// using GopherMcp.Chain; - FilterChain is in GopherMcp.Filters
using GopherMcp.Integration;
using GopherMcp.Manager;
using GopherMcp.Transport;
using GopherMcp.Types;

namespace GopherMcp.Tests.Fixtures
{
    /// <summary>
    /// Test filter configuration for testing
    /// </summary>
    public class TestFilterConfig : FilterConfigBase
    {
        public TestFilterConfig(string name, string type) : base(name, type)
        {
        }

        public override object Clone()
        {
            return new TestFilterConfig(Name, Type)
            {
                Enabled = this.Enabled,
                Priority = this.Priority,
                TimeoutMs = this.TimeoutMs,
                MaxBufferSize = this.MaxBufferSize,
                Description = this.Description,
                Metadata = this.Metadata != null ? new Dictionary<string, string>(this.Metadata) : null,
                Tags = this.Tags != null ? new List<string>(this.Tags) : null
            };
        }
    }

    /// <summary>
    /// Test fixtures providing mock implementations and test data
    /// </summary>
    public static class TestFixtures
    {
        // Mock filter implementations
        public class MockFilter : Filter
        {
            private readonly Func<byte[], ProcessingContext, CancellationToken, Task<FilterResult>> _processor;
            public int ProcessCount { get; private set; }
            public List<byte[]> ProcessedData { get; } = new();

            public MockFilter(string name = "MockFilter") : base(new TestFilterConfig(name, name))
            {
                _processor = (data, context, ct) => Task.FromResult(FilterResult.Success(data, 0, data.Length));
            }

            public MockFilter(string name, Func<byte[], ProcessingContext, CancellationToken, Task<FilterResult>> processor)
                : base(new TestFilterConfig(name, name))
            {
                _processor = processor;
            }

            protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
            {
                ProcessCount++;
                ProcessedData.Add(buffer);
                return await _processor(buffer, context, cancellationToken);
            }

            public void Reset()
            {
                ProcessCount = 0;
                ProcessedData.Clear();
            }
        }

        public class PassthroughFilter : Filter
        {
            public PassthroughFilter(string name = "PassthroughFilter") : base(new TestFilterConfig(name, name))
            {
            }

            protected override Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
            {
                return Task.FromResult(FilterResult.Success(buffer, 0, buffer.Length));
            }
        }

        public class TransformingFilter : Filter
        {
            private readonly Func<byte[], byte[]> _transformer;

            public TransformingFilter(string name, Func<byte[], byte[]> transformer)
                : base(new TestFilterConfig(name, name))
            {
                _transformer = transformer;
            }

            protected override Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
            {
                var transformed = _transformer(buffer);
                return Task.FromResult(FilterResult.Success(transformed, 0, transformed.Length));
            }
        }

        public class DelayFilter : Filter
        {
            private readonly TimeSpan _delay;

            public DelayFilter(string name, TimeSpan delay) : base(new TestFilterConfig(name, name))
            {
                _delay = delay;
            }

            protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
            {
                await Task.Delay(_delay, cancellationToken);
                return FilterResult.Success(buffer, 0, buffer.Length);
            }
        }

        public class FailingFilter : Filter
        {
            private readonly string _errorMessage;
            private readonly int _failAfter;
            private int _processCount;

            public FailingFilter(string name, string errorMessage, int failAfter = 0)
                : base(new TestFilterConfig(name, name))
            {
                _errorMessage = errorMessage;
                _failAfter = failAfter;
            }

            protected override Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
            {
                _processCount++;
                if (_failAfter == 0 || _processCount > _failAfter)
                {
                    return Task.FromResult(FilterResult.Error(_errorMessage));
                }
                return Task.FromResult(FilterResult.Success(buffer, 0, buffer.Length));
            }
        }

        public class ConditionalFilter : Filter
        {
            private readonly Func<ProcessingContext, bool> _condition;
            private readonly Filter _trueFilter;
            private readonly Filter _falseFilter;

            public ConditionalFilter(string name, Func<ProcessingContext, bool> condition, Filter trueFilter, Filter falseFilter)
                : base(new TestFilterConfig(name, name))
            {
                _condition = condition;
                _trueFilter = trueFilter;
                _falseFilter = falseFilter;
            }

            protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
            {
                var filter = _condition(context) ? _trueFilter : _falseFilter;
                return await filter.ProcessAsync(buffer, context, cancellationToken);
            }
        }

        // Test configurations
        public static class Configurations
        {
            public static GopherMcp.Manager.FilterManagerConfig DefaultFilterManagerConfig()
            {
                return new GopherMcp.Manager.FilterManagerConfig
                {
                    MaxConcurrency = 4,
                    DefaultTimeout = TimeSpan.FromSeconds(30),
                    EnableStatistics = true
                    // LogLevel removed - not part of actual FilterManagerConfig
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
                    // ChainName property doesn't exist in ProcessingContext
                    CorrelationId = Guid.NewGuid().ToString()
                };
                context.SetProperty("ChainName", chainName);

                if (properties != null)
                {
                    foreach (var kvp in properties)
                    {
                        context.SetProperty(kvp.Key, kvp.Value);
                    }
                }

                return context;
            }

            public static FilterChain CreateFilterChain(
                FilterManager manager,
                string name,
                params string[] filterNames)
            {
                var config = Configurations.DefaultChainConfig(name);
                var chain = manager.CreateChain(name, config);

                foreach (var filterName in filterNames)
                {
                    var filter = new MockFilter(filterName);
                    chain.AddFilter(filter);
                }

                return chain;
            }

            public static void RegisterMockFilters(
                FilterManager manager,
                params string[] filterNames)
            {
                foreach (var name in filterNames)
                {
                    var filter = new MockFilter(name);
                    manager.RegisterFilter(filter);
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
            private ConnectionState _state = ConnectionState.Disconnected;

            public bool IsConnected => _isConnected;
            public ConnectionState State => _state;
            public IReadOnlyList<JsonRpcMessage> SentMessages => _sentMessages;

            public event EventHandler<MessageReceivedEventArgs>? MessageReceived;
            public event EventHandler<TransportErrorEventArgs>? Error;
            public event EventHandler<ConnectionStateEventArgs>? Connected;
            public event EventHandler<ConnectionStateEventArgs>? Disconnected;

            public Task StartAsync(CancellationToken cancellationToken = default)
            {
                _isConnected = true;
                _state = ConnectionState.Connected;
                Connected?.Invoke(this, new ConnectionStateEventArgs(ConnectionState.Connected, ConnectionState.Disconnected));
                return Task.CompletedTask;
            }

            public Task StopAsync(CancellationToken cancellationToken = default)
            {
                _isConnected = false;
                _state = ConnectionState.Disconnected;
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