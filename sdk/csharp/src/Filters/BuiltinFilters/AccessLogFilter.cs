using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum LogFormat
    {
        Json,
        Text,
        Common,
        Combined,
        Custom
    }

    public enum LogTarget
    {
        Console,
        File,
        Both
    }

    public class AccessLogConfig : FilterConfigBase
    {
        public LogFormat Format { get; set; } = LogFormat.Json;
        public LogTarget Target { get; set; } = LogTarget.Console;
        public string? FilePath { get; set; }
        public bool LogRequests { get; set; } = true;
        public bool LogResponses { get; set; } = true;
        public bool LogErrors { get; set; } = true;
        public List<string> FieldsToLog { get; set; } = new()
        {
            "Timestamp",
            "Method",
            "Path",
            "StatusCode",
            "Duration",
            "ClientIp",
            "UserAgent",
            "UserId"
        };
        public List<string> SensitiveHeaders { get; set; } = new()
        {
            "Authorization",
            "Cookie",
            "X-API-Key"
        };
        public bool MaskSensitiveData { get; set; } = true;
        public string? CustomFormat { get; set; }
        public int MaxFileSizeMB { get; set; } = 100;
        public int MaxFileCount { get; set; } = 10;
        public bool EnableRotation { get; set; } = true;
        public bool IncludeHeaders { get; set; } = false;
        public bool IncludeBody { get; set; } = false;
        public int MaxBodyLength { get; set; } = 1024;

        public AccessLogConfig() : base("AccessLog", "AccessLogFilter")
        {
            Priority = 10; // Low priority to run late in the chain
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (Target == LogTarget.File || Target == LogTarget.Both)
            {
                if (string.IsNullOrEmpty(FilePath))
                {
                    errors.Add("File path is required when logging to file");
                }
                else
                {
                    var directory = Path.GetDirectoryName(FilePath);
                    if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
                    {
                        try
                        {
                            Directory.CreateDirectory(directory);
                        }
                        catch (Exception ex)
                        {
                            errors.Add($"Cannot create log directory: {ex.Message}");
                        }
                    }
                }
            }

            if (Format == LogFormat.Custom && string.IsNullOrEmpty(CustomFormat))
            {
                errors.Add("Custom format string is required when using custom log format");
            }

            if (MaxFileSizeMB <= 0)
            {
                errors.Add("Max file size must be greater than 0");
            }

            if (MaxFileCount <= 0)
            {
                errors.Add("Max file count must be greater than 0");
            }

            return errors.Count == 0;
        }
    }

    public class AccessLogFilter : Filter
    {
        private readonly AccessLogConfig _config;
        private readonly SemaphoreSlim _writeLock;
        private StreamWriter? _fileWriter;
        private long _currentFileSize;
        private int _currentFileIndex;
        private readonly ILogger<AccessLogFilter>? _logger;

        public AccessLogFilter(AccessLogConfig config, ILogger<AccessLogFilter>? logger = null) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _writeLock = new SemaphoreSlim(1, 1);
            _logger = logger;
            InitializeFileWriter();
        }

        private void InitializeFileWriter()
        {
            if (_config.Target == LogTarget.File || _config.Target == LogTarget.Both)
            {
                if (!string.IsNullOrEmpty(_config.FilePath))
                {
                    try
                    {
                        var filePath = GetCurrentLogFilePath();
                        _fileWriter = new StreamWriter(filePath, append: true, encoding: Encoding.UTF8)
                        {
                            AutoFlush = true
                        };
                        
                        var fileInfo = new FileInfo(filePath);
                        _currentFileSize = fileInfo.Exists ? fileInfo.Length : 0;
                    }
                    catch (Exception ex)
                    {
                        _logger?.LogError(ex, "Failed to initialize file writer for access log");
                    }
                }
            }
        }

        private string GetCurrentLogFilePath()
        {
            if (string.IsNullOrEmpty(_config.FilePath))
            {
                throw new InvalidOperationException("File path not configured");
            }

            if (!_config.EnableRotation)
            {
                return _config.FilePath;
            }

            var directory = Path.GetDirectoryName(_config.FilePath) ?? ".";
            var fileName = Path.GetFileNameWithoutExtension(_config.FilePath);
            var extension = Path.GetExtension(_config.FilePath);

            return Path.Combine(directory, $"{fileName}.{_currentFileIndex:000}{extension}");
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            var stopwatch = Stopwatch.StartNew();
            FilterResult? result = null;
            Exception? processingError = null;

            try
            {
                // Store request buffer for logging
                if (_config.LogRequests)
                {
                    StoreRequestData(buffer, context);
                }

                // Continue processing
                result = FilterResult.Continue(buffer);

                // Log after processing completes
                stopwatch.Stop();
                
                if (_config.LogResponses || _config.LogRequests)
                {
                    await LogAccessAsync(context, stopwatch.ElapsedMilliseconds, result, null, cancellationToken);
                }

                UpdateStatistics(1L, 0, true);
                await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                return result;
            }
            catch (Exception ex)
            {
                processingError = ex;
                stopwatch.Stop();

                if (_config.LogErrors)
                {
                    await LogAccessAsync(context, stopwatch.ElapsedMilliseconds, null, ex, cancellationToken);
                }

                UpdateStatistics(0L, 0, false);
                await RaiseOnErrorAsync(ex);
                return FilterResult.Error($"Access log error: {ex.Message}", FilterError.InternalError);
            }
        }

        private void StoreRequestData(byte[] buffer, ProcessingContext context)
        {
            // Store request timestamp
            context.SetProperty("RequestTimestamp", DateTime.UtcNow);

            // Store request size
            context.SetProperty("RequestSize", buffer.Length);

            // Store request body if configured
            if (_config.IncludeBody && buffer.Length > 0)
            {
                var bodyToLog = buffer.Length > _config.MaxBodyLength
                    ? Encoding.UTF8.GetString(buffer, 0, _config.MaxBodyLength) + "..."
                    : Encoding.UTF8.GetString(buffer);
                context.SetProperty("RequestBody", bodyToLog);
            }
        }

        private async Task LogAccessAsync(
            ProcessingContext context,
            long durationMs,
            FilterResult? result,
            Exception? error,
            CancellationToken cancellationToken)
        {
            try
            {
                var logEntry = BuildLogEntry(context, durationMs, result, error);
                var formattedLog = FormatLogEntry(logEntry);

                await _writeLock.WaitAsync(cancellationToken);
                try
                {
                    await WriteLogAsync(formattedLog, cancellationToken);
                }
                finally
                {
                    _writeLock.Release();
                }
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Failed to write access log");
            }
        }

        private Dictionary<string, object?> BuildLogEntry(
            ProcessingContext context,
            long durationMs,
            FilterResult? result,
            Exception? error)
        {
            var logEntry = new Dictionary<string, object?>();

            // Add configured fields
            foreach (var field in _config.FieldsToLog)
            {
                switch (field.ToLower())
                {
                    case "timestamp":
                        logEntry["timestamp"] = DateTime.UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'");
                        break;
                    case "method":
                        logEntry["method"] = context.GetProperty<string>("Method") ?? "-";
                        break;
                    case "path":
                        logEntry["path"] = context.GetProperty<string>("Path") ?? "-";
                        break;
                    case "statuscode":
                        logEntry["status_code"] = result?.IsSuccess == true ? 200 : (error != null ? 500 : 400);
                        break;
                    case "duration":
                        logEntry["duration_ms"] = durationMs;
                        break;
                    case "clientip":
                        logEntry["client_ip"] = context.GetProperty<string>("ClientIp") ?? "-";
                        break;
                    case "useragent":
                        logEntry["user_agent"] = context.GetProperty<string>("UserAgent") ?? "-";
                        break;
                    case "userid":
                        logEntry["user_id"] = context.GetProperty<string>("UserId") ?? "-";
                        break;
                    case "requestsize":
                        logEntry["request_size"] = context.GetProperty<int?>("RequestSize") ?? 0;
                        break;
                    case "responsesize":
                        logEntry["response_size"] = result?.Data?.Length ?? 0;
                        break;
                    default:
                        // Try to get custom property
                        var value = context.GetProperty<object>(field);
                        if (value != null)
                        {
                            logEntry[field.ToLower()] = value;
                        }
                        break;
                }
            }

            // Add headers if configured
            if (_config.IncludeHeaders)
            {
                var headers = context.GetProperty<Dictionary<string, string>>("Headers");
                if (headers != null)
                {
                    var sanitizedHeaders = new Dictionary<string, string>();
                    foreach (var header in headers)
                    {
                        if (_config.MaskSensitiveData && _config.SensitiveHeaders.Contains(header.Key))
                        {
                            sanitizedHeaders[header.Key] = "***MASKED***";
                        }
                        else
                        {
                            sanitizedHeaders[header.Key] = header.Value;
                        }
                    }
                    logEntry["headers"] = sanitizedHeaders;
                }
            }

            // Add body if configured
            if (_config.IncludeBody)
            {
                var requestBody = context.GetProperty<string>("RequestBody");
                if (!string.IsNullOrEmpty(requestBody))
                {
                    logEntry["request_body"] = requestBody;
                }
            }

            // Add error information if present
            if (error != null)
            {
                logEntry["error"] = new
                {
                    message = error.Message,
                    type = error.GetType().Name,
                    stack_trace = _config.LogErrors ? error.StackTrace : null
                };
            }

            return logEntry;
        }

        private string FormatLogEntry(Dictionary<string, object?> logEntry)
        {
            switch (_config.Format)
            {
                case LogFormat.Json:
                    return JsonSerializer.Serialize(logEntry, new JsonSerializerOptions
                    {
                        WriteIndented = false,
                        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
                    });

                case LogFormat.Text:
                    var sb = new StringBuilder();
                    foreach (var kvp in logEntry)
                    {
                        sb.Append($"{kvp.Key}={kvp.Value} ");
                    }
                    return sb.ToString().TrimEnd();

                case LogFormat.Common:
                    // Common Log Format
                    return $"{logEntry.GetValueOrDefault("client_ip", "-")} - - " +
                           $"[{logEntry.GetValueOrDefault("timestamp", "-")}] " +
                           $"\"{logEntry.GetValueOrDefault("method", "-")} {logEntry.GetValueOrDefault("path", "-")} HTTP/1.1\" " +
                           $"{logEntry.GetValueOrDefault("status_code", "-")} " +
                           $"{logEntry.GetValueOrDefault("response_size", "-")}";

                case LogFormat.Combined:
                    // Combined Log Format (Common + referer and user-agent)
                    return $"{logEntry.GetValueOrDefault("client_ip", "-")} - " +
                           $"{logEntry.GetValueOrDefault("user_id", "-")} " +
                           $"[{logEntry.GetValueOrDefault("timestamp", "-")}] " +
                           $"\"{logEntry.GetValueOrDefault("method", "-")} {logEntry.GetValueOrDefault("path", "-")} HTTP/1.1\" " +
                           $"{logEntry.GetValueOrDefault("status_code", "-")} " +
                           $"{logEntry.GetValueOrDefault("response_size", "-")} " +
                           $"\"{logEntry.GetValueOrDefault("referer", "-")}\" " +
                           $"\"{logEntry.GetValueOrDefault("user_agent", "-")}\"";

                case LogFormat.Custom:
                    if (string.IsNullOrEmpty(_config.CustomFormat))
                    {
                        goto case LogFormat.Text;
                    }
                    return FormatCustom(_config.CustomFormat, logEntry);

                default:
                    return JsonSerializer.Serialize(logEntry);
            }
        }

        private string FormatCustom(string format, Dictionary<string, object?> logEntry)
        {
            var result = format;
            foreach (var kvp in logEntry)
            {
                var placeholder = $"{{{kvp.Key}}}";
                result = result.Replace(placeholder, kvp.Value?.ToString() ?? "-");
            }
            return result;
        }

        private async Task WriteLogAsync(string logEntry, CancellationToken cancellationToken)
        {
            // Write to console if configured
            if (_config.Target == LogTarget.Console || _config.Target == LogTarget.Both)
            {
                await Console.Out.WriteLineAsync(logEntry);
            }

            // Write to file if configured
            if ((_config.Target == LogTarget.File || _config.Target == LogTarget.Both) && _fileWriter != null)
            {
                await _fileWriter.WriteLineAsync(logEntry);
                _currentFileSize += Encoding.UTF8.GetByteCount(logEntry + Environment.NewLine);

                // Check if rotation is needed
                if (_config.EnableRotation && _currentFileSize >= _config.MaxFileSizeMB * 1024 * 1024)
                {
                    await RotateLogFileAsync(cancellationToken);
                }
            }
        }

        private async Task RotateLogFileAsync(CancellationToken cancellationToken)
        {
            try
            {
                // Close current file
                if (_fileWriter != null)
                {
                    await _fileWriter.FlushAsync();
                    _fileWriter.Close();
                    _fileWriter.Dispose();
                }

                // Increment file index
                _currentFileIndex = (_currentFileIndex + 1) % _config.MaxFileCount;

                // Delete old file if it exists
                var newFilePath = GetCurrentLogFilePath();
                if (File.Exists(newFilePath))
                {
                    File.Delete(newFilePath);
                }

                // Open new file
                _fileWriter = new StreamWriter(newFilePath, append: false, encoding: Encoding.UTF8)
                {
                    AutoFlush = true
                };
                _currentFileSize = 0;

                _logger?.LogInformation($"Rotated log file to: {newFilePath}");
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Failed to rotate log file");
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _writeLock?.Wait();
                try
                {
                    _fileWriter?.Flush();
                    _fileWriter?.Dispose();
                }
                finally
                {
                    _writeLock?.Release();
                    _writeLock?.Dispose();
                }
            }
            base.Dispose(disposing);
        }
    }
}