using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Configuration for HTTP codec filter.
    /// </summary>
    public class HttpCodecConfig : FilterConfigBase
    {
        /// <summary>
        /// Gets or sets the default encoding.
        /// </summary>
        public Encoding DefaultEncoding { get; set; } = Encoding.UTF8;

        /// <summary>
        /// Gets or sets the maximum header size.
        /// </summary>
        public int MaxHeaderSize { get; set; } = 8192;

        /// <summary>
        /// Gets or sets the maximum body size.
        /// </summary>
        public int MaxBodySize { get; set; } = 10 * 1024 * 1024; // 10MB

        /// <summary>
        /// Gets or sets whether to validate headers.
        /// </summary>
        public bool ValidateHeaders { get; set; } = true;

        /// <summary>
        /// Gets or sets whether to normalize headers.
        /// </summary>
        public bool NormalizeHeaders { get; set; } = true;

        /// <summary>
        /// Gets or sets whether to auto-detect encoding.
        /// </summary>
        public bool AutoDetectEncoding { get; set; } = true;
    }

    /// <summary>
    /// HTTP codec filter for encoding/decoding HTTP messages.
    /// </summary>
    public class HttpCodecFilter : Filter
    {
        private readonly HttpCodecConfig _config;
        private readonly ILogger<HttpCodecFilter> _logger;

        /// <summary>
        /// Initializes a new instance of the HttpCodecFilter class.
        /// </summary>
        /// <param name="config">The HTTP codec configuration.</param>
        /// <param name="logger">Optional logger.</param>
        public HttpCodecFilter(HttpCodecConfig config, ILogger<HttpCodecFilter> logger = null)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
        }

        /// <summary>
        /// Processes buffer through the HTTP codec filter.
        /// </summary>
        protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            try
            {
                // For now, just pass through the buffer
                // In a real implementation, this would parse/serialize HTTP messages
                
                var direction = context?.GetProperty<string>("Direction") ?? "unknown";
                
                if (direction == "encode")
                {
                    // Would encode to HTTP format here
                    _logger?.LogDebug("Would encode to HTTP format");
                }
                else if (direction == "decode")
                {
                    // Would decode from HTTP format here
                    _logger?.LogDebug("Would decode from HTTP format");
                }
                
                await Task.CompletedTask; // Satisfy async requirement
                return FilterResult.Success(buffer, 0, buffer.Length);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error in HTTP codec");
                return FilterResult.Error($"Codec error: {ex.Message}", FilterError.ProcessingFailed);
            }
        }

        /// <summary>
        /// Parses HTTP headers from a stream.
        /// </summary>
        private async Task<Dictionary<string, List<string>>> ParseHeaders(StreamReader reader, CancellationToken cancellationToken)
        {
            var headers = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
            string line;
            
            while ((line = await reader.ReadLineAsync()) != null && !string.IsNullOrEmpty(line))
            {
                var colonIndex = line.IndexOf(':');
                if (colonIndex > 0)
                {
                    var name = line.Substring(0, colonIndex).Trim();
                    var value = line.Substring(colonIndex + 1).Trim();
                    
                    if (_config.NormalizeHeaders)
                    {
                        name = NormalizeHeaderName(name);
                    }
                    
                    if (!headers.ContainsKey(name))
                    {
                        headers[name] = new List<string>();
                    }
                    headers[name].Add(value);
                }
            }
            
            return headers;
        }

        /// <summary>
        /// Normalizes HTTP header name.
        /// </summary>
        private string NormalizeHeaderName(string name)
        {
            // Convert to proper case (e.g., content-type -> Content-Type)
            var parts = name.Split('-');
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i].Length > 0)
                {
                    parts[i] = char.ToUpper(parts[i][0]) + parts[i].Substring(1).ToLower();
                }
            }
            return string.Join("-", parts);
        }

        /// <summary>
        /// Validates HTTP headers.
        /// </summary>
        private bool ValidateHeaders(Dictionary<string, List<string>> headers)
        {
            if (!_config.ValidateHeaders)
                return true;
            
            // Basic validation - check for required headers, invalid characters, etc.
            foreach (var header in headers)
            {
                // Check for invalid characters in header name
                if (header.Key.Any(c => c < 33 || c > 126 || c == ':'))
                {
                    _logger?.LogWarning("Invalid header name: {HeaderName}", header.Key);
                    return false;
                }
                
                // Check for invalid characters in header values
                foreach (var value in header.Value)
                {
                    if (value.Any(c => c == '\r' || c == '\n'))
                    {
                        _logger?.LogWarning("Invalid header value for {HeaderName}", header.Key);
                        return false;
                    }
                }
            }
            
            return true;
        }
    }
}