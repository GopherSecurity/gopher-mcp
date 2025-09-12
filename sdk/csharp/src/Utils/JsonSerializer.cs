using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Utils
{
    /// <summary>
    /// High-performance JSON serialization utilities optimized for MCP protocol
    /// </summary>
    public static class JsonSerializer
    {
        private static readonly Lazy<JsonSerializerOptions> _defaultOptions = new Lazy<JsonSerializerOptions>(CreateDefaultOptions);
        private static readonly Lazy<JsonSerializerOptions> _indentedOptions = new Lazy<JsonSerializerOptions>(CreateIndentedOptions);
        private static readonly Lazy<JsonSerializerOptions> _strictOptions = new Lazy<JsonSerializerOptions>(CreateStrictOptions);
        private static readonly Lazy<JsonSerializerOptions> _webOptions = new Lazy<JsonSerializerOptions>(CreateWebOptions);

        /// <summary>
        /// Gets the default serializer options optimized for MCP
        /// </summary>
        public static JsonSerializerOptions DefaultOptions => _defaultOptions.Value;

        /// <summary>
        /// Gets indented serializer options for human-readable output
        /// </summary>
        public static JsonSerializerOptions IndentedOptions => _indentedOptions.Value;

        /// <summary>
        /// Gets strict serializer options with no null value handling
        /// </summary>
        public static JsonSerializerOptions StrictOptions => _strictOptions.Value;

        /// <summary>
        /// Gets web-optimized serializer options with camelCase naming
        /// </summary>
        public static JsonSerializerOptions WebOptions => _webOptions.Value;

        /// <summary>
        /// Serializes an object to JSON string
        /// </summary>
        /// <typeparam name="T">The type to serialize</typeparam>
        /// <param name="value">The value to serialize</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>JSON string representation</returns>
        public static string Serialize<T>(T value, JsonSerializerOptions options = null)
        {
            return System.Text.Json.JsonSerializer.Serialize(value, options ?? DefaultOptions);
        }

        /// <summary>
        /// Serializes an object to JSON bytes
        /// </summary>
        /// <typeparam name="T">The type to serialize</typeparam>
        /// <param name="value">The value to serialize</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>UTF-8 JSON bytes</returns>
        public static byte[] SerializeToUtf8Bytes<T>(T value, JsonSerializerOptions options = null)
        {
            return System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(value, options ?? DefaultOptions);
        }

        /// <summary>
        /// Serializes an object to a stream
        /// </summary>
        /// <typeparam name="T">The type to serialize</typeparam>
        /// <param name="stream">The stream to write to</param>
        /// <param name="value">The value to serialize</param>
        /// <param name="options">Optional serializer options</param>
        public static void Serialize<T>(Stream stream, T value, JsonSerializerOptions options = null)
        {
            System.Text.Json.JsonSerializer.Serialize(stream, value, options ?? DefaultOptions);
        }

        /// <summary>
        /// Serializes an object to a stream asynchronously
        /// </summary>
        /// <typeparam name="T">The type to serialize</typeparam>
        /// <param name="stream">The stream to write to</param>
        /// <param name="value">The value to serialize</param>
        /// <param name="options">Optional serializer options</param>
        /// <param name="cancellationToken">Cancellation token</param>
        public static Task SerializeAsync<T>(Stream stream, T value, JsonSerializerOptions options = null, CancellationToken cancellationToken = default)
        {
            return System.Text.Json.JsonSerializer.SerializeAsync(stream, value, options ?? DefaultOptions, cancellationToken);
        }

        /// <summary>
        /// Serializes an object to a UTF-8 JSON writer
        /// </summary>
        /// <typeparam name="T">The type to serialize</typeparam>
        /// <param name="writer">The JSON writer</param>
        /// <param name="value">The value to serialize</param>
        /// <param name="options">Optional serializer options</param>
        public static void Serialize<T>(Utf8JsonWriter writer, T value, JsonSerializerOptions options = null)
        {
            System.Text.Json.JsonSerializer.Serialize(writer, value, options ?? DefaultOptions);
        }

        /// <summary>
        /// Deserializes JSON string to an object
        /// </summary>
        /// <typeparam name="T">The type to deserialize to</typeparam>
        /// <param name="json">The JSON string</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>Deserialized object</returns>
        public static T Deserialize<T>(string json, JsonSerializerOptions options = null)
        {
            return System.Text.Json.JsonSerializer.Deserialize<T>(json, options ?? DefaultOptions);
        }

        /// <summary>
        /// Deserializes JSON bytes to an object
        /// </summary>
        /// <typeparam name="T">The type to deserialize to</typeparam>
        /// <param name="utf8Json">The UTF-8 JSON bytes</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>Deserialized object</returns>
        public static T Deserialize<T>(ReadOnlySpan<byte> utf8Json, JsonSerializerOptions options = null)
        {
            return System.Text.Json.JsonSerializer.Deserialize<T>(utf8Json, options ?? DefaultOptions);
        }

        /// <summary>
        /// Deserializes JSON from a stream
        /// </summary>
        /// <typeparam name="T">The type to deserialize to</typeparam>
        /// <param name="stream">The stream to read from</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>Deserialized object</returns>
        public static T Deserialize<T>(Stream stream, JsonSerializerOptions options = null)
        {
            return System.Text.Json.JsonSerializer.Deserialize<T>(stream, options ?? DefaultOptions);
        }

        /// <summary>
        /// Deserializes JSON from a stream asynchronously
        /// </summary>
        /// <typeparam name="T">The type to deserialize to</typeparam>
        /// <param name="stream">The stream to read from</param>
        /// <param name="options">Optional serializer options</param>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Deserialized object</returns>
        public static ValueTask<T> DeserializeAsync<T>(Stream stream, JsonSerializerOptions options = null, CancellationToken cancellationToken = default)
        {
            return System.Text.Json.JsonSerializer.DeserializeAsync<T>(stream, options ?? DefaultOptions, cancellationToken);
        }

        /// <summary>
        /// Deserializes JSON from a UTF-8 JSON reader
        /// </summary>
        /// <typeparam name="T">The type to deserialize to</typeparam>
        /// <param name="reader">The JSON reader</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>Deserialized object</returns>
        public static T Deserialize<T>(ref Utf8JsonReader reader, JsonSerializerOptions options = null)
        {
            return System.Text.Json.JsonSerializer.Deserialize<T>(ref reader, options ?? DefaultOptions);
        }

        /// <summary>
        /// Tries to deserialize JSON string to an object
        /// </summary>
        /// <typeparam name="T">The type to deserialize to</typeparam>
        /// <param name="json">The JSON string</param>
        /// <param name="result">The deserialized result</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>True if successful, false otherwise</returns>
        public static bool TryDeserialize<T>(string json, out T result, JsonSerializerOptions options = null)
        {
            result = default;

            if (string.IsNullOrEmpty(json))
                return false;

            try
            {
                result = Deserialize<T>(json, options);
                return true;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Clones an object using JSON serialization
        /// </summary>
        /// <typeparam name="T">The type to clone</typeparam>
        /// <param name="value">The value to clone</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>Cloned object</returns>
        public static T Clone<T>(T value, JsonSerializerOptions options = null)
        {
            if (value == null)
                return default;

            var json = Serialize(value, options);
            return Deserialize<T>(json, options);
        }

        /// <summary>
        /// Converts a JSON element to a specific type
        /// </summary>
        /// <typeparam name="T">The type to convert to</typeparam>
        /// <param name="element">The JSON element</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>Converted object</returns>
        public static T ToObject<T>(JsonElement element, JsonSerializerOptions options = null)
        {
            var bufferWriter = new ArrayBufferWriter<byte>();
            using (var writer = new Utf8JsonWriter(bufferWriter))
            {
                element.WriteTo(writer);
            }

            return Deserialize<T>(bufferWriter.WrittenSpan, options);
        }

        /// <summary>
        /// Converts an object to a JSON element
        /// </summary>
        /// <typeparam name="T">The type to convert from</typeparam>
        /// <param name="value">The value to convert</param>
        /// <param name="options">Optional serializer options</param>
        /// <returns>JSON element</returns>
        public static JsonElement ToJsonElement<T>(T value, JsonSerializerOptions options = null)
        {
            var bytes = SerializeToUtf8Bytes(value, options);
            using var doc = JsonDocument.Parse(bytes);
            return doc.RootElement.Clone();
        }

        /// <summary>
        /// Creates default serializer options optimized for MCP
        /// </summary>
        private static JsonSerializerOptions CreateDefaultOptions()
        {
            var options = new JsonSerializerOptions
            {
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
                WriteIndented = false,
                ReadCommentHandling = JsonCommentHandling.Skip,
                AllowTrailingCommas = true,
                NumberHandling = JsonNumberHandling.AllowReadingFromString,
                PropertyNameCaseInsensitive = true,
                IncludeFields = false,
                DefaultBufferSize = 16384,
                MaxDepth = 64
            };

            // Add custom converters
            options.Converters.Add(new JsonStringEnumConverter());
            options.Converters.Add(new McpResultConverter());
            options.Converters.Add(new McpBoolConverter());
            options.Converters.Add(new DateTimeConverter());
            options.Converters.Add(new TimeSpanConverter());
            options.Converters.Add(new GuidConverter());

            return options;
        }

        /// <summary>
        /// Creates indented serializer options
        /// </summary>
        private static JsonSerializerOptions CreateIndentedOptions()
        {
            var options = CreateDefaultOptions();
            options.WriteIndented = true;
            return options;
        }

        /// <summary>
        /// Creates strict serializer options
        /// </summary>
        private static JsonSerializerOptions CreateStrictOptions()
        {
            var options = new JsonSerializerOptions
            {
                PropertyNamingPolicy = null,
                DefaultIgnoreCondition = JsonIgnoreCondition.Never,
                WriteIndented = false,
                ReadCommentHandling = JsonCommentHandling.Disallow,
                AllowTrailingCommas = false,
                NumberHandling = JsonNumberHandling.Strict,
                PropertyNameCaseInsensitive = false,
                IncludeFields = false,
                DefaultBufferSize = 16384,
                MaxDepth = 32
            };

            options.Converters.Add(new JsonStringEnumConverter());
            return options;
        }

        /// <summary>
        /// Creates web-optimized serializer options
        /// </summary>
        private static JsonSerializerOptions CreateWebOptions()
        {
            var options = new JsonSerializerOptions(JsonSerializerDefaults.Web)
            {
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
                NumberHandling = JsonNumberHandling.AllowReadingFromString
            };

            options.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.CamelCase));
            options.Converters.Add(new DateTimeConverter());

            return options;
        }

        /// <summary>
        /// Custom converter for McpResult enum
        /// </summary>
        private class McpResultConverter : JsonConverter<McpResult>
        {
            public override McpResult Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
            {
                if (reader.TokenType == JsonTokenType.Number)
                {
                    return (McpResult)reader.GetInt32();
                }

                if (reader.TokenType == JsonTokenType.String)
                {
                    var str = reader.GetString();
                    if (Enum.TryParse<McpResult>(str, true, out var result))
                    {
                        return result;
                    }
                }

                throw new JsonException($"Unable to convert {reader.GetString()} to McpResult");
            }

            public override void Write(Utf8JsonWriter writer, McpResult value, JsonSerializerOptions options)
            {
                writer.WriteStringValue(value.ToString());
            }
        }

        /// <summary>
        /// Custom converter for McpBool enum
        /// </summary>
        private class McpBoolConverter : JsonConverter<McpBool>
        {
            public override McpBool Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
            {
                if (reader.TokenType == JsonTokenType.True)
                    return McpBool.True;

                if (reader.TokenType == JsonTokenType.False)
                    return McpBool.False;

                if (reader.TokenType == JsonTokenType.Number)
                {
                    return reader.GetInt32() != 0 ? McpBool.True : McpBool.False;
                }

                if (reader.TokenType == JsonTokenType.String)
                {
                    var str = reader.GetString()?.ToLowerInvariant();
                    return (str == "true" || str == "1" || str == "yes") ? McpBool.True : McpBool.False;
                }

                return McpBool.False;
            }

            public override void Write(Utf8JsonWriter writer, McpBool value, JsonSerializerOptions options)
            {
                writer.WriteBooleanValue(value == McpBool.True);
            }
        }

        /// <summary>
        /// Custom converter for DateTime with ISO 8601 format
        /// </summary>
        private class DateTimeConverter : JsonConverter<DateTime>
        {
            public override DateTime Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
            {
                if (reader.TokenType == JsonTokenType.String)
                {
                    var str = reader.GetString();
                    if (DateTime.TryParse(str, out var result))
                    {
                        return result.Kind == DateTimeKind.Unspecified
                            ? DateTime.SpecifyKind(result, DateTimeKind.Utc)
                            : result;
                    }
                }

                throw new JsonException($"Unable to convert {reader.GetString()} to DateTime");
            }

            public override void Write(Utf8JsonWriter writer, DateTime value, JsonSerializerOptions options)
            {
                writer.WriteStringValue(value.ToUniversalTime().ToString("O"));
            }
        }

        /// <summary>
        /// Custom converter for TimeSpan
        /// </summary>
        private class TimeSpanConverter : JsonConverter<TimeSpan>
        {
            public override TimeSpan Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
            {
                if (reader.TokenType == JsonTokenType.String)
                {
                    var str = reader.GetString();
                    if (TimeSpan.TryParse(str, out var result))
                    {
                        return result;
                    }
                }

                if (reader.TokenType == JsonTokenType.Number)
                {
                    // Assume milliseconds
                    return TimeSpan.FromMilliseconds(reader.GetDouble());
                }

                throw new JsonException($"Unable to convert {reader.GetString()} to TimeSpan");
            }

            public override void Write(Utf8JsonWriter writer, TimeSpan value, JsonSerializerOptions options)
            {
                writer.WriteStringValue(value.ToString("c"));
            }
        }

        /// <summary>
        /// Custom converter for Guid
        /// </summary>
        private class GuidConverter : JsonConverter<Guid>
        {
            public override Guid Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
            {
                if (reader.TokenType == JsonTokenType.String)
                {
                    var str = reader.GetString();
                    if (Guid.TryParse(str, out var result))
                    {
                        return result;
                    }
                }

                throw new JsonException($"Unable to convert {reader.GetString()} to Guid");
            }

            public override void Write(Utf8JsonWriter writer, Guid value, JsonSerializerOptions options)
            {
                writer.WriteStringValue(value.ToString("D"));
            }
        }

        /// <summary>
        /// Builder for creating custom JsonSerializerOptions
        /// </summary>
        public class OptionsBuilder
        {
            private readonly JsonSerializerOptions _options;

            /// <summary>
            /// Initializes a new instance of OptionsBuilder
            /// </summary>
            public OptionsBuilder()
            {
                _options = new JsonSerializerOptions();
            }

            /// <summary>
            /// Initializes a new instance of OptionsBuilder with base options
            /// </summary>
            public OptionsBuilder(JsonSerializerOptions baseOptions)
            {
                _options = new JsonSerializerOptions(baseOptions);
            }

            /// <summary>
            /// Sets the property naming policy
            /// </summary>
            public OptionsBuilder WithNamingPolicy(JsonNamingPolicy policy)
            {
                _options.PropertyNamingPolicy = policy;
                return this;
            }

            /// <summary>
            /// Sets whether to write indented JSON
            /// </summary>
            public OptionsBuilder WithIndentation(bool writeIndented)
            {
                _options.WriteIndented = writeIndented;
                return this;
            }

            /// <summary>
            /// Sets the default ignore condition
            /// </summary>
            public OptionsBuilder WithIgnoreCondition(JsonIgnoreCondition condition)
            {
                _options.DefaultIgnoreCondition = condition;
                return this;
            }

            /// <summary>
            /// Sets whether property names are case insensitive
            /// </summary>
            public OptionsBuilder WithCaseInsensitive(bool caseInsensitive)
            {
                _options.PropertyNameCaseInsensitive = caseInsensitive;
                return this;
            }

            /// <summary>
            /// Adds a custom converter
            /// </summary>
            public OptionsBuilder WithConverter(JsonConverter converter)
            {
                _options.Converters.Add(converter);
                return this;
            }

            /// <summary>
            /// Sets the maximum depth
            /// </summary>
            public OptionsBuilder WithMaxDepth(int maxDepth)
            {
                _options.MaxDepth = maxDepth;
                return this;
            }

            /// <summary>
            /// Sets the buffer size
            /// </summary>
            public OptionsBuilder WithBufferSize(int bufferSize)
            {
                _options.DefaultBufferSize = bufferSize;
                return this;
            }

            /// <summary>
            /// Builds the JsonSerializerOptions
            /// </summary>
            public JsonSerializerOptions Build()
            {
                return _options;
            }
        }

        /// <summary>
        /// Creates a new options builder
        /// </summary>
        public static OptionsBuilder CreateOptionsBuilder()
        {
            return new OptionsBuilder();
        }

        /// <summary>
        /// Creates a new options builder with base options
        /// </summary>
        public static OptionsBuilder CreateOptionsBuilder(JsonSerializerOptions baseOptions)
        {
            return new OptionsBuilder(baseOptions);
        }
    }
}
