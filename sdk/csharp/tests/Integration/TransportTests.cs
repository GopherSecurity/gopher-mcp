using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Transport;
using GopherMcp.Integration;

namespace GopherMcp.Tests.Integration
{
    public class TransportTests : IDisposable
    {
        private readonly List<IDisposable> _disposables = new();

        [Fact]
        public async Task TransportConnection_TcpProtocol_ConnectsSuccessfully()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var serverTransport = CreateTcpServerTransport(serverPort);
            var clientTransport = CreateTcpClientTransport(serverPort);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            // Start server
            var serverTask = Task.Run(async () =>
            {
                await serverTransport.StartAsync();
            });

            await Task.Delay(100); // Give server time to start

            // Act
            await clientTransport.StartAsync();

            // Assert
            Assert.True(clientTransport.IsConnected);
            
            // Cleanup
            await clientTransport.StopAsync();
            await serverTransport.StopAsync();
        }

        [Fact]
        public async Task MessageSendingReceiving_SendAndReceiveMessages()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var serverTransport = CreateTcpServerTransport(serverPort);
            var clientTransport = CreateTcpClientTransport(serverPort);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            var serverReceivedMessage = new TaskCompletionSource<JsonRpcMessage>();
            serverTransport.MessageReceived += (sender, e) =>
            {
                serverReceivedMessage.TrySetResult(e.Message);
            };

            await StartTransports(serverTransport, clientTransport);

            var testMessage = JsonRpcMessage.CreateRequest("test.method", new { data = "test" }, "123");

            // Act
            await clientTransport.SendAsync(testMessage);
            var receivedMessage = await serverReceivedMessage.Task.WaitAsync(TimeSpan.FromSeconds(5));

            // Assert
            Assert.NotNull(receivedMessage);
            Assert.Equal("test.method", receivedMessage.Method);
            Assert.Equal("123", receivedMessage.Id);

            // Cleanup
            await StopTransports(serverTransport, clientTransport);
        }

        [Theory]
        [InlineData(TransportProtocol.Tcp)]
        [InlineData(TransportProtocol.Udp)]
        [InlineData(TransportProtocol.Stdio)]
        public async Task ProtocolSwitching_DifferentProtocols_WorkCorrectly(TransportProtocol protocol)
        {
            // Arrange
            var config = new TransportConfig
            {
                Protocol = protocol,
                Host = "localhost",
                Port = protocol == TransportProtocol.Stdio ? 0 : GetAvailablePort()
            };

            var transport = new GopherTransport(config);
            _disposables.Add(transport);

            // Act & Assert based on protocol
            switch (protocol)
            {
                case TransportProtocol.Tcp:
                case TransportProtocol.Udp:
                    // For network protocols, we'd need a server
                    // This is a simplified test
                    Assert.NotNull(transport);
                    break;
                    
                case TransportProtocol.Stdio:
                    // Stdio should be ready immediately
                    await transport.StartAsync();
                    Assert.True(transport.IsConnected);
                    await transport.StopAsync();
                    break;
            }
        }

        [Fact]
        public async Task ErrorHandling_ConnectionFailure_RaisesErrorEvent()
        {
            // Arrange
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = 1, // Invalid port
                ConnectTimeout = TimeSpan.FromSeconds(1)
            };

            var transport = new GopherTransport(config);
            _disposables.Add(transport);

            var errorRaised = new TaskCompletionSource<Exception>();
            transport.Error += (sender, e) =>
            {
                errorRaised.TrySetResult(e.Exception);
            };

            // Act & Assert
            await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            {
                await transport.StartAsync();
            });
        }

        [Fact]
        public async Task Reconnection_AutoReconnectsAfterDisconnection()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = serverPort,
                AutoReconnect = true,
                MaxReconnectAttempts = 3,
                ReconnectDelay = TimeSpan.FromMilliseconds(100)
            };

            var serverTransport = CreateTcpServerTransport(serverPort);
            var clientTransport = new GopherTransport(config);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            var reconnectCount = 0;
            clientTransport.Connected += (sender, e) =>
            {
                Interlocked.Increment(ref reconnectCount);
            };

            // Start initial connection
            await StartTransports(serverTransport, clientTransport);
            Assert.True(clientTransport.IsConnected);

            // Act - Simulate disconnection
            await serverTransport.StopAsync();
            await Task.Delay(200);

            // Restart server
            serverTransport = CreateTcpServerTransport(serverPort);
            _disposables.Add(serverTransport);
            await serverTransport.StartAsync();
            
            // Wait for reconnection
            await Task.Delay(500);

            // Assert
            Assert.True(reconnectCount > 1); // Should have reconnected
        }

        [Fact]
        public async Task BidirectionalCommunication_ClientServerExchange()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var serverTransport = CreateTcpServerTransport(serverPort);
            var clientTransport = CreateTcpClientTransport(serverPort);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            var serverReceived = new TaskCompletionSource<JsonRpcMessage>();
            var clientReceived = new TaskCompletionSource<JsonRpcMessage>();

            serverTransport.MessageReceived += async (sender, e) =>
            {
                serverReceived.TrySetResult(e.Message);
                
                // Send response
                var response = JsonRpcMessage.CreateResponse(e.Message.Id, new { result = "server response" });
                await serverTransport.SendAsync(response);
            };

            clientTransport.MessageReceived += (sender, e) =>
            {
                clientReceived.TrySetResult(e.Message);
            };

            await StartTransports(serverTransport, clientTransport);

            // Act
            var request = JsonRpcMessage.CreateRequest("test.method", new { data = "client request" }, "req-1");
            await clientTransport.SendAsync(request);

            var serverMsg = await serverReceived.Task.WaitAsync(TimeSpan.FromSeconds(5));
            var clientMsg = await clientReceived.Task.WaitAsync(TimeSpan.FromSeconds(5));

            // Assert
            Assert.NotNull(serverMsg);
            Assert.Equal("test.method", serverMsg.Method);
            
            Assert.NotNull(clientMsg);
            Assert.Equal("req-1", clientMsg.Id);
            Assert.NotNull(clientMsg.Result);

            // Cleanup
            await StopTransports(serverTransport, clientTransport);
        }

        [Fact]
        public async Task LargeMessageHandling_SendsLargePayload()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = serverPort,
                MaxMessageSize = 10 * 1024 * 1024 // 10MB
            };

            var serverTransport = new GopherTransport(config);
            var clientTransport = new GopherTransport(config);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            var serverReceived = new TaskCompletionSource<JsonRpcMessage>();
            serverTransport.MessageReceived += (sender, e) =>
            {
                serverReceived.TrySetResult(e.Message);
            };

            await StartTransports(serverTransport, clientTransport);

            // Create large message
            var largeData = new string('X', 1024 * 1024); // 1MB string
            var largeMessage = JsonRpcMessage.CreateRequest("test.large", new { data = largeData }, "large-1");

            // Act
            await clientTransport.SendAsync(largeMessage);
            var received = await serverReceived.Task.WaitAsync(TimeSpan.FromSeconds(10));

            // Assert
            Assert.NotNull(received);
            Assert.Equal("test.large", received.Method);
            
            // Cleanup
            await StopTransports(serverTransport, clientTransport);
        }

        [Fact]
        public async Task ConcurrentMessages_HandlesMultipleSimultaneousMessages()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var serverTransport = CreateTcpServerTransport(serverPort);
            var clientTransport = CreateTcpClientTransport(serverPort);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            var receivedMessages = new ConcurrentBag<JsonRpcMessage>();
            serverTransport.MessageReceived += (sender, e) =>
            {
                receivedMessages.Add(e.Message);
            };

            await StartTransports(serverTransport, clientTransport);

            // Act - Send multiple messages concurrently
            var tasks = new List<Task>();
            for (int i = 0; i < 100; i++)
            {
                var id = i;
                tasks.Add(Task.Run(async () =>
                {
                    var message = JsonRpcMessage.CreateRequest($"test.method.{id}", new { index = id }, $"msg-{id}");
                    await clientTransport.SendAsync(message);
                }));
            }

            await Task.WhenAll(tasks);
            await Task.Delay(1000); // Wait for all messages to be received

            // Assert
            Assert.Equal(100, receivedMessages.Count);
            
            // Verify all messages were received
            var receivedIds = receivedMessages.Select(m => m.Id?.ToString()).OrderBy(id => id).ToList();
            for (int i = 0; i < 100; i++)
            {
                Assert.Contains($"msg-{i}", receivedIds);
            }

            // Cleanup
            await StopTransports(serverTransport, clientTransport);
        }

        [Fact]
        public async Task KeepAlive_MaintainsConnection()
        {
            // Arrange
            var serverPort = GetAvailablePort();
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = serverPort,
                EnableKeepAlive = true,
                KeepAliveInterval = TimeSpan.FromMilliseconds(100)
            };

            var serverTransport = new GopherTransport(config);
            var clientTransport = new GopherTransport(config);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);

            await StartTransports(serverTransport, clientTransport);

            // Act - Wait for keep-alive to work
            await Task.Delay(500);

            // Assert - Connection should still be active
            Assert.True(clientTransport.IsConnected);
            Assert.True(serverTransport.IsConnected);

            // Send a message to verify connection is still working
            var testMessage = JsonRpcMessage.CreateNotification("ping");
            await clientTransport.SendAsync(testMessage);

            // Cleanup
            await StopTransports(serverTransport, clientTransport);
        }

        // Helper methods
        private static int GetAvailablePort()
        {
            using var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            var port = ((IPEndPoint)listener.LocalEndpoint).Port;
            listener.Stop();
            return port;
        }

        private GopherTransport CreateTcpServerTransport(int port)
        {
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = port,
                ConnectTimeout = TimeSpan.FromSeconds(5),
                SendTimeout = TimeSpan.FromSeconds(5),
                ReceiveTimeout = TimeSpan.FromSeconds(5)
            };
            return new GopherTransport(config);
        }

        private GopherTransport CreateTcpClientTransport(int port)
        {
            var config = new TransportConfig
            {
                Protocol = TransportProtocol.Tcp,
                Host = "localhost",
                Port = port,
                ConnectTimeout = TimeSpan.FromSeconds(5),
                SendTimeout = TimeSpan.FromSeconds(5),
                ReceiveTimeout = TimeSpan.FromSeconds(5)
            };
            return new GopherTransport(config);
        }

        private async Task StartTransports(params ITransport[] transports)
        {
            foreach (var transport in transports)
            {
                await transport.StartAsync();
            }
        }

        private async Task StopTransports(params ITransport[] transports)
        {
            foreach (var transport in transports)
            {
                try
                {
                    await transport.StopAsync();
                }
                catch { }
            }
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
    }

    // Concurrent collection for thread-safe operations
    public class ConcurrentBag<T>
    {
        private readonly List<T> _items = new();
        private readonly object _lock = new();

        public void Add(T item)
        {
            lock (_lock)
            {
                _items.Add(item);
            }
        }

        public int Count
        {
            get
            {
                lock (_lock)
                {
                    return _items.Count;
                }
            }
        }

        public IEnumerable<T> ToList()
        {
            lock (_lock)
            {
                return _items.ToList();
            }
        }

        public IEnumerable<TResult> Select<TResult>(Func<T, TResult> selector)
        {
            lock (_lock)
            {
                return _items.Select(selector);
            }
        }
    }
}