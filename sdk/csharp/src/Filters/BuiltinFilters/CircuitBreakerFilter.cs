using System;
using System.Linq;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum CircuitBreakerState
    {
        Closed,
        Open,
        HalfOpen
    }

    public class CircuitBreakerConfig : FilterConfigBase
    {
        public int FailureThreshold { get; set; } = 5;
        public int SuccessThreshold { get; set; } = 2;
        public TimeSpan TimeoutDuration { get; set; } = TimeSpan.FromSeconds(60);
        public TimeSpan HalfOpenTimeout { get; set; } = TimeSpan.FromSeconds(30);
        public int SamplingDuration { get; set; } = 10; // seconds
        public double FailureRateThreshold { get; set; } = 0.5; // 50%
        public int MinimumRequestCount { get; set; } = 10;
        public bool UseFailureRate { get; set; } = false;
        public Func<Exception, bool>? ShouldHandle { get; set; }
        public Func<FilterResult, bool>? ShouldHandleResult { get; set; }
        public bool IsolateByKey { get; set; } = false;
        public Func<ProcessingContext, string>? KeyExtractor { get; set; }
        public int MaxCircuitBreakers { get; set; } = 100;
        public Action<CircuitBreakerState, string>? OnStateChange { get; set; }

        public CircuitBreakerConfig() : base("CircuitBreaker", "CircuitBreakerFilter")
        {
            Priority = 70; // Run after rate limiting
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (FailureThreshold <= 0)
            {
                errors.Add("Failure threshold must be greater than 0");
            }

            if (SuccessThreshold <= 0)
            {
                errors.Add("Success threshold must be greater than 0");
            }

            if (TimeoutDuration <= TimeSpan.Zero)
            {
                errors.Add("Timeout duration must be greater than 0");
            }

            if (HalfOpenTimeout <= TimeSpan.Zero)
            {
                errors.Add("Half-open timeout must be greater than 0");
            }

            if (UseFailureRate)
            {
                if (FailureRateThreshold < 0 || FailureRateThreshold > 1)
                {
                    errors.Add("Failure rate threshold must be between 0 and 1");
                }

                if (SamplingDuration <= 0)
                {
                    errors.Add("Sampling duration must be greater than 0");
                }

                if (MinimumRequestCount <= 0)
                {
                    errors.Add("Minimum request count must be greater than 0");
                }
            }

            if (IsolateByKey && KeyExtractor == null)
            {
                errors.Add("Key extractor is required when isolating by key");
            }

            if (MaxCircuitBreakers <= 0)
            {
                errors.Add("Max circuit breakers must be greater than 0");
            }

            return errors.Count == 0;
        }
    }

    public class CircuitBreakerFilter : Filter
    {
        private readonly CircuitBreakerConfig _config;
        private readonly ConcurrentDictionary<string, CircuitBreaker> _circuitBreakers;
        private readonly Timer _cleanupTimer;

        public CircuitBreakerFilter(CircuitBreakerConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _circuitBreakers = new ConcurrentDictionary<string, CircuitBreaker>();
            
            _cleanupTimer = new Timer(
                CleanupCircuitBreakers,
                null,
                TimeSpan.FromMinutes(5),
                TimeSpan.FromMinutes(5));
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            var key = GetCircuitBreakerKey(context);
            var circuitBreaker = GetOrCreateCircuitBreaker(key);

            // Check if circuit is open
            if (circuitBreaker.State == CircuitBreakerState.Open)
            {
                if (circuitBreaker.ShouldAttemptReset())
                {
                    circuitBreaker.TransitionToHalfOpen();
                    _config.OnStateChange?.Invoke(CircuitBreakerState.HalfOpen, key);
                }
                else
                {
                    UpdateStatistics(0L, 0, false);
                    return FilterResult.Error($"Circuit breaker is open for key '{key}'", FilterError.ServiceUnavailable);
                }
            }

            try
            {
                // Execute the operation
                var result = await ExecuteWithCircuitBreakerAsync(buffer, context, circuitBreaker, key, cancellationToken);
                
                // Record success or failure
                if (IsSuccessfulResult(result))
                {
                    circuitBreaker.RecordSuccess();
                    
                    if (circuitBreaker.State == CircuitBreakerState.HalfOpen && 
                        circuitBreaker.ConsecutiveSuccesses >= _config.SuccessThreshold)
                    {
                        circuitBreaker.TransitionToClosed();
                        _config.OnStateChange?.Invoke(CircuitBreakerState.Closed, key);
                    }
                }
                else
                {
                    RecordFailure(circuitBreaker, key, null);
                }

                UpdateStatistics(buffer.Length, 0, IsSuccessfulResult(result));
                await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                return result;
            }
            catch (Exception ex)
            {
                // Check if exception should be handled
                if (ShouldHandleException(ex))
                {
                    RecordFailure(circuitBreaker, key, ex);
                }

                UpdateStatistics(0L, 0, false);
                await RaiseOnErrorAsync(ex);
                
                if (circuitBreaker.State == CircuitBreakerState.Open)
                {
                    return FilterResult.Error($"Circuit breaker opened due to failures: {ex.Message}", FilterError.ServiceUnavailable);
                }
                
                throw;
            }
        }

        private string GetCircuitBreakerKey(ProcessingContext context)
        {
            if (!_config.IsolateByKey)
            {
                return "global";
            }

            return _config.KeyExtractor?.Invoke(context) ?? "default";
        }

        private CircuitBreaker GetOrCreateCircuitBreaker(string key)
        {
            // Limit the number of circuit breakers
            if (_circuitBreakers.Count >= _config.MaxCircuitBreakers && !_circuitBreakers.ContainsKey(key))
            {
                // Remove the oldest circuit breaker
                RemoveOldestCircuitBreaker();
            }

            return _circuitBreakers.GetOrAdd(key, k => new CircuitBreaker(_config));
        }

        private void RemoveOldestCircuitBreaker()
        {
            string? oldestKey = null;
            DateTimeOffset oldestAccess = DateTimeOffset.MaxValue;

            foreach (var kvp in _circuitBreakers)
            {
                if (kvp.Value.LastAccessTime < oldestAccess)
                {
                    oldestAccess = kvp.Value.LastAccessTime;
                    oldestKey = kvp.Key;
                }
            }

            if (oldestKey != null)
            {
                _circuitBreakers.TryRemove(oldestKey, out _);
            }
        }

        private async Task<FilterResult> ExecuteWithCircuitBreakerAsync(
            byte[] buffer,
            ProcessingContext context,
            CircuitBreaker circuitBreaker,
            string key,
            CancellationToken cancellationToken)
        {
            // For simulation, we'll just continue processing
            // In a real implementation, this would execute the actual operation
            await Task.Delay(0, cancellationToken);
            return FilterResult.Continue(buffer);
        }

        private bool IsSuccessfulResult(FilterResult result)
        {
            if (_config.ShouldHandleResult != null)
            {
                return !_config.ShouldHandleResult(result);
            }

            return result.IsSuccess;
        }

        private bool ShouldHandleException(Exception ex)
        {
            if (_config.ShouldHandle != null)
            {
                return _config.ShouldHandle(ex);
            }

            // Default: handle all exceptions except cancellation
            return !(ex is OperationCanceledException);
        }

        private void RecordFailure(CircuitBreaker circuitBreaker, string key, Exception? exception)
        {
            circuitBreaker.RecordFailure();

            // Check if should open circuit
            bool shouldOpen = _config.UseFailureRate
                ? circuitBreaker.GetFailureRate() >= _config.FailureRateThreshold && 
                  circuitBreaker.GetRequestCount() >= _config.MinimumRequestCount
                : circuitBreaker.ConsecutiveFailures >= _config.FailureThreshold;

            if (shouldOpen && circuitBreaker.State != CircuitBreakerState.Open)
            {
                circuitBreaker.TransitionToOpen(_config.TimeoutDuration);
                _config.OnStateChange?.Invoke(CircuitBreakerState.Open, key);
            }
        }

        private void CleanupCircuitBreakers(object? state)
        {
            var keysToRemove = new List<string>();
            var now = DateTimeOffset.UtcNow;
            var maxIdleTime = TimeSpan.FromHours(1);

            foreach (var kvp in _circuitBreakers)
            {
                if (kvp.Value.State == CircuitBreakerState.Closed && 
                    now - kvp.Value.LastAccessTime > maxIdleTime)
                {
                    keysToRemove.Add(kvp.Key);
                }
            }

            foreach (var key in keysToRemove)
            {
                _circuitBreakers.TryRemove(key, out _);
            }
        }

        public CircuitBreakerStatistics GetStatistics(string? key = null)
        {
            if (key != null && _circuitBreakers.TryGetValue(key, out var breaker))
            {
                return new CircuitBreakerStatistics
                {
                    State = breaker.State,
                    FailureCount = breaker.ConsecutiveFailures,
                    SuccessCount = breaker.ConsecutiveSuccesses,
                    LastFailureTime = breaker.LastFailureTime,
                    NextRetryTime = breaker.NextRetryTime,
                    FailureRate = breaker.GetFailureRate(),
                    RequestCount = breaker.GetRequestCount()
                };
            }

            // Return aggregated statistics
            var stats = new CircuitBreakerStatistics
            {
                CircuitBreakerCount = _circuitBreakers.Count
            };

            foreach (var cb in _circuitBreakers.Values)
            {
                if (cb.State == CircuitBreakerState.Open)
                    stats.OpenCircuits++;
                else if (cb.State == CircuitBreakerState.HalfOpen)
                    stats.HalfOpenCircuits++;
                else
                    stats.ClosedCircuits++;

                stats.TotalFailures += cb.ConsecutiveFailures;
                stats.TotalSuccesses += cb.ConsecutiveSuccesses;
            }

            return stats;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _cleanupTimer?.Dispose();
                _circuitBreakers.Clear();
            }
            base.Dispose(disposing);
        }
    }

    internal class CircuitBreaker
    {
        private readonly CircuitBreakerConfig _config;
        private CircuitBreakerState _state;
        private int _consecutiveFailures;
        private int _consecutiveSuccesses;
        private DateTimeOffset _openedAt;
        private DateTimeOffset _lastFailureTime;
        private DateTimeOffset _lastAccessTime;
        private readonly object _lock = new();
        
        // For failure rate calculation
        private readonly Queue<RequestResult> _requestHistory;
        private readonly TimeSpan _samplingDuration;

        public CircuitBreakerState State => _state;
        public int ConsecutiveFailures => _consecutiveFailures;
        public int ConsecutiveSuccesses => _consecutiveSuccesses;
        public DateTimeOffset LastFailureTime => _lastFailureTime;
        public DateTimeOffset LastAccessTime => _lastAccessTime;
        public DateTimeOffset? NextRetryTime => _state == CircuitBreakerState.Open ? _openedAt + _config.TimeoutDuration : null;

        public CircuitBreaker(CircuitBreakerConfig config)
        {
            _config = config;
            _state = CircuitBreakerState.Closed;
            _consecutiveFailures = 0;
            _consecutiveSuccesses = 0;
            _lastAccessTime = DateTimeOffset.UtcNow;
            _samplingDuration = TimeSpan.FromSeconds(config.SamplingDuration);
            _requestHistory = new Queue<RequestResult>();
        }

        public void RecordSuccess()
        {
            lock (_lock)
            {
                _lastAccessTime = DateTimeOffset.UtcNow;
                _consecutiveSuccesses++;
                _consecutiveFailures = 0;
                
                if (_config.UseFailureRate)
                {
                    RecordRequest(true);
                }
            }
        }

        public void RecordFailure()
        {
            lock (_lock)
            {
                _lastAccessTime = DateTimeOffset.UtcNow;
                _lastFailureTime = DateTimeOffset.UtcNow;
                _consecutiveFailures++;
                _consecutiveSuccesses = 0;
                
                if (_config.UseFailureRate)
                {
                    RecordRequest(false);
                }
            }
        }

        public bool ShouldAttemptReset()
        {
            lock (_lock)
            {
                if (_state != CircuitBreakerState.Open)
                    return false;

                return DateTimeOffset.UtcNow >= _openedAt + _config.TimeoutDuration;
            }
        }

        public void TransitionToOpen(TimeSpan timeout)
        {
            lock (_lock)
            {
                _state = CircuitBreakerState.Open;
                _openedAt = DateTimeOffset.UtcNow;
                _consecutiveSuccesses = 0;
            }
        }

        public void TransitionToHalfOpen()
        {
            lock (_lock)
            {
                _state = CircuitBreakerState.HalfOpen;
                _consecutiveSuccesses = 0;
                _consecutiveFailures = 0;
            }
        }

        public void TransitionToClosed()
        {
            lock (_lock)
            {
                _state = CircuitBreakerState.Closed;
                _consecutiveFailures = 0;
                _consecutiveSuccesses = 0;
                _requestHistory.Clear();
            }
        }

        public double GetFailureRate()
        {
            lock (_lock)
            {
                CleanupOldRequests();
                
                if (_requestHistory.Count == 0)
                    return 0;

                var failures = _requestHistory.Count(r => !r.Success);
                return (double)failures / _requestHistory.Count;
            }
        }

        public int GetRequestCount()
        {
            lock (_lock)
            {
                CleanupOldRequests();
                return _requestHistory.Count;
            }
        }

        private void RecordRequest(bool success)
        {
            _requestHistory.Enqueue(new RequestResult
            {
                Success = success,
                Timestamp = DateTimeOffset.UtcNow
            });
            
            CleanupOldRequests();
        }

        private void CleanupOldRequests()
        {
            var cutoff = DateTimeOffset.UtcNow - _samplingDuration;
            
            while (_requestHistory.Count > 0 && _requestHistory.Peek().Timestamp < cutoff)
            {
                _requestHistory.Dequeue();
            }
        }

        private class RequestResult
        {
            public bool Success { get; set; }
            public DateTimeOffset Timestamp { get; set; }
        }
    }

    public class CircuitBreakerStatistics
    {
        public CircuitBreakerState State { get; set; }
        public int FailureCount { get; set; }
        public int SuccessCount { get; set; }
        public DateTimeOffset LastFailureTime { get; set; }
        public DateTimeOffset? NextRetryTime { get; set; }
        public double FailureRate { get; set; }
        public int RequestCount { get; set; }
        
        // Aggregated statistics
        public int CircuitBreakerCount { get; set; }
        public int OpenCircuits { get; set; }
        public int HalfOpenCircuits { get; set; }
        public int ClosedCircuits { get; set; }
        public int TotalFailures { get; set; }
        public int TotalSuccesses { get; set; }
    }
}