using System;
using System.IO;
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
    /// TCP server transport implementation that listens for incoming connections
    /// </summary>
    public class TcpServerTransport : ITransport
    {
        private readonly string _host;
        private readonly int _port;
        private readonly int _sendBufferSize;
        private readonly int _receiveBufferSize;
        private readonly bool _keepAlive;
        private readonly TimeSpan _keepAliveInterval;

        private TcpListener? _tcpListener;
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

        public TcpServerTransport(string host, int port, int sendBufferSize = 8192, int receiveBufferSize = 8192,
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
                // Create and start TCP listener
                var ipAddress = _host == "localhost" ? IPAddress.Loopback : IPAddress.Parse(_host);
                _tcpListener = new TcpListener(ipAddress, _port);
                _tcpListener.Start();

                // Start accepting connections in the background
                _ = Task.Run(async () => await AcceptConnectionAsync(cancellationToken));
                
                // Return immediately - the server is now listening
                await Task.CompletedTask;
            }
            catch (Exception ex)
            {
                _state = ConnectionState.Failed;
                OnConnectionStateChanged(ConnectionState.Failed, ConnectionState.Connecting);
                OnError(ex, "Failed to start server");
                Cleanup();
                throw;
            }
        }

        private async Task AcceptConnectionAsync(CancellationToken cancellationToken)
        {
            try
            {
                // Wait for a client to connect
                _tcpClient = await _tcpListener.AcceptTcpClientAsync().ConfigureAwait(false);

                // Configure the accepted client
                _tcpClient.SendBufferSize = _sendBufferSize;
                _tcpClient.ReceiveBufferSize = _receiveBufferSize;
                _tcpClient.NoDelay = true;

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
                OnError(ex, "Failed to accept connection");
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
            }
        }

        public async Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            // Wait for connection if not connected
            while (!IsConnected && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(100, cancellationToken);
            }

            ThrowIfNotConnected();

            await _receiveLock.WaitAsync(cancellationToken);
            try
            {
                if (_reader == null)
                    throw new InvalidOperationException("Reader not initialized");

                var line = await _reader.ReadLineAsync().ConfigureAwait(false);
                
                if (line == null)
                {
                    throw new EndOfStreamException("Connection closed by remote host");
                }

                if (string.IsNullOrWhiteSpace(line))
                {
                    // Empty line, skip and try again
                    return await ReceiveAsync(cancellationToken);
                }

                var message = JsonSerializer.Deserialize<JsonRpcMessage>(line);
                if (message == null)
                {
                    throw new InvalidOperationException("Failed to deserialize message");
                }

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

        public async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            // Wait for connection if not connected
            while (!IsConnected && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(100, cancellationToken);
            }

            ThrowIfNotConnected();

            if (message == null)
                throw new ArgumentNullException(nameof(message));

            await _sendLock.WaitAsync(cancellationToken);
            try
            {
                if (_writer == null)
                    throw new InvalidOperationException("Writer not initialized");

                var json = JsonSerializer.Serialize(message);
                await _writer.WriteLineAsync(json).ConfigureAwait(false);
                await _writer.FlushAsync().ConfigureAwait(false);
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

        private void ConfigureKeepAlive()
        {
            if (_tcpClient?.Client == null || !_tcpClient.Connected)
                return;

            try
            {
                // Platform-specific keep-alive configuration
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                {
                    // Windows-specific TCP keep-alive settings
                    var keepAliveTime = (uint)_keepAliveInterval.TotalMilliseconds;
                    var keepAliveInterval = (uint)(_keepAliveInterval.TotalMilliseconds / 3);
                    
                    var inOptionValues = new byte[12];
                    BitConverter.GetBytes((uint)1).CopyTo(inOptionValues, 0);  // on/off
                    BitConverter.GetBytes(keepAliveTime).CopyTo(inOptionValues, 4);  // time
                    BitConverter.GetBytes(keepAliveInterval).CopyTo(inOptionValues, 8);  // interval
                    
                    _tcpClient.Client.IOControl(IOControlCode.KeepAliveValues, inOptionValues, null);
                }
                else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) || RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                {
                    // Unix-like keep-alive - already enabled at socket level
                    // Platform-specific TCP keep-alive parameters would require P/Invoke
                }
            }
            catch (Exception ex)
            {
                // Log but don't fail - keep-alive is optional
                OnError(ex, "Failed to configure keep-alive");
            }
        }

        private void Cleanup()
        {
            try
            {
                _writer?.Dispose();
                _reader?.Dispose();
                _networkStream?.Dispose();
                _tcpClient?.Close();
                _tcpListener?.Stop();
            }
            catch { }
            finally
            {
                _writer = null;
                _reader = null;
                _networkStream = null;
                _tcpClient = null;
                _tcpListener = null;
            }
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(TcpServerTransport));
        }

        private void ThrowIfNotConnected()
        {
            if (!IsConnected)
                throw new InvalidOperationException("Transport is not connected");
        }

        private void OnMessageReceived(JsonRpcMessage message)
        {
            MessageReceived?.Invoke(this, new MessageReceivedEventArgs(message));
        }

        private void OnError(Exception exception, string? context)
        {
            Error?.Invoke(this, new TransportErrorEventArgs(exception, context));
        }

        private void OnConnectionStateChanged(ConnectionState newState, ConnectionState oldState)
        {
            var args = new ConnectionStateEventArgs(newState, oldState);
            
            if (newState == ConnectionState.Connected)
            {
                Connected?.Invoke(this, args);
            }
            else if (newState == ConnectionState.Disconnected || newState == ConnectionState.Failed)
            {
                Disconnected?.Invoke(this, args);
            }
        }

        public void Dispose()
        {
            if (_disposed)
                return;

            _disposed = true;
            
            try
            {
                StopAsync().GetAwaiter().GetResult();
            }
            catch { }
            
            Cleanup();
            _sendLock?.Dispose();
            _receiveLock?.Dispose();
        }
    }
}