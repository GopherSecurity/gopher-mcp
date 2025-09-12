using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum RetryStrategy
    {
        Exponential,
        Linear,
        Constant,
        Custom
    }

    public class RetryConfig : FilterConfigBase
    {
        public RetryStrategy Strategy { get; set; } = RetryStrategy.Exponential;
        public int MaxAttempts { get; set; } = 3;
        public TimeSpan InitialDelay { get; set; } = TimeSpan.FromSeconds(1);
        public TimeSpan MaxDelay { get; set; } = TimeSpan.FromSeconds(30);
        public double BackoffMultiplier { get; set; } = 2.0;
        public TimeSpan LinearIncrement { get; set; } = TimeSpan.FromSeconds(1);
        public bool AddJitter { get; set; } = true;
        public double JitterFactor { get; set; } = 0.1; // 10% jitter
        public Func<int, TimeSpan>? CustomDelayCalculator { get; set; }
        public Func<Exception, bool>? ShouldRetry { get; set; }
        public Func<FilterResult, bool>? ShouldRetryResult { get; set; }
        public List<Type> RetryableExceptions { get; set; } = new()
        {
            typeof(TimeoutException),
            typeof(OperationCanceledException)
        };
        public List<FilterError> RetryableErrors { get; set; } = new()
        {
            FilterError.ServiceUnavailable,
            FilterError.Timeout,
            FilterError.NetworkError
        };
        public bool RetryOnTimeout { get; set; } = true;
        public TimeSpan? PerAttemptTimeout { get; set; }
        public Action<int, Exception?>? OnRetryAttempt { get; set; }
        public bool CumulativeTimeout { get; set; } = false;
        public TimeSpan? TotalTimeout { get; set; }

        public RetryConfig() : base("Retry", "RetryFilter")
        {
            Priority = 60; // Run after circuit breaker
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (MaxAttempts <= 0)
            {
                errors.Add("Max attempts must be greater than 0");
            }

            if (InitialDelay < TimeSpan.Zero)
            {
                errors.Add("Initial delay cannot be negative");
            }

            if (MaxDelay < InitialDelay)
            {
                errors.Add("Max delay must be greater than or equal to initial delay");
            }

            if (Strategy == RetryStrategy.Exponential && BackoffMultiplier <= 1)
            {
                errors.Add("Backoff multiplier must be greater than 1 for exponential strategy");
            }

            if (Strategy == RetryStrategy.Linear && LinearIncrement <= TimeSpan.Zero)
            {
                errors.Add("Linear increment must be greater than 0");
            }

            if (Strategy == RetryStrategy.Custom && CustomDelayCalculator == null)
            {
                errors.Add("Custom delay calculator is required for custom strategy");
            }

            if (JitterFactor < 0 || JitterFactor > 1)
            {
                errors.Add("Jitter factor must be between 0 and 1");
            }

            if (CumulativeTimeout && TotalTimeout == null)
            {
                errors.Add("Total timeout is required when cumulative timeout is enabled");
            }

            return errors.Count == 0;
        }
    }

    public class RetryFilter : Filter
    {
        private readonly RetryConfig _config;
        private readonly Random _random = new();

        public RetryFilter(RetryConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            var stopwatch = Stopwatch.StartNew();
            var totalTimeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);

            if (_config.CumulativeTimeout && _config.TotalTimeout.HasValue)
            {
                totalTimeoutCts.CancelAfter(_config.TotalTimeout.Value);
            }

            Exception? lastException = null;
            FilterResult? lastResult = null;
            var attemptNumber = 0;

            try
            {
                while (attemptNumber < _config.MaxAttempts)
                {
                    attemptNumber++;

                    // Create per-attempt timeout if configured
                    using var attemptCts = CancellationTokenSource.CreateLinkedTokenSource(totalTimeoutCts.Token);
                    if (_config.PerAttemptTimeout.HasValue)
                    {
                        attemptCts.CancelAfter(_config.PerAttemptTimeout.Value);
                    }

                    try
                    {
                        // Store attempt information in context
                        context.SetProperty("RetryAttempt", attemptNumber);
                        context.SetProperty("RetryMaxAttempts", _config.MaxAttempts);

                        // Execute the operation
                        var result = await ExecuteOperationAsync(buffer, context, attemptCts.Token);

                        // Check if we should retry based on the result
                        if (ShouldRetryResult(result))
                        {
                            lastResult = result;

                            if (attemptNumber < _config.MaxAttempts)
                            {
                                _config.OnRetryAttempt?.Invoke(attemptNumber, null);

                                var delay = CalculateDelay(attemptNumber);
                                await DelayAsync(delay, totalTimeoutCts.Token);
                                continue;
                            }
                        }

                        // Success - return the result
                        UpdateStatistics(buffer.Length, 0, true);
                        await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                        return result;
                    }
                    catch (OperationCanceledException) when (attemptCts.IsCancellationRequested && !totalTimeoutCts.IsCancellationRequested)
                    {
                        // Per-attempt timeout
                        if (_config.RetryOnTimeout && attemptNumber < _config.MaxAttempts)
                        {
                            lastException = new TimeoutException($"Attempt {attemptNumber} timed out after {_config.PerAttemptTimeout}");
                            _config.OnRetryAttempt?.Invoke(attemptNumber, lastException);

                            var delay = CalculateDelay(attemptNumber);
                            await DelayAsync(delay, totalTimeoutCts.Token);
                            continue;
                        }
                        throw;
                    }
                    catch (Exception ex)
                    {
                        lastException = ex;

                        // Check if we should retry this exception
                        if (ShouldRetryException(ex) && attemptNumber < _config.MaxAttempts)
                        {
                            _config.OnRetryAttempt?.Invoke(attemptNumber, ex);

                            var delay = CalculateDelay(attemptNumber);
                            await DelayAsync(delay, totalTimeoutCts.Token);
                            continue;
                        }

                        // Don't retry - rethrow
                        throw;
                    }
                }

                // All attempts exhausted
                UpdateStatistics(0L, 1, false);

                if (lastException != null)
                {
                    await RaiseOnErrorAsync(lastException);
                    throw new RetryExhaustedException($"Retry filter exhausted after {attemptNumber} attempts", lastException)
                    {
                        Attempts = attemptNumber,
                        TotalDuration = stopwatch.Elapsed
                    };
                }

                if (lastResult != null)
                {
                    return lastResult;
                }

                return FilterResult.Error($"Retry filter exhausted after {attemptNumber} attempts", FilterError.RetryExhausted);
            }
            catch (OperationCanceledException) when (totalTimeoutCts.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
            {
                // Total timeout exceeded
                var message = $"Total retry timeout of {_config.TotalTimeout} exceeded after {attemptNumber} attempts";
                UpdateStatistics(0L, 1, false);
                await RaiseOnErrorAsync(new TimeoutException(message));
                return FilterResult.Error(message, FilterError.Timeout);
            }
            finally
            {
                totalTimeoutCts?.Dispose();
                stopwatch.Stop();

                // Store retry statistics in context
                context.SetProperty("RetryTotalAttempts", attemptNumber);
                context.SetProperty("RetryTotalDuration", stopwatch.Elapsed);
            }
        }

        private async Task<FilterResult> ExecuteOperationAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken)
        {
            // In a real implementation, this would execute the actual downstream operation
            // For now, we'll simulate it
            await Task.Delay(100, cancellationToken);

            // Simulate occasional failures for testing
            var random = new Random().Next(100);
            if (random < 10) // 10% failure rate
            {
                throw new TimeoutException("Simulated timeout");
            }
            if (random < 20) // Additional 10% service unavailable
            {
                return FilterResult.Error("Service temporarily unavailable", FilterError.ServiceUnavailable);
            }

            return FilterResult.Continue(buffer);
        }

        private bool ShouldRetryException(Exception ex)
        {
            // Check custom retry condition first
            if (_config.ShouldRetry != null)
            {
                return _config.ShouldRetry(ex);
            }

            // Check if exception type is retryable
            var exceptionType = ex.GetType();
            return _config.RetryableExceptions.Any(retryableType =>
                retryableType.IsAssignableFrom(exceptionType));
        }

        private bool ShouldRetryResult(FilterResult result)
        {
            // Check custom retry condition first
            if (_config.ShouldRetryResult != null)
            {
                return _config.ShouldRetryResult(result);
            }

            // If successful, don't retry
            if (result.IsSuccess)
            {
                return false;
            }

            // Check if error is retryable
            return _config.RetryableErrors.Contains(result.ErrorCode);
        }

        private TimeSpan CalculateDelay(int attemptNumber)
        {
            TimeSpan baseDelay = _config.Strategy switch
            {
                RetryStrategy.Exponential => CalculateExponentialDelay(attemptNumber),
                RetryStrategy.Linear => CalculateLinearDelay(attemptNumber),
                RetryStrategy.Constant => _config.InitialDelay,
                RetryStrategy.Custom => _config.CustomDelayCalculator?.Invoke(attemptNumber) ?? TimeSpan.Zero,
                _ => _config.InitialDelay
            };

            // Apply max delay cap
            baseDelay = TimeSpan.FromMilliseconds(Math.Min(baseDelay.TotalMilliseconds, _config.MaxDelay.TotalMilliseconds));

            // Add jitter if configured
            if (_config.AddJitter && _config.JitterFactor > 0)
            {
                baseDelay = AddJitter(baseDelay);
            }

            return baseDelay;
        }

        private TimeSpan CalculateExponentialDelay(int attemptNumber)
        {
            // Calculate exponential backoff: initialDelay * (multiplier ^ (attempt - 1))
            var delayMs = _config.InitialDelay.TotalMilliseconds * Math.Pow(_config.BackoffMultiplier, attemptNumber - 1);
            return TimeSpan.FromMilliseconds(delayMs);
        }

        private TimeSpan CalculateLinearDelay(int attemptNumber)
        {
            // Calculate linear backoff: initialDelay + (increment * (attempt - 1))
            var delayMs = _config.InitialDelay.TotalMilliseconds + (_config.LinearIncrement.TotalMilliseconds * (attemptNumber - 1));
            return TimeSpan.FromMilliseconds(delayMs);
        }

        private TimeSpan AddJitter(TimeSpan delay)
        {
            // Add random jitter: delay ± (delay * jitterFactor)
            var jitterRange = delay.TotalMilliseconds * _config.JitterFactor;
            var jitter = (_random.NextDouble() * 2 - 1) * jitterRange; // Random between -jitterRange and +jitterRange
            var delayWithJitter = delay.TotalMilliseconds + jitter;
            return TimeSpan.FromMilliseconds(Math.Max(0, delayWithJitter));
        }

        private async Task DelayAsync(TimeSpan delay, CancellationToken cancellationToken)
        {
            if (delay <= TimeSpan.Zero)
            {
                return;
            }

            try
            {
                await Task.Delay(delay, cancellationToken);
            }
            catch (OperationCanceledException)
            {
                // Delay was cancelled, probably due to total timeout
                throw;
            }
        }

        protected override void Dispose(bool disposing)
        {
            // No resources to dispose
            base.Dispose(disposing);
        }
    }

    public class RetryExhaustedException : Exception
    {
        public int Attempts { get; set; }
        public TimeSpan TotalDuration { get; set; }

        public RetryExhaustedException(string message) : base(message)
        {
        }

        public RetryExhaustedException(string message, Exception innerException) : base(message, innerException)
        {
        }
    }

    public static class RetryPolicies
    {
        public static RetryConfig Default => new()
        {
            Strategy = RetryStrategy.Exponential,
            MaxAttempts = 3,
            InitialDelay = TimeSpan.FromSeconds(1),
            MaxDelay = TimeSpan.FromSeconds(30),
            BackoffMultiplier = 2.0,
            AddJitter = true
        };

        public static RetryConfig Aggressive => new()
        {
            Strategy = RetryStrategy.Exponential,
            MaxAttempts = 5,
            InitialDelay = TimeSpan.FromMilliseconds(100),
            MaxDelay = TimeSpan.FromSeconds(10),
            BackoffMultiplier = 1.5,
            AddJitter = true
        };

        public static RetryConfig Conservative => new()
        {
            Strategy = RetryStrategy.Linear,
            MaxAttempts = 2,
            InitialDelay = TimeSpan.FromSeconds(5),
            MaxDelay = TimeSpan.FromSeconds(60),
            LinearIncrement = TimeSpan.FromSeconds(5),
            AddJitter = false
        };

        public static RetryConfig NoRetry => new()
        {
            MaxAttempts = 1
        };

        public static RetryConfig CreateWithFixedDelay(int maxAttempts, TimeSpan delay) => new()
        {
            Strategy = RetryStrategy.Constant,
            MaxAttempts = maxAttempts,
            InitialDelay = delay,
            AddJitter = false
        };

        public static RetryConfig CreateWithExponentialBackoff(int maxAttempts, TimeSpan initialDelay, double multiplier = 2.0) => new()
        {
            Strategy = RetryStrategy.Exponential,
            MaxAttempts = maxAttempts,
            InitialDelay = initialDelay,
            BackoffMultiplier = multiplier,
            MaxDelay = TimeSpan.FromMinutes(5),
            AddJitter = true
        };
    }
}
