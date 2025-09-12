using System;
using System.Linq;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum RateLimitAlgorithm
    {
        TokenBucket,
        SlidingWindow,
        FixedWindow,
        LeakyBucket
    }

    public enum RateLimitKeyExtractor
    {
        ClientIp,
        UserId,
        ApiKey,
        Custom,
        Combined
    }

    public class RateLimitConfig : FilterConfigBase
    {
        public RateLimitAlgorithm Algorithm { get; set; } = RateLimitAlgorithm.TokenBucket;
        public int RequestsPerMinute { get; set; } = 60;
        public int BurstSize { get; set; } = 10;
        public RateLimitKeyExtractor KeyExtractor { get; set; } = RateLimitKeyExtractor.ClientIp;
        public Func<ProcessingContext, string>? CustomKeyExtractor { get; set; }
        public bool GlobalLimit { get; set; } = false;
        public Dictionary<string, int> CustomLimits { get; set; } = new();
        public int WindowSizeSeconds { get; set; } = 60;
        public bool DistributedMode { get; set; } = false;
        public string? RedisConnectionString { get; set; }
        public int MaxConcurrentRequests { get; set; } = 100;
        public bool ReturnRateLimitHeaders { get; set; } = true;
        public string RateLimitHeaderPrefix { get; set; } = "X-RateLimit-";
        public List<string> WhitelistedKeys { get; set; } = new();
        public List<string> BlacklistedKeys { get; set; } = new();
        public int CleanupIntervalSeconds { get; set; } = 300;

        public RateLimitConfig() : base("RateLimit", "RateLimitFilter")
        {
            Priority = 80; // Run after authentication but before main processing
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (RequestsPerMinute <= 0)
            {
                errors.Add("Requests per minute must be greater than 0");
            }

            if (BurstSize <= 0)
            {
                errors.Add("Burst size must be greater than 0");
            }

            if (WindowSizeSeconds <= 0)
            {
                errors.Add("Window size must be greater than 0");
            }

            if (KeyExtractor == RateLimitKeyExtractor.Custom && CustomKeyExtractor == null)
            {
                errors.Add("Custom key extractor function is required when using custom key extraction");
            }

            if (DistributedMode && string.IsNullOrEmpty(RedisConnectionString))
            {
                errors.Add("Redis connection string is required for distributed rate limiting");
            }

            if (MaxConcurrentRequests <= 0)
            {
                errors.Add("Max concurrent requests must be greater than 0");
            }

            return errors.Count == 0;
        }
    }

    public class RateLimitFilter : Filter
    {
        private readonly RateLimitConfig _config;
        private readonly ConcurrentDictionary<string, IRateLimiter> _limiters;
        private readonly Timer _cleanupTimer;
        private readonly SemaphoreSlim _concurrencyLimiter;
        private long _totalRequests;
        private long _throttledRequests;

        public RateLimitFilter(RateLimitConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _limiters = new ConcurrentDictionary<string, IRateLimiter>();
            _concurrencyLimiter = new SemaphoreSlim(_config.MaxConcurrentRequests, _config.MaxConcurrentRequests);

            _cleanupTimer = new Timer(
                CleanupExpiredLimiters,
                null,
                TimeSpan.FromSeconds(_config.CleanupIntervalSeconds),
                TimeSpan.FromSeconds(_config.CleanupIntervalSeconds));
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            Interlocked.Increment(ref _totalRequests);

            try
            {
                // Extract rate limit key
                var key = ExtractKey(context);

                // Check whitelist/blacklist
                if (IsWhitelisted(key))
                {
                    return await ProcessWithoutRateLimitAsync(buffer, context, cancellationToken);
                }

                if (IsBlacklisted(key))
                {
                    Interlocked.Increment(ref _throttledRequests);
                    return CreateRateLimitExceededResult(key, 0, 0, 0);
                }

                // Get or create limiter for this key
                var limiter = GetOrCreateLimiter(key);

                // Get current limit for this key
                var limit = GetLimitForKey(key);

                // Try to acquire token
                var acquireResult = await limiter.TryAcquireAsync(1, cancellationToken);

                if (!acquireResult.Allowed)
                {
                    Interlocked.Increment(ref _throttledRequests);

                    // Add rate limit headers if configured
                    if (_config.ReturnRateLimitHeaders)
                    {
                        AddRateLimitHeaders(context, acquireResult);
                    }

                    return CreateRateLimitExceededResult(key, acquireResult.Remaining, limit, acquireResult.ResetAfterSeconds);
                }

                // Check concurrent request limit
                if (!await _concurrencyLimiter.WaitAsync(0, cancellationToken))
                {
                    Interlocked.Increment(ref _throttledRequests);
                    return FilterResult.Error("Too many concurrent requests", FilterError.TooManyRequests);
                }

                try
                {
                    // Add rate limit headers if configured
                    if (_config.ReturnRateLimitHeaders)
                    {
                        AddRateLimitHeaders(context, acquireResult);
                    }

                    // Continue processing
                    var result = FilterResult.Continue(buffer);

                    UpdateStatistics(buffer.Length, 0, true);
                    await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                    return result;
                }
                finally
                {
                    _concurrencyLimiter.Release();
                }
            }
            catch (Exception ex)
            {
                UpdateStatistics(0L, 1, false);
                await RaiseOnErrorAsync(ex);
                return FilterResult.Error($"Rate limit error: {ex.Message}", FilterError.InternalError);
            }
        }

        private string ExtractKey(ProcessingContext context)
        {
            if (_config.GlobalLimit)
            {
                return "global";
            }

            switch (_config.KeyExtractor)
            {
                case RateLimitKeyExtractor.ClientIp:
                    return context.GetProperty<string>("ClientIp") ?? "unknown";

                case RateLimitKeyExtractor.UserId:
                    return context.GetProperty<string>("UserId") ?? "anonymous";

                case RateLimitKeyExtractor.ApiKey:
                    return context.GetProperty<string>("ApiKey") ?? "no-key";

                case RateLimitKeyExtractor.Custom:
                    return _config.CustomKeyExtractor?.Invoke(context) ?? "custom";

                case RateLimitKeyExtractor.Combined:
                    var clientIp = context.GetProperty<string>("ClientIp") ?? "unknown";
                    var userId = context.GetProperty<string>("UserId");
                    return string.IsNullOrEmpty(userId) ? clientIp : $"{userId}:{clientIp}";

                default:
                    return "unknown";
            }
        }

        private bool IsWhitelisted(string key)
        {
            return _config.WhitelistedKeys.Contains(key);
        }

        private bool IsBlacklisted(string key)
        {
            return _config.BlacklistedKeys.Contains(key);
        }

        private IRateLimiter GetOrCreateLimiter(string key)
        {
            return _limiters.GetOrAdd(key, k => CreateLimiter());
        }

        private IRateLimiter CreateLimiter()
        {
            return _config.Algorithm switch
            {
                RateLimitAlgorithm.TokenBucket => new TokenBucketLimiter(
                    _config.RequestsPerMinute,
                    _config.BurstSize,
                    _config.WindowSizeSeconds),

                RateLimitAlgorithm.SlidingWindow => new SlidingWindowLimiter(
                    _config.RequestsPerMinute,
                    _config.WindowSizeSeconds),

                RateLimitAlgorithm.FixedWindow => new FixedWindowLimiter(
                    _config.RequestsPerMinute,
                    _config.WindowSizeSeconds),

                RateLimitAlgorithm.LeakyBucket => new LeakyBucketLimiter(
                    _config.RequestsPerMinute,
                    _config.BurstSize,
                    _config.WindowSizeSeconds),

                _ => new TokenBucketLimiter(
                    _config.RequestsPerMinute,
                    _config.BurstSize,
                    _config.WindowSizeSeconds)
            };
        }

        private int GetLimitForKey(string key)
        {
            if (_config.CustomLimits.TryGetValue(key, out var customLimit))
            {
                return customLimit;
            }
            return _config.RequestsPerMinute;
        }

        private async Task<FilterResult> ProcessWithoutRateLimitAsync(
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            var result = FilterResult.Continue(buffer);
            UpdateStatistics(buffer.Length, 0, true);
            await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
            return result;
        }

        private FilterResult CreateRateLimitExceededResult(string key, int remaining, int limit, int resetAfterSeconds)
        {
            var message = $"Rate limit exceeded for key '{key}'. Limit: {limit}, Remaining: {remaining}, Reset in: {resetAfterSeconds}s";
            return FilterResult.Error(message, FilterError.TooManyRequests);
        }

        private void AddRateLimitHeaders(ProcessingContext context, RateLimitResult result)
        {
            var headers = context.GetProperty<Dictionary<string, string>>("ResponseHeaders") ?? new Dictionary<string, string>();

            headers[$"{_config.RateLimitHeaderPrefix}Limit"] = result.Limit.ToString();
            headers[$"{_config.RateLimitHeaderPrefix}Remaining"] = result.Remaining.ToString();
            headers[$"{_config.RateLimitHeaderPrefix}Reset"] = result.ResetAt.ToUnixTimeSeconds().ToString();

            if (!result.Allowed)
            {
                headers["Retry-After"] = result.ResetAfterSeconds.ToString();
            }

            context.SetProperty("ResponseHeaders", headers);
        }

        private void CleanupExpiredLimiters(object? state)
        {
            var keysToRemove = new List<string>();
            var now = DateTimeOffset.UtcNow;

            foreach (var kvp in _limiters)
            {
                if (kvp.Value.IsExpired(now))
                {
                    keysToRemove.Add(kvp.Key);
                }
            }

            foreach (var key in keysToRemove)
            {
                _limiters.TryRemove(key, out _);
            }
        }

        public RateLimitStatistics GetStatistics()
        {
            return new RateLimitStatistics
            {
                TotalRequests = _totalRequests,
                ThrottledRequests = _throttledRequests,
                ThrottleRate = _totalRequests > 0 ? (double)_throttledRequests / _totalRequests : 0,
                ActiveLimiters = _limiters.Count,
                ConcurrentRequests = _config.MaxConcurrentRequests - _concurrencyLimiter.CurrentCount
            };
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _cleanupTimer?.Dispose();
                _concurrencyLimiter?.Dispose();
                _limiters.Clear();
            }
            base.Dispose(disposing);
        }
    }

    // Rate limiter interfaces and implementations
    internal interface IRateLimiter
    {
        Task<RateLimitResult> TryAcquireAsync(int tokens, CancellationToken cancellationToken);
        bool IsExpired(DateTimeOffset now);
    }

    internal class RateLimitResult
    {
        public bool Allowed { get; set; }
        public int Limit { get; set; }
        public int Remaining { get; set; }
        public DateTimeOffset ResetAt { get; set; }
        public int ResetAfterSeconds => Math.Max(0, (int)(ResetAt - DateTimeOffset.UtcNow).TotalSeconds);
    }

    internal class TokenBucketLimiter : IRateLimiter
    {
        private readonly int _capacity;
        private readonly double _refillRate;
        private double _tokens;
        private DateTimeOffset _lastRefill;
        private DateTimeOffset _windowStart;
        private readonly int _windowSizeSeconds;
        private readonly object _lock = new();

        public TokenBucketLimiter(int requestsPerMinute, int burstSize, int windowSizeSeconds)
        {
            _capacity = burstSize;
            _refillRate = requestsPerMinute / 60.0;
            _tokens = _capacity;
            _lastRefill = DateTimeOffset.UtcNow;
            _windowStart = DateTimeOffset.UtcNow;
            _windowSizeSeconds = windowSizeSeconds;
        }

        public Task<RateLimitResult> TryAcquireAsync(int tokens, CancellationToken cancellationToken)
        {
            lock (_lock)
            {
                var now = DateTimeOffset.UtcNow;
                RefillTokens(now);

                var allowed = _tokens >= tokens;
                if (allowed)
                {
                    _tokens -= tokens;
                }

                var resetAt = _windowStart.AddSeconds(_windowSizeSeconds);

                return Task.FromResult(new RateLimitResult
                {
                    Allowed = allowed,
                    Limit = _capacity,
                    Remaining = Math.Max(0, (int)_tokens),
                    ResetAt = resetAt
                });
            }
        }

        private void RefillTokens(DateTimeOffset now)
        {
            var elapsed = (now - _lastRefill).TotalSeconds;
            var tokensToAdd = elapsed * _refillRate;
            _tokens = Math.Min(_capacity, _tokens + tokensToAdd);
            _lastRefill = now;

            // Reset window if expired
            if (now >= _windowStart.AddSeconds(_windowSizeSeconds))
            {
                _windowStart = now;
                _tokens = _capacity;
            }
        }

        public bool IsExpired(DateTimeOffset now)
        {
            return now > _windowStart.AddSeconds(_windowSizeSeconds * 2);
        }
    }

    internal class SlidingWindowLimiter : IRateLimiter
    {
        private readonly int _limit;
        private readonly int _windowSizeSeconds;
        private readonly Queue<DateTimeOffset> _requestTimes;
        private readonly object _lock = new();

        public SlidingWindowLimiter(int requestsPerMinute, int windowSizeSeconds)
        {
            _limit = requestsPerMinute;
            _windowSizeSeconds = windowSizeSeconds;
            _requestTimes = new Queue<DateTimeOffset>();
        }

        public Task<RateLimitResult> TryAcquireAsync(int tokens, CancellationToken cancellationToken)
        {
            lock (_lock)
            {
                var now = DateTimeOffset.UtcNow;
                var windowStart = now.AddSeconds(-_windowSizeSeconds);

                // Remove expired entries
                while (_requestTimes.Count > 0 && _requestTimes.Peek() < windowStart)
                {
                    _requestTimes.Dequeue();
                }

                var allowed = _requestTimes.Count + tokens <= _limit;
                if (allowed)
                {
                    for (int i = 0; i < tokens; i++)
                    {
                        _requestTimes.Enqueue(now);
                    }
                }

                var resetAt = _requestTimes.Count > 0
                    ? _requestTimes.Peek().AddSeconds(_windowSizeSeconds)
                    : now.AddSeconds(_windowSizeSeconds);

                return Task.FromResult(new RateLimitResult
                {
                    Allowed = allowed,
                    Limit = _limit,
                    Remaining = Math.Max(0, _limit - _requestTimes.Count),
                    ResetAt = resetAt
                });
            }
        }

        public bool IsExpired(DateTimeOffset now)
        {
            lock (_lock)
            {
                return _requestTimes.Count == 0 ||
                       _requestTimes.All(t => t < now.AddSeconds(-_windowSizeSeconds * 2));
            }
        }
    }

    internal class FixedWindowLimiter : IRateLimiter
    {
        private readonly int _limit;
        private readonly int _windowSizeSeconds;
        private int _count;
        private DateTimeOffset _windowStart;
        private readonly object _lock = new();

        public FixedWindowLimiter(int requestsPerMinute, int windowSizeSeconds)
        {
            _limit = requestsPerMinute;
            _windowSizeSeconds = windowSizeSeconds;
            _windowStart = DateTimeOffset.UtcNow;
            _count = 0;
        }

        public Task<RateLimitResult> TryAcquireAsync(int tokens, CancellationToken cancellationToken)
        {
            lock (_lock)
            {
                var now = DateTimeOffset.UtcNow;

                // Reset window if expired
                if (now >= _windowStart.AddSeconds(_windowSizeSeconds))
                {
                    _windowStart = now;
                    _count = 0;
                }

                var allowed = _count + tokens <= _limit;
                if (allowed)
                {
                    _count += tokens;
                }

                var resetAt = _windowStart.AddSeconds(_windowSizeSeconds);

                return Task.FromResult(new RateLimitResult
                {
                    Allowed = allowed,
                    Limit = _limit,
                    Remaining = Math.Max(0, _limit - _count),
                    ResetAt = resetAt
                });
            }
        }

        public bool IsExpired(DateTimeOffset now)
        {
            return now > _windowStart.AddSeconds(_windowSizeSeconds * 2);
        }
    }

    internal class LeakyBucketLimiter : IRateLimiter
    {
        private readonly int _capacity;
        private readonly double _leakRate;
        private double _water;
        private DateTimeOffset _lastLeak;
        private readonly int _windowSizeSeconds;
        private readonly object _lock = new();

        public LeakyBucketLimiter(int requestsPerMinute, int burstSize, int windowSizeSeconds)
        {
            _capacity = burstSize;
            _leakRate = requestsPerMinute / 60.0;
            _water = 0;
            _lastLeak = DateTimeOffset.UtcNow;
            _windowSizeSeconds = windowSizeSeconds;
        }

        public Task<RateLimitResult> TryAcquireAsync(int tokens, CancellationToken cancellationToken)
        {
            lock (_lock)
            {
                var now = DateTimeOffset.UtcNow;
                Leak(now);

                var allowed = _water + tokens <= _capacity;
                if (allowed)
                {
                    _water += tokens;
                }

                var timeToEmpty = _water / _leakRate;
                var resetAt = now.AddSeconds(timeToEmpty);

                return Task.FromResult(new RateLimitResult
                {
                    Allowed = allowed,
                    Limit = _capacity,
                    Remaining = Math.Max(0, (int)(_capacity - _water)),
                    ResetAt = resetAt
                });
            }
        }

        private void Leak(DateTimeOffset now)
        {
            var elapsed = (now - _lastLeak).TotalSeconds;
            var leaked = elapsed * _leakRate;
            _water = Math.Max(0, _water - leaked);
            _lastLeak = now;
        }

        public bool IsExpired(DateTimeOffset now)
        {
            return now > _lastLeak.AddSeconds(_windowSizeSeconds * 2);
        }
    }

    public class RateLimitStatistics
    {
        public long TotalRequests { get; set; }
        public long ThrottledRequests { get; set; }
        public double ThrottleRate { get; set; }
        public int ActiveLimiters { get; set; }
        public int ConcurrentRequests { get; set; }
    }
}
