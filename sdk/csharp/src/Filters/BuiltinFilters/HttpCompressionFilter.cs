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
    public class HttpCompressionConfig : FilterConfigBase
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
        /// Processes buffer through the HTTP compression filter.
        /// </summary>
        /// <param name="buffer">The buffer to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            try
            {
                // For now, just pass through the buffer
                // In a real implementation, this would parse HTTP headers and compress/decompress the body

                // Check if this is an HTTP response based on context
                var isResponse = context?.GetProperty<bool>("IsHttpResponse") ?? false;

                if (isResponse && !_config.CompressResponsesOnly)
                {
                    // Would compress the response body here
                    _logger?.LogDebug("Would compress HTTP response");
                }
                else if (!isResponse && _config.DecompressRequests)
                {
                    // Would decompress the request body here
                    _logger?.LogDebug("Would decompress HTTP request");
                }

                await Task.CompletedTask; // Satisfy async requirement
                return FilterResult.Success(buffer, 0, buffer.Length);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error in HTTP compression");
                return FilterResult.Error($"Compression error: {ex.Message}", FilterError.ProcessingFailed);
            }
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
    }
}
