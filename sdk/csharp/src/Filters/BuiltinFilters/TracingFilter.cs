using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum TracingProvider
    {
        OpenTelemetry,
        Jaeger,
        Zipkin,
        Custom
    }

    public enum SamplingStrategy
    {
        AlwaysOn,
        AlwaysOff,
        Probabilistic,
        RateLimiting,
        ParentBased,
        Custom
    }

    public class TracingConfig : FilterConfigBase
    {
        public TracingProvider Provider { get; set; } = TracingProvider.OpenTelemetry;
        public string ServiceName { get; set; } = "mcp-service";
        public string ServiceVersion { get; set; } = "1.0.0";
        public SamplingStrategy SamplingStrategy { get; set; } = SamplingStrategy.Probabilistic;
        public double SamplingProbability { get; set; } = 0.1; // 10% sampling
        public int RateLimitPerSecond { get; set; } = 100;
        public bool PropagateContext { get; set; } = true;
        public List<string> PropagationHeaders { get; set; } = new()
        {
            "traceparent",
            "tracestate",
            "baggage"
        };
        public Dictionary<string, string> ResourceAttributes { get; set; } = new();
        public bool RecordExceptions { get; set; } = true;
        public bool RecordEvents { get; set; } = true;
        public int MaxAttributeLength { get; set; } = 1024;
        public int MaxEventCount { get; set; } = 128;
        public int MaxLinkCount { get; set; } = 128;
        public int MaxAttributeCount { get; set; } = 128;
        public string? ExporterEndpoint { get; set; }
        public bool EnableBatching { get; set; } = true;
        public int BatchSize { get; set; } = 512;
        public int BatchDelayMilliseconds { get; set; } = 5000;

        public TracingConfig() : base("Tracing", "TracingFilter")
        {
            Priority = 95; // High priority to run early
            InitializeDefaultAttributes();
        }

        private void InitializeDefaultAttributes()
        {
            ResourceAttributes["service.name"] = ServiceName;
            ResourceAttributes["service.version"] = ServiceVersion;
            ResourceAttributes["deployment.environment"] = Environment.GetEnvironmentVariable("ENVIRONMENT") ?? "development";
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (string.IsNullOrEmpty(ServiceName))
            {
                errors.Add("Service name is required for tracing");
            }

            if (SamplingStrategy == SamplingStrategy.Probabilistic)
            {
                if (SamplingProbability < 0 || SamplingProbability > 1)
                {
                    errors.Add("Sampling probability must be between 0 and 1");
                }
            }

            if (SamplingStrategy == SamplingStrategy.RateLimiting)
            {
                if (RateLimitPerSecond <= 0)
                {
                    errors.Add("Rate limit must be greater than 0");
                }
            }

            if (Provider != TracingProvider.Custom && string.IsNullOrEmpty(ExporterEndpoint))
            {
                errors.Add($"Exporter endpoint is required for {Provider} provider");
            }

            return errors.Count == 0;
        }
    }

    public class TracingFilter : Filter
    {
        private readonly TracingConfig _config;
        private readonly ISampler _sampler;
        private readonly ConcurrentDictionary<string, SpanContext> _activeSpans;
        private readonly ISpanExporter _exporter;
        private readonly Timer _exportTimer;
        private readonly ConcurrentQueue<CompletedSpan> _completedSpans;
        private long _traceIdCounter;

        public TracingFilter(TracingConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _activeSpans = new ConcurrentDictionary<string, SpanContext>();
            _completedSpans = new ConcurrentQueue<CompletedSpan>();
            _sampler = CreateSampler();
            _exporter = CreateExporter();
            
            if (_config.EnableBatching)
            {
                _exportTimer = new Timer(
                    ExportSpans,
                    null,
                    TimeSpan.FromMilliseconds(_config.BatchDelayMilliseconds),
                    TimeSpan.FromMilliseconds(_config.BatchDelayMilliseconds));
            }
            else
            {
                _exportTimer = new Timer(_ => { }, null, Timeout.Infinite, Timeout.Infinite);
            }
        }

        private ISampler CreateSampler()
        {
            return _config.SamplingStrategy switch
            {
                SamplingStrategy.AlwaysOn => new AlwaysOnSampler(),
                SamplingStrategy.AlwaysOff => new AlwaysOffSampler(),
                SamplingStrategy.Probabilistic => new ProbabilisticSampler(_config.SamplingProbability),
                SamplingStrategy.RateLimiting => new RateLimitingSampler(_config.RateLimitPerSecond),
                SamplingStrategy.ParentBased => new ParentBasedSampler(new ProbabilisticSampler(_config.SamplingProbability)),
                _ => new AlwaysOnSampler()
            };
        }

        private ISpanExporter CreateExporter()
        {
            // In a real implementation, this would create the appropriate exporter
            // For now, we'll use a mock exporter
            return new MockSpanExporter(_config.ExporterEndpoint);
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            SpanContext? span = null;
            
            try
            {
                // Extract or create trace context
                var traceContext = ExtractOrCreateTraceContext(context);
                
                // Make sampling decision
                var shouldSample = _sampler.ShouldSample(traceContext, context);
                
                if (shouldSample)
                {
                    // Create new span
                    span = CreateSpan(traceContext, context);
                    _activeSpans[span.SpanId] = span;
                    
                    // Store span in context for downstream filters
                    context.SetProperty("TraceId", span.TraceId);
                    context.SetProperty("SpanId", span.SpanId);
                    context.SetProperty("ParentSpanId", span.ParentSpanId);
                    
                    // Add span attributes
                    AddSpanAttributes(span, context);
                    
                    // Record span start event
                    if (_config.RecordEvents)
                    {
                        span.AddEvent("filter.start", new Dictionary<string, object>
                        {
                            ["filter.name"] = _config.Name,
                            ["buffer.size"] = buffer.Length
                        });
                    }
                }

                // Propagate context if configured
                if (_config.PropagateContext && span != null)
                {
                    PropagateContext(span, context);
                }

                // Continue processing
                var result = FilterResult.Continue(buffer);

                // Complete span if sampling
                if (span != null)
                {
                    span.End();
                    
                    if (_config.RecordEvents)
                    {
                        span.AddEvent("filter.complete", new Dictionary<string, object>
                        {
                            ["result.success"] = result.IsSuccess,
                            ["result.buffer.size"] = result.Data?.Length ?? 0
                        });
                    }
                    
                    RecordSpan(span);
                }

                UpdateStatistics(buffer.Length, 0, true);
                await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                return result;
            }
            catch (Exception ex)
            {
                // Record exception in span
                if (span != null && _config.RecordExceptions)
                {
                    span.RecordException(ex);
                    span.SetStatus(SpanStatus.Error, ex.Message);
                    span.End();
                    RecordSpan(span);
                }

                UpdateStatistics(0L, 0, false);
                await RaiseOnErrorAsync(ex);
                return FilterResult.Error($"Tracing error: {ex.Message}", FilterError.InternalError);
            }
        }

        private TraceContext ExtractOrCreateTraceContext(ProcessingContext context)
        {
            // Try to extract trace context from propagation headers
            var traceParent = context.GetProperty<string>("traceparent");
            if (!string.IsNullOrEmpty(traceParent))
            {
                return ParseTraceParent(traceParent);
            }

            // Check for other tracing headers (Jaeger, Zipkin, etc.)
            var traceId = context.GetProperty<string>("X-Trace-Id") ?? 
                         context.GetProperty<string>("X-B3-TraceId");
            
            if (!string.IsNullOrEmpty(traceId))
            {
                var spanId = context.GetProperty<string>("X-Span-Id") ?? 
                            context.GetProperty<string>("X-B3-SpanId");
                return new TraceContext
                {
                    TraceId = traceId,
                    ParentSpanId = spanId,
                    Sampled = true
                };
            }

            // Create new trace context
            return new TraceContext
            {
                TraceId = GenerateTraceId(),
                ParentSpanId = null,
                Sampled = null
            };
        }

        private TraceContext ParseTraceParent(string traceParent)
        {
            // W3C Trace Context format: version-traceid-parentid-flags
            var parts = traceParent.Split('-');
            if (parts.Length >= 4)
            {
                return new TraceContext
                {
                    TraceId = parts[1],
                    ParentSpanId = parts[2],
                    Sampled = (Convert.ToInt32(parts[3], 16) & 0x01) == 0x01
                };
            }

            return new TraceContext
            {
                TraceId = GenerateTraceId(),
                ParentSpanId = null,
                Sampled = null
            };
        }

        private SpanContext CreateSpan(TraceContext traceContext, ProcessingContext context)
        {
            var span = new SpanContext
            {
                TraceId = traceContext.TraceId,
                SpanId = GenerateSpanId(),
                ParentSpanId = traceContext.ParentSpanId,
                SpanName = context.GetProperty<string>("Method") ?? "unknown",
                StartTime = DateTimeOffset.UtcNow,
                Attributes = new Dictionary<string, object>(),
                Events = new List<SpanEvent>(),
                Links = new List<SpanLink>(),
                Status = SpanStatus.Unset
            };

            // Add resource attributes
            foreach (var attr in _config.ResourceAttributes)
            {
                span.Attributes[attr.Key] = attr.Value;
            }

            return span;
        }

        private void AddSpanAttributes(SpanContext span, ProcessingContext context)
        {
            // Add standard attributes
            span.SetAttribute("http.method", context.GetProperty<string>("Method") ?? "unknown");
            span.SetAttribute("http.url", context.GetProperty<string>("Path") ?? "/");
            span.SetAttribute("http.scheme", context.GetProperty<string>("Scheme") ?? "http");
            span.SetAttribute("http.host", context.GetProperty<string>("Host") ?? "localhost");
            span.SetAttribute("http.user_agent", context.GetProperty<string>("UserAgent") ?? "");
            span.SetAttribute("net.peer.ip", context.GetProperty<string>("ClientIp") ?? "");
            span.SetAttribute("filter.name", _config.Name);
            span.SetAttribute("filter.type", _config.Type);

            // Add custom attributes from context
            var customAttributes = context.GetProperty<Dictionary<string, object>>("TraceAttributes");
            if (customAttributes != null)
            {
                foreach (var attr in customAttributes.Take(_config.MaxAttributeCount))
                {
                    span.SetAttribute(attr.Key, attr.Value);
                }
            }
        }

        private void PropagateContext(SpanContext span, ProcessingContext context)
        {
            // W3C Trace Context propagation
            var traceParent = $"00-{span.TraceId}-{span.SpanId}-01";
            context.SetProperty("traceparent", traceParent);

            // Add to response headers if applicable
            var responseHeaders = context.GetProperty<Dictionary<string, string>>("ResponseHeaders");
            if (responseHeaders != null)
            {
                responseHeaders["traceparent"] = traceParent;
                
                // Add tracestate if present
                var traceState = context.GetProperty<string>("tracestate");
                if (!string.IsNullOrEmpty(traceState))
                {
                    responseHeaders["tracestate"] = traceState;
                }
            }
        }

        private void RecordSpan(SpanContext span)
        {
            _activeSpans.TryRemove(span.SpanId, out _);
            
            var completedSpan = new CompletedSpan
            {
                TraceId = span.TraceId,
                SpanId = span.SpanId,
                ParentSpanId = span.ParentSpanId,
                Name = span.SpanName,
                StartTime = span.StartTime,
                EndTime = span.EndTime ?? DateTimeOffset.UtcNow,
                Duration = (span.EndTime ?? DateTimeOffset.UtcNow) - span.StartTime,
                Attributes = new Dictionary<string, object>(span.Attributes),
                Events = new List<SpanEvent>(span.Events),
                Links = new List<SpanLink>(span.Links),
                Status = span.Status,
                StatusMessage = span.StatusMessage
            };

            _completedSpans.Enqueue(completedSpan);

            // Export immediately if batching is disabled
            if (!_config.EnableBatching)
            {
                ExportSpans(null);
            }
            else if (_completedSpans.Count >= _config.BatchSize)
            {
                // Export if batch size reached
                ExportSpans(null);
            }
        }

        private void ExportSpans(object? state)
        {
            var spansToExport = new List<CompletedSpan>();
            
            while (_completedSpans.TryDequeue(out var span) && spansToExport.Count < _config.BatchSize)
            {
                spansToExport.Add(span);
            }

            if (spansToExport.Count > 0)
            {
                _exporter.Export(spansToExport);
            }
        }

        private string GenerateTraceId()
        {
            var id = Interlocked.Increment(ref _traceIdCounter);
            var timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return $"{timestamp:x16}{id:x16}";
        }

        private string GenerateSpanId()
        {
#if NET6_0_OR_GREATER
            var random = new Random().NextInt64();
            return $"{random:x16}";
#else
            // For older frameworks, combine two 32-bit random numbers to get a 64-bit value
            var random = new Random();
            var high = (long)random.Next() << 32;
            var low = (long)random.Next();
            var value = high | low;
            return $"{value:x16}";
#endif
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                // Export remaining spans
                ExportSpans(null);
                
                _exportTimer?.Dispose();
                _exporter?.Dispose();
                _activeSpans.Clear();
            }
            base.Dispose(disposing);
        }
    }

    // Supporting classes
    internal class TraceContext
    {
        public string TraceId { get; set; } = string.Empty;
        public string? ParentSpanId { get; set; }
        public bool? Sampled { get; set; }
    }

    internal class SpanContext
    {
        public string TraceId { get; set; } = string.Empty;
        public string SpanId { get; set; } = string.Empty;
        public string? ParentSpanId { get; set; }
        public string SpanName { get; set; } = string.Empty;
        public DateTimeOffset StartTime { get; set; }
        public DateTimeOffset? EndTime { get; set; }
        public Dictionary<string, object> Attributes { get; set; } = new();
        public List<SpanEvent> Events { get; set; } = new();
        public List<SpanLink> Links { get; set; } = new();
        public SpanStatus Status { get; set; }
        public string? StatusMessage { get; set; }

        public void SetAttribute(string key, object value)
        {
            Attributes[key] = value;
        }

        public void AddEvent(string name, Dictionary<string, object>? attributes = null)
        {
            Events.Add(new SpanEvent
            {
                Name = name,
                Timestamp = DateTimeOffset.UtcNow,
                Attributes = attributes ?? new Dictionary<string, object>()
            });
        }

        public void RecordException(Exception exception)
        {
            AddEvent("exception", new Dictionary<string, object>
            {
                ["exception.type"] = exception.GetType().FullName ?? "Unknown",
                ["exception.message"] = exception.Message,
                ["exception.stacktrace"] = exception.StackTrace ?? ""
            });
        }

        public void SetStatus(SpanStatus status, string? message = null)
        {
            Status = status;
            StatusMessage = message;
        }

        public void End()
        {
            EndTime = DateTimeOffset.UtcNow;
        }
    }

    internal class CompletedSpan
    {
        public string TraceId { get; set; } = string.Empty;
        public string SpanId { get; set; } = string.Empty;
        public string? ParentSpanId { get; set; }
        public string Name { get; set; } = string.Empty;
        public DateTimeOffset StartTime { get; set; }
        public DateTimeOffset EndTime { get; set; }
        public TimeSpan Duration { get; set; }
        public Dictionary<string, object> Attributes { get; set; } = new();
        public List<SpanEvent> Events { get; set; } = new();
        public List<SpanLink> Links { get; set; } = new();
        public SpanStatus Status { get; set; }
        public string? StatusMessage { get; set; }
    }

    internal class SpanEvent
    {
        public string Name { get; set; } = string.Empty;
        public DateTimeOffset Timestamp { get; set; }
        public Dictionary<string, object> Attributes { get; set; } = new();
    }

    internal class SpanLink
    {
        public string TraceId { get; set; } = string.Empty;
        public string SpanId { get; set; } = string.Empty;
        public Dictionary<string, object> Attributes { get; set; } = new();
    }

    internal enum SpanStatus
    {
        Unset,
        Ok,
        Error
    }

    // Sampler interfaces and implementations
    internal interface ISampler
    {
        bool ShouldSample(TraceContext traceContext, ProcessingContext context);
    }

    internal class AlwaysOnSampler : ISampler
    {
        public bool ShouldSample(TraceContext traceContext, ProcessingContext context) => true;
    }

    internal class AlwaysOffSampler : ISampler
    {
        public bool ShouldSample(TraceContext traceContext, ProcessingContext context) => false;
    }

    internal class ProbabilisticSampler : ISampler
    {
        private readonly double _probability;
        private readonly Random _random = new();

        public ProbabilisticSampler(double probability)
        {
            _probability = Math.Max(0, Math.Min(1, probability));
        }

        public bool ShouldSample(TraceContext traceContext, ProcessingContext context)
        {
            return _random.NextDouble() < _probability;
        }
    }

    internal class RateLimitingSampler : ISampler
    {
        private readonly int _maxPerSecond;
        private long _currentSecond;
        private int _currentCount;
        private readonly object _lock = new();

        public RateLimitingSampler(int maxPerSecond)
        {
            _maxPerSecond = maxPerSecond;
        }

        public bool ShouldSample(TraceContext traceContext, ProcessingContext context)
        {
            lock (_lock)
            {
                var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
                
                if (now != _currentSecond)
                {
                    _currentSecond = now;
                    _currentCount = 0;
                }

                if (_currentCount < _maxPerSecond)
                {
                    _currentCount++;
                    return true;
                }

                return false;
            }
        }
    }

    internal class ParentBasedSampler : ISampler
    {
        private readonly ISampler _rootSampler;

        public ParentBasedSampler(ISampler rootSampler)
        {
            _rootSampler = rootSampler;
        }

        public bool ShouldSample(TraceContext traceContext, ProcessingContext context)
        {
            // If parent was sampled, sample this span
            if (traceContext.Sampled.HasValue)
            {
                return traceContext.Sampled.Value;
            }

            // If no parent, use root sampler
            if (string.IsNullOrEmpty(traceContext.ParentSpanId))
            {
                return _rootSampler.ShouldSample(traceContext, context);
            }

            // Default to sampling if parent exists but sampling decision unknown
            return true;
        }
    }

    // Mock exporter for demonstration
    internal interface ISpanExporter : IDisposable
    {
        void Export(List<CompletedSpan> spans);
    }

    internal class MockSpanExporter : ISpanExporter
    {
        private readonly string? _endpoint;

        public MockSpanExporter(string? endpoint)
        {
            _endpoint = endpoint;
        }

        public void Export(List<CompletedSpan> spans)
        {
            // In a real implementation, this would send spans to the configured endpoint
            // For now, this is a no-op
        }

        public void Dispose()
        {
            // Cleanup resources
        }
    }
}