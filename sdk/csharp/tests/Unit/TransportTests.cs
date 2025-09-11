using System;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Transport;
using GopherMcp.Integration;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    /// <summary>
    /// Transport layer tests
    /// </summary>
    public class TransportTests
    {
        [Fact]
        public void TransportConfig_DefaultValues_AreCorrect()
        {
            // Arrange & Act
            var config = new TransportConfig();

            // Assert
            Assert.Equal(TransportProtocol.Tcp, config.Protocol);
            Assert.Equal("localhost", config.Host);
            Assert.Equal(9000, config.Port);
            Assert.Equal(TimeSpan.FromSeconds(30), config.ConnectTimeout);
            Assert.Equal(4 * 1024 * 1024, config.MaxMessageSize);
        }

        [Fact]
        public void TransportConfig_SetProperties_UpdatesCorrectly()
        {
            // Arrange
            var config = new TransportConfig();

            // Act
            config.Protocol = TransportProtocol.Stdio;
            config.Host = "192.168.1.1";
            config.Port = 8080;
            config.ConnectTimeout = TimeSpan.FromMinutes(1);
            config.MaxMessageSize = 10 * 1024 * 1024;

            // Assert
            Assert.Equal(TransportProtocol.Stdio, config.Protocol);
            Assert.Equal("192.168.1.1", config.Host);
            Assert.Equal(8080, config.Port);
            Assert.Equal(TimeSpan.FromMinutes(1), config.ConnectTimeout);
            Assert.Equal(10 * 1024 * 1024, config.MaxMessageSize);
        }

        [Fact]
        public async Task MockTransport_StartAsync_ChangesStateToConnected()
        {
            // Arrange
            var transport = new MockTransport();
            var stateChanged = false;
            transport.Connected += (s, e) => stateChanged = true;

            // Act
            await transport.StartAsync();

            // Assert
            Assert.True(transport.IsConnected);
            Assert.Equal(ConnectionState.Connected, transport.State);
            Assert.True(stateChanged);
        }

        [Fact]
        public async Task MockTransport_StopAsync_ChangesStateToDisconnected()
        {
            // Arrange
            var transport = new MockTransport();
            await transport.StartAsync();
            var stateChanged = false;
            transport.Disconnected += (s, e) => stateChanged = true;

            // Act
            await transport.StopAsync();

            // Assert
            Assert.False(transport.IsConnected);
            Assert.Equal(ConnectionState.Disconnected, transport.State);
            Assert.True(stateChanged);
        }

        [Fact]
        public async Task MockTransport_SendAsync_AddsToSentMessages()
        {
            // Arrange
            var transport = new MockTransport();
            var message = JsonRpcMessage.CreateRequest("test.method", new { data = "test" }, "1");

            // Act
            await transport.SendAsync(message);

            // Assert
            Assert.Single(transport.SentMessages);
            Assert.Equal("test.method", transport.SentMessages[0].Method);
        }

        [Fact]
        public async Task MockTransport_ReceiveAsync_ReturnsEnqueuedMessage()
        {
            // Arrange
            var transport = new MockTransport();
            var expectedMessage = JsonRpcMessage.CreateRequest("test.method", new { data = "test" }, "1");
            transport.EnqueueReceiveMessage(expectedMessage);

            // Act
            var receivedMessage = await transport.ReceiveAsync();

            // Assert
            Assert.NotNull(receivedMessage);
            Assert.Equal(expectedMessage.Method, receivedMessage.Method);
            Assert.Equal(expectedMessage.Id, receivedMessage.Id);
        }

        [Fact]
        public void MockTransport_SimulateError_RaisesErrorEvent()
        {
            // Arrange
            var transport = new MockTransport();
            Exception capturedError = null;
            transport.Error += (s, e) => capturedError = e.Exception;

            // Act
            var testException = new InvalidOperationException("Test error");
            transport.SimulateError(testException);

            // Assert
            Assert.NotNull(capturedError);
            Assert.Equal(testException, capturedError);
        }

        [Fact]
        public void MockTransport_Dispose_ClearsResources()
        {
            // Arrange
            var transport = new MockTransport();
            var message = JsonRpcMessage.CreateRequest("test", null, "1");
            transport.EnqueueReceiveMessage(message);

            // Act
            transport.Dispose();

            // Assert
            Assert.False(transport.IsConnected);
            // After disposal, internal queues should be cleared
        }

        [Theory]
        [InlineData(TransportProtocol.Tcp)]
        [InlineData(TransportProtocol.Stdio)]
        [InlineData(TransportProtocol.WebSocket)]
        public void TransportConfig_DifferentProtocols_ConfiguresCorrectly(TransportProtocol protocol)
        {
            // Arrange & Act
            var config = new TransportConfig
            {
                Protocol = protocol
            };

            // Assert
            Assert.Equal(protocol, config.Protocol);
        }

        [Fact]
        public async Task Transport_MessageSequence_HandlesCorrectly()
        {
            // Arrange
            var transport = new MockTransport();
            var messages = new[]
            {
                JsonRpcMessage.CreateRequest("method1", null, "1"),
                JsonRpcMessage.CreateRequest("method2", null, "2"),
                JsonRpcMessage.CreateNotification("notify1", null),
                JsonRpcMessage.CreateResponse("3", new { result = "test" })
            };

            // Act
            foreach (var msg in messages)
            {
                await transport.SendAsync(msg);
            }

            // Assert
            Assert.Equal(4, transport.SentMessages.Count);
            Assert.Equal("method1", transport.SentMessages[0].Method);
            Assert.Equal("method2", transport.SentMessages[1].Method);
            Assert.Equal("notify1", transport.SentMessages[2].Method);
            Assert.NotNull(transport.SentMessages[3].Result);
        }
    }

    /// <summary>
    /// Mock transport implementation for testing
    /// </summary>
    internal class MockTransport : ITransport
    {
        private readonly System.Collections.Generic.Queue<JsonRpcMessage> _receiveQueue = new();
        private readonly System.Collections.Generic.List<JsonRpcMessage> _sentMessages = new();
        private bool _isConnected;
        private ConnectionState _state = ConnectionState.Disconnected;

        public bool IsConnected => _isConnected;
        public ConnectionState State => _state;
        public System.Collections.Generic.IReadOnlyList<JsonRpcMessage> SentMessages => _sentMessages;

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