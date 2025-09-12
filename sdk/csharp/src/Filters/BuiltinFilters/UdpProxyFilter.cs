using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Configuration for UDP proxy filter.
    /// </summary>
    public class UdpProxyConfig : FilterConfigBase
    {
        /// <summary>
        /// Gets or sets the upstream endpoint.
        /// </summary>
        public IPEndPoint UpstreamEndpoint { get; set; }

        /// <summary>
        /// Gets or sets the upstream host (alternative to endpoint).
        /// </summary>
        public string UpstreamHost { get; set; }

        /// <summary>
        /// Gets or sets the upstream port (alternative to endpoint).
        /// </summary>
        public int UpstreamPort { get; set; }

        /// <summary>
        /// Gets or sets the receive timeout in milliseconds.
        /// </summary>
        public int ReceiveTimeoutMs { get; set; } = 5000;

        /// <summary>
        /// Gets or sets whether to enable session tracking.
        /// </summary>
        public bool EnableSessionTracking { get; set; } = true;

        /// <summary>
        /// Gets or sets the session timeout in seconds.
        /// </summary>
        public int SessionTimeoutSeconds { get; set; } = 300;

        /// <summary>
        /// Gets or sets the maximum datagram size.
        /// </summary>
        public int MaxDatagramSize { get; set; } = 65507;

        /// <summary>
        /// Gets or sets whether to enable multicast support.
        /// </summary>
        public bool EnableMulticast { get; set; } = false;

        /// <summary>
        /// Gets or sets the multicast group address.
        /// </summary>
        public IPAddress MulticastGroup { get; set; }
    }

    /// <summary>
    /// Represents a UDP session.
    /// </summary>
    public class UdpSession
    {
        public IPEndPoint ClientEndpoint { get; set; }
        public DateTime LastActivity { get; set; }
        public long BytesSent { get; set; }
        public long BytesReceived { get; set; }
        public string SessionId { get; set; }

        public UdpSession()
        {
            SessionId = Guid.NewGuid().ToString();
            LastActivity = DateTime.UtcNow;
        }
    }

    /// <summary>
    /// UDP proxy filter for forwarding UDP datagrams.
    /// </summary>
    public class UdpProxyFilter : Filter
    {
        private readonly UdpProxyConfig _config;
        private readonly ILogger<UdpProxyFilter> _logger;
        private readonly ConcurrentDictionary<string, UdpSession> _sessions;
        private readonly UdpClient _upstreamClient;
        private Timer _sessionCleanupTimer;
        private IPEndPoint _upstreamEndpoint;

        /// <summary>
        /// Initializes a new instance of the UdpProxyFilter class.
        /// </summary>
        /// <param name="config">The UDP proxy configuration.</param>
        /// <param name="logger">Optional logger.</param>
        public UdpProxyFilter(UdpProxyConfig config, ILogger<UdpProxyFilter> logger = null)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
            _sessions = new ConcurrentDictionary<string, UdpSession>();

            // Initialize upstream endpoint
            _upstreamEndpoint = config.UpstreamEndpoint;
            if (_upstreamEndpoint == null && !string.IsNullOrEmpty(config.UpstreamHost))
            {
                var addresses = Dns.GetHostAddresses(config.UpstreamHost);
                if (addresses.Length > 0)
                {
                    _upstreamEndpoint = new IPEndPoint(addresses[0], config.UpstreamPort);
                }
            }

            if (_upstreamEndpoint == null)
            {
                throw new ArgumentException("Upstream endpoint must be configured");
            }

            // Create UDP client
            _upstreamClient = new UdpClient();
            _upstreamClient.Client.ReceiveTimeout = config.ReceiveTimeoutMs;

            // Configure multicast if enabled
            if (config.EnableMulticast && config.MulticastGroup != null)
            {
                _upstreamClient.JoinMulticastGroup(config.MulticastGroup);
            }

            // Start session cleanup timer if session tracking is enabled
            if (config.EnableSessionTracking)
            {
                _sessionCleanupTimer = new Timer(
                    CleanupSessions,
                    null,
                    TimeSpan.FromSeconds(60),
                    TimeSpan.FromSeconds(60));
            }
        }

        /// <summary>
        /// Processes buffer through the UDP proxy.
        /// </summary>
        /// <param name="buffer">The buffer to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
        {
            try
            {
                // Forward datagram
                var result = await ForwardDatagramAsync(buffer, cancellationToken);
                return result;
            }
            catch (SocketException ex)
            {
                _logger?.LogError(ex, "UDP proxy error");
                return FilterResult.Error($"UDP proxy error: {ex.Message}");
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Unexpected error in UDP proxy");
                return FilterResult.Error($"Unexpected error: {ex.Message}");
            }
        }

        /// <summary>
        /// Forwards a datagram through the UDP proxy.
        /// </summary>
        private async Task<FilterResult> ForwardDatagramAsync(byte[] buffer, CancellationToken cancellationToken)
        {

            // Check datagram size
            if (buffer.Length > _config.MaxDatagramSize)
            {
                return FilterResult.Error($"Datagram too large: {buffer.Length} bytes (max: {_config.MaxDatagramSize})");
            }

            // Track session if enabled
            UdpSession session = null;
            if (_config.EnableSessionTracking)
            {
                session = GetOrCreateSession(GetClientIdentifier(buffer));
                session.BytesSent += buffer.Length;
                session.LastActivity = DateTime.UtcNow;
            }

            // Send datagram to upstream
            await _upstreamClient.SendAsync(buffer, buffer.Length, _upstreamEndpoint);
            _logger?.LogDebug("Sent {Bytes} bytes to upstream {Endpoint}", buffer.Length, _upstreamEndpoint);

            // Try to receive response
            try
            {
                using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                cts.CancelAfter(_config.ReceiveTimeoutMs);

#if NET6_0_OR_GREATER
                var receiveTask = _upstreamClient.ReceiveAsync(cts.Token);
                var result = await receiveTask;
#else
                // For .NET Standard 2.1, use the non-cancellable overload
                var receiveTask = _upstreamClient.ReceiveAsync();
                var completedTask = await Task.WhenAny(receiveTask, Task.Delay(Timeout.Infinite, cts.Token));
                
                if (completedTask != receiveTask)
                {
                    // Timeout occurred
                    throw new OperationCanceledException("Receive operation timed out");
                }
                
                var result = await receiveTask;
#endif

                if (session != null)
                {
                    session.BytesReceived += result.Buffer.Length;
                    session.LastActivity = DateTime.UtcNow;
                }

                _logger?.LogDebug("Received {Bytes} bytes from upstream", result.Buffer.Length);
                return FilterResult.Success(result.Buffer, 0, result.Buffer.Length);
            }
            catch (OperationCanceledException)
            {
                // Timeout is acceptable for UDP
                _logger?.LogDebug("No response received within timeout");
                return FilterResult.Success(new byte[0], 0, 0);
            }
        }

        /// <summary>
        /// Gets a client identifier from the buffer.
        /// </summary>
        private string GetClientIdentifier(object buffer)
        {
            // In a real implementation, this would extract client info from metadata
            return "default";
        }

        /// <summary>
        /// Gets or creates a session for tracking.
        /// </summary>
        private UdpSession GetOrCreateSession(string clientId)
        {
            return _sessions.GetOrAdd(clientId, _ => new UdpSession
            {
                ClientEndpoint = new IPEndPoint(IPAddress.Any, 0)
            });
        }

        /// <summary>
        /// Cleans up expired sessions.
        /// </summary>
        private void CleanupSessions(object state)
        {
            var cutoff = DateTime.UtcNow.AddSeconds(-_config.SessionTimeoutSeconds);
            var expiredSessions = _sessions.Where(kvp => kvp.Value.LastActivity < cutoff).ToList();

            foreach (var kvp in expiredSessions)
            {
                if (_sessions.TryRemove(kvp.Key, out var session))
                {
                    _logger?.LogDebug("Removed expired session {SessionId}", session.SessionId);
                }
            }

            if (expiredSessions.Count > 0)
            {
                _logger?.LogInformation("Cleaned up {Count} expired UDP sessions", expiredSessions.Count);
            }
        }

        /// <summary>
        /// Gets session statistics.
        /// </summary>
        public IDictionary<string, UdpSession> GetSessions()
        {
            return new Dictionary<string, UdpSession>(_sessions);
        }

        /// <summary>
        /// Disposes the filter and releases resources.
        /// </summary>
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _sessionCleanupTimer?.Dispose();

                if (_config.EnableMulticast && _config.MulticastGroup != null)
                {
                    try
                    {
                        _upstreamClient?.DropMulticastGroup(_config.MulticastGroup);
                    }
                    catch { }
                }

                _upstreamClient?.Dispose();
                _sessions.Clear();
            }

            base.Dispose(disposing);
        }
    }
}
