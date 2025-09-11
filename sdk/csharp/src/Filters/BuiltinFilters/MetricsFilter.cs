using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum MetricType
    {
        Counter,
        Gauge,
        Histogram,
        Summary
    }

    public class MetricDefinition
    {
        public string Name { get; set; } = string.Empty;
        public MetricType Type { get; set; }
        public string Description { get; set; } = string.Empty;
        public List<string> Labels { get; set; } = new();
        public double[]? Buckets { get; set; } // For histograms
        public double[]? Quantiles { get; set; } // For summaries
    }

    public class MetricsConfig : FilterConfigBase
    {
        public List<MetricDefinition> Metrics { get; set; } = new();
        public bool EnableDefaultMetrics { get; set; } = true;
        public Dictionary<string, string> DefaultLabels { get; set; } = new();
        public bool PrometheusFormat { get; set; } = true;
        public string MetricsPath { get; set; } = "/metrics";
        public int CollectionIntervalSeconds { get; set; } = 60;
        public bool EnableHistograms { get; set; } = true;
        public double[] DefaultHistogramBuckets { get; set; } = new[] { 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10 };
        public bool EnableSummaries { get; set; } = false;
        public double[] DefaultSummaryQuantiles { get; set; } = new[] { 0.5, 0.9, 0.95, 0.99 };
        public int SummaryWindowSeconds { get; set; } = 600;
        public bool IncludeTimestamp { get; set; } = true;

        public MetricsConfig() : base("Metrics", "MetricsFilter")
        {
            Priority = 5; // Very low priority to run at the end
            InitializeDefaultMetrics();
        }

        private void InitializeDefaultMetrics()
        {
            if (EnableDefaultMetrics)
            {
                Metrics.AddRange(new[]
                {
                    new MetricDefinition
                    {
                        Name = "mcp_requests_total",
                        Type = MetricType.Counter,
                        Description = "Total number of requests",
                        Labels = new List<string> { "method", "path", "status" }
                    },
                    new MetricDefinition
                    {
                        Name = "mcp_request_duration_seconds",
                        Type = MetricType.Histogram,
                        Description = "Request duration in seconds",
                        Labels = new List<string> { "method", "path" },
                        Buckets = DefaultHistogramBuckets
                    },
                    new MetricDefinition
                    {
                        Name = "mcp_request_size_bytes",
                        Type = MetricType.Histogram,
                        Description = "Request size in bytes",
                        Labels = new List<string> { "method", "path" },
                        Buckets = new[] { 100.0, 1000, 10000, 100000, 1000000 }
                    },
                    new MetricDefinition
                    {
                        Name = "mcp_response_size_bytes",
                        Type = MetricType.Histogram,
                        Description = "Response size in bytes",
                        Labels = new List<string> { "method", "path" },
                        Buckets = new[] { 100.0, 1000, 10000, 100000, 1000000 }
                    },
                    new MetricDefinition
                    {
                        Name = "mcp_active_connections",
                        Type = MetricType.Gauge,
                        Description = "Number of active connections",
                        Labels = new List<string> { "protocol" }
                    },
                    new MetricDefinition
                    {
                        Name = "mcp_errors_total",
                        Type = MetricType.Counter,
                        Description = "Total number of errors",
                        Labels = new List<string> { "type", "code" }
                    }
                });
            }
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            foreach (var metric in Metrics)
            {
                if (string.IsNullOrEmpty(metric.Name))
                {
                    errors.Add("Metric name cannot be empty");
                }

                if (!IsValidMetricName(metric.Name))
                {
                    errors.Add($"Invalid metric name: {metric.Name}. Must match [a-zA-Z_:][a-zA-Z0-9_:]*");
                }

                if (metric.Type == MetricType.Histogram && metric.Buckets == null)
                {
                    metric.Buckets = DefaultHistogramBuckets;
                }

                if (metric.Type == MetricType.Summary && metric.Quantiles == null)
                {
                    metric.Quantiles = DefaultSummaryQuantiles;
                }
            }

            if (CollectionIntervalSeconds <= 0)
            {
                errors.Add("Collection interval must be greater than 0");
            }

            return errors.Count == 0;
        }

        private bool IsValidMetricName(string name)
        {
            if (string.IsNullOrEmpty(name))
                return false;

            if (!char.IsLetter(name[0]) && name[0] != '_' && name[0] != ':')
                return false;

            return name.Skip(1).All(c => char.IsLetterOrDigit(c) || c == '_' || c == ':');
        }
    }

    public class MetricsFilter : Filter
    {
        private readonly MetricsConfig _config;
        private readonly ConcurrentDictionary<string, IMetric> _metrics;
        private readonly Timer _collectionTimer;
        private readonly object _lockObject = new();

        public MetricsFilter(MetricsConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _metrics = new ConcurrentDictionary<string, IMetric>();
            InitializeMetrics();
            
            _collectionTimer = new Timer(
                CollectMetrics,
                null,
                TimeSpan.FromSeconds(_config.CollectionIntervalSeconds),
                TimeSpan.FromSeconds(_config.CollectionIntervalSeconds));
        }

        private void InitializeMetrics()
        {
            foreach (var definition in _config.Metrics)
            {
                var metric = CreateMetric(definition);
                _metrics[definition.Name] = metric;
            }
        }

        private IMetric CreateMetric(MetricDefinition definition)
        {
            return definition.Type switch
            {
                MetricType.Counter => new Counter(definition.Name, definition.Description, definition.Labels),
                MetricType.Gauge => new Gauge(definition.Name, definition.Description, definition.Labels),
                MetricType.Histogram => new Histogram(definition.Name, definition.Description, definition.Labels, definition.Buckets ?? _config.DefaultHistogramBuckets),
                MetricType.Summary => new Summary(definition.Name, definition.Description, definition.Labels, definition.Quantiles ?? _config.DefaultSummaryQuantiles, _config.SummaryWindowSeconds),
                _ => throw new NotSupportedException($"Metric type {definition.Type} is not supported")
            };
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            var stopwatch = Stopwatch.StartNew();

            try
            {
                // Check if this is a metrics request
                var path = context.GetProperty<string>("Path");
                if (path == _config.MetricsPath)
                {
                    var metricsData = ExportMetrics();
                    return FilterResult.Continue(Encoding.UTF8.GetBytes(metricsData));
                }

                // Collect request metrics
                CollectRequestMetrics(buffer, context);

                // Continue processing
                var result = FilterResult.Continue(buffer);

                // Collect response metrics
                stopwatch.Stop();
                CollectResponseMetrics(result, context, stopwatch.ElapsedMilliseconds);

                UpdateStatistics(buffer.Length, 0, true);
                await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                return result;
            }
            catch (Exception ex)
            {
                // Collect error metrics
                CollectErrorMetrics(ex, context);
                
                UpdateStatistics(0L, 0, false);
                await RaiseOnErrorAsync(ex);
                return FilterResult.Error($"Metrics error: {ex.Message}", FilterError.InternalError);
            }
        }

        private void CollectRequestMetrics(byte[] buffer, ProcessingContext context)
        {
            var method = context.GetProperty<string>("Method") ?? "unknown";
            var path = context.GetProperty<string>("Path") ?? "/";
            var labels = CreateLabels(context, "method", method, "path", path);

            // Increment request counter
            if (_metrics.TryGetValue("mcp_requests_total", out var requestCounter) && requestCounter is Counter counter)
            {
                counter.Increment(labels);
            }

            // Record request size
            if (_metrics.TryGetValue("mcp_request_size_bytes", out var requestSize) && requestSize is Histogram histogram)
            {
                histogram.Observe(buffer.Length, labels);
            }

            // Update active connections gauge
            if (_metrics.TryGetValue("mcp_active_connections", out var activeConnections) && activeConnections is Gauge gauge)
            {
                var protocol = context.GetProperty<string>("Protocol") ?? "unknown";
                gauge.Set(context.GetProperty<int>("ActiveConnections"), CreateLabels(context, "protocol", protocol));
            }

            // Store request start time for duration calculation
            context.SetProperty("MetricsRequestStartTime", DateTime.UtcNow);
        }

        private void CollectResponseMetrics(FilterResult result, ProcessingContext context, long durationMs)
        {
            var method = context.GetProperty<string>("Method") ?? "unknown";
            var path = context.GetProperty<string>("Path") ?? "/";
            var status = result.IsSuccess ? "success" : "error";
            var labels = CreateLabels(context, "method", method, "path", path, "status", status);

            // Record request duration
            if (_metrics.TryGetValue("mcp_request_duration_seconds", out var requestDuration) && requestDuration is Histogram histogram)
            {
                histogram.Observe(durationMs / 1000.0, CreateLabels(context, "method", method, "path", path));
            }

            // Record response size
            if (result.Data != null && _metrics.TryGetValue("mcp_response_size_bytes", out var responseSize) && responseSize is Histogram sizeHistogram)
            {
                sizeHistogram.Observe(result.Data.Length, CreateLabels(context, "method", method, "path", path));
            }

            // Custom metrics
            CollectCustomMetrics(context);
        }

        private void CollectErrorMetrics(Exception ex, ProcessingContext context)
        {
            var errorType = ex.GetType().Name;
            var errorCode = ex is FilterException filterEx ? filterEx.ErrorCode.ToString() : "Unknown";
            var labels = CreateLabels(context, "type", errorType, "code", errorCode);

            if (_metrics.TryGetValue("mcp_errors_total", out var errorCounter) && errorCounter is Counter counter)
            {
                counter.Increment(labels);
            }
        }

        private void CollectCustomMetrics(ProcessingContext context)
        {
            // Allow custom metrics to be collected via context properties
            var customMetrics = context.GetProperty<Dictionary<string, double>>("CustomMetrics");
            if (customMetrics != null)
            {
                foreach (var kvp in customMetrics)
                {
                    if (_metrics.TryGetValue(kvp.Key, out var metric))
                    {
                        var labels = CreateLabels(context);
                        
                        switch (metric)
                        {
                            case Counter counter:
                                counter.Increment(labels, kvp.Value);
                                break;
                            case Gauge gauge:
                                gauge.Set(kvp.Value, labels);
                                break;
                            case Histogram histogram:
                                histogram.Observe(kvp.Value, labels);
                                break;
                            case Summary summary:
                                summary.Observe(kvp.Value, labels);
                                break;
                        }
                    }
                }
            }
        }

        private Dictionary<string, string> CreateLabels(ProcessingContext context, params string[] additionalLabels)
        {
            var labels = new Dictionary<string, string>(_config.DefaultLabels);

            // Add additional labels in pairs (key, value, key, value, ...)
            for (int i = 0; i < additionalLabels.Length - 1; i += 2)
            {
                labels[additionalLabels[i]] = additionalLabels[i + 1];
            }

            return labels;
        }

        private void CollectMetrics(object? state)
        {
            // This method is called periodically to perform any cleanup or aggregation
            // For now, it's a placeholder for future enhancements
        }

        public string ExportMetrics()
        {
            if (_config.PrometheusFormat)
            {
                return ExportPrometheusFormat();
            }
            else
            {
                return ExportJsonFormat();
            }
        }

        private string ExportPrometheusFormat()
        {
            var sb = new StringBuilder();
            var timestamp = _config.IncludeTimestamp ? DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() : (long?)null;

            foreach (var metric in _metrics.Values)
            {
                metric.Export(sb, timestamp);
            }

            return sb.ToString();
        }

        private string ExportJsonFormat()
        {
            var metrics = new List<object>();

            foreach (var metric in _metrics.Values)
            {
                metrics.Add(metric.ToJson());
            }

            return System.Text.Json.JsonSerializer.Serialize(metrics, new System.Text.Json.JsonSerializerOptions
            {
                WriteIndented = true
            });
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _collectionTimer?.Dispose();
                _metrics.Clear();
            }
            base.Dispose(disposing);
        }
    }

    // Metric implementations
    internal interface IMetric
    {
        string Name { get; }
        string Description { get; }
        void Export(StringBuilder sb, long? timestamp);
        object ToJson();
    }

    internal class Counter : IMetric
    {
        private readonly ConcurrentDictionary<string, double> _values = new();
        public string Name { get; }
        public string Description { get; }
        private readonly List<string> _labels;

        public Counter(string name, string description, List<string> labels)
        {
            Name = name;
            Description = description;
            _labels = labels;
        }

        public void Increment(Dictionary<string, string> labels, double value = 1)
        {
            var key = GetLabelKey(labels);
            _values.AddOrUpdate(key, value, (k, v) => v + value);
        }

        public void Export(StringBuilder sb, long? timestamp)
        {
            sb.AppendLine($"# HELP {Name} {Description}");
            sb.AppendLine($"# TYPE {Name} counter");
            
            foreach (var kvp in _values)
            {
                var labelStr = kvp.Key;
                var timestampStr = timestamp.HasValue ? $" {timestamp}" : "";
                sb.AppendLine($"{Name}{labelStr} {kvp.Value}{timestampStr}");
            }
        }

        public object ToJson()
        {
            return new
            {
                name = Name,
                type = "counter",
                description = Description,
                values = _values.ToDictionary(kvp => kvp.Key, kvp => kvp.Value)
            };
        }

        private string GetLabelKey(Dictionary<string, string> labels)
        {
            if (labels.Count == 0)
                return "";

            var pairs = labels.Select(kvp => $"{kvp.Key}=\"{kvp.Value}\"");
            return "{" + string.Join(",", pairs) + "}";
        }
    }

    internal class Gauge : IMetric
    {
        private readonly ConcurrentDictionary<string, double> _values = new();
        public string Name { get; }
        public string Description { get; }
        private readonly List<string> _labels;

        public Gauge(string name, string description, List<string> labels)
        {
            Name = name;
            Description = description;
            _labels = labels;
        }

        public void Set(double value, Dictionary<string, string> labels)
        {
            var key = GetLabelKey(labels);
            _values[key] = value;
        }

        public void Export(StringBuilder sb, long? timestamp)
        {
            sb.AppendLine($"# HELP {Name} {Description}");
            sb.AppendLine($"# TYPE {Name} gauge");
            
            foreach (var kvp in _values)
            {
                var labelStr = kvp.Key;
                var timestampStr = timestamp.HasValue ? $" {timestamp}" : "";
                sb.AppendLine($"{Name}{labelStr} {kvp.Value}{timestampStr}");
            }
        }

        public object ToJson()
        {
            return new
            {
                name = Name,
                type = "gauge",
                description = Description,
                values = _values.ToDictionary(kvp => kvp.Key, kvp => kvp.Value)
            };
        }

        private string GetLabelKey(Dictionary<string, string> labels)
        {
            if (labels.Count == 0)
                return "";

            var pairs = labels.Select(kvp => $"{kvp.Key}=\"{kvp.Value}\"");
            return "{" + string.Join(",", pairs) + "}";
        }
    }

    internal class Histogram : IMetric
    {
        private readonly ConcurrentDictionary<string, HistogramData> _values = new();
        public string Name { get; }
        public string Description { get; }
        private readonly List<string> _labels;
        private readonly double[] _buckets;

        public Histogram(string name, string description, List<string> labels, double[] buckets)
        {
            Name = name;
            Description = description;
            _labels = labels;
            _buckets = buckets.OrderBy(b => b).ToArray();
        }

        public void Observe(double value, Dictionary<string, string> labels)
        {
            var key = GetLabelKey(labels);
            _values.AddOrUpdate(key,
                k => new HistogramData(_buckets, value),
                (k, buffer) => { buffer.Observe(value); return buffer; });
        }

        public void Export(StringBuilder sb, long? timestamp)
        {
            sb.AppendLine($"# HELP {Name} {Description}");
            sb.AppendLine($"# TYPE {Name} histogram");
            
            foreach (var kvp in _values)
            {
                var labelStr = kvp.Key;
                var buffer = kvp.Value;
                var timestampStr = timestamp.HasValue ? $" {timestamp}" : "";
                
                foreach (var bucket in buffer.GetBuckets())
                {
                    var bucketLabel = string.IsNullOrEmpty(labelStr) 
                        ? $"{{le=\"{bucket.UpperBound}\" + (bucket.UpperBound == double.PositiveInfinity ? \"+Inf\" : bucket.UpperBound.ToString())}}"
                        : labelStr.TrimEnd('}') + $",le=\"{(bucket.UpperBound == double.PositiveInfinity ? "+Inf" : bucket.UpperBound.ToString())}\"}}";
                    sb.AppendLine($"{Name}_bucket{bucketLabel} {bucket.Count}{timestampStr}");
                }
                
                sb.AppendLine($"{Name}_sum{labelStr} {buffer.Sum}{timestampStr}");
                sb.AppendLine($"{Name}_count{labelStr} {buffer.Count}{timestampStr}");
            }
        }

        public object ToJson()
        {
            return new
            {
                name = Name,
                type = "histogram",
                description = Description,
                values = _values.ToDictionary(kvp => kvp.Key, kvp => kvp.Value.ToJson())
            };
        }

        private string GetLabelKey(Dictionary<string, string> labels)
        {
            if (labels.Count == 0)
                return "";

            var pairs = labels.Select(kvp => $"{kvp.Key}=\"{kvp.Value}\"");
            return "{" + string.Join(",", pairs) + "}";
        }

        private class HistogramData
        {
            private readonly double[] _buckets;
            private readonly long[] _bucketCounts;
            private long _count;
            private double _sum;
            private readonly object _lock = new();

            public long Count => _count;
            public double Sum => _sum;

            public HistogramData(double[] buckets, double initialValue)
            {
                _buckets = buckets;
                _bucketCounts = new long[buckets.Length];
                Observe(initialValue);
            }

            public void Observe(double value)
            {
                lock (_lock)
                {
                    _count++;
                    _sum += value;
                    
                    for (int i = 0; i < _buckets.Length; i++)
                    {
                        if (value <= _buckets[i])
                        {
                            _bucketCounts[i]++;
                        }
                    }
                }
            }

            public IEnumerable<(double UpperBound, long Count)> GetBuckets()
            {
                lock (_lock)
                {
                    long cumulativeCount = 0;
                    for (int i = 0; i < _buckets.Length; i++)
                    {
                        cumulativeCount += _bucketCounts[i];
                        yield return (_buckets[i], cumulativeCount);
                    }
                    yield return (double.PositiveInfinity, _count);
                }
            }

            public object ToJson()
            {
                lock (_lock)
                {
                    return new
                    {
                        count = _count,
                        sum = _sum,
                        buckets = GetBuckets().Select(b => new { upperBound = b.UpperBound, count = b.Count }).ToList()
                    };
                }
            }
        }
    }

    internal class Summary : IMetric
    {
        // Simplified summary implementation - in production, use a proper sliding window algorithm
        private readonly ConcurrentDictionary<string, SummaryData> _values = new();
        public string Name { get; }
        public string Description { get; }
        private readonly List<string> _labels;
        private readonly double[] _quantiles;
        private readonly int _windowSeconds;

        public Summary(string name, string description, List<string> labels, double[] quantiles, int windowSeconds)
        {
            Name = name;
            Description = description;
            _labels = labels;
            _quantiles = quantiles;
            _windowSeconds = windowSeconds;
        }

        public void Observe(double value, Dictionary<string, string> labels)
        {
            var key = GetLabelKey(labels);
            _values.AddOrUpdate(key,
                k => new SummaryData(_quantiles, _windowSeconds, value),
                (k, buffer) => { buffer.Observe(value); return buffer; });
        }

        public void Export(StringBuilder sb, long? timestamp)
        {
            sb.AppendLine($"# HELP {Name} {Description}");
            sb.AppendLine($"# TYPE {Name} summary");
            
            foreach (var kvp in _values)
            {
                var labelStr = kvp.Key;
                var buffer = kvp.Value;
                var timestampStr = timestamp.HasValue ? $" {timestamp}" : "";
                
                foreach (var quantile in buffer.GetQuantiles())
                {
                    var quantileLabel = string.IsNullOrEmpty(labelStr) 
                        ? $"{{quantile=\"{quantile.Quantile}\"}}"
                        : labelStr.TrimEnd('}') + $",quantile=\"{quantile.Quantile}\"}}";
                    sb.AppendLine($"{Name}{quantileLabel} {quantile.Value}{timestampStr}");
                }
                
                sb.AppendLine($"{Name}_sum{labelStr} {buffer.Sum}{timestampStr}");
                sb.AppendLine($"{Name}_count{labelStr} {buffer.Count}{timestampStr}");
            }
        }

        public object ToJson()
        {
            return new
            {
                name = Name,
                type = "summary",
                description = Description,
                values = _values.ToDictionary(kvp => kvp.Key, kvp => kvp.Value.ToJson())
            };
        }

        private string GetLabelKey(Dictionary<string, string> labels)
        {
            if (labels.Count == 0)
                return "";

            var pairs = labels.Select(kvp => $"{kvp.Key}=\"{kvp.Value}\"");
            return "{" + string.Join(",", pairs) + "}";
        }

        private class SummaryData
        {
            private readonly double[] _quantiles;
            private readonly List<double> _values = new();
            private long _count;
            private double _sum;
            private readonly object _lock = new();

            public long Count => _count;
            public double Sum => _sum;

            public SummaryData(double[] quantiles, int windowSeconds, double initialValue)
            {
                _quantiles = quantiles;
                Observe(initialValue);
            }

            public void Observe(double value)
            {
                lock (_lock)
                {
                    _count++;
                    _sum += value;
                    _values.Add(value);
                    
                    // Simple implementation - in production use a proper sliding window
                    if (_values.Count > 10000)
                    {
                        _values.RemoveRange(0, 5000);
                    }
                }
            }

            public IEnumerable<(double Quantile, double Value)> GetQuantiles()
            {
                lock (_lock)
                {
                    if (_values.Count == 0)
                    {
                        foreach (var q in _quantiles)
                        {
                            yield return (q, 0);
                        }
                        yield break;
                    }

                    var sorted = _values.OrderBy(v => v).ToList();
                    foreach (var quantile in _quantiles)
                    {
                        var index = (int)Math.Ceiling(quantile * sorted.Count) - 1;
                        index = Math.Max(0, Math.Min(index, sorted.Count - 1));
                        yield return (quantile, sorted[index]);
                    }
                }
            }

            public object ToJson()
            {
                lock (_lock)
                {
                    return new
                    {
                        count = _count,
                        sum = _sum,
                        quantiles = GetQuantiles().Select(q => new { quantile = q.Quantile, value = q.Value }).ToList()
                    };
                }
            }
        }
    }
}