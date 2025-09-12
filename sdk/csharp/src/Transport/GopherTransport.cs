using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using GopherMcp.Manager;
using GopherMcp.Integration;
#if !NET6_0_OR_GREATER
using GopherMcp.Utils;
#endif
namespace GopherMcp.Transport
{
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
        
        public bool IsServer => _config.IsServer;

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

                    var result = await _filterManager.ProcessAsync(message, null, linkedCts.Token);

                    if (!result.IsSuccess)
                    {
                        throw new InvalidOperationException($"Filter processing failed: {result.ErrorMessage}");
                    }

                    // Use the filtered message
                    messageToSend = result.Data ?? message;
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
            while (!_shutdownTokenSource.Token.IsCancellationRequested)
            {
                try
                {
                    // Continuous receive from protocol
                    if (_protocolTransport == null || !_protocolTransport.IsConnected)
                    {
                        await Task.Delay(100, _shutdownTokenSource.Token);
                        continue;
                    }

                    // Receive message from protocol transport with timeout
                    using var receiveCts = CancellationTokenSource.CreateLinkedTokenSource(_shutdownTokenSource.Token);
                    receiveCts.CancelAfter(_config.ReceiveTimeout);

                    JsonRpcMessage receivedMessage;
                    try
                    {
                        receivedMessage = await _protocolTransport.ReceiveAsync(receiveCts.Token);
                    }
                    catch (OperationCanceledException) when (receiveCts.IsCancellationRequested && !_shutdownTokenSource.Token.IsCancellationRequested)
                    {
                        // Receive timeout - continue loop
                        continue;
                    }

                    if (receivedMessage == null)
                    {
                        // Null message might indicate disconnection
                        await HandleDisconnectionAsync("Null message received");
                        break;
                    }

                    // Process through FilterManager if configured
                    JsonRpcMessage messageToDeliver = receivedMessage;

                    if (_filterManager != null)
                    {
                        var messageBytes = SerializeMessage(receivedMessage);
                        var context = new Types.ProcessingContext
                        {
                            Direction = Types.ProcessingDirection.Inbound
                        };

                        // Store message metadata in context
                        context.SetProperty("MessageId", receivedMessage.Id);
                        context.SetProperty("Method", receivedMessage.Method);
                        context.SetProperty("IsRequest", receivedMessage.IsRequest);
                        context.SetProperty("IsNotification", receivedMessage.IsNotification);
                        context.SetProperty("IsResponse", receivedMessage.IsResponse);

                        try
                        {
                            var result = await _filterManager.ProcessAsync(receivedMessage, null, _shutdownTokenSource.Token);

                            if (!result.IsSuccess)
                            {
                                OnError(new InvalidOperationException($"Filter processing failed: {result.ErrorMessage}"), "Receive filter error");
                                continue; // Skip this message
                            }

                            // Use the filtered message
                            messageToDeliver = result.Data ?? receivedMessage;
                        }
                        catch (Exception ex)
                        {
                            OnError(ex, "Error processing received message through filters");
                            continue; // Skip this message
                        }
                    }

                    // Add to receive queue
                    await _receiveQueue.Writer.WriteAsync(messageToDeliver, _shutdownTokenSource.Token);

                    // Raise MessageReceived event
                    OnMessageReceived(messageToDeliver);

                    // Update statistics
                    UpdateReceiveStatistics(messageToDeliver);
                }
                catch (OperationCanceledException) when (_shutdownTokenSource.Token.IsCancellationRequested)
                {
                    // Normal shutdown
                    break;
                }
                catch (Exception ex)
                {
                    OnError(ex, "Receive loop error");

                    // Check if this is a fatal error that requires disconnection
                    if (IsFatalError(ex))
                    {
                        await HandleDisconnectionAsync($"Fatal error in receive loop: {ex.Message}");
                        break;
                    }

                    // Non-fatal error - continue after a short delay
                    try
                    {
                        await Task.Delay(100, _shutdownTokenSource.Token);
                    }
                    catch (OperationCanceledException)
                    {
                        break;
                    }
                }
            }

            // Mark receive queue as completed
            _receiveQueue.Writer.TryComplete();
        }

        private async Task HandleDisconnectionAsync(string reason)
        {
            await _stateLock.WaitAsync();
            try
            {
                if (State == ConnectionState.Connected)
                {
                    State = ConnectionState.Disconnected;
                    OnConnectionStateChanged(ConnectionState.Disconnected, ConnectionState.Connected);
                }
            }
            finally
            {
                _stateLock.Release();
            }

            // Cancel shutdown to stop all loops
            _shutdownTokenSource.Cancel();
        }

        private bool IsFatalError(Exception ex)
        {
            // Determine if the error is fatal and requires disconnection
            return ex is IOException ||
                   ex is InvalidOperationException ||
                   ex is ObjectDisposedException ||
                   ex.InnerException is IOException ||
                   ex.InnerException is ObjectDisposedException;
        }

        private void UpdateReceiveStatistics(JsonRpcMessage message)
        {
            // Update internal statistics
            if (message.IsRequest)
            {
                Interlocked.Increment(ref _totalRequestsReceived);
            }
            else if (message.IsNotification)
            {
                Interlocked.Increment(ref _totalNotificationsReceived);
            }
            else if (message.IsResponse)
            {
                Interlocked.Increment(ref _totalResponsesReceived);
            }
        }

        // Additional statistics fields
        private long _totalRequestsReceived;
        private long _totalNotificationsReceived;
        private long _totalResponsesReceived;

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
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    try
                    {
                        // Stop transport if running
                        if (State == ConnectionState.Connected || State == ConnectionState.Connecting)
                        {
                            try
                            {
                                // Use a separate cancellation token for disposal with timeout
                                using var disposeCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
                                StopAsync(disposeCts.Token).GetAwaiter().GetResult();
                            }
                            catch (Exception ex)
                            {
                                // Log but don't throw during disposal
                                OnError(ex, "Error stopping transport during disposal");
                            }
                        }

                        // Cancel all operations
                        _shutdownTokenSource?.Cancel();

                        // Wait for background tasks to complete
                        var tasksToWait = new List<Task>();
                        if (_receiveLoopTask != null && !_receiveLoopTask.IsCompleted)
                        {
                            tasksToWait.Add(_receiveLoopTask);
                        }
                        if (_sendLoopTask != null && !_sendLoopTask.IsCompleted)
                        {
                            tasksToWait.Add(_sendLoopTask);
                        }

                        if (tasksToWait.Count > 0)
                        {
                            try
                            {
                                Task.WaitAll(tasksToWait.ToArray(), TimeSpan.FromSeconds(2));
                            }
                            catch
                            {
                                // Ignore errors during task cleanup
                            }
                        }

                        // Complete the channels
                        _receiveQueue?.Writer.TryComplete();
                        _sendQueue?.Writer.TryComplete();

                        // Dispose managed resources
                        _shutdownTokenSource?.Dispose();
                        _stateLock?.Dispose();
                        _filterManager?.Dispose();
                        _protocolTransport?.Dispose();

                        // Clear event handlers to prevent memory leaks
                        MessageReceived = null;
                        Error = null;
                        Connected = null;
                        Disconnected = null;
                    }
                    catch (Exception ex)
                    {
                        // Suppress exceptions during disposal but try to log them
                        try
                        {
                            OnError(ex, "Unexpected error during disposal");
                        }
                        catch
                        {
                            // Even error logging failed, nothing we can do
                        }
                    }
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
        private System.Net.Sockets.TcpClient? _tcpClient;
        private System.Net.Sockets.TcpListener? _tcpListener;
        private System.IO.StreamReader? _reader;
        private System.IO.StreamWriter? _writer;
        private readonly bool _isServer;
        private readonly SemaphoreSlim _sendLock = new(1, 1);
        private readonly SemaphoreSlim _receiveLock = new(1, 1);
        private Task? _acceptTask;
        private readonly TaskCompletionSource<bool> _connectionReady = new();

        public TcpProtocolTransport(TransportConfig config) : base(config) 
        {
            _isServer = config.IsServer;
        }

        public override bool IsConnected => _isServer ? (_connectionReady.Task.IsCompletedSuccessfully && _tcpClient?.Connected == true) : (_tcpClient?.Connected ?? false);

        public override async Task ConnectAsync(CancellationToken cancellationToken)
        {
            if (_isServer)
            {
                // Server mode: listen for connections
                var ipAddress = Config.Host == "localhost" || string.IsNullOrEmpty(Config.Host) 
                    ? System.Net.IPAddress.Loopback 
                    : System.Net.IPAddress.Parse(Config.Host);
                _tcpListener = new System.Net.Sockets.TcpListener(ipAddress, Config.Port);
                _tcpListener.Start();
                
                // Start accepting in the background
                _acceptTask = Task.Run(async () =>
                {
                    try
                    {
                        _tcpClient = await _tcpListener.AcceptTcpClientAsync().ConfigureAwait(false);
                        
                        // Setup streams
                        var stream = _tcpClient.GetStream();
                        _reader = new System.IO.StreamReader(stream, System.Text.Encoding.UTF8);
                        _writer = new System.IO.StreamWriter(stream, System.Text.Encoding.UTF8) 
                        { 
                            AutoFlush = true 
                        };
                        
                        _connectionReady.SetResult(true);
                    }
                    catch (Exception ex)
                    {
                        _connectionReady.SetException(ex);
                    }
                });
                
                // Return immediately for server - connection will be established when client connects
                return;
            }
            else
            {
                // Client mode: connect to server
                _tcpClient = new System.Net.Sockets.TcpClient();
                await _tcpClient.ConnectAsync(
                    Config.Host ?? "localhost", 
                    Config.Port, 
                    cancellationToken).ConfigureAwait(false);
                    
                // Setup streams
                var stream = _tcpClient.GetStream();
                _reader = new System.IO.StreamReader(stream, System.Text.Encoding.UTF8);
                _writer = new System.IO.StreamWriter(stream, System.Text.Encoding.UTF8) 
                { 
                    AutoFlush = true 
                };
            }
        }

        public override async Task DisconnectAsync(CancellationToken cancellationToken)
        {
            try
            {
                _writer?.Close();
                _reader?.Close();
                _tcpClient?.Close();
                _tcpListener?.Stop();
            }
            catch { }
            
            _writer = null;
            _reader = null;
            _tcpClient = null;
            _tcpListener = null;
            
            await Task.CompletedTask;
        }

        public override async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken)
        {
            // For server, wait for connection to be established
            if (_isServer && !_connectionReady.Task.IsCompleted)
            {
                await _connectionReady.Task.ConfigureAwait(false);
            }
            
            if (_writer == null)
                throw new InvalidOperationException("Not connected");

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                var json = System.Text.Json.JsonSerializer.Serialize(message);
                await _writer.WriteLineAsync(json).ConfigureAwait(false);
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public override async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken)
        {
            // For server, wait for connection to be established
            if (_isServer && !_connectionReady.Task.IsCompleted)
            {
                await _connectionReady.Task.ConfigureAwait(false);
            }
            
            if (_reader == null)
                throw new InvalidOperationException("Not connected");

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                var line = await _reader.ReadLineAsync().ConfigureAwait(false);
                if (line == null)
                {
                    throw new System.IO.EndOfStreamException("Connection closed");
                }

                var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(line);
                return message ?? new JsonRpcMessage();
            }
            finally
            {
                _receiveLock.Release();
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                DisconnectAsync(CancellationToken.None).Wait(1000);
                _sendLock?.Dispose();
                _receiveLock?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    internal class UdpProtocolTransport : ProtocolTransportBase
    {
        private System.Net.Sockets.UdpClient? _udpClient;
        private System.Net.IPEndPoint? _remoteEndPoint;
        private readonly bool _isServer;
        private readonly SemaphoreSlim _sendLock = new(1, 1);
        private readonly SemaphoreSlim _receiveLock = new(1, 1);
        private bool _isConnected;

        public UdpProtocolTransport(TransportConfig config) : base(config) 
        {
            _isServer = config.IsServer;
        }

        public override bool IsConnected => _isConnected;

        public override async Task ConnectAsync(CancellationToken cancellationToken)
        {
            var ipAddress = Config.Host == "localhost" || string.IsNullOrEmpty(Config.Host) 
                ? System.Net.IPAddress.Loopback 
                : System.Net.IPAddress.Parse(Config.Host);

            if (_isServer)
            {
                // Server mode: bind to local endpoint
                var localEndPoint = new System.Net.IPEndPoint(ipAddress, Config.Port);
                _udpClient = new System.Net.Sockets.UdpClient(localEndPoint);
            }
            else
            {
                // Client mode: connect to remote endpoint
                _udpClient = new System.Net.Sockets.UdpClient();
                _remoteEndPoint = new System.Net.IPEndPoint(ipAddress, Config.Port);
                _udpClient.Connect(_remoteEndPoint);
            }
            
            _isConnected = true;
            await Task.CompletedTask;
        }

        public override async Task DisconnectAsync(CancellationToken cancellationToken)
        {
            try
            {
                _udpClient?.Close();
                _isConnected = false;
            }
            catch { }
            
            _udpClient = null;
            _remoteEndPoint = null;
            
            await Task.CompletedTask;
        }

        public override async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken)
        {
            if (_udpClient == null)
                throw new InvalidOperationException("Not connected");

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                var json = System.Text.Json.JsonSerializer.Serialize(message);
                var data = System.Text.Encoding.UTF8.GetBytes(json);
                
                if (_isServer)
                {
                    // For server, we need to know the client endpoint
                    // In a real implementation, this would be stored from previous receives
                    // For now, we'll throw an exception as server-initiated sends need client tracking
                    throw new InvalidOperationException("UDP server cannot send without client endpoint");
                }
                else
                {
                    await _udpClient.SendAsync(data, data.Length).ConfigureAwait(false);
                }
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public override async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken)
        {
            if (_udpClient == null)
                throw new InvalidOperationException("Not connected");

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                var result = await _udpClient.ReceiveAsync().ConfigureAwait(false);
                
                // Store the remote endpoint for potential replies (server mode)
                if (_isServer && _remoteEndPoint == null)
                {
                    _remoteEndPoint = result.RemoteEndPoint;
                }
                
                var json = System.Text.Encoding.UTF8.GetString(result.Buffer);
                var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(json);
                return message ?? new JsonRpcMessage();
            }
            finally
            {
                _receiveLock.Release();
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                DisconnectAsync(CancellationToken.None).Wait(1000);
                _sendLock?.Dispose();
                _receiveLock?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    internal class StdioProtocolTransport : ProtocolTransportBase
    {
        private System.IO.StreamWriter? _writer;
        private System.IO.StreamReader? _reader;
        private readonly SemaphoreSlim _sendLock = new(1, 1);
        private readonly SemaphoreSlim _receiveLock = new(1, 1);
        private bool _isConnected;

        public StdioProtocolTransport(TransportConfig config) : base(config) { }

        public override bool IsConnected => _isConnected;

        public override async Task ConnectAsync(CancellationToken cancellationToken)
        {
            // Setup stdin/stdout streams
            _reader = new System.IO.StreamReader(Console.OpenStandardInput(), System.Text.Encoding.UTF8);
            _writer = new System.IO.StreamWriter(Console.OpenStandardOutput(), System.Text.Encoding.UTF8) 
            { 
                AutoFlush = true 
            };
            
            _isConnected = true;
            await Task.CompletedTask;
        }

        public override async Task DisconnectAsync(CancellationToken cancellationToken)
        {
            try
            {
                _writer?.Close();
                _reader?.Close();
                _isConnected = false;
            }
            catch { }
            
            _writer = null;
            _reader = null;
            
            await Task.CompletedTask;
        }

        public override async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken)
        {
            if (_writer == null)
                throw new InvalidOperationException("Not connected");

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                var json = System.Text.Json.JsonSerializer.Serialize(message);
                await _writer.WriteLineAsync(json).ConfigureAwait(false);
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public override async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken)
        {
            if (_reader == null)
                throw new InvalidOperationException("Not connected");

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                var line = await _reader.ReadLineAsync().ConfigureAwait(false);
                if (line == null)
                {
                    throw new System.IO.EndOfStreamException("Input stream closed");
                }

                var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(line);
                return message ?? new JsonRpcMessage();
            }
            finally
            {
                _receiveLock.Release();
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                DisconnectAsync(CancellationToken.None).Wait(1000);
                _sendLock?.Dispose();
                _receiveLock?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    internal class HttpProtocolTransport : ProtocolTransportBase
    {
        private System.Net.Http.HttpClient? _httpClient;
        private System.Net.HttpListener? _httpListener;
        private readonly bool _isServer;
        private readonly SemaphoreSlim _sendLock = new(1, 1);
        private readonly SemaphoreSlim _receiveLock = new(1, 1);
        private readonly Channel<JsonRpcMessage> _receiveQueue;
        private bool _isConnected;
        private Task? _listenTask;
        private CancellationTokenSource? _listenCancellation;

        public HttpProtocolTransport(TransportConfig config) : base(config) 
        {
            _isServer = config.IsServer;
            
            // Create receive queue for HTTP requests/responses
            var receiveChannelOptions = new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false
            };
            _receiveQueue = Channel.CreateUnbounded<JsonRpcMessage>(receiveChannelOptions);
        }

        public override bool IsConnected => _isConnected;

        public override async Task ConnectAsync(CancellationToken cancellationToken)
        {
            if (_isServer)
            {
                // Server mode: start HTTP listener
                _httpListener = new System.Net.HttpListener();
                var prefix = $"http://{Config.Host}:{Config.Port}/";
                _httpListener.Prefixes.Add(prefix);
                _httpListener.Start();
                
                _listenCancellation = new CancellationTokenSource();
                _listenTask = Task.Run(async () => await ListenForRequests(_listenCancellation.Token));
            }
            else
            {
                // Client mode: create HTTP client
                _httpClient = new System.Net.Http.HttpClient();
                _httpClient.BaseAddress = new Uri($"http://{Config.Host}:{Config.Port}/");
                _httpClient.DefaultRequestHeaders.Add("Content-Type", "application/json");
            }
            
            _isConnected = true;
            await Task.CompletedTask;
        }

        private async Task ListenForRequests(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested && _httpListener?.IsListening == true)
            {
                try
                {
                    var context = await _httpListener.GetContextAsync();
                    _ = Task.Run(async () => await HandleRequest(context), cancellationToken);
                }
                catch (Exception) when (cancellationToken.IsCancellationRequested)
                {
                    break;
                }
                catch
                {
                    // Continue listening on errors
                }
            }
        }

        private async Task HandleRequest(System.Net.HttpListenerContext context)
        {
            try
            {
                using var reader = new System.IO.StreamReader(context.Request.InputStream);
                var json = await reader.ReadToEndAsync();
                
                var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(json);
                if (message != null)
                {
                    await _receiveQueue.Writer.WriteAsync(message);
                }
                
                // Send acknowledgment response
                context.Response.StatusCode = 200;
                context.Response.ContentType = "application/json";
                
                var response = new { status = "received" };
                var responseJson = System.Text.Json.JsonSerializer.Serialize(response);
                var responseBytes = System.Text.Encoding.UTF8.GetBytes(responseJson);
                
                await context.Response.OutputStream.WriteAsync(responseBytes, 0, responseBytes.Length);
                context.Response.Close();
            }
            catch
            {
                context.Response.StatusCode = 500;
                context.Response.Close();
            }
        }

        public override async Task DisconnectAsync(CancellationToken cancellationToken)
        {
            try
            {
                _listenCancellation?.Cancel();
                _httpListener?.Stop();
                _httpClient?.Dispose();
                _isConnected = false;
                
                if (_listenTask != null)
                {
                    await _listenTask;
                }
            }
            catch { }
            
            _httpClient = null;
            _httpListener = null;
            _listenTask = null;
            _listenCancellation?.Dispose();
            _listenCancellation = null;
            
            await Task.CompletedTask;
        }

        public override async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken)
        {
            if (!_isConnected)
                throw new InvalidOperationException("Not connected");

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                var json = System.Text.Json.JsonSerializer.Serialize(message);
                
                if (_isServer)
                {
                    // Server sending to client - add to receive queue for now
                    // In a real implementation, this would need client endpoint tracking
                    throw new InvalidOperationException("HTTP server cannot initiate sends without client tracking");
                }
                else
                {
                    // Client sending to server
                    var content = new System.Net.Http.StringContent(json, System.Text.Encoding.UTF8, "application/json");
                    var response = await _httpClient!.PostAsync("", content, cancellationToken);
                    response.EnsureSuccessStatusCode();
                }
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public override async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken)
        {
            if (!_isConnected)
                throw new InvalidOperationException("Not connected");

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                if (_isServer)
                {
                    // Server receiving from queue
                    return await _receiveQueue.Reader.ReadAsync(cancellationToken);
                }
                else
                {
                    // HTTP client typically doesn't receive unsolicited messages
                    // This would be used for request/response patterns
                    throw new InvalidOperationException("HTTP client receive not implemented for unsolicited messages");
                }
            }
            finally
            {
                _receiveLock.Release();
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                DisconnectAsync(CancellationToken.None).Wait(1000);
                _sendLock?.Dispose();
                _receiveLock?.Dispose();
                _receiveQueue?.Writer.TryComplete();
                _listenCancellation?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    internal class WebSocketProtocolTransport : ProtocolTransportBase
    {
        private System.Net.WebSockets.ClientWebSocket? _clientWebSocket;
        private System.Net.WebSockets.WebSocket? _serverWebSocket;
        private System.Net.HttpListener? _httpListener;
        private readonly bool _isServer;
        private readonly SemaphoreSlim _sendLock = new(1, 1);
        private readonly SemaphoreSlim _receiveLock = new(1, 1);
        private bool _isConnected;
        private Task? _listenTask;
        private CancellationTokenSource? _listenCancellation;

        public WebSocketProtocolTransport(TransportConfig config) : base(config) 
        {
            _isServer = config.IsServer;
        }

        public override bool IsConnected => _isConnected;

        public override async Task ConnectAsync(CancellationToken cancellationToken)
        {
            if (_isServer)
            {
                // Server mode: start HTTP listener for WebSocket upgrade requests
                _httpListener = new System.Net.HttpListener();
                var prefix = $"http://{Config.Host}:{Config.Port}/";
                _httpListener.Prefixes.Add(prefix);
                _httpListener.Start();
                
                _listenCancellation = new CancellationTokenSource();
                _listenTask = Task.Run(async () => await ListenForWebSocketConnections(_listenCancellation.Token));
            }
            else
            {
                // Client mode: connect to WebSocket server
                _clientWebSocket = new System.Net.WebSockets.ClientWebSocket();
                var uri = new Uri($"ws://{Config.Host}:{Config.Port}/");
                await _clientWebSocket.ConnectAsync(uri, cancellationToken);
            }
            
            _isConnected = true;
        }

        private async Task ListenForWebSocketConnections(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested && _httpListener?.IsListening == true)
            {
                try
                {
                    var context = await _httpListener.GetContextAsync();
                    if (context.Request.IsWebSocketRequest)
                    {
                        var wsContext = await context.AcceptWebSocketAsync(null);
                        _serverWebSocket = wsContext.WebSocket;
                        _isConnected = true;
                        return; // Accept first connection for simplicity
                    }
                    else
                    {
                        context.Response.StatusCode = 400;
                        context.Response.Close();
                    }
                }
                catch (Exception) when (cancellationToken.IsCancellationRequested)
                {
                    break;
                }
                catch
                {
                    // Continue listening on errors
                }
            }
        }

        public override async Task DisconnectAsync(CancellationToken cancellationToken)
        {
            try
            {
                _listenCancellation?.Cancel();
                
                if (_clientWebSocket?.State == System.Net.WebSockets.WebSocketState.Open)
                {
                    await _clientWebSocket.CloseAsync(
                        System.Net.WebSockets.WebSocketCloseStatus.NormalClosure, 
                        "Closing", 
                        cancellationToken);
                }
                
                if (_serverWebSocket?.State == System.Net.WebSockets.WebSocketState.Open)
                {
                    await _serverWebSocket.CloseAsync(
                        System.Net.WebSockets.WebSocketCloseStatus.NormalClosure, 
                        "Closing", 
                        cancellationToken);
                }
                
                _httpListener?.Stop();
                _isConnected = false;
                
                if (_listenTask != null)
                {
                    await _listenTask;
                }
            }
            catch { }
            
            _clientWebSocket?.Dispose();
            _serverWebSocket?.Dispose();
            _httpListener = null;
            _listenTask = null;
            _listenCancellation?.Dispose();
            _listenCancellation = null;
            _clientWebSocket = null;
            _serverWebSocket = null;
        }

        public override async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken)
        {
            var webSocket = _clientWebSocket ?? _serverWebSocket;
            if (webSocket?.State != System.Net.WebSockets.WebSocketState.Open)
                throw new InvalidOperationException("WebSocket not connected");

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                var json = System.Text.Json.JsonSerializer.Serialize(message);
                var bytes = System.Text.Encoding.UTF8.GetBytes(json);
                var segment = new ArraySegment<byte>(bytes);
                
                await webSocket.SendAsync(
                    segment, 
                    System.Net.WebSockets.WebSocketMessageType.Text, 
                    true, 
                    cancellationToken);
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public override async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken)
        {
            var webSocket = _clientWebSocket ?? _serverWebSocket;
            if (webSocket?.State != System.Net.WebSockets.WebSocketState.Open)
                throw new InvalidOperationException("WebSocket not connected");

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                var buffer = new byte[Config.MaxMessageSize];
                var segment = new ArraySegment<byte>(buffer);
                
                var result = await webSocket.ReceiveAsync(segment, cancellationToken);
                
                if (result.MessageType == System.Net.WebSockets.WebSocketMessageType.Text)
                {
                    var json = System.Text.Encoding.UTF8.GetString(buffer, 0, result.Count);
                    var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(json);
                    return message ?? new JsonRpcMessage();
                }
                else if (result.MessageType == System.Net.WebSockets.WebSocketMessageType.Close)
                {
                    throw new System.IO.EndOfStreamException("WebSocket closed");
                }
                
                return new JsonRpcMessage();
            }
            finally
            {
                _receiveLock.Release();
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                DisconnectAsync(CancellationToken.None).Wait(1000);
                _sendLock?.Dispose();
                _receiveLock?.Dispose();
                _listenCancellation?.Dispose();
                _clientWebSocket?.Dispose();
                _serverWebSocket?.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
