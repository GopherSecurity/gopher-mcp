using System;
using System.Collections.Concurrent;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace GopherMcp.Transport
{
    /// <summary>
    /// Standard I/O transport implementation for MCP
    /// </summary>
    public class StdioTransport : ITransport
    {
        private readonly TextReader _input;
        private readonly TextWriter _output;
        private readonly bool _ownsStreams;
        private readonly int _bufferSize;
        
        private ConnectionState _state;
        private readonly ConcurrentQueue<JsonRpcMessage> _receiveQueue;
        private readonly CancellationTokenSource _shutdownTokenSource;
        private Task? _receiveTask;
        private readonly SemaphoreSlim _sendLock;
        private readonly SemaphoreSlim _receiveLock;
        private bool _disposed;

        public ConnectionState State => _state;
        public bool IsConnected => _state == ConnectionState.Connected;

        public event EventHandler<MessageReceivedEventArgs>? MessageReceived;
        public event EventHandler<TransportErrorEventArgs>? Error;
        public event EventHandler<ConnectionStateEventArgs>? Connected;
        public event EventHandler<ConnectionStateEventArgs>? Disconnected;

        public StdioTransport(TextReader? input = null, TextWriter? output = null, int bufferSize = 8192)
        {
            _input = input ?? Console.In;
            _output = output ?? Console.Out;
            _ownsStreams = input == null && output == null;
            _bufferSize = bufferSize;
            _state = ConnectionState.Disconnected;
            _receiveQueue = new ConcurrentQueue<JsonRpcMessage>();
            _shutdownTokenSource = new CancellationTokenSource();
            _sendLock = new SemaphoreSlim(1, 1);
            _receiveLock = new SemaphoreSlim(1, 1);
        }

        public Task StartAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            
            if (_state == ConnectionState.Connected || _state == ConnectionState.Connecting)
                return Task.CompletedTask;

            var oldState = _state;
            _state = ConnectionState.Connecting;
            OnConnectionStateChanged(ConnectionState.Connecting, oldState);

            try
            {
                // Start receive task for non-blocking I/O
                _receiveTask = Task.Run(ReceiveLoop, _shutdownTokenSource.Token);

                _state = ConnectionState.Connected;
                OnConnectionStateChanged(ConnectionState.Connected, ConnectionState.Connecting);
                
                return Task.CompletedTask;
            }
            catch (Exception ex)
            {
                _state = ConnectionState.Failed;
                OnConnectionStateChanged(ConnectionState.Failed, ConnectionState.Connecting);
                OnError(ex, "Failed to start stdio transport");
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

                // Flush output
                await _output.FlushAsync();
                
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
                var json = JsonSerializer.Serialize(message, new JsonSerializerOptions
                {
                    WriteIndented = false,
                    DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
                });

                // Write line-based protocol
                await _output.WriteLineAsync(json);
                await _output.FlushAsync();
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
            var buffer = new char[_bufferSize];
            var messageBuilder = new StringBuilder();
            var braceDepth = 0;
            var inString = false;
            var escapeNext = false;

            while (!_shutdownTokenSource.Token.IsCancellationRequested)
            {
                try
                {
                    // Read from input
                    string? line = null;
                    
                    // Use async reading if available
                    if (_input is StreamReader streamReader)
                    {
                        line = await streamReader.ReadLineAsync();
                    }
                    else
                    {
                        // Fallback to synchronous reading on thread pool
                        line = await Task.Run(() => _input.ReadLine(), _shutdownTokenSource.Token);
                    }

                    if (line == null)
                    {
                        // End of input stream
                        break;
                    }

                    // Skip empty lines
                    if (string.IsNullOrWhiteSpace(line))
                        continue;

                    // Try to parse as complete JSON message
                    try
                    {
                        var message = JsonSerializer.Deserialize<JsonRpcMessage>(line);
                        if (message != null)
                        {
                            _receiveQueue.Enqueue(message);
                            OnMessageReceived(message);
                        }
                    }
                    catch (JsonException)
                    {
                        // Not a complete JSON object, might be part of multi-line JSON
                        // This handles pretty-printed JSON
                        messageBuilder.Append(line);
                        
                        // Simple JSON completion detection
                        foreach (char c in line)
                        {
                            if (escapeNext)
                            {
                                escapeNext = false;
                                continue;
                            }

                            if (c == '\\')
                            {
                                escapeNext = true;
                                continue;
                            }

                            if (c == '"' && !escapeNext)
                            {
                                inString = !inString;
                                continue;
                            }

                            if (!inString)
                            {
                                if (c == '{')
                                    braceDepth++;
                                else if (c == '}')
                                    braceDepth--;
                            }
                        }

                        // Check if we have a complete JSON object
                        if (braceDepth == 0 && messageBuilder.Length > 0)
                        {
                            var completeJson = messageBuilder.ToString();
                            messageBuilder.Clear();

                            try
                            {
                                var message = JsonSerializer.Deserialize<JsonRpcMessage>(completeJson);
                                if (message != null)
                                {
                                    _receiveQueue.Enqueue(message);
                                    OnMessageReceived(message);
                                }
                            }
                            catch (JsonException ex)
                            {
                                OnError(ex, $"Failed to parse JSON: {completeJson}");
                            }
                        }
                    }
                }
                catch (IOException ex)
                {
                    // Input stream error
                    OnError(ex, "Error reading from input stream");
                    break;
                }
                catch (ObjectDisposedException)
                {
                    // Stream was closed, exit loop
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

            // Mark as disconnected if loop exits
            if (_state == ConnectionState.Connected)
            {
                _state = ConnectionState.Disconnected;
                OnConnectionStateChanged(ConnectionState.Disconnected, ConnectionState.Connected);
            }
        }

        private bool IsFatalError(Exception ex)
        {
            return ex is IOException ||
                   ex is ObjectDisposedException ||
                   ex is OutOfMemoryException;
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
                throw new ObjectDisposedException(nameof(StdioTransport));
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

                _shutdownTokenSource?.Cancel();
                _shutdownTokenSource?.Dispose();
                
                // Close streams if we own them
                if (_ownsStreams)
                {
                    try
                    {
                        if (_input != Console.In)
                            _input?.Dispose();
                    }
                    catch { }

                    try
                    {
                        if (_output != Console.Out)
                        {
                            _output?.Flush();
                            _output?.Dispose();
                        }
                    }
                    catch { }
                }

                _sendLock?.Dispose();
                _receiveLock?.Dispose();
                _receiveQueue.Clear();
                
                _disposed = true;
            }
        }
    }
}