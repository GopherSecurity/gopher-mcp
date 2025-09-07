using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Configuration for TCP proxy filter.
    /// </summary>
    public class TcpProxyConfig : FilterConfig
    {
        /// <summary>
        /// Gets or sets the upstream host.
        /// </summary>
        public string UpstreamHost { get; set; }

        /// <summary>
        /// Gets or sets the upstream port.
        /// </summary>
        public int UpstreamPort { get; set; }

        /// <summary>
        /// Gets or sets the connection timeout in milliseconds.
        /// </summary>
        public int ConnectionTimeoutMs { get; set; } = 5000;

        /// <summary>
        /// Gets or sets whether to enable connection pooling.
        /// </summary>
        public bool EnableConnectionPooling { get; set; } = true;

        /// <summary>
        /// Gets or sets the maximum pool size.
        /// </summary>
        public int MaxPoolSize { get; set; } = 100;

        /// <summary>
        /// Gets or sets the health check interval in seconds.
        /// </summary>
        public int HealthCheckIntervalSeconds { get; set; } = 30;

        /// <summary>
        /// Gets or sets whether to enable keep-alive.
        /// </summary>
        public bool EnableKeepAlive { get; set; } = true;

        /// <summary>
        /// Gets or sets the keep-alive interval in seconds.
        /// </summary>
        public int KeepAliveIntervalSeconds { get; set; } = 60;
    }

    /// <summary>
    /// TCP proxy filter for forwarding TCP connections.
    /// </summary>
    public class TcpProxyFilter : Filter
    {
        private readonly TcpProxyConfig _config;
        private readonly ILogger<TcpProxyFilter> _logger;
        private readonly ConcurrentQueue<TcpClient> _connectionPool;
        private readonly SemaphoreSlim _poolSemaphore;
        private Timer _healthCheckTimer;
        private bool _isHealthy = true;

        /// <summary>
        /// Initializes a new instance of the TcpProxyFilter class.
        /// </summary>
        /// <param name="config">The TCP proxy configuration.</param>
        /// <param name="logger">Optional logger.</param>
        public TcpProxyFilter(TcpProxyConfig config, ILogger<TcpProxyFilter> logger = null)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
            _connectionPool = new ConcurrentQueue<TcpClient>();
            _poolSemaphore = new SemaphoreSlim(config.MaxPoolSize, config.MaxPoolSize);

            if (config.HealthCheckIntervalSeconds > 0)
            {
                _healthCheckTimer = new Timer(
                    PerformHealthCheck,
                    null,
                    TimeSpan.FromSeconds(config.HealthCheckIntervalSeconds),
                    TimeSpan.FromSeconds(config.HealthCheckIntervalSeconds));
            }
        }

        /// <summary>
        /// Processes data through the TCP proxy.
        /// </summary>
        /// <param name="data">The data to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        public override async Task<FilterResult> ProcessAsync(object data, CancellationToken cancellationToken = default)
        {
            if (!_isHealthy)
            {
                return FilterResult.Error("Upstream connection unhealthy");
            }

            TcpClient client = null;
            try
            {
                // Get or create connection
                client = await GetConnectionAsync(cancellationToken);

                // Forward data
                var result = await ForwardDataAsync(client, data, cancellationToken);

                // Return connection to pool if enabled
                if (_config.EnableConnectionPooling && client.Connected)
                {
                    ReturnToPool(client);
                    client = null;
                }

                return result;
            }
            catch (SocketException ex)
            {
                _logger?.LogError(ex, "TCP proxy error");
                return FilterResult.Error($"TCP proxy error: {ex.Message}");
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Unexpected error in TCP proxy");
                return FilterResult.Error($"Unexpected error: {ex.Message}");
            }
            finally
            {
                if (client != null && !_config.EnableConnectionPooling)
                {
                    client.Dispose();
                }
            }
        }

        /// <summary>
        /// Gets a connection from the pool or creates a new one.
        /// </summary>
        private async Task<TcpClient> GetConnectionAsync(CancellationToken cancellationToken)
        {
            if (_config.EnableConnectionPooling)
            {
                // Try to get from pool
                while (_connectionPool.TryDequeue(out var pooledClient))
                {
                    if (pooledClient.Connected)
                    {
                        _logger?.LogDebug("Reusing pooled connection");
                        return pooledClient;
                    }
                    pooledClient.Dispose();
                    _poolSemaphore.Release();
                }
            }

            // Create new connection
            _logger?.LogDebug("Creating new connection to {Host}:{Port}", _config.UpstreamHost, _config.UpstreamPort);
            
            var client = new TcpClient();
            
            if (_config.EnableKeepAlive)
            {
                client.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
                client.Client.SetSocketOption(SocketOptionLevel.Tcp, SocketOptionName.TcpKeepAliveInterval, _config.KeepAliveIntervalSeconds);
            }

            using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            cts.CancelAfter(_config.ConnectionTimeoutMs);

            await client.ConnectAsync(_config.UpstreamHost, _config.UpstreamPort, cts.Token);
            
            return client;
        }

        /// <summary>
        /// Returns a connection to the pool.
        /// </summary>
        private void ReturnToPool(TcpClient client)
        {
            if (!client.Connected)
            {
                client.Dispose();
                return;
            }

            if (_poolSemaphore.Wait(0))
            {
                _connectionPool.Enqueue(client);
                _logger?.LogDebug("Connection returned to pool");
            }
            else
            {
                // Pool is full
                client.Dispose();
                _logger?.LogDebug("Pool full, closing connection");
            }
        }

        /// <summary>
        /// Forwards data through the TCP connection.
        /// </summary>
        private async Task<FilterResult> ForwardDataAsync(TcpClient client, object data, CancellationToken cancellationToken)
        {
            var stream = client.GetStream();

            // Convert data to bytes
            byte[] buffer = data switch
            {
                byte[] bytes => bytes,
                FilterBuffer filterBuffer => filterBuffer.ToArray(),
                string str => System.Text.Encoding.UTF8.GetBytes(str),
                _ => throw new ArgumentException($"Unsupported data type: {data?.GetType()}")
            };

            // Send data
            await stream.WriteAsync(buffer, cancellationToken);
            await stream.FlushAsync(cancellationToken);

            // Read response (simplified - real implementation would handle framing)
            var responseBuffer = new byte[4096];
            var bytesRead = await stream.ReadAsync(responseBuffer, cancellationToken);

            if (bytesRead == 0)
            {
                return FilterResult.Error("No response from upstream");
            }

            var response = new byte[bytesRead];
            Array.Copy(responseBuffer, response, bytesRead);

            UpdateStatistics(buffer.Length, bytesRead);

            return FilterResult.Success(response);
        }

        /// <summary>
        /// Performs health check on upstream connection.
        /// </summary>
        private async void PerformHealthCheck(object state)
        {
            try
            {
                using var client = new TcpClient();
                using var cts = new CancellationTokenSource(_config.ConnectionTimeoutMs);
                
                await client.ConnectAsync(_config.UpstreamHost, _config.UpstreamPort, cts.Token);
                
                if (!_isHealthy)
                {
                    _logger?.LogInformation("Upstream connection restored");
                    _isHealthy = true;
                }
            }
            catch
            {
                if (_isHealthy)
                {
                    _logger?.LogWarning("Upstream connection unhealthy");
                    _isHealthy = false;
                }
            }
        }

        /// <summary>
        /// Updates filter statistics.
        /// </summary>
        private void UpdateStatistics(long bytesSent, long bytesReceived)
        {
            // Update internal statistics
            // This would normally update the native filter statistics
        }

        /// <summary>
        /// Disposes the filter and releases resources.
        /// </summary>
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _healthCheckTimer?.Dispose();
                _poolSemaphore?.Dispose();

                while (_connectionPool.TryDequeue(out var client))
                {
                    client.Dispose();
                }
            }

            base.Dispose(disposing);
        }
    }
}