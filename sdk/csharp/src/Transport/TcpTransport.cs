using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Integration;

namespace GopherMcp.Transport
{
    /// <summary>
    /// TCP transport implementation for MCP
    /// </summary>
    public class TcpTransport : ITransport
    {
        private readonly string _host;
        private readonly int _port;
        private readonly int _sendBufferSize;
        private readonly int _receiveBufferSize;
        private readonly bool _keepAlive;
        private readonly TimeSpan _keepAliveInterval;

        private TcpClient? _tcpClient;
        private NetworkStream? _networkStream;
        private StreamReader? _reader;
        private StreamWriter? _writer;
        private ConnectionState _state;
        private readonly SemaphoreSlim _sendLock;
        private readonly SemaphoreSlim _receiveLock;
        private bool _disposed;

        public ConnectionState State => _state;
        public bool IsConnected => _state == ConnectionState.Connected && _tcpClient?.Connected == true;

        public event EventHandler<MessageReceivedEventArgs>? MessageReceived;
        public event EventHandler<TransportErrorEventArgs>? Error;
        public event EventHandler<ConnectionStateEventArgs>? Connected;
        public event EventHandler<ConnectionStateEventArgs>? Disconnected;

        public TcpTransport(string host, int port, int sendBufferSize = 8192, int receiveBufferSize = 8192,
                           bool keepAlive = true, TimeSpan? keepAliveInterval = null)
        {
            if (string.IsNullOrWhiteSpace(host))
                throw new ArgumentException("Host cannot be null or empty", nameof(host));
            if (port <= 0 || port > 65535)
                throw new ArgumentException("Port must be between 1 and 65535", nameof(port));

            _host = host;
            _port = port;
            _sendBufferSize = sendBufferSize;
            _receiveBufferSize = receiveBufferSize;
            _keepAlive = keepAlive;
            _keepAliveInterval = keepAliveInterval ?? TimeSpan.FromSeconds(30);
            _state = ConnectionState.Disconnected;
            _sendLock = new SemaphoreSlim(1, 1);
            _receiveLock = new SemaphoreSlim(1, 1);
        }

        public async Task StartAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            if (_state == ConnectionState.Connected || _state == ConnectionState.Connecting)
                return;

            var oldState = _state;
            _state = ConnectionState.Connecting;
            OnConnectionStateChanged(ConnectionState.Connecting, oldState);

            try
            {
                _tcpClient = new TcpClient
                {
                    SendBufferSize = _sendBufferSize,
                    ReceiveBufferSize = _receiveBufferSize,
                    NoDelay = true
                };

                // Connect with timeout
                var connectTask = _tcpClient.ConnectAsync(_host, _port);
                var timeoutTask = Task.Delay(TimeSpan.FromSeconds(30), cancellationToken);

                var completedTask = await Task.WhenAny(connectTask, timeoutTask);

                if (completedTask == timeoutTask)
                {
                    _tcpClient.Close();
                    throw new TimeoutException($"Connection to {_host}:{_port} timed out");
                }

                await connectTask;

                // Configure keep-alive
                if (_keepAlive)
                {
                    _tcpClient.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
                    ConfigureKeepAlive();
                }

                _networkStream = _tcpClient.GetStream();
                _reader = new StreamReader(_networkStream, Encoding.UTF8, detectEncodingFromByteOrderMarks: false,
                                         bufferSize: _receiveBufferSize, leaveOpen: true);
                _writer = new StreamWriter(_networkStream, Encoding.UTF8, bufferSize: _sendBufferSize, leaveOpen: true)
                {
                    AutoFlush = true
                };

                _state = ConnectionState.Connected;
                OnConnectionStateChanged(ConnectionState.Connected, ConnectionState.Connecting);
            }
            catch (Exception ex)
            {
                _state = ConnectionState.Failed;
                OnConnectionStateChanged(ConnectionState.Failed, ConnectionState.Connecting);
                OnError(ex, "Failed to connect");
                Cleanup();
                throw;
            }
        }

        public async Task StopAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            if (_state == ConnectionState.Disconnected || _state == ConnectionState.Disconnecting)
                return;

            var oldState = _state;
            _state = ConnectionState.Disconnecting;
            OnConnectionStateChanged(ConnectionState.Disconnecting, oldState);

            try
            {
                if (_writer != null)
                {
                    await _writer.FlushAsync();
                }

                Cleanup();

                _state = ConnectionState.Disconnected;
                OnConnectionStateChanged(ConnectionState.Disconnected, ConnectionState.Disconnecting);
            }
            catch (Exception ex)
            {
                OnError(ex, "Error during disconnect");
                _state = ConnectionState.Disconnected;
                throw;
            }
        }

        public async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            if (!IsConnected)
                throw new InvalidOperationException("Not connected");

            if (message == null)
                throw new ArgumentNullException(nameof(message));

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                if (_writer == null)
                    throw new InvalidOperationException("Writer not initialized");

                var json = JsonSerializer.Serialize(message);

                // Send message with newline delimiter for line-based protocol
                await _writer.WriteLineAsync(json);
                await _writer.FlushAsync();
            }
            catch (Exception ex)
            {
                OnError(ex, "Error sending message");
                throw;
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            if (!IsConnected)
                throw new InvalidOperationException("Not connected");

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                if (_reader == null)
                    throw new InvalidOperationException("Reader not initialized");

                // Read line-based message
                var json = await _reader.ReadLineAsync();

                if (json == null)
                {
                    // End of stream - connection closed
                    throw new EndOfStreamException("Connection closed by remote host");
                }

                var message = JsonSerializer.Deserialize<JsonRpcMessage>(json);
                if (message == null)
                    throw new InvalidOperationException("Failed to deserialize message");

                OnMessageReceived(message);
                return message;
            }
            catch (Exception ex)
            {
                OnError(ex, "Error receiving message");
                throw;
            }
            finally
            {
                _receiveLock.Release();
            }
        }

        private void ConfigureKeepAlive()
        {
            if (_tcpClient?.Client == null)
                return;

            // Platform-specific keep-alive configuration
#if NET5_0_OR_GREATER
            if (OperatingSystem.IsWindows())
#else
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
#endif
            {
                // Windows TCP keep-alive
                var keepAliveTime = (uint)_keepAliveInterval.TotalMilliseconds;
                var keepAliveInterval = (uint)(_keepAliveInterval.TotalMilliseconds / 3);

                var input = BitConverter.GetBytes((uint)1)  // on/off
                    .Concat(BitConverter.GetBytes(keepAliveTime))
                    .Concat(BitConverter.GetBytes(keepAliveInterval))
                    .ToArray();

                _tcpClient.Client.IOControl(IOControlCode.KeepAliveValues, input, null);
            }
#if NET5_0_OR_GREATER
            else if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
#else
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) || 
                     RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
#endif
            {
                // Unix-like keep-alive (requires platform-specific constants)
                // This is simplified - actual implementation would use platform-specific socket options
                _tcpClient.Client.SetSocketOption(SocketOptionLevel.Tcp, SocketOptionName.KeepAlive, true);
            }
        }

        private void Cleanup()
        {
            try
            {
                _writer?.Close();
                _writer?.Dispose();
                _writer = null;
            }
            catch { }

            try
            {
                _reader?.Close();
                _reader?.Dispose();
                _reader = null;
            }
            catch { }

            try
            {
                _networkStream?.Close();
                _networkStream?.Dispose();
                _networkStream = null;
            }
            catch { }

            try
            {
                _tcpClient?.Close();
                _tcpClient?.Dispose();
                _tcpClient = null;
            }
            catch { }
        }

        private void OnConnectionStateChanged(ConnectionState newState, ConnectionState oldState)
        {
            var args = new ConnectionStateEventArgs(newState, oldState);

            if (newState == ConnectionState.Connected)
                Connected?.Invoke(this, args);
            else if (newState == ConnectionState.Disconnected || newState == ConnectionState.Failed)
                Disconnected?.Invoke(this, args);
        }

        private void OnMessageReceived(JsonRpcMessage message)
        {
            MessageReceived?.Invoke(this, new MessageReceivedEventArgs(message));
        }

        private void OnError(Exception exception, string context)
        {
            Error?.Invoke(this, new TransportErrorEventArgs(exception, context));
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(TcpTransport));
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                try
                {
                    if (IsConnected)
                    {
                        StopAsync().GetAwaiter().GetResult();
                    }
                }
                catch { }

                Cleanup();

                _sendLock?.Dispose();
                _receiveLock?.Dispose();

                _disposed = true;
            }
        }
    }
}
