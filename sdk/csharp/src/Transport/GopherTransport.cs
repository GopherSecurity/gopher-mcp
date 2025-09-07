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
            // Implementation will be added in prompt #71
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _shutdownTokenSource = new CancellationTokenSource();
            _stateLock = new SemaphoreSlim(1, 1);
            _state = ConnectionState.Disconnected;

            // Create channels for message queuing
            var channelOptions = new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false
            };
            _receiveQueue = Channel.CreateUnbounded<JsonRpcMessage>(channelOptions);
            _sendQueue = Channel.CreateUnbounded<JsonRpcMessage>(channelOptions);

            // Initialize filter manager if configured
            if (_config.Filters != null)
            {
                _filterManager = new FilterManager(_config.Filters);
            }

            // Select protocol implementation
            SelectProtocolImplementation();
        }

        public async Task StartAsync(CancellationToken cancellationToken = default)
        {
            // Implementation will be added in prompt #72
            await Task.CompletedTask;
        }

        public async Task StopAsync(CancellationToken cancellationToken = default)
        {
            // Implementation will be added in prompt #73
            await Task.CompletedTask;
        }

        public async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
        {
            // Implementation will be added in prompt #74
            await Task.CompletedTask;
        }

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