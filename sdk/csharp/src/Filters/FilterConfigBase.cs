using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using GopherMcp.Types;

namespace GopherMcp.Filters
{
    /// <summary>
    /// Abstract base class for all filter configurations
    /// </summary>
    public abstract class FilterConfigBase : ICloneable, IValidatableObject
    {
        private string _name;
        private int _priority = 100;
        private int _timeoutMs = 30000;
        private int _maxBufferSize = 65536;
        
        /// <summary>
        /// Gets or sets the filter name
        /// </summary>
        [Required(ErrorMessage = "Filter name is required")]
        [StringLength(256, MinimumLength = 1, ErrorMessage = "Filter name must be between 1 and 256 characters")]
        public virtual string Name
        {
            get => _name;
            set
            {
                if (string.IsNullOrWhiteSpace(value))
                    throw new ArgumentException("Filter name cannot be null or empty", nameof(value));
                _name = value;
            }
        }
        
        /// <summary>
        /// Gets or sets whether the filter is enabled
        /// </summary>
        [JsonPropertyName("enabled")]
        public virtual bool Enabled { get; set; } = true;
        
        /// <summary>
        /// Gets or sets the filter priority (lower values = higher priority)
        /// </summary>
        [Range(0, int.MaxValue, ErrorMessage = "Priority must be non-negative")]
        [JsonPropertyName("priority")]
        public virtual int Priority
        {
            get => _priority;
            set
            {
                if (value < 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Priority must be non-negative");
                _priority = value;
            }
        }
        
        /// <summary>
        /// Gets or sets the filter type
        /// </summary>
        [JsonPropertyName("type")]
        public virtual string Type { get; set; }
        
        /// <summary>
        /// Gets or sets the filter description
        /// </summary>
        [JsonPropertyName("description")]
        public virtual string Description { get; set; }
        
        /// <summary>
        /// Gets or sets the filter version
        /// </summary>
        [JsonPropertyName("version")]
        public virtual string Version { get; set; } = "1.0.0";
        
        /// <summary>
        /// Gets or sets the filter timeout in milliseconds
        /// </summary>
        [Range(0, int.MaxValue, ErrorMessage = "Timeout must be non-negative")]
        [JsonPropertyName("timeoutMs")]
        public virtual int TimeoutMs
        {
            get => _timeoutMs;
            set
            {
                if (value < 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Timeout must be non-negative");
                _timeoutMs = value;
            }
        }
        
        /// <summary>
        /// Gets or sets whether to bypass this filter on error
        /// </summary>
        [JsonPropertyName("bypassOnError")]
        public virtual bool BypassOnError { get; set; } = false;
        
        /// <summary>
        /// Gets or sets the maximum buffer size for this filter
        /// </summary>
        [Range(1, int.MaxValue, ErrorMessage = "MaxBufferSize must be positive")]
        [JsonPropertyName("maxBufferSize")]
        public virtual int MaxBufferSize
        {
            get => _maxBufferSize;
            set
            {
                if (value <= 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "MaxBufferSize must be positive");
                _maxBufferSize = value;
            }
        }
        
        /// <summary>
        /// Gets or sets the filter layer
        /// </summary>
        [JsonPropertyName("layer")]
        [JsonConverter(typeof(JsonStringEnumConverter))]
        public virtual FilterLayer Layer { get; set; } = FilterLayer.Application;
        
        /// <summary>
        /// Gets or sets the filter position preference
        /// </summary>
        [JsonPropertyName("position")]
        [JsonConverter(typeof(JsonStringEnumConverter))]
        public virtual FilterPosition Position { get; set; } = FilterPosition.Last;
        
        /// <summary>
        /// Gets or sets additional configuration settings
        /// </summary>
        [JsonPropertyName("settings")]
        public virtual Dictionary<string, object> Settings { get; set; }
        
        /// <summary>
        /// Gets or sets custom metadata
        /// </summary>
        [JsonPropertyName("metadata")]
        public virtual Dictionary<string, string> Metadata { get; set; }
        
        /// <summary>
        /// Gets or sets tags for categorization
        /// </summary>
        [JsonPropertyName("tags")]
        public virtual List<string> Tags { get; set; }
        
        /// <summary>
        /// Initializes a new instance of FilterConfigBase
        /// </summary>
        protected FilterConfigBase()
        {
            Settings = new Dictionary<string, object>();
            Metadata = new Dictionary<string, string>();
            Tags = new List<string>();
            Type = GetType().Name.Replace("Config", "");
        }
        
        /// <summary>
        /// Initializes a new instance of FilterConfigBase with a name
        /// </summary>
        /// <param name="name">Filter name</param>
        protected FilterConfigBase(string name) : this()
        {
            Name = name;
        }
        
        /// <summary>
        /// Validates the configuration
        /// </summary>
        /// <returns>True if valid, false otherwise</returns>
        public virtual bool Validate()
        {
            var context = new ValidationContext(this);
            var results = new List<ValidationResult>();
            return Validator.TryValidateObject(this, context, results, true);
        }
        
        /// <summary>
        /// Validates the configuration and returns validation results
        /// </summary>
        /// <param name="validationContext">Validation context</param>
        /// <returns>Validation results</returns>
        public virtual IEnumerable<ValidationResult> Validate(ValidationContext validationContext)
        {
            var results = new List<ValidationResult>();
            
            // Validate basic properties
            if (string.IsNullOrWhiteSpace(Name))
            {
                results.Add(new ValidationResult("Filter name is required", new[] { nameof(Name) }));
            }
            
            if (Priority < 0)
            {
                results.Add(new ValidationResult("Priority must be non-negative", new[] { nameof(Priority) }));
            }
            
            if (TimeoutMs < 0)
            {
                results.Add(new ValidationResult("Timeout must be non-negative", new[] { nameof(TimeoutMs) }));
            }
            
            if (MaxBufferSize <= 0)
            {
                results.Add(new ValidationResult("MaxBufferSize must be positive", new[] { nameof(MaxBufferSize) }));
            }
            
            // Call derived class validation
            var derivedResults = ValidateCore(validationContext);
            if (derivedResults != null)
            {
                results.AddRange(derivedResults);
            }
            
            return results;
        }
        
        /// <summary>
        /// Core validation to be implemented by derived classes
        /// </summary>
        /// <param name="validationContext">Validation context</param>
        /// <returns>Validation results</returns>
        protected virtual IEnumerable<ValidationResult> ValidateCore(ValidationContext validationContext)
        {
            return Enumerable.Empty<ValidationResult>();
        }
        
        /// <summary>
        /// Merges another configuration into this one
        /// </summary>
        /// <param name="other">Configuration to merge</param>
        public virtual void Merge(FilterConfigBase other)
        {
            if (other == null)
                return;
            
            // Don't overwrite name unless it's null
            if (string.IsNullOrWhiteSpace(Name))
                Name = other.Name;
            
            // Merge simple properties
            if (other.Enabled != true)
                Enabled = other.Enabled;
            
            if (other.Priority != 100)
                Priority = other.Priority;
            
            if (!string.IsNullOrWhiteSpace(other.Type))
                Type = other.Type;
            
            if (!string.IsNullOrWhiteSpace(other.Description))
                Description = other.Description;
            
            if (other.Version != "1.0.0")
                Version = other.Version;
            
            if (other.TimeoutMs != 30000)
                TimeoutMs = other.TimeoutMs;
            
            if (other.BypassOnError)
                BypassOnError = other.BypassOnError;
            
            if (other.MaxBufferSize != 65536)
                MaxBufferSize = other.MaxBufferSize;
            
            if (other.Layer != FilterLayer.Application)
                Layer = other.Layer;
            
            if (other.Position != FilterPosition.Last)
                Position = other.Position;
            
            // Merge collections
            if (other.Settings != null)
            {
                foreach (var kvp in other.Settings)
                {
                    Settings[kvp.Key] = kvp.Value;
                }
            }
            
            if (other.Metadata != null)
            {
                foreach (var kvp in other.Metadata)
                {
                    Metadata[kvp.Key] = kvp.Value;
                }
            }
            
            if (other.Tags != null)
            {
                foreach (var tag in other.Tags)
                {
                    if (!Tags.Contains(tag))
                        Tags.Add(tag);
                }
            }
            
            // Call derived class merge
            MergeCore(other);
        }
        
        /// <summary>
        /// Core merge to be implemented by derived classes
        /// </summary>
        /// <param name="other">Configuration to merge</param>
        protected virtual void MergeCore(FilterConfigBase other)
        {
            // Override in derived classes
        }
        
        /// <summary>
        /// Sets default values for the configuration
        /// </summary>
        public virtual void SetDefaults()
        {
            Enabled = true;
            Priority = 100;
            TimeoutMs = 30000;
            BypassOnError = false;
            MaxBufferSize = 65536;
            Layer = FilterLayer.Application;
            Position = FilterPosition.Last;
            Version = "1.0.0";
            
            Settings?.Clear();
            Metadata?.Clear();
            Tags?.Clear();
            
            // Call derived class defaults
            SetDefaultsCore();
        }
        
        /// <summary>
        /// Core default setting to be implemented by derived classes
        /// </summary>
        protected virtual void SetDefaultsCore()
        {
            // Override in derived classes
        }
        
        /// <summary>
        /// Creates a deep clone of the configuration
        /// </summary>
        /// <returns>Cloned configuration</returns>
        public virtual object Clone()
        {
            var clone = (FilterConfigBase)MemberwiseClone();
            
            // Deep clone collections
            if (Settings != null)
                clone.Settings = new Dictionary<string, object>(Settings);
            
            if (Metadata != null)
                clone.Metadata = new Dictionary<string, string>(Metadata);
            
            if (Tags != null)
                clone.Tags = new List<string>(Tags);
            
            // Call derived class clone
            CloneCore(clone);
            
            return clone;
        }
        
        /// <summary>
        /// Core clone to be implemented by derived classes
        /// </summary>
        /// <param name="clone">The cloned instance to customize</param>
        protected virtual void CloneCore(FilterConfigBase clone)
        {
            // Override in derived classes
        }
        
        /// <summary>
        /// Serializes the configuration to JSON
        /// </summary>
        /// <returns>JSON string</returns>
        public virtual string ToJson()
        {
            var options = new JsonSerializerOptions
            {
                WriteIndented = true,
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
                Converters = { new JsonStringEnumConverter() }
            };
            
            return JsonSerializer.Serialize(this, GetType(), options);
        }
        
        /// <summary>
        /// Deserializes configuration from JSON
        /// </summary>
        /// <typeparam name="T">Configuration type</typeparam>
        /// <param name="json">JSON string</param>
        /// <returns>Deserialized configuration</returns>
        public static T FromJson<T>(string json) where T : FilterConfigBase
        {
            var options = new JsonSerializerOptions
            {
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
                Converters = { new JsonStringEnumConverter() }
            };
            
            return JsonSerializer.Deserialize<T>(json, options);
        }
        
        /// <summary>
        /// Gets configuration value or default
        /// </summary>
        /// <typeparam name="T">Value type</typeparam>
        /// <param name="key">Setting key</param>
        /// <param name="defaultValue">Default value if not found</param>
        /// <returns>Setting value or default</returns>
        public virtual T GetSetting<T>(string key, T defaultValue = default)
        {
            if (Settings?.TryGetValue(key, out var value) == true)
            {
                if (value is T typedValue)
                    return typedValue;
                
                if (value is JsonElement element)
                {
                    try
                    {
                        return JsonSerializer.Deserialize<T>(element.GetRawText());
                    }
                    catch
                    {
                        // Fall through to default
                    }
                }
                
                try
                {
                    return (T)Convert.ChangeType(value, typeof(T));
                }
                catch
                {
                    // Fall through to default
                }
            }
            
            return defaultValue;
        }
        
        /// <summary>
        /// Sets a configuration value
        /// </summary>
        /// <typeparam name="T">Value type</typeparam>
        /// <param name="key">Setting key</param>
        /// <param name="value">Setting value</param>
        public virtual void SetSetting<T>(string key, T value)
        {
            if (Settings == null)
                Settings = new Dictionary<string, object>();
            
            Settings[key] = value;
        }
        
        /// <summary>
        /// Gets a metadata value
        /// </summary>
        /// <param name="key">Metadata key</param>
        /// <param name="defaultValue">Default value if not found</param>
        /// <returns>Metadata value or default</returns>
        public virtual string GetMetadata(string key, string defaultValue = null)
        {
            if (Metadata?.TryGetValue(key, out var value) == true)
                return value;
            
            return defaultValue;
        }
        
        /// <summary>
        /// Sets a metadata value
        /// </summary>
        /// <param name="key">Metadata key</param>
        /// <param name="value">Metadata value</param>
        public virtual void SetMetadata(string key, string value)
        {
            if (Metadata == null)
                Metadata = new Dictionary<string, string>();
            
            Metadata[key] = value;
        }
        
        /// <summary>
        /// Checks if a tag exists
        /// </summary>
        /// <param name="tag">Tag to check</param>
        /// <returns>True if tag exists</returns>
        public virtual bool HasTag(string tag)
        {
            return Tags?.Contains(tag) == true;
        }
        
        /// <summary>
        /// Adds a tag
        /// </summary>
        /// <param name="tag">Tag to add</param>
        public virtual void AddTag(string tag)
        {
            if (Tags == null)
                Tags = new List<string>();
            
            if (!Tags.Contains(tag))
                Tags.Add(tag);
        }
        
        /// <summary>
        /// Removes a tag
        /// </summary>
        /// <param name="tag">Tag to remove</param>
        /// <returns>True if removed</returns>
        public virtual bool RemoveTag(string tag)
        {
            return Tags?.Remove(tag) == true;
        }
        
        /// <summary>
        /// Gets a string representation of the configuration
        /// </summary>
        public override string ToString()
        {
            return $"{Type} '{Name}' (Priority: {Priority}, Enabled: {Enabled})";
        }
    }
}