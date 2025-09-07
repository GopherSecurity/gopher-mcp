using System;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp;
using GopherMcp.Filters;
using GopherMcp.Manager;
using GopherMcp.Types;

namespace BasicUsage
{
    class Program
    {
        static async Task Main(string[] args)
        {
            Console.WriteLine("=== GopherMcp Basic Usage Example ===\n");

            try
            {
                // Create FilterManager configuration
                var config = new FilterManagerConfig
                {
                    MaxConcurrency = 4,
                    DefaultTimeout = TimeSpan.FromSeconds(30),
                    EnableMetrics = true,
                    LogLevel = LogLevel.Debug
                };

                // Create FilterManager
                using var manager = new FilterManager(config);

                // Register custom event handlers
                manager.FilterRegistered += (sender, e) =>
                {
                    Console.WriteLine($"[Event] Filter registered: {e.FilterName}");
                };

                manager.FilterUnregistered += (sender, e) =>
                {
                    Console.WriteLine($"[Event] Filter unregistered: {e.FilterName}");
                };

                manager.ChainCreated += (sender, e) =>
                {
                    Console.WriteLine($"[Event] Chain created: {e.ChainName}");
                };

                manager.ProcessingCompleted += (sender, e) =>
                {
                    Console.WriteLine($"[Event] Processing completed - Success: {e.IsSuccess}, Duration: {e.Duration}ms");
                };

                // Configure basic filters
                await ConfigureBasicFilters(manager);

                // Create and configure filter chain
                var chainConfig = new ChainConfig
                {
                    Name = "BasicProcessingChain",
                    Mode = ExecutionMode.Sequential,
                    ContinueOnError = false,
                    MaxRetries = 3,
                    RetryDelay = TimeSpan.FromSeconds(1)
                };

                var chain = await manager.CreateChainAsync(chainConfig);
                
                // Add filters to chain
                chain.AddFilter("ValidationFilter");
                chain.AddFilter("LoggingFilter");
                chain.AddFilter("TransformFilter");
                chain.AddFilter("CompressionFilter");

                // Process some sample messages
                await ProcessSampleMessages(manager);

                // Display statistics
                DisplayStatistics(manager);

                // Demonstrate error handling
                await DemonstrateErrorHandling(manager);

                Console.WriteLine("\n=== Example completed successfully ===");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"\n[ERROR] {ex.GetType().Name}: {ex.Message}");
                if (ex.InnerException != null)
                {
                    Console.WriteLine($"  Inner: {ex.InnerException.Message}");
                }
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }

        static async Task ConfigureBasicFilters(FilterManager manager)
        {
            Console.WriteLine("Configuring filters...\n");

            // Register a validation filter
            var validationFilter = new ValidationFilter(new ValidationFilterConfig
            {
                MaxMessageSize = 1024 * 1024, // 1MB
                RequiredFields = new[] { "id", "method" },
                ValidateJsonSchema = true
            });
            await manager.RegisterFilterAsync("ValidationFilter", validationFilter);

            // Register a logging filter
            var loggingFilter = new LoggingFilter(new LoggingFilterConfig
            {
                LogLevel = LogLevel.Info,
                IncludeTimestamp = true,
                IncludeMessageContent = true,
                MaxContentLength = 500
            });
            await manager.RegisterFilterAsync("LoggingFilter", loggingFilter);

            // Register a transform filter
            var transformFilter = new TransformFilter(new TransformFilterConfig
            {
                AddTimestamp = true,
                AddMessageId = true,
                TransformCase = CaseTransform.None
            });
            await manager.RegisterFilterAsync("TransformFilter", transformFilter);

            // Register a compression filter
            var compressionFilter = new CompressionFilter(new CompressionFilterConfig
            {
                Algorithm = CompressionAlgorithm.Gzip,
                Level = 6,
                MinSizeThreshold = 100
            });
            await manager.RegisterFilterAsync("CompressionFilter", compressionFilter);

            Console.WriteLine("Filters configured successfully.\n");
        }

        static async Task ProcessSampleMessages(FilterManager manager)
        {
            Console.WriteLine("Processing sample messages...\n");

            var messages = new[]
            {
                new { id = "msg-001", method = "test.ping", data = "Hello World" },
                new { id = "msg-002", method = "test.echo", data = "Echo this message" },
                new { id = "msg-003", method = "test.info", data = "System information request" }
            };

            foreach (var message in messages)
            {
                var json = System.Text.Json.JsonSerializer.Serialize(message);
                var data = Encoding.UTF8.GetBytes(json);

                var context = new ProcessingContext
                {
                    Direction = ProcessingDirection.Inbound,
                    CorrelationId = Guid.NewGuid().ToString()
                };
                context.SetProperty("MessageId", message.id);
                context.SetProperty("Method", message.method);

                Console.WriteLine($"Processing message: {message.id} ({message.method})");

                var result = await manager.ProcessAsync(data, context);

                if (result.IsSuccess)
                {
                    Console.WriteLine($"  ✓ Success - Processed size: {result.Data.Length} bytes");
                    
                    // Display transformed message
                    if (result.Data.Length < 1000)
                    {
                        var processedJson = Encoding.UTF8.GetString(result.Data);
                        Console.WriteLine($"  Result: {processedJson}");
                    }
                }
                else
                {
                    Console.WriteLine($"  ✗ Failed: {result.ErrorMessage}");
                }

                Console.WriteLine();
            }
        }

        static void DisplayStatistics(FilterManager manager)
        {
            Console.WriteLine("=== Processing Statistics ===\n");

            var stats = manager.GetStatistics();

            Console.WriteLine($"Total Messages Processed: {stats.TotalMessagesProcessed}");
            Console.WriteLine($"Successful: {stats.SuccessfulMessages}");
            Console.WriteLine($"Failed: {stats.FailedMessages}");
            Console.WriteLine($"Average Processing Time: {stats.AverageProcessingTime:F2}ms");
            Console.WriteLine($"Total Bytes Processed: {stats.TotalBytesProcessed:N0}");
            Console.WriteLine($"Active Chains: {stats.ActiveChains}");
            Console.WriteLine($"Registered Filters: {stats.RegisteredFilters}");

            if (stats.FilterStatistics != null && stats.FilterStatistics.Count > 0)
            {
                Console.WriteLine("\nFilter-specific Statistics:");
                foreach (var filterStat in stats.FilterStatistics)
                {
                    Console.WriteLine($"  {filterStat.Key}:");
                    Console.WriteLine($"    - Invocations: {filterStat.Value.Invocations}");
                    Console.WriteLine($"    - Avg Time: {filterStat.Value.AverageTime:F2}ms");
                    Console.WriteLine($"    - Errors: {filterStat.Value.Errors}");
                }
            }

            Console.WriteLine();
        }

        static async Task DemonstrateErrorHandling(FilterManager manager)
        {
            Console.WriteLine("=== Error Handling Demonstration ===\n");

            // Try to process invalid message
            Console.WriteLine("1. Processing invalid message (missing required fields):");
            var invalidMessage = new { data = "Invalid message without required fields" };
            var invalidJson = System.Text.Json.JsonSerializer.Serialize(invalidMessage);
            var invalidData = Encoding.UTF8.GetBytes(invalidJson);

            var context = new ProcessingContext
            {
                Direction = ProcessingDirection.Inbound
            };

            var result = await manager.ProcessAsync(invalidData, context);
            Console.WriteLine($"   Result: {(result.IsSuccess ? "Success" : $"Failed - {result.ErrorMessage}")}");

            // Try to process oversized message
            Console.WriteLine("\n2. Processing oversized message:");
            var largeMessage = new 
            { 
                id = "large-msg", 
                method = "test.large",
                data = new string('X', 2 * 1024 * 1024) // 2MB of data
            };
            var largeJson = System.Text.Json.JsonSerializer.Serialize(largeMessage);
            var largeData = Encoding.UTF8.GetBytes(largeJson);

            var largeContext = new ProcessingContext
            {
                Direction = ProcessingDirection.Outbound
            };

            var largeResult = await manager.ProcessAsync(largeData, largeContext);
            Console.WriteLine($"   Result: {(largeResult.IsSuccess ? "Success" : $"Failed - {largeResult.ErrorMessage}")}");

            // Demonstrate retry mechanism
            Console.WriteLine("\n3. Testing retry mechanism with transient error:");
            
            // Register a filter that fails intermittently
            var retryTestFilter = new TransientErrorFilter();
            await manager.RegisterFilterAsync("TransientErrorFilter", retryTestFilter);

            var retryChainConfig = new ChainConfig
            {
                Name = "RetryTestChain",
                Mode = ExecutionMode.Sequential,
                ContinueOnError = false,
                MaxRetries = 3,
                RetryDelay = TimeSpan.FromMilliseconds(500)
            };

            var retryChain = await manager.CreateChainAsync(retryChainConfig);
            retryChain.AddFilter("TransientErrorFilter");

            var retryMessage = new { id = "retry-msg", method = "test.retry", data = "Test retry" };
            var retryJson = System.Text.Json.JsonSerializer.Serialize(retryMessage);
            var retryData = Encoding.UTF8.GetBytes(retryJson);

            var retryContext = new ProcessingContext
            {
                Direction = ProcessingDirection.Inbound,
                ChainName = "RetryTestChain"
            };

            var retryResult = await manager.ProcessAsync(retryData, retryContext);
            Console.WriteLine($"   Result after retries: {(retryResult.IsSuccess ? "Success" : $"Failed - {retryResult.ErrorMessage}")}");
            Console.WriteLine($"   Attempts made: {retryTestFilter.AttemptCount}");
        }
    }

    // Custom filter implementations for demonstration
    public class ValidationFilter : FilterBase
    {
        private readonly ValidationFilterConfig _config;

        public ValidationFilter(ValidationFilterConfig config) : base(config)
        {
            _config = config;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            try
            {
                // Check message size
                if (data.Length > _config.MaxMessageSize)
                {
                    return ProcessingResult.Failure($"Message size {data.Length} exceeds maximum {_config.MaxMessageSize}");
                }

                // Parse and validate JSON
                var json = Encoding.UTF8.GetString(data);
                var document = System.Text.Json.JsonDocument.Parse(json);

                // Check required fields
                foreach (var field in _config.RequiredFields)
                {
                    if (!document.RootElement.TryGetProperty(field, out _))
                    {
                        return ProcessingResult.Failure($"Required field '{field}' is missing");
                    }
                }

                return ProcessingResult.Success(data);
            }
            catch (Exception ex)
            {
                return ProcessingResult.Failure($"Validation error: {ex.Message}");
            }
        }
    }

    public class ValidationFilterConfig : FilterConfig
    {
        public int MaxMessageSize { get; set; } = 1024 * 1024;
        public string[] RequiredFields { get; set; } = Array.Empty<string>();
        public bool ValidateJsonSchema { get; set; } = false;
    }

    public class LoggingFilter : FilterBase
    {
        private readonly LoggingFilterConfig _config;

        public LoggingFilter(LoggingFilterConfig config) : base(config)
        {
            _config = config;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            var timestamp = _config.IncludeTimestamp ? $"[{DateTime.UtcNow:yyyy-MM-dd HH:mm:ss.fff}] " : "";
            var direction = context.Direction == ProcessingDirection.Inbound ? "IN" : "OUT";
            
            Console.WriteLine($"{timestamp}[{_config.LogLevel}] {direction} - Processing message");

            if (_config.IncludeMessageContent && data.Length <= _config.MaxContentLength)
            {
                var content = Encoding.UTF8.GetString(data);
                Console.WriteLine($"  Content: {content}");
            }

            return ProcessingResult.Success(data);
        }
    }

    public class LoggingFilterConfig : FilterConfig
    {
        public LogLevel LogLevel { get; set; } = LogLevel.Info;
        public bool IncludeTimestamp { get; set; } = true;
        public bool IncludeMessageContent { get; set; } = false;
        public int MaxContentLength { get; set; } = 1000;
    }

    public class TransformFilter : FilterBase
    {
        private readonly TransformFilterConfig _config;

        public TransformFilter(TransformFilterConfig config) : base(config)
        {
            _config = config;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            try
            {
                var json = Encoding.UTF8.GetString(data);
                var document = System.Text.Json.JsonDocument.Parse(json);
                
                using var stream = new System.IO.MemoryStream();
                using var writer = new System.Text.Json.Utf8JsonWriter(stream);

                writer.WriteStartObject();

                // Copy existing properties
                foreach (var property in document.RootElement.EnumerateObject())
                {
                    property.WriteTo(writer);
                }

                // Add timestamp if configured
                if (_config.AddTimestamp)
                {
                    writer.WriteString("timestamp", DateTime.UtcNow);
                }

                // Add message ID if configured
                if (_config.AddMessageId && !document.RootElement.TryGetProperty("messageId", out _))
                {
                    writer.WriteString("messageId", Guid.NewGuid().ToString());
                }

                writer.WriteEndObject();
                writer.Flush();

                return ProcessingResult.Success(stream.ToArray());
            }
            catch (Exception ex)
            {
                return ProcessingResult.Failure($"Transform error: {ex.Message}");
            }
        }
    }

    public class TransformFilterConfig : FilterConfig
    {
        public bool AddTimestamp { get; set; } = true;
        public bool AddMessageId { get; set; } = true;
        public CaseTransform TransformCase { get; set; } = CaseTransform.None;
    }

    public enum CaseTransform
    {
        None,
        Upper,
        Lower,
        CamelCase,
        SnakeCase
    }

    public class CompressionFilter : FilterBase
    {
        private readonly CompressionFilterConfig _config;

        public CompressionFilter(CompressionFilterConfig config) : base(config)
        {
            _config = config;
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            // Skip compression for small messages
            if (data.Length < _config.MinSizeThreshold)
            {
                return ProcessingResult.Success(data);
            }

            try
            {
                using var output = new System.IO.MemoryStream();
                
                if (_config.Algorithm == CompressionAlgorithm.Gzip)
                {
                    using var compressor = new System.IO.Compression.GZipStream(output, 
                        (System.IO.Compression.CompressionLevel)_config.Level);
                    await compressor.WriteAsync(data, 0, data.Length, cancellationToken);
                }
                else
                {
                    // Other algorithms would be implemented here
                    return ProcessingResult.Success(data);
                }

                var compressed = output.ToArray();
                context.SetProperty("CompressionRatio", (double)compressed.Length / data.Length);
                
                return ProcessingResult.Success(compressed);
            }
            catch (Exception ex)
            {
                return ProcessingResult.Failure($"Compression error: {ex.Message}");
            }
        }
    }

    public class CompressionFilterConfig : FilterConfig
    {
        public CompressionAlgorithm Algorithm { get; set; } = CompressionAlgorithm.Gzip;
        public int Level { get; set; } = 6;
        public int MinSizeThreshold { get; set; } = 100;
    }

    public enum CompressionAlgorithm
    {
        None,
        Gzip,
        Deflate,
        Brotli,
        LZ4,
        Snappy
    }

    // Test filter that simulates transient errors
    public class TransientErrorFilter : FilterBase
    {
        private int _attemptCount = 0;
        
        public int AttemptCount => _attemptCount;

        public TransientErrorFilter() : base(new FilterConfig { Name = "TransientErrorFilter" })
        {
        }

        public override async Task<ProcessingResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            _attemptCount++;
            
            // Fail first 2 attempts, succeed on 3rd
            if (_attemptCount < 3)
            {
                return ProcessingResult.Failure($"Transient error (attempt {_attemptCount})");
            }

            return ProcessingResult.Success(data);
        }
    }

    public enum LogLevel
    {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    }
}