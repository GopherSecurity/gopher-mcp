using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Integration;
#if !NET6_0_OR_GREATER
using GopherMcp.Utils;
#endif

namespace GopherMcp.Transport
{
    /// <summary>
    /// UDP transport implementation for MCP
    /// </summary>
    public class UdpTransport : ITransport
    {
        private readonly string _host;
        private readonly int _port;
        private readonly int _localPort;
        private readonly int _maxPacketSize;
        private readonly TimeSpan _receiveTimeout;
        
        private UdpClient? _udpClient;
        private IPEndPoint? _remoteEndPoint;
        private ConnectionState _state;
        private readonly ConcurrentQueue<JsonRpcMessage> _receiveQueue;
        private readonly CancellationTokenSource _shutdownTokenSource;
        private Task? _receiveTask;
        private readonly SemaphoreSlim _sendLock;
        private bool _disposed;

        // Message fragmentation support
        private readonly ConcurrentDictionary<string, MessageFragment> _fragmentBuffer;
        private readonly Timer _fragmentCleanupTimer;

        public ConnectionState State => _state;
        public bool IsConnected => _state == ConnectionState.Connected;

        public event EventHandler<MessageReceivedEventArgs>? MessageReceived;
        public event EventHandler<TransportErrorEventArgs>? Error;
        public event EventHandler<ConnectionStateEventArgs>? Connected;
        public event EventHandler<ConnectionStateEventArgs>? Disconnected;

        public UdpTransport(string host, int port, int localPort = 0, int maxPacketSize = 65507, 
                           TimeSpan? receiveTimeout = null)
        {
            if (string.IsNullOrWhiteSpace(host))
                throw new ArgumentException("Host cannot be null or empty", nameof(host));
            if (port <= 0 || port > 65535)
                throw new ArgumentException("Port must be between 1 and 65535", nameof(port));
            if (maxPacketSize <= 0 || maxPacketSize > 65507)
                throw new ArgumentException("Max packet size must be between 1 and 65507", nameof(maxPacketSize));
            
            _host = host;
            _port = port;
            _localPort = localPort;
            _maxPacketSize = maxPacketSize;
            _receiveTimeout = receiveTimeout ?? TimeSpan.FromSeconds(30);
            _state = ConnectionState.Disconnected;
            _receiveQueue = new ConcurrentQueue<JsonRpcMessage>();
            _shutdownTokenSource = new CancellationTokenSource();
            _sendLock = new SemaphoreSlim(1, 1);
            _fragmentBuffer = new ConcurrentDictionary<string, MessageFragment>();
            
            // Cleanup old fragments every minute
            _fragmentCleanupTimer = new Timer(CleanupFragments, null, TimeSpan.FromMinutes(1), TimeSpan.FromMinutes(1));
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
                // Create UDP client
                if (_localPort > 0)
                {
                    _udpClient = new UdpClient(_localPort);
                }
                else
                {
                    _udpClient = new UdpClient();
                }

                // Set receive timeout
                _udpClient.Client.ReceiveTimeout = (int)_receiveTimeout.TotalMilliseconds;

                // Resolve remote endpoint
                var addresses = await Dns.GetHostAddressesAsync(_host);
                if (addresses.Length == 0)
                    throw new InvalidOperationException($"Could not resolve host: {_host}");

                _remoteEndPoint = new IPEndPoint(addresses[0], _port);

                // Start receive task
                _receiveTask = Task.Run(ReceiveLoop, _shutdownTokenSource.Token);

                // Send initial handshake/ping message to establish "connection"
                var handshake = new JsonRpcMessage
                {
                    JsonRpc = "2.0",
                    Method = "ping",
                    Id = Guid.NewGuid().ToString()
                };
                
                await SendAsync(handshake, cancellationToken);

                _state = ConnectionState.Connected;
                OnConnectionStateChanged(ConnectionState.Connected, ConnectionState.Connecting);
            }
            catch (Exception ex)
            {
                _state = ConnectionState.Failed;
                OnConnectionStateChanged(ConnectionState.Failed, ConnectionState.Connecting);
                OnError(ex, "Failed to start UDP transport");
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
                // Cancel receive loop
                _shutdownTokenSource.Cancel();

                // Wait for receive task to complete
                if (_receiveTask != null)
                {
                    try
                    {
                        await _receiveTask.WaitAsync(TimeSpan.FromSeconds(5), cancellationToken);
                    }
                    catch (TimeoutException)
                    {
                        // Receive task didn't complete in time
                    }
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
                if (_udpClient == null || _remoteEndPoint == null)
                    throw new InvalidOperationException("UDP client not initialized");

                var json = System.Text.Json.JsonSerializer.Serialize(message);
                var data = Encoding.UTF8.GetBytes(json);

                // Check if fragmentation is needed
                if (data.Length > _maxPacketSize)
                {
                    await SendFragmentedAsync(message, data, cancellationToken);
                }
                else
                {
                    // Send as single packet
                    await _udpClient.SendAsync(data, data.Length, _remoteEndPoint);
                }
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

            // Try to dequeue a message
            while (!cancellationToken.IsCancellationRequested)
            {
                if (_receiveQueue.TryDequeue(out var message))
                {
                    return message;
                }

                // Wait a bit before trying again
                await Task.Delay(10, cancellationToken);
            }

            throw new OperationCanceledException("Receive cancelled");
        }

        private async Task ReceiveLoop()
        {
            var buffer = new byte[65507]; // Maximum UDP packet size

            while (!_shutdownTokenSource.Token.IsCancellationRequested)
            {
                try
                {
                    if (_udpClient == null)
                        break;

                    // Receive datagram
                    var result = await _udpClient.ReceiveAsync();
                    
                    // Process received data
                    var json = Encoding.UTF8.GetString(result.Buffer);
                    
                    // Check if this is a fragment
                    if (json.StartsWith("FRAG:"))
                    {
                        ProcessFragment(json);
                    }
                    else
                    {
                        // Complete message
                        var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(json);
                        if (message != null)
                        {
                            _receiveQueue.Enqueue(message);
                            OnMessageReceived(message);
                        }
                    }
                }
                catch (SocketException ex) when (ex.SocketErrorCode == SocketError.TimedOut)
                {
                    // Timeout is normal, continue
                    continue;
                }
                catch (ObjectDisposedException)
                {
                    // Socket was closed, exit loop
                    break;
                }
                catch (Exception ex)
                {
                    OnError(ex, "Error in receive loop");
                    
                    // Continue unless it's a fatal error
                    if (IsFatalError(ex))
                        break;
                }
            }
        }

        private async Task SendFragmentedAsync(JsonRpcMessage message, byte[] data, CancellationToken cancellationToken)
        {
            var messageId = Guid.NewGuid().ToString();
            var totalFragments = (int)Math.Ceiling((double)data.Length / _maxPacketSize);

            for (int i = 0; i < totalFragments; i++)
            {
                var offset = i * _maxPacketSize;
                var length = Math.Min(_maxPacketSize, data.Length - offset);
                var fragment = new byte[length];
                Array.Copy(data, offset, fragment, 0, length);

                // Create fragment header
                var header = $"FRAG:{messageId}:{i}:{totalFragments}:";
                var headerBytes = Encoding.UTF8.GetBytes(header);
                
                // Combine header and fragment
                var packet = new byte[headerBytes.Length + fragment.Length];
                Array.Copy(headerBytes, 0, packet, 0, headerBytes.Length);
                Array.Copy(fragment, 0, packet, headerBytes.Length, fragment.Length);

                await _udpClient!.SendAsync(packet, packet.Length, _remoteEndPoint);
                
                // Small delay between fragments to avoid overwhelming the receiver
                if (i < totalFragments - 1)
                    await Task.Delay(1, cancellationToken);
            }
        }

        private void ProcessFragment(string fragmentData)
        {
            try
            {
                // Parse fragment header: FRAG:messageId:index:total:data
                var parts = fragmentData.Split(':', 5);
                if (parts.Length != 5)
                    return;

                var messageId = parts[1];
                var index = int.Parse(parts[2]);
                var total = int.Parse(parts[3]);
                var data = parts[4];

                // Get or create fragment buffer
                var fragment = _fragmentBuffer.GetOrAdd(messageId, _ => new MessageFragment(total));
                
                // Add fragment
                fragment.AddFragment(index, data);

                // Check if complete
                if (fragment.IsComplete)
                {
                    _fragmentBuffer.TryRemove(messageId, out _);
                    
                    var completeJson = fragment.GetCompleteMessage();
                    var message = System.Text.Json.JsonSerializer.Deserialize<JsonRpcMessage>(completeJson);
                    
                    if (message != null)
                    {
                        _receiveQueue.Enqueue(message);
                        OnMessageReceived(message);
                    }
                }
            }
            catch (Exception ex)
            {
                OnError(ex, "Error processing fragment");
            }
        }

        private void CleanupFragments(object? state)
        {
            var cutoff = DateTime.UtcNow.AddMinutes(-5);
            var keysToRemove = new List<string>();

            foreach (var kvp in _fragmentBuffer)
            {
                if (kvp.Value.CreatedAt < cutoff)
                {
                    keysToRemove.Add(kvp.Key);
                }
            }

            foreach (var key in keysToRemove)
            {
                _fragmentBuffer.TryRemove(key, out _);
            }
        }

        private bool IsFatalError(Exception ex)
        {
            return ex is SocketException socketEx && 
                   (socketEx.SocketErrorCode == SocketError.NetworkDown ||
                    socketEx.SocketErrorCode == SocketError.NetworkUnreachable ||
                    socketEx.SocketErrorCode == SocketError.HostUnreachable);
        }

        private void Cleanup()
        {
            try
            {
                _udpClient?.Close();
                _udpClient?.Dispose();
                _udpClient = null;
            }
            catch { }

            _receiveQueue.Clear();
            _fragmentBuffer.Clear();
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
                throw new ObjectDisposedException(nameof(UdpTransport));
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

                _fragmentCleanupTimer?.Dispose();
                _shutdownTokenSource?.Cancel();
                _shutdownTokenSource?.Dispose();
                Cleanup();
                _sendLock?.Dispose();
                
                _disposed = true;
            }
        }

        private class MessageFragment
        {
            private readonly string[] _fragments;
            private int _receivedCount;
            
            public DateTime CreatedAt { get; }
            public bool IsComplete => _receivedCount == _fragments.Length;

            public MessageFragment(int totalFragments)
            {
                _fragments = new string[totalFragments];
                CreatedAt = DateTime.UtcNow;
            }

            public void AddFragment(int index, string data)
            {
                if (index >= 0 && index < _fragments.Length && _fragments[index] == null)
                {
                    _fragments[index] = data;
                    Interlocked.Increment(ref _receivedCount);
                }
            }

            public string GetCompleteMessage()
            {
                return string.Concat(_fragments);
            }
        }
    }
}