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

        // Nested configuration classes will be added in subsequent prompts
        public class SecurityConfig { public static SecurityConfig Default => new(); public SecurityConfig Merge(SecurityConfig other) => this; }
        public class ObservabilityConfig { public static ObservabilityConfig Default => new(); public ObservabilityConfig Merge(ObservabilityConfig other) => this; }
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