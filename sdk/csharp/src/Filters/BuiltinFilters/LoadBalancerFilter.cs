using System;
using System.Net.Http;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum LoadBalancingAlgorithm
    {
        RoundRobin,
        LeastConnections,
        Random,
        WeightedRoundRobin,
        WeightedRandom,
        IpHash,
        ConsistentHash,
        Custom
    }

    public class UpstreamServer
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Host { get; set; } = string.Empty;
        public int Port { get; set; }
        public int Weight { get; set; } = 1;
        public bool IsHealthy { get; set; } = true;

        // Use fields for thread-safe operations
        private int _activeConnections;
        private long _totalRequests;
        private long _failedRequests;

        public int ActiveConnections => _activeConnections;
        public DateTimeOffset LastHealthCheck { get; set; }
        public DateTimeOffset LastUsed { get; set; }
        public long TotalRequests => _totalRequests;
        public long FailedRequests => _failedRequests;
        public TimeSpan AverageResponseTime { get; set; }
        public Dictionary<string, string> Metadata { get; set; } = new();

        public string Address => $"{Host}:{Port}";

        // Methods for thread-safe operations
        public void IncrementActiveConnections() => Interlocked.Increment(ref _activeConnections);
        public void DecrementActiveConnections() => Interlocked.Decrement(ref _activeConnections);
        public void IncrementTotalRequests() => Interlocked.Increment(ref _totalRequests);
        public void IncrementFailedRequests() => Interlocked.Increment(ref _failedRequests);
    }

    public class HealthCheckConfig
    {
        public bool Enabled { get; set; } = true;
        public TimeSpan Interval { get; set; } = TimeSpan.FromSeconds(30);
        public TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(5);
        public string Path { get; set; } = "/health";
        public int UnhealthyThreshold { get; set; } = 3;
        public int HealthyThreshold { get; set; } = 2;
        public Func<UpstreamServer, Task<bool>>? CustomHealthCheck { get; set; }
    }

    public class LoadBalancerConfig : FilterConfigBase
    {
        public LoadBalancingAlgorithm Algorithm { get; set; } = LoadBalancingAlgorithm.RoundRobin;
        public List<UpstreamServer> Upstreams { get; set; } = new();
        public HealthCheckConfig HealthCheck { get; set; } = new();
        public bool SessionAffinity { get; set; } = false;
        public string SessionAffinityKey { get; set; } = "ClientIp";
        public TimeSpan SessionAffinityTimeout { get; set; } = TimeSpan.FromMinutes(5);
        public int MaxConnectionsPerUpstream { get; set; } = 100;
        public bool EnableFailover { get; set; } = true;
        public int FailoverAttempts { get; set; } = 3;
        public Func<ProcessingContext, UpstreamServer?, UpstreamServer>? CustomSelector { get; set; }
        public bool EnableConnectionPooling { get; set; } = true;
        public int ConnectionPoolSize { get; set; } = 10;
        public TimeSpan ConnectionIdleTimeout { get; set; } = TimeSpan.FromMinutes(5);
        public bool EnableStatistics { get; set; } = true;

        public LoadBalancerConfig() : base("LoadBalancer", "LoadBalancerFilter")
        {
            Priority = 50; // Run after retry filter
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (Upstreams.Count == 0)
            {
                errors.Add("At least one upstream server must be configured");
            }

            foreach (var upstream in Upstreams)
            {
                if (string.IsNullOrEmpty(upstream.Host))
                {
                    errors.Add($"Upstream server {upstream.Id} has empty host");
                }

                if (upstream.Port <= 0 || upstream.Port > 65535)
                {
                    errors.Add($"Upstream server {upstream.Id} has invalid port: {upstream.Port}");
                }

                if (upstream.Weight <= 0)
                {
                    errors.Add($"Upstream server {upstream.Id} has invalid weight: {upstream.Weight}");
                }
            }

            if (Algorithm == LoadBalancingAlgorithm.Custom && CustomSelector == null)
            {
                errors.Add("Custom selector is required for custom load balancing algorithm");
            }

            if (HealthCheck.Enabled)
            {
                if (HealthCheck.Interval <= TimeSpan.Zero)
                {
                    errors.Add("Health check interval must be greater than 0");
                }

                if (HealthCheck.Timeout <= TimeSpan.Zero || HealthCheck.Timeout >= HealthCheck.Interval)
                {
                    errors.Add("Health check timeout must be greater than 0 and less than interval");
                }
            }

            if (MaxConnectionsPerUpstream <= 0)
            {
                errors.Add("Max connections per upstream must be greater than 0");
            }

            return errors.Count == 0;
        }
    }

    public class LoadBalancerFilter : Filter
    {
        private readonly LoadBalancerConfig _config;
        private readonly ILoadBalancer _loadBalancer;
        private readonly ConcurrentDictionary<string, SessionAffinityEntry> _sessionAffinityMap;
        private readonly ConcurrentDictionary<string, ConnectionPool> _connectionPools;
        private readonly Timer? _healthCheckTimer;
        private readonly Timer _cleanupTimer;
        private long _totalRequests;
        private long _failedRequests;

        public LoadBalancerFilter(LoadBalancerConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _loadBalancer = CreateLoadBalancer();
            _sessionAffinityMap = new ConcurrentDictionary<string, SessionAffinityEntry>();
            _connectionPools = new ConcurrentDictionary<string, ConnectionPool>();

            if (_config.HealthCheck.Enabled)
            {
                _healthCheckTimer = new Timer(
                    PerformHealthChecks,
                    null,
                    _config.HealthCheck.Interval,
                    _config.HealthCheck.Interval);
            }

            _cleanupTimer = new Timer(
                CleanupExpiredSessions,
                null,
                TimeSpan.FromMinutes(1),
                TimeSpan.FromMinutes(1));

            InitializeConnectionPools();
        }

        private ILoadBalancer CreateLoadBalancer()
        {
            return _config.Algorithm switch
            {
                LoadBalancingAlgorithm.RoundRobin => new RoundRobinBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.LeastConnections => new LeastConnectionsBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.Random => new RandomBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.WeightedRoundRobin => new WeightedRoundRobinBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.WeightedRandom => new WeightedRandomBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.IpHash => new IpHashBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.ConsistentHash => new ConsistentHashBalancer(_config.Upstreams),
                LoadBalancingAlgorithm.Custom => new CustomBalancer(_config.Upstreams, _config.CustomSelector!),
                _ => new RoundRobinBalancer(_config.Upstreams)
            };
        }

        private void InitializeConnectionPools()
        {
            if (_config.EnableConnectionPooling)
            {
                foreach (var upstream in _config.Upstreams)
                {
                    _connectionPools[upstream.Id] = new ConnectionPool(
                        upstream,
                        _config.ConnectionPoolSize,
                        _config.ConnectionIdleTimeout);
                }
            }
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            Interlocked.Increment(ref _totalRequests);

            try
            {
                // Get upstream server
                var upstream = await SelectUpstreamAsync(context, cancellationToken);

                if (upstream == null)
                {
                    Interlocked.Increment(ref _failedRequests);
                    return FilterResult.Error("No healthy upstream servers available", FilterError.ServiceUnavailable);
                }

                // Check connection limit
                if (upstream.ActiveConnections >= _config.MaxConnectionsPerUpstream)
                {
                    // Try to find another upstream
                    upstream = await FindAlternativeUpstreamAsync(upstream, context, cancellationToken);

                    if (upstream == null)
                    {
                        Interlocked.Increment(ref _failedRequests);
                        return FilterResult.Error("All upstream servers at capacity", FilterError.ServiceUnavailable);
                    }
                }

                // Process with failover if enabled
                FilterResult result;
                if (_config.EnableFailover)
                {
                    result = await ProcessWithFailoverAsync(buffer, context, upstream, cancellationToken);
                }
                else
                {
                    result = await ProcessWithUpstreamAsync(buffer, context, upstream, cancellationToken);
                }

                if (result.IsSuccess)
                {
                    UpdateUpstreamStatistics(upstream, true);
                }
                else
                {
                    UpdateUpstreamStatistics(upstream, false);
                    Interlocked.Increment(ref _failedRequests);
                }

                UpdateStatistics(buffer.Length, 0, result.IsSuccess);
                await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                return result;
            }
            catch (Exception ex)
            {
                Interlocked.Increment(ref _failedRequests);
                UpdateStatistics(0L, 0, false);
                await RaiseOnErrorAsync(ex);
                return FilterResult.Error($"Load balancer error: {ex.Message}", FilterError.InternalError);
            }
        }

        private async Task<UpstreamServer?> SelectUpstreamAsync(ProcessingContext context, CancellationToken cancellationToken)
        {
            // Check session affinity first
            if (_config.SessionAffinity)
            {
                var sessionKey = GetSessionKey(context);
                if (_sessionAffinityMap.TryGetValue(sessionKey, out var entry) && !entry.IsExpired)
                {
                    var upstream = _config.Upstreams.FirstOrDefault(u => u.Id == entry.UpstreamId);
                    if (upstream?.IsHealthy == true)
                    {
                        entry.LastAccessed = DateTimeOffset.UtcNow;
                        return upstream;
                    }
                }
            }

            // Select using load balancing algorithm
            var selected = await Task.Run(() => _loadBalancer.SelectUpstream(context), cancellationToken);

            // Store session affinity if configured
            if (selected != null && _config.SessionAffinity)
            {
                var sessionKey = GetSessionKey(context);
                _sessionAffinityMap[sessionKey] = new SessionAffinityEntry
                {
                    UpstreamId = selected.Id,
                    Created = DateTimeOffset.UtcNow,
                    LastAccessed = DateTimeOffset.UtcNow,
                    Timeout = _config.SessionAffinityTimeout
                };
            }

            return selected;
        }

        private string GetSessionKey(ProcessingContext context)
        {
            return _config.SessionAffinityKey switch
            {
                "ClientIp" => context.GetProperty<string>("ClientIp") ?? "unknown",
                "UserId" => context.GetProperty<string>("UserId") ?? "anonymous",
                "SessionId" => context.GetProperty<string>("SessionId") ?? Guid.NewGuid().ToString(),
                _ => context.GetProperty<string>(_config.SessionAffinityKey) ?? "default"
            };
        }

        private async Task<UpstreamServer?> FindAlternativeUpstreamAsync(UpstreamServer current, ProcessingContext context, CancellationToken cancellationToken)
        {
            var healthyUpstreams = _config.Upstreams
                .Where(u => u.Id != current.Id && u.IsHealthy && u.ActiveConnections < _config.MaxConnectionsPerUpstream)
                .ToList();

            if (healthyUpstreams.Count == 0)
            {
                return null;
            }

            // Select from healthy upstreams with capacity
            return await Task.Run(() => _loadBalancer.SelectFromSubset(healthyUpstreams, context), cancellationToken);
        }

        private async Task<FilterResult> ProcessWithFailoverAsync(byte[] buffer, ProcessingContext context, UpstreamServer initialUpstream, CancellationToken cancellationToken)
        {
            var attemptsLeft = _config.FailoverAttempts;
            var currentUpstream = initialUpstream;
            var usedUpstreams = new HashSet<string>();

            while (attemptsLeft > 0)
            {
                usedUpstreams.Add(currentUpstream.Id);

                try
                {
                    var result = await ProcessWithUpstreamAsync(buffer, context, currentUpstream, cancellationToken);

                    if (result.IsSuccess || !ShouldFailover(result))
                    {
                        return result;
                    }
                }
                catch (Exception ex) when (ShouldFailoverOnException(ex))
                {
                    // Mark upstream as unhealthy if too many failures
                    RecordUpstreamFailure(currentUpstream);
                }

                attemptsLeft--;

                if (attemptsLeft > 0)
                {
                    // Find another upstream
                    var healthyUpstreams = _config.Upstreams
                        .Where(u => !usedUpstreams.Contains(u.Id) && u.IsHealthy)
                        .ToList();

                    if (healthyUpstreams.Count == 0)
                    {
                        break;
                    }

                    currentUpstream = _loadBalancer.SelectFromSubset(healthyUpstreams, context);
                    if (currentUpstream == null)
                    {
                        break;
                    }
                }
            }

            return FilterResult.Error("All failover attempts exhausted", FilterError.ServiceUnavailable);
        }

        private async Task<FilterResult> ProcessWithUpstreamAsync(byte[] buffer, ProcessingContext context, UpstreamServer upstream, CancellationToken cancellationToken)
        {
            upstream.IncrementActiveConnections();
            var startTime = DateTimeOffset.UtcNow;

            try
            {
                // Store upstream information in context
                context.SetProperty("UpstreamHost", upstream.Host);
                context.SetProperty("UpstreamPort", upstream.Port);
                context.SetProperty("UpstreamId", upstream.Id);

                // Simulate processing with upstream
                // In a real implementation, this would forward the request to the upstream
                await Task.Delay(new Random().Next(10, 100), cancellationToken);

                // Update response time statistics
                var responseTime = DateTimeOffset.UtcNow - startTime;
                UpdateResponseTime(upstream, responseTime);

                upstream.LastUsed = DateTimeOffset.UtcNow;
                upstream.IncrementTotalRequests();

                return FilterResult.Continue(buffer);
            }
            finally
            {
                upstream.DecrementActiveConnections();
            }
        }

        private bool ShouldFailover(FilterResult result)
        {
            return result.ErrorCode == FilterError.ServiceUnavailable ||
                   result.ErrorCode == FilterError.Timeout ||
                   result.ErrorCode == FilterError.NetworkError;
        }

        private bool ShouldFailoverOnException(Exception ex)
        {
            return ex is TimeoutException ||
                   ex is OperationCanceledException ||
                   ex is HttpRequestException;
        }

        private void RecordUpstreamFailure(UpstreamServer upstream)
        {
            upstream.IncrementFailedRequests();

            // Simple health check: mark unhealthy if failure rate is too high
            var failureRate = upstream.TotalRequests > 0
                ? (double)upstream.FailedRequests / upstream.TotalRequests
                : 0;

            if (failureRate > 0.5 && upstream.TotalRequests > 10)
            {
                upstream.IsHealthy = false;
            }
        }

        private void UpdateUpstreamStatistics(UpstreamServer upstream, bool success)
        {
            if (success)
            {
                upstream.IncrementTotalRequests();
            }
            else
            {
                upstream.IncrementFailedRequests();
            }
        }

        private void UpdateResponseTime(UpstreamServer upstream, TimeSpan responseTime)
        {
            // Simple moving average
            var alpha = 0.2; // Smoothing factor
            var currentAvg = upstream.AverageResponseTime.TotalMilliseconds;
            var newSample = responseTime.TotalMilliseconds;
            var newAvg = currentAvg * (1 - alpha) + newSample * alpha;
            upstream.AverageResponseTime = TimeSpan.FromMilliseconds(newAvg);
        }

        private async void PerformHealthChecks(object? state)
        {
            var tasks = _config.Upstreams.Select(upstream => PerformHealthCheckAsync(upstream));
            await Task.WhenAll(tasks);
        }

        private async Task PerformHealthCheckAsync(UpstreamServer upstream)
        {
            try
            {
                bool isHealthy;

                if (_config.HealthCheck.CustomHealthCheck != null)
                {
                    isHealthy = await _config.HealthCheck.CustomHealthCheck(upstream);
                }
                else
                {
                    // Simple TCP health check simulation
                    isHealthy = await Task.Run(() => new Random().Next(100) > 5); // 95% healthy
                }

                upstream.LastHealthCheck = DateTimeOffset.UtcNow;
                upstream.IsHealthy = isHealthy;

                if (!isHealthy)
                {
                    // Clear session affinity for unhealthy upstreams
                    var sessionsToRemove = _sessionAffinityMap
                        .Where(kvp => kvp.Value.UpstreamId == upstream.Id)
                        .Select(kvp => kvp.Key)
                        .ToList();

                    foreach (var key in sessionsToRemove)
                    {
                        _sessionAffinityMap.TryRemove(key, out _);
                    }
                }
            }
            catch
            {
                upstream.IsHealthy = false;
            }
        }

        private void CleanupExpiredSessions(object? state)
        {
            var expiredKeys = _sessionAffinityMap
                .Where(kvp => kvp.Value.IsExpired)
                .Select(kvp => kvp.Key)
                .ToList();

            foreach (var key in expiredKeys)
            {
                _sessionAffinityMap.TryRemove(key, out _);
            }
        }

        public LoadBalancerStatistics GetStatistics()
        {
            return new LoadBalancerStatistics
            {
                TotalRequests = _totalRequests,
                FailedRequests = _failedRequests,
                SuccessRate = _totalRequests > 0 ? 1.0 - ((double)_failedRequests / _totalRequests) : 1.0,
                HealthyUpstreams = _config.Upstreams.Count(u => u.IsHealthy),
                TotalUpstreams = _config.Upstreams.Count,
                ActiveSessions = _sessionAffinityMap.Count,
                UpstreamStatistics = _config.Upstreams.Select(u => new UpstreamStatistics
                {
                    Id = u.Id,
                    Address = u.Address,
                    IsHealthy = u.IsHealthy,
                    ActiveConnections = u.ActiveConnections,
                    TotalRequests = u.TotalRequests,
                    FailedRequests = u.FailedRequests,
                    SuccessRate = u.TotalRequests > 0 ? 1.0 - ((double)u.FailedRequests / u.TotalRequests) : 1.0,
                    AverageResponseTime = u.AverageResponseTime
                }).ToList()
            };
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _healthCheckTimer?.Dispose();
                _cleanupTimer?.Dispose();

                foreach (var pool in _connectionPools.Values)
                {
                    pool.Dispose();
                }
                _connectionPools.Clear();
                _sessionAffinityMap.Clear();
            }
            base.Dispose(disposing);
        }
    }

    // Load balancer implementations
    internal interface ILoadBalancer
    {
        UpstreamServer? SelectUpstream(ProcessingContext context);
        UpstreamServer? SelectFromSubset(List<UpstreamServer> subset, ProcessingContext context);
    }

    internal abstract class BaseLoadBalancer : ILoadBalancer
    {
        protected readonly List<UpstreamServer> _upstreams;

        protected BaseLoadBalancer(List<UpstreamServer> upstreams)
        {
            _upstreams = upstreams;
        }

        public abstract UpstreamServer? SelectUpstream(ProcessingContext context);

        public virtual UpstreamServer? SelectFromSubset(List<UpstreamServer> subset, ProcessingContext context)
        {
            // Default implementation: use same algorithm on subset
            return SelectUpstream(context);
        }

        protected List<UpstreamServer> GetHealthyUpstreams()
        {
            return _upstreams.Where(u => u.IsHealthy).ToList();
        }
    }

    internal class RoundRobinBalancer : BaseLoadBalancer
    {
        private long _currentIndex;

        public RoundRobinBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            var healthyUpstreams = GetHealthyUpstreams();
            if (healthyUpstreams.Count == 0)
                return null;

            var index = Interlocked.Increment(ref _currentIndex) % healthyUpstreams.Count;
            return healthyUpstreams[(int)index];
        }
    }

    internal class LeastConnectionsBalancer : BaseLoadBalancer
    {
        public LeastConnectionsBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            return GetHealthyUpstreams()
                .OrderBy(u => u.ActiveConnections)
                .FirstOrDefault();
        }
    }

    internal class RandomBalancer : BaseLoadBalancer
    {
        public RandomBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            var healthyUpstreams = GetHealthyUpstreams();
            if (healthyUpstreams.Count == 0)
                return null;

            var index = new Random().Next(healthyUpstreams.Count);
            return healthyUpstreams[index];
        }
    }

    internal class WeightedRoundRobinBalancer : BaseLoadBalancer
    {
        private readonly List<UpstreamServer> _weightedList;
        private long _currentIndex;

        public WeightedRoundRobinBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
            _weightedList = new List<UpstreamServer>();
            foreach (var upstream in upstreams)
            {
                for (int i = 0; i < upstream.Weight; i++)
                {
                    _weightedList.Add(upstream);
                }
            }
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            var healthyWeightedList = _weightedList.Where(u => u.IsHealthy).ToList();
            if (healthyWeightedList.Count == 0)
                return null;

            var index = Interlocked.Increment(ref _currentIndex) % healthyWeightedList.Count;
            return healthyWeightedList[(int)index];
        }
    }

    internal class WeightedRandomBalancer : BaseLoadBalancer
    {
        public WeightedRandomBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            var healthyUpstreams = GetHealthyUpstreams();
            if (healthyUpstreams.Count == 0)
                return null;

            var totalWeight = healthyUpstreams.Sum(u => u.Weight);
            var randomWeight = new Random().Next(totalWeight);

            var currentWeight = 0;
            foreach (var upstream in healthyUpstreams)
            {
                currentWeight += upstream.Weight;
                if (randomWeight < currentWeight)
                {
                    return upstream;
                }
            }

            return healthyUpstreams.Last();
        }
    }

    internal class IpHashBalancer : BaseLoadBalancer
    {
        public IpHashBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            var healthyUpstreams = GetHealthyUpstreams();
            if (healthyUpstreams.Count == 0)
                return null;

            var clientIp = context.GetProperty<string>("ClientIp") ?? "unknown";
            var hash = clientIp.GetHashCode();
            var index = Math.Abs(hash) % healthyUpstreams.Count;
            return healthyUpstreams[index];
        }
    }

    internal class ConsistentHashBalancer : BaseLoadBalancer
    {
        private readonly SortedDictionary<int, UpstreamServer> _hashRing;
        private const int VirtualNodesPerServer = 150;

        public ConsistentHashBalancer(List<UpstreamServer> upstreams) : base(upstreams)
        {
            _hashRing = new SortedDictionary<int, UpstreamServer>();
            BuildHashRing();
        }

        private void BuildHashRing()
        {
            foreach (var upstream in _upstreams)
            {
                for (int i = 0; i < VirtualNodesPerServer; i++)
                {
                    var virtualNodeKey = $"{upstream.Id}:{i}";
                    var hash = virtualNodeKey.GetHashCode();
                    _hashRing[hash] = upstream;
                }
            }
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            if (_hashRing.Count == 0)
                return null;

            var key = context.GetProperty<string>("RequestId") ?? Guid.NewGuid().ToString();
            var hash = key.GetHashCode();

            // Find the first node with hash >= our hash
            var node = _hashRing.FirstOrDefault(kvp => kvp.Key >= hash);

            // If no node found, wrap around to the first node
            if (node.Value == null)
            {
                node = _hashRing.First();
            }

            return node.Value?.IsHealthy == true ? node.Value : FindNextHealthyNode(hash);
        }

        private UpstreamServer? FindNextHealthyNode(int startHash)
        {
            foreach (var kvp in _hashRing.Where(k => k.Key >= startHash))
            {
                if (kvp.Value.IsHealthy)
                    return kvp.Value;
            }

            // Wrap around
            foreach (var kvp in _hashRing)
            {
                if (kvp.Value.IsHealthy)
                    return kvp.Value;
            }

            return null;
        }
    }

    internal class CustomBalancer : BaseLoadBalancer
    {
        private readonly Func<ProcessingContext, UpstreamServer?, UpstreamServer> _selector;

        public CustomBalancer(List<UpstreamServer> upstreams, Func<ProcessingContext, UpstreamServer?, UpstreamServer> selector)
            : base(upstreams)
        {
            _selector = selector;
        }

        public override UpstreamServer? SelectUpstream(ProcessingContext context)
        {
            var healthyUpstreams = GetHealthyUpstreams();
            if (healthyUpstreams.Count == 0)
                return null;

            return _selector(context, healthyUpstreams.FirstOrDefault());
        }
    }

    // Supporting classes
    internal class SessionAffinityEntry
    {
        public string UpstreamId { get; set; } = string.Empty;
        public DateTimeOffset Created { get; set; }
        public DateTimeOffset LastAccessed { get; set; }
        public TimeSpan Timeout { get; set; }

        public bool IsExpired => DateTimeOffset.UtcNow - LastAccessed > Timeout;
    }

    internal class ConnectionPool : IDisposable
    {
        private readonly UpstreamServer _upstream;
        private readonly int _maxSize;
        private readonly TimeSpan _idleTimeout;
        // Implementation details omitted for brevity

        public ConnectionPool(UpstreamServer upstream, int maxSize, TimeSpan idleTimeout)
        {
            _upstream = upstream;
            _maxSize = maxSize;
            _idleTimeout = idleTimeout;
        }

        public void Dispose()
        {
            // Clean up connections
        }
    }

    public class LoadBalancerStatistics
    {
        public long TotalRequests { get; set; }
        public long FailedRequests { get; set; }
        public double SuccessRate { get; set; }
        public int HealthyUpstreams { get; set; }
        public int TotalUpstreams { get; set; }
        public int ActiveSessions { get; set; }
        public List<UpstreamStatistics> UpstreamStatistics { get; set; } = new();
    }

    public class UpstreamStatistics
    {
        public string Id { get; set; } = string.Empty;
        public string Address { get; set; } = string.Empty;
        public bool IsHealthy { get; set; }
        public int ActiveConnections { get; set; }
        public long TotalRequests { get; set; }
        public long FailedRequests { get; set; }
        public double SuccessRate { get; set; }
        public TimeSpan AverageResponseTime { get; set; }
    }
}
