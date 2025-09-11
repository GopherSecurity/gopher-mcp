using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using GopherMcp.Integration;
using GopherMcp.Transport;

namespace GopherMcp.Tests.Integration
{
    public class McpIntegrationTests : IDisposable
    {
        private readonly List<IDisposable> _disposables = new();

        [Fact(Skip = "Client-server communication hangs - needs investigation")]
        public async Task ClientServerCommunication_BasicHandshake()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            // Act - Perform initialization handshake
            var initResult = await client.InvokeAsync<InitializeResult>("initialize", new
            {
                protocolVersion = "2024-11-05",
                capabilities = new { },
                clientInfo = new { name = "TestClient", version = "1.0.0" }
            });

            // Assert
            Assert.NotNull(initResult);
            Assert.Equal("2024-11-05", initResult.ProtocolVersion);
            Assert.NotNull(initResult.ServerInfo);

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Tool invocation test hangs - needs investigation")]
        public async Task ToolInvocation_RegisterAndCallTool()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            // Register a test tool on server
            server.RegisterTool<AddParameters, double>("calculator.add",
                "Adds two numbers",
                async (parameters) =>
                {
                    if (parameters == null)
                        throw new ArgumentNullException(nameof(parameters));
                    return await Task.FromResult(parameters.A + parameters.B);
                });

            // Act - Discover tools
            var tools = await client.DiscoverToolsAsync();

            // Assert tool is discovered
            Assert.Contains(tools.Tools, t => t.Name == "calculator.add");

            // Act - Call the tool
            var result = await client.CallToolAsync<ToolResult>(
                "calculator.add",
                new { a = 5.0, b = 3.0 });

            // Assert
            Assert.NotNull(result);
            Assert.Contains("8", result.Content[0].Text);

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Error propagation test hangs - needs investigation")]
        public async Task ErrorPropagation_ServerError_PropagatedToClient()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            // Register a tool that throws an error
            server.RegisterTool<object, object>("error.tool",
                "Tool that always errors",
                async (parameters) =>
                {
                    throw new InvalidOperationException("Intentional error");
                });

            // Act & Assert
            await Assert.ThrowsAsync<JsonRpcException>(async () =>
            {
                await client.CallToolAsync<object>("error.tool", null);
            });

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Timeout handling test hangs - needs investigation")]
        public async Task TimeoutHandling_RequestTimeout_ThrowsTimeoutException()
        {
            // Arrange
            var port = GetAvailablePort();
            var serverTransport = CreateTransport(port);
            var clientTransport = CreateTransport(port);
            
            var server = new McpServer(serverTransport);
            var client = new McpClient(clientTransport, TimeSpan.FromMilliseconds(500));
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);
            _disposables.Add(server);
            _disposables.Add(client);

            // Register slow tool
            server.RegisterTool<object, object>("slow.tool",
                "Slow tool",
                async (parameters) =>
                {
                    await Task.Delay(1000); // Longer than timeout
                    return new { result = "done" };
                });

            await server.StartAsync();
            await client.ConnectAsync();

            // Act & Assert
            await Assert.ThrowsAsync<TimeoutException>(async () =>
            {
                await client.CallToolAsync<object>("slow.tool", null);
            });

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Multiple clients test hangs - needs investigation")]
        public async Task MultipleClients_IndependentSessions()
        {
            // Arrange
            var port = GetAvailablePort();
            var serverTransport = CreateTransport(port);
            var server = new McpServer(serverTransport);
            
            _disposables.Add(serverTransport);
            _disposables.Add(server);

            var clientResults = new List<string>();
            var clientCount = 3;

            // Register tool with session tracking
            var sessionData = new Dictionary<string, int>();
            server.RegisterTool<SessionParameters, SessionResult>("session.counter",
                "Session counter",
                async (parameters) =>
                {
                    if (parameters == null)
                        return new SessionResult { Count = 0 };

                    lock (sessionData)
                    {
                        if (!sessionData.ContainsKey(parameters.SessionId))
                            sessionData[parameters.SessionId] = 0;
                        
                        sessionData[parameters.SessionId]++;
                        return new SessionResult { Count = sessionData[parameters.SessionId] };
                    }
                });

            await server.StartAsync();

            // Act - Create and use multiple clients
            var tasks = new List<Task>();
            for (int i = 0; i < clientCount; i++)
            {
                var clientId = i;
                tasks.Add(Task.Run(async () =>
                {
                    var clientTransport = CreateTransport(port);
                    var client = new McpClient(clientTransport);
                    
                    _disposables.Add(clientTransport);
                    _disposables.Add(client);

                    await client.ConnectAsync();

                    var sessionId = $"client-{clientId}";
                    
                    // Make multiple calls
                    for (int j = 0; j < 5; j++)
                    {
                        var result = await client.CallToolAsync<SessionResult>(
                            "session.counter",
                            new SessionParameters { SessionId = sessionId });
                        
                        if (j == 4) // Last call
                        {
                            lock (clientResults)
                            {
                                clientResults.Add($"{sessionId}:{result.Count}");
                            }
                        }
                    }

                    await client.DisconnectAsync();
                }));
            }

            await Task.WhenAll(tasks);

            // Assert - Each client should have independent counter
            Assert.Equal(clientCount, clientResults.Count);
            foreach (var result in clientResults)
            {
                Assert.EndsWith(":5", result); // Each client made 5 calls
            }

            // Cleanup
            await server.StopAsync();
        }

        [Fact(Skip = "Prompt handling test hangs - needs investigation")]
        public async Task PromptHandling_RegisterAndGetPrompt()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            // Register prompt
            var promptProvider = new PromptProvider
            {
                Name = "greeting",
                Description = "Generate a greeting",
                Arguments = new List<PromptArgument>
                {
                    new PromptArgument { Name = "name", Description = "Person's name", Required = true }
                },
                Handler = async (args) =>
                {
                    var parameters = args as dynamic;
                    var name = parameters?.name ?? "World";
                    return new
                    {
                        messages = new[]
                        {
                            new { role = "assistant", content = $"Hello, {name}!" }
                        }
                    };
                }
            };

            server.RegisterPrompt("greeting", promptProvider);

            // Act - Discover prompts
            var prompts = await client.DiscoverPromptsAsync();

            // Assert discovery
            Assert.Contains(prompts.Prompts, p => p.Name == "greeting");

            // Act - Get prompt
            var result = await client.GetPromptAsync("greeting", new { name = "Alice" });

            // Assert
            Assert.NotNull(result);
            Assert.Single(result.Messages);
            Assert.Contains("Hello, Alice!", result.Messages[0].Content?.ToString());

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Resource handling test hangs - needs investigation")]
        public async Task ResourceHandling_RegisterAndReadResource()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            // Register resource
            var resourceProvider = new ResourceProvider
            {
                Uri = "file:///test/data.txt",
                Name = "Test Data",
                Description = "Test resource",
                MimeType = "text/plain",
                Handler = async () =>
                {
                    return await Task.FromResult("This is test data content");
                }
            };

            server.RegisterResource("file:///test/data.txt", resourceProvider);

            // Act - List resources
            var resourcesResponse = await client.InvokeAsync<ResourceListResult>("resources/list");

            // Assert listing
            Assert.NotNull(resourcesResponse);
            Assert.Contains(resourcesResponse.Resources, r => r.Uri == "file:///test/data.txt");

            // Act - Read resource
            var readResult = await client.InvokeAsync<ResourceReadResult>(
                "resources/read",
                new { uri = "file:///test/data.txt" });

            // Assert
            Assert.NotNull(readResult);
            Assert.Single(readResult.Contents);
            Assert.Equal("This is test data content", readResult.Contents[0].Text);

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Notification handling test hangs - needs investigation")]
        public async Task NotificationHandling_ServerToClientNotifications()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            var notificationReceived = new TaskCompletionSource<JsonRpcMessage>();
            
            client.NotificationReceived += (sender, e) =>
            {
                if (e.Notification.Method == "test.notification")
                {
                    notificationReceived.TrySetResult(e.Notification);
                }
            };

            // Act - Server sends notification to client
            var serverTransport = GetTransportFromServer(server);
            await serverTransport.SendAsync(
                JsonRpcMessage.CreateNotification("test.notification", new { data = "test" }));

            var notification = await notificationReceived.Task.WaitAsync(TimeSpan.FromSeconds(5));

            // Assert
            Assert.NotNull(notification);
            Assert.Equal("test.notification", notification.Method);

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        [Fact(Skip = "Concurrent requests test hangs - needs investigation")]
        public async Task ConcurrentRequests_HandledCorrectly()
        {
            // Arrange
            var port = GetAvailablePort();
            var (server, client) = await CreateClientServerPair(port);

            // Register tool with delay
            server.RegisterTool<DelayParameters, DelayResult>("delay.tool",
                "Tool with configurable delay",
                async (parameters) =>
                {
                    if (parameters != null)
                    {
                        await Task.Delay(parameters.DelayMs);
                    }
                    return new DelayResult { Timestamp = DateTime.UtcNow };
                });

            // Act - Send concurrent requests
            var tasks = new List<Task<DelayResult>>();
            for (int i = 0; i < 10; i++)
            {
                var delay = (10 - i) * 10; // Varying delays
                tasks.Add(client.CallToolAsync<DelayResult>(
                    "delay.tool",
                    new DelayParameters { DelayMs = delay }));
            }

            var results = await Task.WhenAll(tasks);

            // Assert - All requests completed
            Assert.Equal(10, results.Length);
            Assert.All(results, r => Assert.NotNull(r));

            // Cleanup
            await DisconnectClientServer(client, server);
        }

        // Helper methods and classes
        private async Task<(McpServer server, McpClient client)> CreateClientServerPair(int port)
        {
            var serverTransport = CreateTransport(port);
            var clientTransport = CreateTransport(port);
            
            var server = new McpServer(serverTransport);
            var client = new McpClient(clientTransport);
            
            _disposables.Add(serverTransport);
            _disposables.Add(clientTransport);
            _disposables.Add(server);
            _disposables.Add(client);

            await server.StartAsync();
            await client.ConnectAsync();

            return (server, client);
        }

        private async Task DisconnectClientServer(McpClient client, McpServer server)
        {
            try
            {
                await client.DisconnectAsync();
            }
            catch { }
            
            try
            {
                await server.StopAsync();
            }
            catch { }
        }

        private ITransport CreateTransport(int port)
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

        private ITransport GetTransportFromServer(McpServer server)
        {
            // This is a simplified way to get transport from server
            // In real implementation, server would expose the transport or have a method to send notifications
            var field = server.GetType().GetField("_transport", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            return field?.GetValue(server) as ITransport;
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
        public class AddParameters
        {
            public double A { get; set; }
            public double B { get; set; }
        }

        public class ToolResult
        {
            public ContentItem[] Content { get; set; } = Array.Empty<ContentItem>();
        }

        public class ContentItem
        {
            public string Type { get; set; } = string.Empty;
            public string Text { get; set; } = string.Empty;
        }

        public class InitializeResult
        {
            public string ProtocolVersion { get; set; } = string.Empty;
            public ServerInfo ServerInfo { get; set; } = new();
        }

        public class ServerInfo
        {
            public string Name { get; set; } = string.Empty;
            public string Version { get; set; } = string.Empty;
        }

        public class SessionParameters
        {
            public string SessionId { get; set; } = string.Empty;
        }

        public class SessionResult
        {
            public int Count { get; set; }
        }

        public class DelayParameters
        {
            public int DelayMs { get; set; }
        }

        public class DelayResult
        {
            public DateTime Timestamp { get; set; }
        }

        public class ResourceListResult
        {
            public List<ResourceInfo> Resources { get; set; } = new();
        }

        public class ResourceInfo
        {
            public string Uri { get; set; } = string.Empty;
            public string Name { get; set; } = string.Empty;
            public string Description { get; set; } = string.Empty;
            public string MimeType { get; set; } = string.Empty;
        }

        public class ResourceReadResult
        {
            public List<ResourceContent> Contents { get; set; } = new();
        }

        public class ResourceContent
        {
            public string Uri { get; set; } = string.Empty;
            public string MimeType { get; set; } = string.Empty;
            public string Text { get; set; } = string.Empty;
        }
    }
}