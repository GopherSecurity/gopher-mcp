using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
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
    public class HttpCodecConfig : FilterConfig
    {
        /// <summary>
        /// Gets or sets the maximum header size in bytes.
        /// </summary>
        public int MaxHeaderSize { get; set; } = 8192;

        /// <summary>
        /// Gets or sets the maximum body size in bytes.
        /// </summary>
        public long MaxBodySize { get; set; } = 10 * 1024 * 1024; // 10MB

        /// <summary>
        /// Gets or sets whether to parse request body.
        /// </summary>
        public bool ParseBody { get; set; } = true;

        /// <summary>
        /// Gets or sets whether to validate headers.
        /// </summary>
        public bool ValidateHeaders { get; set; } = true;

        /// <summary>
        /// Gets or sets the default HTTP version.
        /// </summary>
        public Version DefaultHttpVersion { get; set; } = new Version(1, 1);

        /// <summary>
        /// Gets or sets whether to handle chunked encoding.
        /// </summary>
        public bool HandleChunkedEncoding { get; set; } = true;

        /// <summary>
        /// Gets or sets the encoding for text content.
        /// </summary>
        public Encoding DefaultEncoding { get; set; } = Encoding.UTF8;
    }

    /// <summary>
    /// Represents an HTTP message.
    /// </summary>
    public class HttpMessage
    {
        public string Method { get; set; }
        public string Path { get; set; }
        public Version HttpVersion { get; set; }
        public int StatusCode { get; set; }
        public string StatusText { get; set; }
        public Dictionary<string, List<string>> Headers { get; set; }
        public byte[] Body { get; set; }
        public bool IsRequest { get; set; }

        public HttpMessage()
        {
            Headers = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
            HttpVersion = new Version(1, 1);
        }
    }

    /// <summary>
    /// HTTP codec filter for parsing and encoding HTTP messages.
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
        /// Processes data through the HTTP codec.
        /// </summary>
        /// <param name="data">The data to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        public override async Task<FilterResult> ProcessAsync(object data, CancellationToken cancellationToken = default)
        {
            try
            {
                switch (data)
                {
                    case byte[] bytes:
                        return await ParseHttpMessage(bytes, cancellationToken);
                    
                    case string str:
                        return await ParseHttpMessage(_config.DefaultEncoding.GetBytes(str), cancellationToken);
                    
                    case HttpMessage message:
                        return await EncodeHttpMessage(message, cancellationToken);
                    
                    case HttpRequestMessage request:
                        return await ConvertFromHttpRequestMessage(request, cancellationToken);
                    
                    case HttpResponseMessage response:
                        return await ConvertFromHttpResponseMessage(response, cancellationToken);
                    
                    default:
                        return FilterResult.Error($"Unsupported data type: {data?.GetType()}");
                }
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error processing HTTP message");
                return FilterResult.Error($"HTTP codec error: {ex.Message}");
            }
        }

        /// <summary>
        /// Parses an HTTP message from bytes.
        /// </summary>
        private async Task<FilterResult> ParseHttpMessage(byte[] data, CancellationToken cancellationToken)
        {
            using var stream = new MemoryStream(data);
            using var reader = new StreamReader(stream, _config.DefaultEncoding, false, 1024, true);

            var message = new HttpMessage();
            
            // Parse first line
            var firstLine = await reader.ReadLineAsync(cancellationToken);
            if (string.IsNullOrEmpty(firstLine))
            {
                return FilterResult.Error("Empty HTTP message");
            }

            var parts = firstLine.Split(' ', 3);
            if (parts.Length < 2)
            {
                return FilterResult.Error("Invalid HTTP first line");
            }

            // Determine if request or response
            if (parts[0].StartsWith("HTTP/"))
            {
                // Response
                message.IsRequest = false;
                message.HttpVersion = ParseHttpVersion(parts[0]);
                if (int.TryParse(parts[1], out var statusCode))
                {
                    message.StatusCode = statusCode;
                }
                message.StatusText = parts.Length > 2 ? parts[2] : "";
            }
            else
            {
                // Request
                message.IsRequest = true;
                message.Method = parts[0];
                message.Path = parts[1];
                message.HttpVersion = parts.Length > 2 ? ParseHttpVersion(parts[2]) : _config.DefaultHttpVersion;
            }

            // Parse headers
            var headerSize = 0;
            string line;
            while (!string.IsNullOrEmpty(line = await reader.ReadLineAsync(cancellationToken)))
            {
                headerSize += line.Length + 2; // +2 for CRLF
                if (headerSize > _config.MaxHeaderSize)
                {
                    return FilterResult.Error($"Headers too large: {headerSize} bytes");
                }

                var colonIndex = line.IndexOf(':');
                if (colonIndex > 0)
                {
                    var name = line.Substring(0, colonIndex).Trim();
                    var value = line.Substring(colonIndex + 1).Trim();

                    if (_config.ValidateHeaders && !IsValidHeaderName(name))
                    {
                        return FilterResult.Error($"Invalid header name: {name}");
                    }

                    if (!message.Headers.TryGetValue(name, out var values))
                    {
                        values = new List<string>();
                        message.Headers[name] = values;
                    }
                    values.Add(value);
                }
            }

            // Parse body if present
            if (_config.ParseBody)
            {
                var contentLength = GetContentLength(message.Headers);
                if (contentLength > 0)
                {
                    if (contentLength > _config.MaxBodySize)
                    {
                        return FilterResult.Error($"Body too large: {contentLength} bytes");
                    }

                    message.Body = new byte[contentLength];
                    stream.Position = stream.Length - contentLength;
                    await stream.ReadAsync(message.Body, 0, (int)contentLength, cancellationToken);
                }
                else if (_config.HandleChunkedEncoding && IsChunkedEncoding(message.Headers))
                {
                    message.Body = await ReadChunkedBody(stream, cancellationToken);
                }
            }

            _logger?.LogDebug("Parsed HTTP {Type}: {Method} {Path} {StatusCode}",
                message.IsRequest ? "request" : "response",
                message.Method, message.Path, message.StatusCode);

            return FilterResult.Success(message);
        }

        /// <summary>
        /// Encodes an HTTP message to bytes.
        /// </summary>
        private async Task<FilterResult> EncodeHttpMessage(HttpMessage message, CancellationToken cancellationToken)
        {
            using var stream = new MemoryStream();
            using var writer = new StreamWriter(stream, _config.DefaultEncoding, 1024, true);

            // Write first line
            if (message.IsRequest)
            {
                await writer.WriteAsync($"{message.Method} {message.Path} HTTP/{message.HttpVersion}\r\n");
            }
            else
            {
                await writer.WriteAsync($"HTTP/{message.HttpVersion} {message.StatusCode} {message.StatusText}\r\n");
            }

            // Write headers
            foreach (var header in message.Headers)
            {
                foreach (var value in header.Value)
                {
                    await writer.WriteAsync($"{header.Key}: {value}\r\n");
                }
            }

            // Update Content-Length if body present
            if (message.Body != null && message.Body.Length > 0)
            {
                if (!message.Headers.ContainsKey("Content-Length"))
                {
                    await writer.WriteAsync($"Content-Length: {message.Body.Length}\r\n");
                }
            }

            // End headers
            await writer.WriteAsync("\r\n");
            await writer.FlushAsync();

            // Write body
            if (message.Body != null && message.Body.Length > 0)
            {
                await stream.WriteAsync(message.Body, 0, message.Body.Length, cancellationToken);
            }

            var result = stream.ToArray();
            _logger?.LogDebug("Encoded HTTP {Type}: {Size} bytes", 
                message.IsRequest ? "request" : "response", result.Length);

            return FilterResult.Success(result);
        }

        /// <summary>
        /// Converts from HttpRequestMessage.
        /// </summary>
        private async Task<FilterResult> ConvertFromHttpRequestMessage(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            var message = new HttpMessage
            {
                IsRequest = true,
                Method = request.Method.ToString(),
                Path = request.RequestUri?.PathAndQuery ?? "/",
                HttpVersion = request.Version
            };

            // Copy headers
            foreach (var header in request.Headers)
            {
                message.Headers[header.Key] = header.Value.ToList();
            }

            if (request.Content != null)
            {
                foreach (var header in request.Content.Headers)
                {
                    message.Headers[header.Key] = header.Value.ToList();
                }

                message.Body = await request.Content.ReadAsByteArrayAsync(cancellationToken);
            }

            return FilterResult.Success(message);
        }

        /// <summary>
        /// Converts from HttpResponseMessage.
        /// </summary>
        private async Task<FilterResult> ConvertFromHttpResponseMessage(HttpResponseMessage response, CancellationToken cancellationToken)
        {
            var message = new HttpMessage
            {
                IsRequest = false,
                StatusCode = (int)response.StatusCode,
                StatusText = response.ReasonPhrase ?? "",
                HttpVersion = response.Version
            };

            // Copy headers
            foreach (var header in response.Headers)
            {
                message.Headers[header.Key] = header.Value.ToList();
            }

            if (response.Content != null)
            {
                foreach (var header in response.Content.Headers)
                {
                    message.Headers[header.Key] = header.Value.ToList();
                }

                message.Body = await response.Content.ReadAsByteArrayAsync(cancellationToken);
            }

            return FilterResult.Success(message);
        }

        /// <summary>
        /// Parses HTTP version string.
        /// </summary>
        private Version ParseHttpVersion(string versionStr)
        {
            if (versionStr.StartsWith("HTTP/"))
            {
                versionStr = versionStr.Substring(5);
            }

            if (Version.TryParse(versionStr, out var version))
            {
                return version;
            }

            return _config.DefaultHttpVersion;
        }

        /// <summary>
        /// Gets content length from headers.
        /// </summary>
        private long GetContentLength(Dictionary<string, List<string>> headers)
        {
            if (headers.TryGetValue("Content-Length", out var values) && values.Count > 0)
            {
                if (long.TryParse(values[0], out var length))
                {
                    return length;
                }
            }
            return 0;
        }

        /// <summary>
        /// Checks if chunked encoding is used.
        /// </summary>
        private bool IsChunkedEncoding(Dictionary<string, List<string>> headers)
        {
            if (headers.TryGetValue("Transfer-Encoding", out var values))
            {
                return values.Any(v => v.Contains("chunked", StringComparison.OrdinalIgnoreCase));
            }
            return false;
        }

        /// <summary>
        /// Reads chunked body.
        /// </summary>
        private async Task<byte[]> ReadChunkedBody(Stream stream, CancellationToken cancellationToken)
        {
            var chunks = new List<byte>();
            var reader = new StreamReader(stream, _config.DefaultEncoding, false, 1024, true);

            while (true)
            {
                var sizeLine = await reader.ReadLineAsync(cancellationToken);
                if (string.IsNullOrEmpty(sizeLine))
                    break;

                var sizeStr = sizeLine.Split(';')[0].Trim();
                if (!int.TryParse(sizeStr, System.Globalization.NumberStyles.HexNumber, null, out var chunkSize))
                    break;

                if (chunkSize == 0)
                    break;

                var chunk = new byte[chunkSize];
                await stream.ReadAsync(chunk, 0, chunkSize, cancellationToken);
                chunks.AddRange(chunk);

                // Read trailing CRLF
                await reader.ReadLineAsync(cancellationToken);
            }

            return chunks.ToArray();
        }

        /// <summary>
        /// Validates header name.
        /// </summary>
        private bool IsValidHeaderName(string name)
        {
            if (string.IsNullOrEmpty(name))
                return false;

            foreach (char c in name)
            {
                if (!char.IsLetterOrDigit(c) && c != '-' && c != '_')
                    return false;
            }

            return true;
        }
    }
}