using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using GopherMcp.Manager;

namespace GopherMcp.Transport
{
    /// <summary>
    /// Transport configuration
    /// </summary>
    public class TransportConfig
    {
        public TransportProtocol Protocol { get; set; } = TransportProtocol.Tcp;
        public string Host { get; set; } = "localhost";
        public int Port { get; set; } = 9000;
        public TimeSpan ConnectTimeout { get; set; } = TimeSpan.FromSeconds(30);
        public TimeSpan SendTimeout { get; set; } = TimeSpan.FromSeconds(30);
        public TimeSpan ReceiveTimeout { get; set; } = TimeSpan.FromSeconds(30);
        public int MaxMessageSize { get; set; } = 4 * 1024 * 1024; // 4MB
        public int SendBufferSize { get; set; } = 8192;
        public int ReceiveBufferSize { get; set; } = 8192;
        public bool EnableKeepAlive { get; set; } = true;
        public TimeSpan KeepAliveInterval { get; set; } = TimeSpan.FromSeconds(30);
        public FilterManagerConfig? Filters { get; set; }
    }

    /// <summary>
    /// Transport protocol selection
    /// </summary>
    public enum TransportProtocol
    {
        Tcp,
        Udp,
        Stdio,
        Http,
        WebSocket
    }

    /// <summary>
    /// Main transport implementation with filter integration
    /// </summary>
    public class GopherTransport : ITransport
    {
        private readonly TransportConfig _config;
        private readonly FilterManager? _filterManager;
        private readonly Channel<JsonRpcMessage> _receiveQueue;
        private readonly Channel<JsonRpcMessage> _sendQueue;
        private readonly CancellationTokenSource _shutdownTokenSource;
        private readonly SemaphoreSlim _stateLock;
        
        private IProtocolTransport? _protocolTransport;
        private ConnectionState _state;
        private Task? _receiveLoopTask;
        private Task? _sendLoopTask;
        private bool _disposed;

        public ConnectionState State
        {
            get => _state;
            private set
            {
                var oldState = _state;
                _state = value;
                
                if (oldState != value)
                {
                    OnConnectionStateChanged(value, oldState);
                }
            }
        }

        public bool IsConnected => State == ConnectionState.Connected;

        public event EventHandler<MessageReceivedEventArgs>? MessageReceived;
        public event EventHandler<TransportErrorEventArgs>? Error;
        public event EventHandler<ConnectionStateEventArgs>? Connected;
        public event EventHandler<ConnectionStateEventArgs>? Disconnected;

        public GopherTransport(TransportConfig config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _shutdownTokenSource = new CancellationTokenSource();
            _stateLock = new SemaphoreSlim(1, 1);
            _state = ConnectionState.Disconnected;

            // Validate configuration
            ValidateConfiguration();

            // Create channels for message queuing with proper options
            var receiveChannelOptions = new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false
            };
            _receiveQueue = Channel.CreateUnbounded<JsonRpcMessage>(receiveChannelOptions);

            var sendChannelOptions = new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false
            };
            _sendQueue = Channel.CreateUnbounded<JsonRpcMessage>(sendChannelOptions);

            // Initialize filter manager if configured
            if (_config.Filters != null)
            {
                _filterManager = new FilterManager(_config.Filters);
            }

            // Select protocol implementation based on configuration
            SelectProtocolImplementation();
        }

        private void ValidateConfiguration()
        {
            if (_config.Port <= 0 || _config.Port > 65535)
            {
                throw new ArgumentException($"Invalid port number: {_config.Port}", nameof(_config));
            }

            if (string.IsNullOrWhiteSpace(_config.Host) && _config.Protocol != TransportProtocol.Stdio)
            {
                throw new ArgumentException("Host cannot be empty for non-stdio protocols", nameof(_config));
            }

            if (_config.MaxMessageSize <= 0)
            {
                throw new ArgumentException("MaxMessageSize must be greater than 0", nameof(_config));
            }

            if (_config.ConnectTimeout <= TimeSpan.Zero)
            {
                throw new ArgumentException("ConnectTimeout must be greater than 0", nameof(_config));
            }

            if (_config.SendTimeout <= TimeSpan.Zero)
            {
                throw new ArgumentException("SendTimeout must be greater than 0", nameof(_config));
            }

            if (_config.ReceiveTimeout <= TimeSpan.Zero)
            {
                throw new ArgumentException("ReceiveTimeout must be greater than 0", nameof(_config));
            }
        }

        public async Task StartAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            await _stateLock.WaitAsync(cancellationToken);
            try
            {
                if (State == ConnectionState.Connected || State == ConnectionState.Connecting)
                {
                    return; // Already connected or connecting
                }

                State = ConnectionState.Connecting;
            }
            finally
            {
                _stateLock.Release();
            }

            try
            {
                // Connect via selected protocol
                using var connectCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                connectCts.CancelAfter(_config.ConnectTimeout);

                if (_protocolTransport == null)
                {
                    throw new InvalidOperationException("Protocol transport not initialized");
                }

                await _protocolTransport.ConnectAsync(connectCts.Token);

                // Start receive loop task
                _receiveLoopTask = Task.Run(async () => await ReceiveLoop(), _shutdownTokenSource.Token);

                // Start send loop task
                _sendLoopTask = Task.Run(async () => await SendLoop(), _shutdownTokenSource.Token);

                // Initialize connection state
                await _stateLock.WaitAsync(cancellationToken);
                try
                {
                    State = ConnectionState.Connected;
                }
                finally
                {
                    _stateLock.Release();
                }

                // Raise Connected event
                OnConnectionStateChanged(ConnectionState.Connected, ConnectionState.Connecting);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                await HandleConnectionFailureAsync("Connection cancelled");
                throw;
            }
            catch (Exception ex)
            {
                await HandleConnectionFailureAsync($"Connection failed: {ex.Message}");
                OnError(ex, "Failed to start transport");
                throw new InvalidOperationException("Failed to start transport", ex);
            }
        }

        private async Task HandleConnectionFailureAsync(string reason)
        {
            await _stateLock.WaitAsync();
            try
            {
                State = ConnectionState.Failed;
                OnConnectionStateChanged(ConnectionState.Failed, ConnectionState.Connecting);
            }
            finally
            {
                _stateLock.Release();
            }

            // Clean up any partial connection
            try
            {
                if (_protocolTransport?.IsConnected == true)
                {
                    await _protocolTransport.DisconnectAsync(CancellationToken.None);
                }
            }
            catch
            {
                // Ignore cleanup errors
            }
        }

        public async Task StopAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            await _stateLock.WaitAsync(cancellationToken);
            try
            {
                if (State == ConnectionState.Disconnected || State == ConnectionState.Disconnecting)
                {
                    return; // Already disconnected or disconnecting
                }

                State = ConnectionState.Disconnecting;
            }
            finally
            {
                _stateLock.Release();
            }

            try
            {
                // Cancel receive loop
                _shutdownTokenSource.Cancel();

                // Wait for loops to complete with timeout
                var loopTasks = new List<Task>();
                if (_receiveLoopTask != null)
                {
                    loopTasks.Add(_receiveLoopTask);
                }
                if (_sendLoopTask != null)
                {
                    loopTasks.Add(_sendLoopTask);
                }

                if (loopTasks.Count > 0)
                {
                    using var stopCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                    stopCts.CancelAfter(TimeSpan.FromSeconds(5)); // 5 second timeout for loops to stop

                    try
                    {
                        await Task.WhenAll(loopTasks).WaitAsync(stopCts.Token);
                    }
                    catch (OperationCanceledException)
                    {
                        // Loops didn't stop in time, but we'll continue with cleanup
                    }
                    catch (Exception ex)
                    {
                        OnError(ex, "Error stopping message loops");
                    }
                }

                // Flush pending messages
                await FlushPendingMessagesAsync(cancellationToken);

                // Close protocol connection
                if (_protocolTransport?.IsConnected == true)
                {
                    try
                    {
                        using var disconnectCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                        disconnectCts.CancelAfter(TimeSpan.FromSeconds(5));
                        await _protocolTransport.DisconnectAsync(disconnectCts.Token);
                    }
                    catch (Exception ex)
                    {
                        OnError(ex, "Error disconnecting protocol transport");
                    }
                }

                // Update state
                await _stateLock.WaitAsync(cancellationToken);
                try
                {
                    State = ConnectionState.Disconnected;
                }
                finally
                {
                    _stateLock.Release();
                }

                // Raise Disconnected event
                OnConnectionStateChanged(ConnectionState.Disconnected, ConnectionState.Disconnecting);
            }
            catch (Exception ex)
            {
                OnError(ex, "Error during transport stop");
                
                // Force state to disconnected
                await _stateLock.WaitAsync();
                try
                {
                    State = ConnectionState.Disconnected;
                }
                finally
                {
                    _stateLock.Release();
                }
                
                throw;
            }
            finally
            {
                // Cleanup resources
                CleanupResources();
            }
        }

        private async Task FlushPendingMessagesAsync(CancellationToken cancellationToken)
        {
            try
            {
                // Try to send any remaining messages in the send queue
                while (_sendQueue.Reader.TryRead(out var message))
                {
                    if (_protocolTransport?.IsConnected == true)
                    {
                        try
                        {
                            using var sendCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                            sendCts.CancelAfter(TimeSpan.FromSeconds(1));
                            await _protocolTransport.SendAsync(message, sendCts.Token);
                        }
                        catch
                        {
                            // Best effort - ignore failures during flush
                        }
                    }
                }

                // Clear receive queue
                while (_receiveQueue.Reader.TryRead(out _))
                {
                    // Just drain the queue
                }
            }
            catch (Exception ex)
            {
                OnError(ex, "Error flushing pending messages");
            }
        }

        private void CleanupResources()
        {
            _receiveLoopTask = null;
            _sendLoopTask = null;
        }

        public async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            ThrowIfNotConnected();

            if (message == null)
            {
                throw new ArgumentNullException(nameof(message));
            }

            // Validate message
            ValidateMessage(message);

            using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                _shutdownTokenSource.Token);

            try
            {
                // Process message through FilterManager if configured
                JsonRpcMessage messageToSend = message;
                
                if (_filterManager != null)
                {
                    var messageBytes = SerializeMessage(message);
                    var context = new Types.ProcessingContext
                    {
                        Direction = Types.ProcessingDirection.Outbound
                    };
                    
                    // Store message metadata in context
                    context.SetProperty("MessageId", message.Id);
                    context.SetProperty("Method", message.Method);
                    context.SetProperty("IsRequest", message.IsRequest);
                    context.SetProperty("IsNotification", message.IsNotification);
                    context.SetProperty("IsResponse", message.IsResponse);

                    var result = await _filterManager.ProcessAsync(messageBytes, context, linkedCts.Token);
                    
                    if (!result.IsSuccess)
                    {
                        throw new InvalidOperationException($"Filter processing failed: {result.ErrorMessage}");
                    }

                    // Deserialize filtered message
                    messageToSend = DeserializeMessage(result.Data);
                }

                // Add to send queue
                await _sendQueue.Writer.WriteAsync(messageToSend, linkedCts.Token);

                // Update statistics
                UpdateSendStatistics(messageToSend);
            }
            catch (OperationCanceledException) when (_shutdownTokenSource.Token.IsCancellationRequested)
            {
                throw new ObjectDisposedException(nameof(GopherTransport), "Transport is shutting down");
            }
            catch (Exception ex)
            {
                OnError(ex, "Error sending message");
                throw;
            }
        }

        private void ValidateMessage(JsonRpcMessage message)
        {
            // Validate JSON-RPC version
            if (string.IsNullOrEmpty(message.JsonRpc))
            {
                message.JsonRpc = "2.0";
            }
            else if (message.JsonRpc != "2.0")
            {
                throw new ArgumentException($"Invalid JSON-RPC version: {message.JsonRpc}", nameof(message));
            }

            // Validate message type
            bool hasMethod = !string.IsNullOrEmpty(message.Method);
            bool hasResult = message.Result != null;
            bool hasError = message.Error != null;

            if (hasMethod && (hasResult || hasError))
            {
                throw new ArgumentException("Request/notification cannot have result or error", nameof(message));
            }

            if (!hasMethod && !hasResult && !hasError)
            {
                throw new ArgumentException("Response must have either result or error", nameof(message));
            }

            if (hasResult && hasError)
            {
                throw new ArgumentException("Response cannot have both result and error", nameof(message));
            }

            // Validate request
            if (hasMethod)
            {
                if (string.IsNullOrWhiteSpace(message.Method))
                {
                    throw new ArgumentException("Method name cannot be empty", nameof(message));
                }

                if (message.Method.StartsWith("rpc."))
                {
                    throw new ArgumentException("Method names starting with 'rpc.' are reserved", nameof(message));
                }
            }

            // Validate error
            if (hasError)
            {
                if (string.IsNullOrWhiteSpace(message.Error.Message))
                {
                    throw new ArgumentException("Error message cannot be empty", nameof(message));
                }
            }
        }

        private byte[] SerializeMessage(JsonRpcMessage message)
        {
            var json = System.Text.Json.JsonSerializer.Serialize(message);
            return System.Text.Encoding.UTF8.GetBytes(json);
        }

        private JsonRpcMessage DeserializeMessage(byte[] data)
        {
            var json = System.Text.Encoding.UTF8.GetString(data);
            return System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(json) 
                ?? throw new InvalidOperationException("Failed to deserialize message");
        }

        private void UpdateSendStatistics(JsonRpcMessage message)
        {
            // Update internal statistics (can be extended as needed)
            if (message.IsRequest)
            {
                Interlocked.Increment(ref _totalRequestsSent);
            }
            else if (message.IsNotification)
            {
                Interlocked.Increment(ref _totalNotificationsSent);
            }
            else if (message.IsResponse)
            {
                Interlocked.Increment(ref _totalResponsesSent);
            }
        }

        // Statistics fields
        private long _totalRequestsSent;
        private long _totalNotificationsSent;
        private long _totalResponsesSent;

        public async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            ThrowIfNotConnected();

            using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken, 
                _shutdownTokenSource.Token);

            try
            {
                return await _receiveQueue.Reader.ReadAsync(linkedCts.Token);
            }
            catch (OperationCanceledException) when (_shutdownTokenSource.Token.IsCancellationRequested)
            {
                throw new ObjectDisposedException(nameof(GopherTransport), "Transport is shutting down");
            }
        }

        private void SelectProtocolImplementation()
        {
            _protocolTransport = _config.Protocol switch
            {
                TransportProtocol.Tcp => new TcpProtocolTransport(_config),
                TransportProtocol.Udp => new UdpProtocolTransport(_config),
                TransportProtocol.Stdio => new StdioProtocolTransport(_config),
                TransportProtocol.Http => new HttpProtocolTransport(_config),
                TransportProtocol.WebSocket => new WebSocketProtocolTransport(_config),
                _ => throw new NotSupportedException($"Protocol {_config.Protocol} is not supported")
            };
        }

        private async Task ReceiveLoop()
        {
            // Implementation will be added in prompt #75
            await Task.CompletedTask;
        }

        private async Task SendLoop()
        {
            while (!_shutdownTokenSource.Token.IsCancellationRequested)
            {
                try
                {
                    var message = await _sendQueue.Reader.ReadAsync(_shutdownTokenSource.Token);
                    
                    if (_protocolTransport != null)
                    {
                        await _protocolTransport.SendAsync(message, _shutdownTokenSource.Token);
                    }
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    OnError(ex, "Send loop error");
                }
            }
        }

        private void OnConnectionStateChanged(ConnectionState newState, ConnectionState oldState)
        {
            var args = new ConnectionStateEventArgs(newState, oldState);
            
            switch (newState)
            {
                case ConnectionState.Connected:
                    Connected?.Invoke(this, args);
                    break;
                case ConnectionState.Disconnected:
                case ConnectionState.Failed:
                    Disconnected?.Invoke(this, args);
                    break;
            }
        }

        private void OnMessageReceived(JsonRpcMessage message)
        {
            MessageReceived?.Invoke(this, new MessageReceivedEventArgs(message));
        }

        private void OnError(Exception exception, string? context = null)
        {
            Error?.Invoke(this, new TransportErrorEventArgs(exception, context));
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(GopherTransport));
            }
        }

        private void ThrowIfNotConnected()
        {
            if (!IsConnected)
            {
                throw new InvalidOperationException("Transport is not connected");
            }
        }

        public void Dispose()
        {
            // Implementation will be added in prompt #76
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    // Dispose managed resources
                    _shutdownTokenSource?.Cancel();
                    _shutdownTokenSource?.Dispose();
                    _stateLock?.Dispose();
                    _filterManager?.Dispose();
                    _protocolTransport?.Dispose();
                }

                _disposed = true;
            }
        }
    }

    /// <summary>
    /// Base interface for protocol-specific transport implementations
    /// </summary>
    internal interface IProtocolTransport : IDisposable
    {
        Task ConnectAsync(CancellationToken cancellationToken);
        Task DisconnectAsync(CancellationToken cancellationToken);
        Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken);
        Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken);
        bool IsConnected { get; }
    }

    /// <summary>
    /// Base class for protocol transport implementations
    /// </summary>
    internal abstract class ProtocolTransportBase : IProtocolTransport
    {
        protected readonly TransportConfig Config;
        protected bool Disposed;

        protected ProtocolTransportBase(TransportConfig config)
        {
            Config = config ?? throw new ArgumentNullException(nameof(config));
        }

        public abstract bool IsConnected { get; }
        public abstract Task ConnectAsync(CancellationToken cancellationToken);
        public abstract Task DisconnectAsync(CancellationToken cancellationToken);
        public abstract Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken);
        public abstract Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken);

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!Disposed)
            {
                if (disposing)
                {
                    // Dispose managed resources
                }
                Disposed = true;
            }
        }

        protected void ThrowIfDisposed()
        {
            if (Disposed)
            {
                throw new ObjectDisposedException(GetType().Name);
            }
        }
    }

    // Placeholder implementations for different protocols
    internal class TcpProtocolTransport : ProtocolTransportBase
    {
        public TcpProtocolTransport(TransportConfig config) : base(config) { }
        public override bool IsConnected => false;
        public override Task ConnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task DisconnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken) => Task.FromResult(new JsonRpcMessage());
    }

    internal class UdpProtocolTransport : ProtocolTransportBase
    {
        public UdpProtocolTransport(TransportConfig config) : base(config) { }
        public override bool IsConnected => false;
        public override Task ConnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task DisconnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken) => Task.FromResult(new JsonRpcMessage());
    }

    internal class StdioProtocolTransport : ProtocolTransportBase
    {
        public StdioProtocolTransport(TransportConfig config) : base(config) { }
        public override bool IsConnected => true;
        public override Task ConnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task DisconnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken) => Task.FromResult(new JsonRpcMessage());
    }

    internal class HttpProtocolTransport : ProtocolTransportBase
    {
        public HttpProtocolTransport(TransportConfig config) : base(config) { }
        public override bool IsConnected => false;
        public override Task ConnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task DisconnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken) => Task.FromResult(new JsonRpcMessage());
    }

    internal class WebSocketProtocolTransport : ProtocolTransportBase
    {
        public WebSocketProtocolTransport(TransportConfig config) : base(config) { }
        public override bool IsConnected => false;
        public override Task ConnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task DisconnectAsync(CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken) => Task.CompletedTask;
        public override Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken) => Task.FromResult(new JsonRpcMessage());
    }
}