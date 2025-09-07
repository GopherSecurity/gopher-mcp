using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using GopherMcp.Integration;
using GopherMcp.Transport;

namespace McpCalculatorServer
{
    class Program
    {
        static async Task Main(string[] args)
        {
            // Configure logging
            using var loggerFactory = LoggerFactory.Create(builder =>
            {
                builder
                    .AddConsole()
                    .SetMinimumLevel(LogLevel.Information);
            });
            var logger = loggerFactory.CreateLogger<Program>();

            logger.LogInformation("=== MCP Calculator Server ===");
            logger.LogInformation("Starting server...");

            try
            {
                // Configure transport
                var transportConfig = new TransportConfig
                {
                    Protocol = TransportProtocol.Tcp,
                    Host = "localhost",
                    Port = 9000,
                    EnableKeepAlive = true,
                    KeepAliveInterval = TimeSpan.FromSeconds(30),
                    MaxMessageSize = 4 * 1024 * 1024, // 4MB
                    ConnectTimeout = TimeSpan.FromSeconds(30),
                    SendTimeout = TimeSpan.FromSeconds(30),
                    ReceiveTimeout = TimeSpan.FromSeconds(30)
                };

                // Create transport
                using var transport = new GopherTransport(transportConfig);

                // Create MCP server
                using var server = new McpServer(transport)
                {
                    Info = new ServerInfo
                    {
                        Name = "Calculator Server",
                        Version = "1.0.0"
                    }
                };

                // Register calculator tools
                RegisterCalculatorTools(server, logger);

                // Handle server events
                server.ErrorOccurred += (sender, e) =>
                {
                    logger.LogError(e.Exception, "Server error: {Context}", e.Context);
                };

                // Setup graceful shutdown
                var cts = new CancellationTokenSource();
                Console.CancelKeyPress += (sender, e) =>
                {
                    e.Cancel = true;
                    logger.LogInformation("Shutdown signal received");
                    cts.Cancel();
                };

                // Start server
                await server.StartAsync(cts.Token);
                logger.LogInformation("Server started on {Host}:{Port}", transportConfig.Host, transportConfig.Port);
                logger.LogInformation("Press Ctrl+C to stop the server");

                // Keep server running
                try
                {
                    await Task.Delay(Timeout.Infinite, cts.Token);
                }
                catch (OperationCanceledException)
                {
                    // Expected when cancellation is requested
                }

                // Graceful shutdown
                logger.LogInformation("Shutting down server...");
                await server.StopAsync();
                logger.LogInformation("Server stopped successfully");
            }
            catch (Exception ex)
            {
                logger.LogCritical(ex, "Fatal error occurred");
                Environment.Exit(1);
            }
        }

        static void RegisterCalculatorTools(McpServer server, ILogger logger)
        {
            logger.LogInformation("Registering calculator tools...");

            // Add tool
            server.RegisterTool<AddParameters, double>("calculator.add", 
                "Adds two numbers together",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    logger.LogDebug("Add: {A} + {B}", parameters.A, parameters.B);
                    var result = parameters.A + parameters.B;
                    return await Task.FromResult(result);
                });

            // Subtract tool
            server.RegisterTool<SubtractParameters, double>("calculator.subtract",
                "Subtracts second number from first",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    logger.LogDebug("Subtract: {A} - {B}", parameters.A, parameters.B);
                    var result = parameters.A - parameters.B;
                    return await Task.FromResult(result);
                });

            // Multiply tool
            server.RegisterTool<MultiplyParameters, double>("calculator.multiply",
                "Multiplies two numbers",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    logger.LogDebug("Multiply: {A} * {B}", parameters.A, parameters.B);
                    var result = parameters.A * parameters.B;
                    return await Task.FromResult(result);
                });

            // Divide tool
            server.RegisterTool<DivideParameters, double>("calculator.divide",
                "Divides first number by second",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    if (Math.Abs(parameters.B) < double.Epsilon)
                    {
                        throw new DivideByZeroException("Cannot divide by zero");
                    }

                    logger.LogDebug("Divide: {A} / {B}", parameters.A, parameters.B);
                    var result = parameters.A / parameters.B;
                    return await Task.FromResult(result);
                });

            // Power tool
            server.RegisterTool<PowerParameters, double>("calculator.power",
                "Raises first number to the power of second",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    logger.LogDebug("Power: {Base} ^ {Exponent}", parameters.Base, parameters.Exponent);
                    var result = Math.Pow(parameters.Base, parameters.Exponent);
                    return await Task.FromResult(result);
                });

            // Square root tool
            server.RegisterTool<SquareRootParameters, double>("calculator.sqrt",
                "Calculates square root of a number",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    if (parameters.Value < 0)
                    {
                        throw new ArgumentException("Cannot calculate square root of negative number");
                    }

                    logger.LogDebug("Square root: √{Value}", parameters.Value);
                    var result = Math.Sqrt(parameters.Value);
                    return await Task.FromResult(result);
                });

            // Percentage tool
            server.RegisterTool<PercentageParameters, double>("calculator.percentage",
                "Calculates percentage of a number",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    logger.LogDebug("Percentage: {Percent}% of {Value}", parameters.Percent, parameters.Value);
                    var result = (parameters.Value * parameters.Percent) / 100.0;
                    return await Task.FromResult(result);
                });

            // Average tool
            server.RegisterTool<AverageParameters, double>("calculator.average",
                "Calculates average of numbers",
                async (parameters) =>
                {
                    if (parameters?.Numbers == null || parameters.Numbers.Length == 0)
                    {
                        throw new ArgumentException("At least one number is required");
                    }

                    logger.LogDebug("Average of {Count} numbers", parameters.Numbers.Length);
                    var sum = 0.0;
                    foreach (var num in parameters.Numbers)
                    {
                        sum += num;
                    }
                    var result = sum / parameters.Numbers.Length;
                    return await Task.FromResult(result);
                });

            // Factorial tool
            server.RegisterTool<FactorialParameters, long>("calculator.factorial",
                "Calculates factorial of a number",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    if (parameters.N < 0)
                    {
                        throw new ArgumentException("Factorial is not defined for negative numbers");
                    }

                    if (parameters.N > 20)
                    {
                        throw new ArgumentException("Number too large for factorial calculation");
                    }

                    logger.LogDebug("Factorial: {N}!", parameters.N);
                    long result = 1;
                    for (int i = 2; i <= parameters.N; i++)
                    {
                        result *= i;
                    }
                    return await Task.FromResult(result);
                });

            // Complex calculation tool
            server.RegisterTool<ComplexCalculationParameters, ComplexCalculationResult>("calculator.complex",
                "Performs complex calculation with multiple operations",
                async (parameters) =>
                {
                    if (parameters == null)
                    {
                        throw new ArgumentNullException(nameof(parameters));
                    }

                    logger.LogDebug("Complex calculation: {Expression}", parameters.Expression);

                    // This is a simplified expression evaluator
                    // In production, use a proper expression parser
                    var result = new ComplexCalculationResult
                    {
                        Expression = parameters.Expression,
                        Result = 0, // Would evaluate expression here
                        Steps = new[]
                        {
                            "Parse expression",
                            "Evaluate operations",
                            "Return result"
                        },
                        ExecutionTime = DateTime.UtcNow
                    };

                    // Simulate some processing time
                    await Task.Delay(100);

                    return result;
                });

            logger.LogInformation("Calculator tools registered successfully");
        }
    }

    // Parameter classes for calculator tools
    public class AddParameters
    {
        public double A { get; set; }
        public double B { get; set; }
    }

    public class SubtractParameters
    {
        public double A { get; set; }
        public double B { get; set; }
    }

    public class MultiplyParameters
    {
        public double A { get; set; }
        public double B { get; set; }
    }

    public class DivideParameters
    {
        public double A { get; set; }
        public double B { get; set; }
    }

    public class PowerParameters
    {
        public double Base { get; set; }
        public double Exponent { get; set; }
    }

    public class SquareRootParameters
    {
        public double Value { get; set; }
    }

    public class PercentageParameters
    {
        public double Value { get; set; }
        public double Percent { get; set; }
    }

    public class AverageParameters
    {
        public double[] Numbers { get; set; } = Array.Empty<double>();
    }

    public class FactorialParameters
    {
        public int N { get; set; }
    }

    public class ComplexCalculationParameters
    {
        public string Expression { get; set; } = string.Empty;
        public Dictionary<string, double>? Variables { get; set; }
    }

    public class ComplexCalculationResult
    {
        public string Expression { get; set; } = string.Empty;
        public double Result { get; set; }
        public string[] Steps { get; set; } = Array.Empty<string>();
        public DateTime ExecutionTime { get; set; }
    }
}