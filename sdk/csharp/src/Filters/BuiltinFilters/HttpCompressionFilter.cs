using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Configuration for HTTP compression filter.
    /// </summary>
    public class HttpCompressionConfig : FilterConfig
    {
        /// <summary>
        /// Gets or sets the compression algorithms to use.
        /// </summary>
        public List<CompressionAlgorithm> Algorithms { get; set; } = new List<CompressionAlgorithm>
        {
            CompressionAlgorithm.Gzip,
            CompressionAlgorithm.Deflate,
            CompressionAlgorithm.Brotli
        };

        /// <summary>
        /// Gets or sets the minimum size threshold for compression (in bytes).
        /// </summary>
        public int MinimumSizeThreshold { get; set; } = 1024;

        /// <summary>
        /// Gets or sets the compression level.
        /// </summary>
        public CompressionLevel CompressionLevel { get; set; } = CompressionLevel.Optimal;

        /// <summary>
        /// Gets or sets the MIME types to compress.
        /// </summary>
        public List<string> CompressibleMimeTypes { get; set; } = new List<string>
        {
            "text/html",
            "text/css",
            "text/javascript",
            "application/javascript",
            "application/json",
            "application/xml",
            "text/plain",
            "text/xml"
        };

        /// <summary>
        /// Gets or sets whether to compress responses only.
        /// </summary>
        public bool CompressResponsesOnly { get; set; } = true;

        /// <summary>
        /// Gets or sets whether to decompress incoming requests.
        /// </summary>
        public bool DecompressRequests { get; set; } = true;
    }

    /// <summary>
    /// Compression algorithm enumeration.
    /// </summary>
    public enum CompressionAlgorithm
    {
        Gzip,
        Deflate,
        Brotli
    }

    /// <summary>
    /// HTTP compression filter for compressing/decompressing HTTP content.
    /// </summary>
    public class HttpCompressionFilter : Filter
    {
        private readonly HttpCompressionConfig _config;
        private readonly ILogger<HttpCompressionFilter> _logger;

        /// <summary>
        /// Initializes a new instance of the HttpCompressionFilter class.
        /// </summary>
        /// <param name="config">The HTTP compression configuration.</param>
        /// <param name="logger">Optional logger.</param>
        public HttpCompressionFilter(HttpCompressionConfig config, ILogger<HttpCompressionFilter> logger = null)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
        }

        /// <summary>
        /// Processes data through the HTTP compression filter.
        /// </summary>
        /// <param name="data">The data to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        public override async Task<FilterResult> ProcessAsync(object data, CancellationToken cancellationToken = default)
        {
            try
            {
                if (data is not HttpMessage httpMessage)
                {
                    return FilterResult.Error("HttpCompressionFilter requires HttpMessage input");
                }

                if (httpMessage.IsRequest)
                {
                    if (_config.DecompressRequests)
                    {
                        return await DecompressMessage(httpMessage, cancellationToken);
                    }
                }
                else
                {
                    if (!_config.CompressResponsesOnly || !httpMessage.IsRequest)
                    {
                        return await CompressMessage(httpMessage, cancellationToken);
                    }
                }

                return FilterResult.Success(httpMessage);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error in HTTP compression");
                return FilterResult.Error($"Compression error: {ex.Message}");
            }
        }

        /// <summary>
        /// Compresses an HTTP message.
        /// </summary>
        private async Task<FilterResult> CompressMessage(HttpMessage message, CancellationToken cancellationToken)
        {
            // Check if already compressed
            if (HasContentEncoding(message))
            {
                _logger?.LogDebug("Message already has Content-Encoding, skipping compression");
                return FilterResult.Success(message);
            }

            // Check size threshold
            if (message.Body == null || message.Body.Length < _config.MinimumSizeThreshold)
            {
                _logger?.LogDebug("Body size {Size} below threshold {Threshold}, skipping compression",
                    message.Body?.Length ?? 0, _config.MinimumSizeThreshold);
                return FilterResult.Success(message);
            }

            // Check content type
            if (!IsCompressibleContentType(message))
            {
                _logger?.LogDebug("Content type not compressible, skipping compression");
                return FilterResult.Success(message);
            }

            // Determine best algorithm based on Accept-Encoding
            var algorithm = SelectCompressionAlgorithm(message);
            if (algorithm == null)
            {
                _logger?.LogDebug("No acceptable compression algorithm found");
                return FilterResult.Success(message);
            }

            // Compress body
            var compressedBody = await CompressData(message.Body, algorithm.Value, cancellationToken);
            
            if (compressedBody.Length >= message.Body.Length)
            {
                _logger?.LogDebug("Compressed size not smaller, skipping compression");
                return FilterResult.Success(message);
            }

            var originalSize = message.Body.Length;
            message.Body = compressedBody;

            // Update headers
            AddOrUpdateHeader(message.Headers, "Content-Encoding", GetEncodingName(algorithm.Value));
            AddOrUpdateHeader(message.Headers, "Content-Length", compressedBody.Length.ToString());
            AddOrUpdateHeader(message.Headers, "X-Original-Content-Length", originalSize.ToString());

            _logger?.LogInformation("Compressed body from {Original} to {Compressed} bytes using {Algorithm} ({Ratio:P})",
                originalSize, compressedBody.Length, algorithm.Value,
                1.0 - (double)compressedBody.Length / originalSize);

            return FilterResult.Success(message);
        }

        /// <summary>
        /// Decompresses an HTTP message.
        /// </summary>
        private async Task<FilterResult> DecompressMessage(HttpMessage message, CancellationToken cancellationToken)
        {
            var encoding = GetContentEncoding(message);
            if (string.IsNullOrEmpty(encoding) || encoding.Equals("identity", StringComparison.OrdinalIgnoreCase))
            {
                return FilterResult.Success(message);
            }

            if (message.Body == null || message.Body.Length == 0)
            {
                return FilterResult.Success(message);
            }

            var algorithm = ParseEncodingName(encoding);
            if (algorithm == null)
            {
                _logger?.LogWarning("Unknown Content-Encoding: {Encoding}", encoding);
                return FilterResult.Success(message);
            }

            // Decompress body
            var decompressedBody = await DecompressData(message.Body, algorithm.Value, cancellationToken);
            
            var originalSize = message.Body.Length;
            message.Body = decompressedBody;

            // Update headers
            RemoveHeader(message.Headers, "Content-Encoding");
            AddOrUpdateHeader(message.Headers, "Content-Length", decompressedBody.Length.ToString());

            _logger?.LogInformation("Decompressed body from {Compressed} to {Original} bytes using {Algorithm}",
                originalSize, decompressedBody.Length, algorithm.Value);

            return FilterResult.Success(message);
        }

        /// <summary>
        /// Compresses data using the specified algorithm.
        /// </summary>
        private async Task<byte[]> CompressData(byte[] data, CompressionAlgorithm algorithm, CancellationToken cancellationToken)
        {
            using var output = new MemoryStream();
            Stream compressionStream = algorithm switch
            {
                CompressionAlgorithm.Gzip => new GZipStream(output, _config.CompressionLevel),
                CompressionAlgorithm.Deflate => new DeflateStream(output, _config.CompressionLevel),
                CompressionAlgorithm.Brotli => new BrotliStream(output, _config.CompressionLevel),
                _ => throw new NotSupportedException($"Algorithm {algorithm} not supported")
            };

            using (compressionStream)
            {
                await compressionStream.WriteAsync(data, 0, data.Length, cancellationToken);
            }

            return output.ToArray();
        }

        /// <summary>
        /// Decompresses data using the specified algorithm.
        /// </summary>
        private async Task<byte[]> DecompressData(byte[] data, CompressionAlgorithm algorithm, CancellationToken cancellationToken)
        {
            using var input = new MemoryStream(data);
            using var output = new MemoryStream();
            
            Stream decompressionStream = algorithm switch
            {
                CompressionAlgorithm.Gzip => new GZipStream(input, CompressionMode.Decompress),
                CompressionAlgorithm.Deflate => new DeflateStream(input, CompressionMode.Decompress),
                CompressionAlgorithm.Brotli => new BrotliStream(input, CompressionMode.Decompress),
                _ => throw new NotSupportedException($"Algorithm {algorithm} not supported")
            };

            using (decompressionStream)
            {
                await decompressionStream.CopyToAsync(output, 81920, cancellationToken);
            }

            return output.ToArray();
        }

        /// <summary>
        /// Selects the best compression algorithm based on Accept-Encoding header.
        /// </summary>
        private CompressionAlgorithm? SelectCompressionAlgorithm(HttpMessage message)
        {
            if (!message.Headers.TryGetValue("Accept-Encoding", out var acceptEncodings))
            {
                return _config.Algorithms.FirstOrDefault();
            }

            var acceptedEncodings = ParseAcceptEncoding(string.Join(",", acceptEncodings));

            foreach (var algorithm in _config.Algorithms)
            {
                var name = GetEncodingName(algorithm);
                if (acceptedEncodings.ContainsKey(name))
                {
                    return algorithm;
                }
            }

            return null;
        }

        /// <summary>
        /// Parses Accept-Encoding header value.
        /// </summary>
        private Dictionary<string, double> ParseAcceptEncoding(string acceptEncoding)
        {
            var encodings = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);

            if (string.IsNullOrEmpty(acceptEncoding))
                return encodings;

            foreach (var part in acceptEncoding.Split(','))
            {
                var trimmed = part.Trim();
                var semicolonIndex = trimmed.IndexOf(';');
                
                string encoding;
                double quality = 1.0;

                if (semicolonIndex >= 0)
                {
                    encoding = trimmed.Substring(0, semicolonIndex).Trim();
                    var qualityPart = trimmed.Substring(semicolonIndex + 1).Trim();
                    
                    if (qualityPart.StartsWith("q="))
                    {
                        if (double.TryParse(qualityPart.Substring(2), out var q))
                        {
                            quality = q;
                        }
                    }
                }
                else
                {
                    encoding = trimmed;
                }

                if (!string.IsNullOrEmpty(encoding))
                {
                    encodings[encoding] = quality;
                }
            }

            return encodings;
        }

        /// <summary>
        /// Checks if content type is compressible.
        /// </summary>
        private bool IsCompressibleContentType(HttpMessage message)
        {
            if (!message.Headers.TryGetValue("Content-Type", out var contentTypes))
            {
                return false;
            }

            var contentType = contentTypes.FirstOrDefault()?.Split(';')[0].Trim();
            if (string.IsNullOrEmpty(contentType))
            {
                return false;
            }

            return _config.CompressibleMimeTypes.Any(mime => 
                contentType.Equals(mime, StringComparison.OrdinalIgnoreCase));
        }

        /// <summary>
        /// Checks if message has Content-Encoding header.
        /// </summary>
        private bool HasContentEncoding(HttpMessage message)
        {
            return message.Headers.ContainsKey("Content-Encoding");
        }

        /// <summary>
        /// Gets Content-Encoding header value.
        /// </summary>
        private string GetContentEncoding(HttpMessage message)
        {
            if (message.Headers.TryGetValue("Content-Encoding", out var encodings))
            {
                return encodings.FirstOrDefault();
            }
            return null;
        }

        /// <summary>
        /// Gets encoding name for algorithm.
        /// </summary>
        private string GetEncodingName(CompressionAlgorithm algorithm)
        {
            return algorithm switch
            {
                CompressionAlgorithm.Gzip => "gzip",
                CompressionAlgorithm.Deflate => "deflate",
                CompressionAlgorithm.Brotli => "br",
                _ => algorithm.ToString().ToLowerInvariant()
            };
        }

        /// <summary>
        /// Parses encoding name to algorithm.
        /// </summary>
        private CompressionAlgorithm? ParseEncodingName(string encoding)
        {
            return encoding?.ToLowerInvariant() switch
            {
                "gzip" => CompressionAlgorithm.Gzip,
                "deflate" => CompressionAlgorithm.Deflate,
                "br" => CompressionAlgorithm.Brotli,
                _ => null
            };
        }

        /// <summary>
        /// Adds or updates a header.
        /// </summary>
        private void AddOrUpdateHeader(Dictionary<string, List<string>> headers, string name, string value)
        {
            headers[name] = new List<string> { value };
        }

        /// <summary>
        /// Removes a header.
        /// </summary>
        private void RemoveHeader(Dictionary<string, List<string>> headers, string name)
        {
            headers.Remove(name);
        }
    }
}