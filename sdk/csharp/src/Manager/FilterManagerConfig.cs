using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Text.Json.Serialization;
using GopherMcp.Types;

namespace GopherMcp.Manager
{
    /// <summary>
    /// Root configuration class for FilterManager.
    /// </summary>
    public class FilterManagerConfig
    {
        /// <summary>
        /// Gets or sets the manager name.
        /// </summary>
        [Required]
        [StringLength(100, MinimumLength = 1)]
        [JsonPropertyName("name")]
        public string Name { get; set; } = "DefaultManager";

        /// <summary>
        /// Gets or sets whether to enable statistics collection.
        /// </summary>
        [JsonPropertyName("enableStatistics")]
        public bool EnableStatistics { get; set; } = true;

        /// <summary>
        /// Gets or sets the maximum number of concurrent operations.
        /// </summary>
        [Range(1, 1000)]
        [JsonPropertyName("maxConcurrency")]
        public int MaxConcurrency { get; set; } = Environment.ProcessorCount;

        /// <summary>
        /// Gets or sets the default timeout for operations.
        /// </summary>
        [JsonPropertyName("defaultTimeout")]
        [JsonConverter(typeof(TimeSpanJsonConverter))]
        public TimeSpan DefaultTimeout { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Gets or sets whether to enable logging.
        /// </summary>
        [JsonPropertyName("enableLogging")]
        public bool EnableLogging { get; set; } = false;

        /// <summary>
        /// Gets or sets the log level.
        /// </summary>
        [JsonPropertyName("logLevel")]
        [JsonConverter(typeof(JsonStringEnumConverter))]
        public McpLogLevel LogLevel { get; set; } = McpLogLevel.Info;

        /// <summary>
        /// Gets or sets whether to enable default filters.
        /// </summary>
        [JsonPropertyName("enableDefaultFilters")]
        public bool EnableDefaultFilters { get; set; } = true;

        /// <summary>
        /// Gets or sets whether to enable fallback processing.
        /// </summary>
        [JsonPropertyName("enableFallback")]
        public bool EnableFallback { get; set; } = true;

        /// <summary>
        /// Gets or sets the security configuration.
        /// </summary>
        [JsonPropertyName("security")]
        public SecurityConfig Security { get; set; } = new();

        /// <summary>
        /// Gets or sets the observability configuration.
        /// </summary>
        [JsonPropertyName("observability")]
        public ObservabilityConfig Observability { get; set; } = new();

        /// <summary>
        /// Gets or sets the traffic management configuration.
        /// </summary>
        [JsonPropertyName("trafficManagement")]
        public TrafficManagementConfig TrafficManagement { get; set; } = new();

        /// <summary>
        /// Gets or sets the HTTP configuration.
        /// </summary>
        [JsonPropertyName("http")]
        public HttpConfig Http { get; set; } = new();

        /// <summary>
        /// Gets or sets the network configuration.
        /// </summary>
        [JsonPropertyName("network")]
        public NetworkConfig Network { get; set; } = new();

        /// <summary>
        /// Gets or sets the error handling configuration.
        /// </summary>
        [JsonPropertyName("errorHandling")]
        public ErrorHandlingConfig ErrorHandling { get; set; } = new();

        /// <summary>
        /// Gets or sets additional settings.
        /// </summary>
        [JsonPropertyName("settings")]
        [JsonExtensionData]
        public Dictionary<string, object> Settings { get; set; } = new();

        /// <summary>
        /// Gets a static instance with default configuration.
        /// </summary>
        public static FilterManagerConfig Default => new()
        {
            Name = "DefaultManager",
            EnableStatistics = true,
            MaxConcurrency = Environment.ProcessorCount,
            DefaultTimeout = TimeSpan.FromSeconds(30),
            EnableLogging = false,
            LogLevel = McpLogLevel.Info,
            EnableDefaultFilters = true,
            EnableFallback = true,
            Security = SecurityConfig.Default,
            Observability = ObservabilityConfig.Default,
            TrafficManagement = TrafficManagementConfig.Default,
            Http = HttpConfig.Default,
            Network = NetworkConfig.Default,
            ErrorHandling = ErrorHandlingConfig.Default
        };

        /// <summary>
        /// Validates the configuration.
        /// </summary>
        /// <returns>A list of validation errors, or empty if valid.</returns>
        public IList<ValidationResult> Validate()
        {
            var results = new List<ValidationResult>();
            var context = new ValidationContext(this);
            
            // Validate this object
            Validator.TryValidateObject(this, context, results, true);

            // Validate nested configurations
            if (Security != null)
            {
                var securityContext = new ValidationContext(Security);
                Validator.TryValidateObject(Security, securityContext, results, true);
            }

            if (Observability != null)
            {
                var observabilityContext = new ValidationContext(Observability);
                Validator.TryValidateObject(Observability, observabilityContext, results, true);
            }

            if (TrafficManagement != null)
            {
                var trafficContext = new ValidationContext(TrafficManagement);
                Validator.TryValidateObject(TrafficManagement, trafficContext, results, true);
            }

            if (Http != null)
            {
                var httpContext = new ValidationContext(Http);
                Validator.TryValidateObject(Http, httpContext, results, true);
            }

            if (Network != null)
            {
                var networkContext = new ValidationContext(Network);
                Validator.TryValidateObject(Network, networkContext, results, true);
            }

            if (ErrorHandling != null)
            {
                var errorContext = new ValidationContext(ErrorHandling);
                Validator.TryValidateObject(ErrorHandling, errorContext, results, true);
            }

            // Custom validation logic
            if (DefaultTimeout <= TimeSpan.Zero)
            {
                results.Add(new ValidationResult("DefaultTimeout must be positive", new[] { nameof(DefaultTimeout) }));
            }

            if (MaxConcurrency <= 0)
            {
                results.Add(new ValidationResult("MaxConcurrency must be positive", new[] { nameof(MaxConcurrency) }));
            }

            return results;
        }

        /// <summary>
        /// Merges this configuration with another, with the other taking precedence.
        /// </summary>
        /// <param name="other">The configuration to merge with.</param>
        /// <returns>A new merged configuration.</returns>
        public FilterManagerConfig Merge(FilterManagerConfig other)
        {
            if (other == null)
                return this;

            var merged = new FilterManagerConfig
            {
                Name = other.Name ?? Name,
                EnableStatistics = other.EnableStatistics,
                MaxConcurrency = other.MaxConcurrency > 0 ? other.MaxConcurrency : MaxConcurrency,
                DefaultTimeout = other.DefaultTimeout > TimeSpan.Zero ? other.DefaultTimeout : DefaultTimeout,
                EnableLogging = other.EnableLogging,
                LogLevel = other.LogLevel != McpLogLevel.Info ? other.LogLevel : LogLevel,
                EnableDefaultFilters = other.EnableDefaultFilters,
                EnableFallback = other.EnableFallback,
                Security = Security?.Merge(other.Security) ?? other.Security,
                Observability = Observability?.Merge(other.Observability) ?? other.Observability,
                TrafficManagement = TrafficManagement?.Merge(other.TrafficManagement) ?? other.TrafficManagement,
                Http = Http?.Merge(other.Http) ?? other.Http,
                Network = Network?.Merge(other.Network) ?? other.Network,
                ErrorHandling = ErrorHandling?.Merge(other.ErrorHandling) ?? other.ErrorHandling
            };

            // Merge settings
            foreach (var kvp in Settings)
            {
                merged.Settings[kvp.Key] = kvp.Value;
            }
            foreach (var kvp in other.Settings)
            {
                merged.Settings[kvp.Key] = kvp.Value;
            }

            return merged;
        }

        /// <summary>
        /// Creates a deep copy of this configuration.
        /// </summary>
        public FilterManagerConfig Clone()
        {
            var json = System.Text.Json.JsonSerializer.Serialize(this);
            return System.Text.Json.JsonSerializer.Deserialize<FilterManagerConfig>(json);
        }

        /// <summary>
        /// Security configuration settings.
        /// </summary>
        public class SecurityConfig
        {
            /// <summary>
            /// Gets or sets the authentication configuration.
            /// </summary>
            [JsonPropertyName("authentication")]
            public AuthenticationConfig Authentication { get; set; } = new();

            /// <summary>
            /// Gets or sets the authorization configuration.
            /// </summary>
            [JsonPropertyName("authorization")]
            public AuthorizationConfig Authorization { get; set; } = new();

            /// <summary>
            /// Gets or sets the TLS configuration.
            /// </summary>
            [JsonPropertyName("tls")]
            public TlsConfig Tls { get; set; } = new();

            /// <summary>
            /// Gets the default security configuration.
            /// </summary>
            public static SecurityConfig Default => new()
            {
                Authentication = new AuthenticationConfig
                {
                    Enabled = false,
                    Method = AuthenticationMethod.None,
                    RequireAuthentication = false,
                    AllowAnonymous = true
                },
                Authorization = new AuthorizationConfig
                {
                    Enabled = false,
                    RequireAuthorization = false,
                    DefaultPolicy = "Allow"
                },
                Tls = new TlsConfig
                {
                    Enabled = false,
                    RequireTls = false,
                    MinimumVersion = TlsVersion.Tls12,
                    VerifyPeer = true
                }
            };

            /// <summary>
            /// Merges this configuration with another.
            /// </summary>
            public SecurityConfig Merge(SecurityConfig other)
            {
                if (other == null) return this;
                
                return new SecurityConfig
                {
                    Authentication = Authentication?.Merge(other.Authentication) ?? other.Authentication,
                    Authorization = Authorization?.Merge(other.Authorization) ?? other.Authorization,
                    Tls = Tls?.Merge(other.Tls) ?? other.Tls
                };
            }

            /// <summary>
            /// Authentication configuration.
            /// </summary>
            public class AuthenticationConfig
            {
                [JsonPropertyName("enabled")]
                public bool Enabled { get; set; }

                [JsonPropertyName("method")]
                [JsonConverter(typeof(JsonStringEnumConverter))]
                public AuthenticationMethod Method { get; set; } = AuthenticationMethod.None;

                [JsonPropertyName("tokenEndpoint")]
                public string TokenEndpoint { get; set; } = "";

                [JsonPropertyName("clientId")]
                public string ClientId { get; set; } = "";

                [JsonPropertyName("clientSecret")]
                public string ClientSecret { get; set; } = "";

                [JsonPropertyName("scope")]
                public string Scope { get; set; } = "";

                [JsonPropertyName("audience")]
                public string Audience { get; set; } = "";

                [JsonPropertyName("requireAuthentication")]
                public bool RequireAuthentication { get; set; }

                [JsonPropertyName("allowAnonymous")]
                public bool AllowAnonymous { get; set; } = true;

                [JsonPropertyName("tokenExpiration")]
                [JsonConverter(typeof(TimeSpanJsonConverter))]
                public TimeSpan TokenExpiration { get; set; } = TimeSpan.FromHours(1);

                [JsonPropertyName("refreshTokenExpiration")]
                [JsonConverter(typeof(TimeSpanJsonConverter))]
                public TimeSpan RefreshTokenExpiration { get; set; } = TimeSpan.FromDays(30);

                public AuthenticationConfig Merge(AuthenticationConfig other)
                {
                    if (other == null) return this;
                    
                    return new AuthenticationConfig
                    {
                        Enabled = other.Enabled,
                        Method = other.Method != AuthenticationMethod.None ? other.Method : Method,
                        TokenEndpoint = !string.IsNullOrEmpty(other.TokenEndpoint) ? other.TokenEndpoint : TokenEndpoint,
                        ClientId = !string.IsNullOrEmpty(other.ClientId) ? other.ClientId : ClientId,
                        ClientSecret = !string.IsNullOrEmpty(other.ClientSecret) ? other.ClientSecret : ClientSecret,
                        Scope = !string.IsNullOrEmpty(other.Scope) ? other.Scope : Scope,
                        Audience = !string.IsNullOrEmpty(other.Audience) ? other.Audience : Audience,
                        RequireAuthentication = other.RequireAuthentication,
                        AllowAnonymous = other.AllowAnonymous,
                        TokenExpiration = other.TokenExpiration > TimeSpan.Zero ? other.TokenExpiration : TokenExpiration,
                        RefreshTokenExpiration = other.RefreshTokenExpiration > TimeSpan.Zero ? other.RefreshTokenExpiration : RefreshTokenExpiration
                    };
                }
            }

            /// <summary>
            /// Authorization configuration.
            /// </summary>
            public class AuthorizationConfig
            {
                [JsonPropertyName("enabled")]
                public bool Enabled { get; set; }

                [JsonPropertyName("requireAuthorization")]
                public bool RequireAuthorization { get; set; }

                [JsonPropertyName("defaultPolicy")]
                public string DefaultPolicy { get; set; } = "Allow";

                [JsonPropertyName("policies")]
                public Dictionary<string, PolicyConfig> Policies { get; set; } = new();

                [JsonPropertyName("roles")]
                public List<string> Roles { get; set; } = new();

                [JsonPropertyName("permissions")]
                public List<string> Permissions { get; set; } = new();

                public AuthorizationConfig Merge(AuthorizationConfig other)
                {
                    if (other == null) return this;
                    
                    var merged = new AuthorizationConfig
                    {
                        Enabled = other.Enabled,
                        RequireAuthorization = other.RequireAuthorization,
                        DefaultPolicy = !string.IsNullOrEmpty(other.DefaultPolicy) ? other.DefaultPolicy : DefaultPolicy,
                        Policies = new Dictionary<string, PolicyConfig>(Policies),
                        Roles = new List<string>(Roles),
                        Permissions = new List<string>(Permissions)
                    };

                    // Merge policies
                    foreach (var policy in other.Policies)
                    {
                        merged.Policies[policy.Key] = policy.Value;
                    }

                    // Merge roles and permissions
                    merged.Roles.AddRange(other.Roles);
                    merged.Permissions.AddRange(other.Permissions);

                    return merged;
                }

                public class PolicyConfig
                {
                    [JsonPropertyName("name")]
                    public string Name { get; set; } = "";

                    [JsonPropertyName("requiredRoles")]
                    public List<string> RequiredRoles { get; set; } = new();

                    [JsonPropertyName("requiredPermissions")]
                    public List<string> RequiredPermissions { get; set; } = new();

                    [JsonPropertyName("requireAll")]
                    public bool RequireAll { get; set; }
                }
            }

            /// <summary>
            /// TLS configuration.
            /// </summary>
            public class TlsConfig
            {
                [JsonPropertyName("enabled")]
                public bool Enabled { get; set; }

                [JsonPropertyName("requireTls")]
                public bool RequireTls { get; set; }

                [JsonPropertyName("minimumVersion")]
                [JsonConverter(typeof(JsonStringEnumConverter))]
                public TlsVersion MinimumVersion { get; set; } = TlsVersion.Tls12;

                [JsonPropertyName("certificatePath")]
                public string CertificatePath { get; set; } = "";

                [JsonPropertyName("keyPath")]
                public string KeyPath { get; set; } = "";

                [JsonPropertyName("caPath")]
                public string CaPath { get; set; } = "";

                [JsonPropertyName("verifyPeer")]
                public bool VerifyPeer { get; set; } = true;

                [JsonPropertyName("verifyHostname")]
                public bool VerifyHostname { get; set; } = true;

                [JsonPropertyName("allowedCiphers")]
                public List<string> AllowedCiphers { get; set; } = new();

                [JsonPropertyName("clientCertificateRequired")]
                public bool ClientCertificateRequired { get; set; }

                public TlsConfig Merge(TlsConfig other)
                {
                    if (other == null) return this;
                    
                    return new TlsConfig
                    {
                        Enabled = other.Enabled,
                        RequireTls = other.RequireTls,
                        MinimumVersion = other.MinimumVersion != TlsVersion.Tls12 ? other.MinimumVersion : MinimumVersion,
                        CertificatePath = !string.IsNullOrEmpty(other.CertificatePath) ? other.CertificatePath : CertificatePath,
                        KeyPath = !string.IsNullOrEmpty(other.KeyPath) ? other.KeyPath : KeyPath,
                        CaPath = !string.IsNullOrEmpty(other.CaPath) ? other.CaPath : CaPath,
                        VerifyPeer = other.VerifyPeer,
                        VerifyHostname = other.VerifyHostname,
                        AllowedCiphers = other.AllowedCiphers?.Count > 0 ? new List<string>(other.AllowedCiphers) : new List<string>(AllowedCiphers),
                        ClientCertificateRequired = other.ClientCertificateRequired
                    };
                }
            }
        }

        /// <summary>
        /// Authentication method enumeration.
        /// </summary>
        public enum AuthenticationMethod
        {
            None,
            Basic,
            Bearer,
            OAuth2,
            ApiKey,
            Certificate,
            Kerberos,
            NTLM
        }

        /// <summary>
        /// TLS version enumeration.
        /// </summary>
        public enum TlsVersion
        {
            Tls10,
            Tls11,
            Tls12,
            Tls13
        }
        /// <summary>
        /// Observability configuration settings.
        /// </summary>
        public class ObservabilityConfig
        {
            /// <summary>
            /// Gets or sets the access log configuration.
            /// </summary>
            [JsonPropertyName("accessLog")]
            public AccessLogConfig AccessLog { get; set; } = new();

            /// <summary>
            /// Gets or sets the metrics configuration.
            /// </summary>
            [JsonPropertyName("metrics")]
            public MetricsConfig Metrics { get; set; } = new();

            /// <summary>
            /// Gets or sets the tracing configuration.
            /// </summary>
            [JsonPropertyName("tracing")]
            public TracingConfig Tracing { get; set; } = new();

            /// <summary>
            /// Gets the default observability configuration.
            /// </summary>
            public static ObservabilityConfig Default => new()
            {
                AccessLog = new AccessLogConfig
                {
                    Enabled = false,
                    Format = LogFormat.Json,
                    IncludeHeaders = false,
                    IncludeBody = false
                },
                Metrics = new MetricsConfig
                {
                    Enabled = true,
                    CollectSystemMetrics = true,
                    CollectProcessMetrics = true,
                    ExportInterval = TimeSpan.FromMinutes(1)
                },
                Tracing = new TracingConfig
                {
                    Enabled = false,
                    SamplingRate = 0.1,
                    PropagateContext = true
                }
            };

            /// <summary>
            /// Merges this configuration with another.
            /// </summary>
            public ObservabilityConfig Merge(ObservabilityConfig other)
            {
                if (other == null) return this;
                
                return new ObservabilityConfig
                {
                    AccessLog = AccessLog?.Merge(other.AccessLog) ?? other.AccessLog,
                    Metrics = Metrics?.Merge(other.Metrics) ?? other.Metrics,
                    Tracing = Tracing?.Merge(other.Tracing) ?? other.Tracing
                };
            }

            /// <summary>
            /// Access log configuration.
            /// </summary>
            public class AccessLogConfig
            {
                [JsonPropertyName("enabled")]
                public bool Enabled { get; set; }

                [JsonPropertyName("format")]
                [JsonConverter(typeof(JsonStringEnumConverter))]
                public LogFormat Format { get; set; } = LogFormat.Json;

                [JsonPropertyName("outputPath")]
                public string OutputPath { get; set; } = "logs/access.log";

                [JsonPropertyName("rotationSize")]
                public long RotationSize { get; set; } = 100 * 1024 * 1024; // 100MB

                [JsonPropertyName("rotationInterval")]
                [JsonConverter(typeof(TimeSpanJsonConverter))]
                public TimeSpan RotationInterval { get; set; } = TimeSpan.FromDays(1);

                [JsonPropertyName("retentionDays")]
                public int RetentionDays { get; set; } = 30;

                [JsonPropertyName("includeHeaders")]
                public bool IncludeHeaders { get; set; }

                [JsonPropertyName("includeBody")]
                public bool IncludeBody { get; set; }

                [JsonPropertyName("includeTiming")]
                public bool IncludeTiming { get; set; } = true;

                [JsonPropertyName("excludePaths")]
                public List<string> ExcludePaths { get; set; } = new() { "/health", "/metrics" };

                [JsonPropertyName("sanitizeHeaders")]
                public List<string> SanitizeHeaders { get; set; } = new() { "Authorization", "Cookie", "X-Api-Key" };

                public AccessLogConfig Merge(AccessLogConfig other)
                {
                    if (other == null) return this;
                    
                    return new AccessLogConfig
                    {
                        Enabled = other.Enabled,
                        Format = other.Format != LogFormat.Json ? other.Format : Format,
                        OutputPath = !string.IsNullOrEmpty(other.OutputPath) ? other.OutputPath : OutputPath,
                        RotationSize = other.RotationSize > 0 ? other.RotationSize : RotationSize,
                        RotationInterval = other.RotationInterval > TimeSpan.Zero ? other.RotationInterval : RotationInterval,
                        RetentionDays = other.RetentionDays > 0 ? other.RetentionDays : RetentionDays,
                        IncludeHeaders = other.IncludeHeaders,
                        IncludeBody = other.IncludeBody,
                        IncludeTiming = other.IncludeTiming,
                        ExcludePaths = other.ExcludePaths?.Count > 0 ? new List<string>(other.ExcludePaths) : new List<string>(ExcludePaths),
                        SanitizeHeaders = other.SanitizeHeaders?.Count > 0 ? new List<string>(other.SanitizeHeaders) : new List<string>(SanitizeHeaders)
                    };
                }
            }

            /// <summary>
            /// Metrics configuration.
            /// </summary>
            public class MetricsConfig
            {
                [JsonPropertyName("enabled")]
                public bool Enabled { get; set; } = true;

                [JsonPropertyName("endpoint")]
                public string Endpoint { get; set; } = "/metrics";

                [JsonPropertyName("exportFormat")]
                [JsonConverter(typeof(JsonStringEnumConverter))]
                public MetricsFormat ExportFormat { get; set; } = MetricsFormat.Prometheus;

                [JsonPropertyName("exportInterval")]
                [JsonConverter(typeof(TimeSpanJsonConverter))]
                public TimeSpan ExportInterval { get; set; } = TimeSpan.FromMinutes(1);

                [JsonPropertyName("collectSystemMetrics")]
                public bool CollectSystemMetrics { get; set; } = true;

                [JsonPropertyName("collectProcessMetrics")]
                public bool CollectProcessMetrics { get; set; } = true;

                [JsonPropertyName("collectFilterMetrics")]
                public bool CollectFilterMetrics { get; set; } = true;

                [JsonPropertyName("collectChainMetrics")]
                public bool CollectChainMetrics { get; set; } = true;

                [JsonPropertyName("histogramBuckets")]
                public List<double> HistogramBuckets { get; set; } = new() { 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10 };

                [JsonPropertyName("customMetrics")]
                public Dictionary<string, MetricDefinition> CustomMetrics { get; set; } = new();

                public MetricsConfig Merge(MetricsConfig other)
                {
                    if (other == null) return this;
                    
                    var merged = new MetricsConfig
                    {
                        Enabled = other.Enabled,
                        Endpoint = !string.IsNullOrEmpty(other.Endpoint) ? other.Endpoint : Endpoint,
                        ExportFormat = other.ExportFormat != MetricsFormat.Prometheus ? other.ExportFormat : ExportFormat,
                        ExportInterval = other.ExportInterval > TimeSpan.Zero ? other.ExportInterval : ExportInterval,
                        CollectSystemMetrics = other.CollectSystemMetrics,
                        CollectProcessMetrics = other.CollectProcessMetrics,
                        CollectFilterMetrics = other.CollectFilterMetrics,
                        CollectChainMetrics = other.CollectChainMetrics,
                        HistogramBuckets = other.HistogramBuckets?.Count > 0 ? new List<double>(other.HistogramBuckets) : new List<double>(HistogramBuckets),
                        CustomMetrics = new Dictionary<string, MetricDefinition>(CustomMetrics)
                    };

                    // Merge custom metrics
                    foreach (var metric in other.CustomMetrics)
                    {
                        merged.CustomMetrics[metric.Key] = metric.Value;
                    }

                    return merged;
                }

                public class MetricDefinition
                {
                    [JsonPropertyName("name")]
                    public string Name { get; set; } = "";

                    [JsonPropertyName("type")]
                    [JsonConverter(typeof(JsonStringEnumConverter))]
                    public MetricType Type { get; set; } = MetricType.Counter;

                    [JsonPropertyName("description")]
                    public string Description { get; set; } = "";

                    [JsonPropertyName("unit")]
                    public string Unit { get; set; } = "";

                    [JsonPropertyName("labels")]
                    public List<string> Labels { get; set; } = new();
                }
            }

            /// <summary>
            /// Tracing configuration.
            /// </summary>
            public class TracingConfig
            {
                [JsonPropertyName("enabled")]
                public bool Enabled { get; set; }

                [JsonPropertyName("serviceName")]
                public string ServiceName { get; set; } = "mcp-filter-manager";

                [JsonPropertyName("endpoint")]
                public string Endpoint { get; set; } = "http://localhost:4317";

                [JsonPropertyName("protocol")]
                [JsonConverter(typeof(JsonStringEnumConverter))]
                public TracingProtocol Protocol { get; set; } = TracingProtocol.Otlp;

                [JsonPropertyName("samplingRate")]
                public double SamplingRate { get; set; } = 0.1;

                [JsonPropertyName("propagateContext")]
                public bool PropagateContext { get; set; } = true;

                [JsonPropertyName("exportBatchSize")]
                public int ExportBatchSize { get; set; } = 512;

                [JsonPropertyName("exportTimeout")]
                [JsonConverter(typeof(TimeSpanJsonConverter))]
                public TimeSpan ExportTimeout { get; set; } = TimeSpan.FromSeconds(30);

                [JsonPropertyName("resourceAttributes")]
                public Dictionary<string, string> ResourceAttributes { get; set; } = new();

                [JsonPropertyName("excludeOperations")]
                public List<string> ExcludeOperations { get; set; } = new();

                public TracingConfig Merge(TracingConfig other)
                {
                    if (other == null) return this;
                    
                    var merged = new TracingConfig
                    {
                        Enabled = other.Enabled,
                        ServiceName = !string.IsNullOrEmpty(other.ServiceName) ? other.ServiceName : ServiceName,
                        Endpoint = !string.IsNullOrEmpty(other.Endpoint) ? other.Endpoint : Endpoint,
                        Protocol = other.Protocol != TracingProtocol.Otlp ? other.Protocol : Protocol,
                        SamplingRate = other.SamplingRate > 0 ? other.SamplingRate : SamplingRate,
                        PropagateContext = other.PropagateContext,
                        ExportBatchSize = other.ExportBatchSize > 0 ? other.ExportBatchSize : ExportBatchSize,
                        ExportTimeout = other.ExportTimeout > TimeSpan.Zero ? other.ExportTimeout : ExportTimeout,
                        ResourceAttributes = new Dictionary<string, string>(ResourceAttributes),
                        ExcludeOperations = other.ExcludeOperations?.Count > 0 ? new List<string>(other.ExcludeOperations) : new List<string>(ExcludeOperations)
                    };

                    // Merge resource attributes
                    foreach (var attr in other.ResourceAttributes)
                    {
                        merged.ResourceAttributes[attr.Key] = attr.Value;
                    }

                    return merged;
                }
            }
        }

        /// <summary>
        /// Log format enumeration.
        /// </summary>
        public enum LogFormat
        {
            Text,
            Json,
            Csv,
            Common,
            Combined
        }

        /// <summary>
        /// Metrics format enumeration.
        /// </summary>
        public enum MetricsFormat
        {
            Prometheus,
            OpenTelemetry,
            StatsD,
            Json
        }

        /// <summary>
        /// Metric type enumeration.
        /// </summary>
        public enum MetricType
        {
            Counter,
            Gauge,
            Histogram,
            Summary
        }

        /// <summary>
        /// Tracing protocol enumeration.
        /// </summary>
        public enum TracingProtocol
        {
            Otlp,
            Jaeger,
            Zipkin,
            XRay
        }
        public class TrafficManagementConfig { public static TrafficManagementConfig Default => new(); public TrafficManagementConfig Merge(TrafficManagementConfig other) => this; }
        public class HttpConfig { public static HttpConfig Default => new(); public HttpConfig Merge(HttpConfig other) => this; }
        public class NetworkConfig { public static NetworkConfig Default => new(); public NetworkConfig Merge(NetworkConfig other) => this; }
        public class ErrorHandlingConfig { public static ErrorHandlingConfig Default => new(); public ErrorHandlingConfig Merge(ErrorHandlingConfig other) => this; }
    }

    /// <summary>
    /// Custom JSON converter for TimeSpan.
    /// </summary>
    public class TimeSpanJsonConverter : JsonConverter<TimeSpan>
    {
        public override TimeSpan Read(ref System.Text.Json.Utf8JsonReader reader, Type typeToConvert, System.Text.Json.JsonSerializerOptions options)
        {
            if (reader.TokenType == System.Text.Json.JsonTokenType.String)
            {
                var value = reader.GetString();
                if (TimeSpan.TryParse(value, out var timeSpan))
                    return timeSpan;
                
                // Try parsing as seconds if it's a number string
                if (double.TryParse(value, out var seconds))
                    return TimeSpan.FromSeconds(seconds);
            }
            else if (reader.TokenType == System.Text.Json.JsonTokenType.Number)
            {
                return TimeSpan.FromSeconds(reader.GetDouble());
            }

            throw new System.Text.Json.JsonException($"Unable to parse TimeSpan from {reader.TokenType}");
        }

        public override void Write(System.Text.Json.Utf8JsonWriter writer, TimeSpan value, System.Text.Json.JsonSerializerOptions options)
        {
            writer.WriteStringValue(value.ToString());
        }
    }
}