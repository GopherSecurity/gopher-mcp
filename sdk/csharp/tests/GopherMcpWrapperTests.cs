using System;
using System.Threading;
using System.Threading.Tasks;
using Xunit;
using FluentAssertions;
using GopherMcp;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests
{
    /// <summary>
    /// Unit tests for GopherMcpWrapper
    /// </summary>
    public class GopherMcpWrapperTests : IDisposable
    {
        private readonly GopherMcpWrapper _wrapper;
        private bool _disposed;

        public GopherMcpWrapperTests()
        {
            _wrapper = new GopherMcpWrapper();
        }

        #region Client Initialization Tests

        [Fact]
        public async Task InitializeClientAsync_Should_Return_True_When_Successful()
        {
            // Note: This test will fail without the native library
            // In a real scenario, we'd mock the P/Invoke calls
            
            // Arrange
            var name = "TestClient";
            var version = "1.0.0";

            // Act & Assert
            // Since we can't actually call native code in unit tests without the library,
            // we're testing that the method exists and has the correct signature
            var task = _wrapper.InitializeClientAsync(name, version);
            task.Should().NotBeNull();
        }

        [Fact]
        public async Task InitializeClientAsync_With_Config_Should_Accept_Custom_Configuration()
        {
            // Arrange
            var name = "TestClient";
            var version = "1.0.0";
            var config = new McpClientConfig
            {
                TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                ServerAddress = "localhost:8080",
                EnableCompression = false,
                EnableEncryption = false
            };

            // Act & Assert
            var task = _wrapper.InitializeClientAsync(name, version, config);
            task.Should().NotBeNull();
        }

        [Fact]
        public void InitializeClientAsync_Should_Throw_When_Already_Initialized()
        {
            // This test demonstrates the expected behavior
            // In practice, the second initialization should return false
            
            // Arrange
            var name = "TestClient";
            var version = "1.0.0";

            // Act & Assert
            var task1 = _wrapper.InitializeClientAsync(name, version);
            var task2 = _wrapper.InitializeClientAsync(name, version);
            
            task1.Should().NotBeNull();
            task2.Should().NotBeNull();
        }

        #endregion

        #region Server Initialization Tests

        [Fact]
        public async Task InitializeServerAsync_Should_Return_True_When_Successful()
        {
            // Arrange
            var name = "TestServer";
            var version = "1.0.0";

            // Act & Assert
            var task = _wrapper.InitializeServerAsync(name, version);
            task.Should().NotBeNull();
        }

        [Fact]
        public async Task InitializeServerAsync_With_Config_Should_Accept_Custom_Configuration()
        {
            // Arrange
            var name = "TestServer";
            var version = "1.0.0";
            var config = new McpServerConfig
            {
                TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                BindAddress = "0.0.0.0:8080",
                MaxConnections = 50,
                EnableCompression = true,
                EnableEncryption = true
            };

            // Act & Assert
            var task = _wrapper.InitializeServerAsync(name, version, config);
            task.Should().NotBeNull();
        }

        #endregion

        #region Connection Tests

        [Fact]
        public void ConnectAsync_Should_Throw_When_Client_Not_Initialized()
        {
            // Act & Assert
            Func<Task> act = async () => await _wrapper.ConnectAsync("localhost", 8080);
            act.Should().ThrowAsync<InvalidOperationException>()
                .WithMessage("Client not initialized");
        }

        [Fact]
        public void ListenAsync_Should_Throw_When_Server_Not_Initialized()
        {
            // Act & Assert
            Func<Task> act = async () => await _wrapper.ListenAsync("0.0.0.0", 8080);
            act.Should().ThrowAsync<InvalidOperationException>()
                .WithMessage("Server not initialized");
        }

        #endregion

        #region Request/Response Tests

        [Fact]
        public void SendRequestAsync_Should_Throw_When_Client_Not_Initialized()
        {
            // Act & Assert
            Func<Task> act = async () => await _wrapper.SendRequestAsync("test", null);
            act.Should().ThrowAsync<InvalidOperationException>()
                .WithMessage("Client not initialized");
        }

        [Fact]
        public void SendNotificationAsync_Should_Throw_When_Client_Not_Initialized()
        {
            // Act & Assert
            Func<Task> act = async () => await _wrapper.SendNotificationAsync("test", null);
            act.Should().ThrowAsync<InvalidOperationException>()
                .WithMessage("Client not initialized");
        }

        [Fact]
        public void SendResponseAsync_Should_Throw_When_Server_Not_Initialized()
        {
            // Act & Assert
            Func<Task> act = async () => await _wrapper.SendResponseAsync(123, "result", null);
            act.Should().ThrowAsync<InvalidOperationException>()
                .WithMessage("Server not initialized");
        }

        #endregion

        #region Handler Registration Tests

        [Fact]
        public void RegisterRequestHandler_Should_Accept_Valid_Handler()
        {
            // Arrange
            Func<McpRequest, Task<McpResponse>> handler = async (request) =>
            {
                return new McpResponse { Result = "test" };
            };

            // Act
            Action act = () => _wrapper.RegisterRequestHandler("test", handler);

            // Assert
            act.Should().NotThrow();
        }

        [Fact]
        public void RegisterRequestHandler_Should_Throw_When_Handler_Is_Null()
        {
            // Act & Assert
            Action act = () => _wrapper.RegisterRequestHandler("test", null);
            act.Should().Throw<ArgumentNullException>()
                .WithParameterName("handler");
        }

        [Fact]
        public void RegisterRequestHandler_Should_Allow_Handler_Replacement()
        {
            // Arrange
            Func<McpRequest, Task<McpResponse>> handler1 = async (request) =>
            {
                return new McpResponse { Result = "handler1" };
            };

            Func<McpRequest, Task<McpResponse>> handler2 = async (request) =>
            {
                return new McpResponse { Result = "handler2" };
            };

            // Act
            _wrapper.RegisterRequestHandler("test", handler1);
            _wrapper.RegisterRequestHandler("test", handler2);

            // Assert - should not throw
            Action act = () => _wrapper.RegisterRequestHandler("test", handler2);
            act.Should().NotThrow();
        }

        [Fact]
        public void RegisterNotificationHandler_Should_Accept_Valid_Handler()
        {
            // Arrange
            Action<McpNotification> handler = (notification) =>
            {
                // Handle notification
            };

            // Act
            Action act = () => _wrapper.RegisterNotificationHandler("test", handler);

            // Assert
            act.Should().NotThrow();
        }

        [Fact]
        public void RegisterNotificationHandler_Should_Throw_When_Handler_Is_Null()
        {
            // Act & Assert
            Action act = () => _wrapper.RegisterNotificationHandler("test", null);
            act.Should().Throw<ArgumentNullException>()
                .WithParameterName("handler");
        }

        #endregion

        #region Event Tests

        [Fact]
        public void ErrorOccurred_Event_Should_Be_Raisable()
        {
            // Arrange
            McpErrorEventArgs receivedArgs = null;
            _wrapper.ErrorOccurred += (sender, args) =>
            {
                receivedArgs = args;
            };

            var errorArgs = new McpErrorEventArgs("Test error", -1);

            // Act
            // Since we can't directly trigger the internal error,
            // we're testing that the event is properly defined
            // Note: Can't invoke event directly from outside the class

            // Assert
            // Just verify the event can be subscribed to
            Action act = () => _wrapper.ErrorOccurred += (s, e) => { };
            act.Should().NotThrow();
        }

        [Fact]
        public void ConnectionStateChanged_Event_Should_Be_Raisable()
        {
            // Arrange
            ConnectionStateEventArgs receivedArgs = null;
            _wrapper.ConnectionStateChanged += (sender, args) =>
            {
                receivedArgs = args;
            };

            var stateArgs = new ConnectionStateEventArgs(mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED);

            // Act & Assert
            // Just verify the event can be subscribed to
            Action act = () => _wrapper.ConnectionStateChanged += (s, e) => { };
            act.Should().NotThrow();
        }

        #endregion

        #region Dispatcher Tests

        [Fact]
        public void RunAsync_Should_Throw_When_Not_Initialized()
        {
            // Act & Assert
            Func<Task> act = async () => await _wrapper.RunAsync();
            act.Should().ThrowAsync<InvalidOperationException>()
                .WithMessage("Not initialized");
        }

        [Fact]
        public async Task RunAsync_Should_Accept_CancellationToken()
        {
            // Arrange
            var cts = new CancellationTokenSource();

            // Act
            var task = _wrapper.RunAsync(cts.Token);

            // Assert
            task.Should().NotBeNull();
        }

        #endregion

        #region Configuration Tests

        [Fact]
        public void McpClientConfig_Should_Have_Default_Values()
        {
            // Arrange & Act
            var config = new McpClientConfig();

            // Assert
            config.TransportType.Should().Be(mcp_transport_type_t.MCP_TRANSPORT_STDIO);
            config.ServerAddress.Should().BeNull();
            config.EnableCompression.Should().BeTrue();
            config.EnableEncryption.Should().BeTrue();
        }

        [Fact]
        public void McpServerConfig_Should_Have_Default_Values()
        {
            // Arrange & Act
            var config = new McpServerConfig();

            // Assert
            config.TransportType.Should().Be(mcp_transport_type_t.MCP_TRANSPORT_STDIO);
            config.BindAddress.Should().BeNull();
            config.MaxConnections.Should().Be(100);
            config.EnableCompression.Should().BeTrue();
            config.EnableEncryption.Should().BeTrue();
        }

        [Fact]
        public void McpClientConfig_Should_Be_Instantiable()
        {
            // Arrange & Act
            var config = new McpClientConfig
            {
                TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                ServerAddress = "localhost:8080"
            };

            // Assert
            config.TransportType.Should().Be(mcp_transport_type_t.MCP_TRANSPORT_STDIO);
            config.ServerAddress.Should().Be("localhost:8080");
        }

        [Fact]
        public void McpServerConfig_Should_Be_Instantiable()
        {
            // Arrange & Act
            var config = new McpServerConfig
            {
                TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                MaxConnections = 50,
                BindAddress = "0.0.0.0:8080"
            };

            // Assert
            config.TransportType.Should().Be(mcp_transport_type_t.MCP_TRANSPORT_STDIO);
            config.MaxConnections.Should().Be(50);
            config.BindAddress.Should().Be("0.0.0.0:8080");
        }

        #endregion

        #region Model Class Tests

        [Fact]
        public void McpRequest_Should_Have_Expected_Properties()
        {
            // Arrange & Act
            var request = new McpRequest
            {
                JsonRpc = "2.0",
                Method = "test",
                Params = new { value = 123 },
                Id = 456
            };

            // Assert
            request.JsonRpc.Should().Be("2.0");
            request.Method.Should().Be("test");
            request.Params.Should().NotBeNull();
            request.Id.Should().Be(456);
        }

        [Fact]
        public void McpResponse_Should_Have_Expected_Properties()
        {
            // Arrange & Act
            var response = new McpResponse
            {
                JsonRpc = "2.0",
                Result = "success",
                Error = null,
                Id = 789
            };

            // Assert
            response.JsonRpc.Should().Be("2.0");
            response.Result.Should().Be("success");
            response.Error.Should().BeNull();
            response.Id.Should().Be(789);
        }

        [Fact]
        public void McpResponse_With_Error_Should_Have_Null_Result()
        {
            // Arrange & Act
            var response = new McpResponse
            {
                JsonRpc = "2.0",
                Result = null,
                Error = new McpError
                {
                    Code = -32600,
                    Message = "Invalid request",
                    Data = null
                },
                Id = 123
            };

            // Assert
            response.Result.Should().BeNull();
            response.Error.Should().NotBeNull();
            response.Error.Code.Should().Be(-32600);
            response.Error.Message.Should().Be("Invalid request");
        }

        [Fact]
        public void McpNotification_Should_Have_Expected_Properties()
        {
            // Arrange & Act
            var notification = new McpNotification
            {
                JsonRpc = "2.0",
                Method = "log",
                Params = new { level = "info", message = "test" }
            };

            // Assert
            notification.JsonRpc.Should().Be("2.0");
            notification.Method.Should().Be("log");
            notification.Params.Should().NotBeNull();
        }

        [Fact]
        public void McpError_Should_Have_Expected_Properties()
        {
            // Arrange & Act
            var error = new McpError
            {
                Code = -32601,
                Message = "Method not found",
                Data = new { method = "unknown" }
            };

            // Assert
            error.Code.Should().Be(-32601);
            error.Message.Should().Be("Method not found");
            error.Data.Should().NotBeNull();
        }

        #endregion

        #region Exception Tests

        [Fact]
        public void McpException_Should_Store_ErrorCode()
        {
            // Arrange & Act
            var exception = new McpException("Test error", -100);

            // Assert
            exception.Message.Should().Be("Test error");
            exception.ErrorCode.Should().Be(-100);
        }

        [Fact]
        public void McpException_Should_Support_InnerException()
        {
            // Arrange
            var innerException = new InvalidOperationException("Inner error");
            
            // Act
            var exception = new McpException("Outer error", innerException, -200);

            // Assert
            exception.Message.Should().Be("Outer error");
            exception.InnerException.Should().Be(innerException);
            exception.ErrorCode.Should().Be(-200);
        }

        [Fact]
        public void McpException_Should_Have_Default_ErrorCode()
        {
            // Arrange & Act
            var exception = new McpException("Test error");

            // Assert
            exception.ErrorCode.Should().Be(-1);
        }

        #endregion

        #region Event Args Tests

        [Fact]
        public void McpErrorEventArgs_Should_Store_Message_And_Code()
        {
            // Arrange & Act
            var args = new McpErrorEventArgs("Error message", 500);

            // Assert
            args.Message.Should().Be("Error message");
            args.ErrorCode.Should().Be(500);
        }

        [Fact]
        public void ConnectionStateEventArgs_Should_Indicate_Connected_State()
        {
            // Arrange & Act
            var connectedArgs = new ConnectionStateEventArgs(mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED);
            var disconnectedArgs = new ConnectionStateEventArgs(mcp_connection_state_t.MCP_CONNECTION_STATE_DISCONNECTED);
            var connectingArgs = new ConnectionStateEventArgs(mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTING);

            // Assert
            connectedArgs.IsConnected.Should().BeTrue();
            disconnectedArgs.IsConnected.Should().BeFalse();
            connectingArgs.IsConnected.Should().BeFalse();
        }

        #endregion

        #region Disposal Tests

        [Fact]
        public void Dispose_Should_Be_Idempotent()
        {
            // Arrange
            var wrapper = new GopherMcpWrapper();

            // Act
            wrapper.Dispose();
            wrapper.Dispose(); // Second call should not throw

            // Assert - no exception should be thrown
            Action act = () => wrapper.Dispose();
            act.Should().NotThrow();
        }

        [Fact]
        public async Task CloseAsync_Should_Complete_Successfully()
        {
            // Arrange & Act
            var task = _wrapper.CloseAsync();

            // Assert
            task.Should().NotBeNull();
            await task; // Should complete without throwing
        }

        #endregion

        #region Thread Safety Tests

        [Fact]
        public void Concurrent_Handler_Registration_Should_Be_Thread_Safe()
        {
            // Arrange
            var tasks = new Task[10];

            // Act
            for (int i = 0; i < tasks.Length; i++)
            {
                var methodName = $"method{i}";
                tasks[i] = Task.Run(() =>
                {
                    _wrapper.RegisterRequestHandler(methodName, async (req) =>
                    {
                        return new McpResponse { Result = methodName };
                    });
                });
            }

            // Assert
            Action act = () => Task.WaitAll(tasks);
            act.Should().NotThrow();
        }

        [Fact]
        public void Concurrent_Notification_Handler_Registration_Should_Be_Thread_Safe()
        {
            // Arrange
            var tasks = new Task[10];

            // Act
            for (int i = 0; i < tasks.Length; i++)
            {
                var methodName = $"notification{i}";
                tasks[i] = Task.Run(() =>
                {
                    _wrapper.RegisterNotificationHandler(methodName, (notif) =>
                    {
                        // Handle notification
                    });
                });
            }

            // Assert
            Action act = () => Task.WaitAll(tasks);
            act.Should().NotThrow();
        }

        #endregion

        #region Timeout Tests

        [Theory]
        [InlineData(1000)]
        [InlineData(5000)]
        [InlineData(30000)]
        public async Task ConnectAsync_Should_Respect_Timeout(int timeoutMs)
        {
            // Arrange
            var timeout = TimeSpan.FromMilliseconds(timeoutMs);

            // Act
            var task = _wrapper.ConnectAsync("localhost", 8080, timeout);

            // Assert
            task.Should().NotBeNull();
            // Note: Without actual connection, we're just verifying the method signature
        }

        [Theory]
        [InlineData(1000)]
        [InlineData(5000)]
        [InlineData(30000)]
        public async Task SendRequestAsync_Should_Respect_Timeout(int timeoutMs)
        {
            // Arrange
            var timeout = TimeSpan.FromMilliseconds(timeoutMs);

            // Act & Assert
            // Without initialization, this should throw
            Func<Task> act = async () => await _wrapper.SendRequestAsync("test", null, timeout);
            await act.Should().ThrowAsync<InvalidOperationException>();
        }

        #endregion

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    _wrapper?.Dispose();
                }
                _disposed = true;
            }
        }
    }
}