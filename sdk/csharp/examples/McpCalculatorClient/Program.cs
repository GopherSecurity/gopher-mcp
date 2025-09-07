using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using GopherMcp.Integration;
using GopherMcp.Transport;

namespace McpCalculatorClient
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

            logger.LogInformation("=== MCP Calculator Client ===");

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

                // Create MCP client
                using var client = new McpClient(transport, TimeSpan.FromSeconds(30));

                // Handle client events
                client.NotificationReceived += (sender, e) =>
                {
                    logger.LogInformation("Notification received: {Method}", e.Notification.Method);
                };

                client.ErrorOccurred += (sender, e) =>
                {
                    logger.LogError(e.Exception, "Client error: {Context}", e.Context);
                };

                // Connect to server
                logger.LogInformation("Connecting to server at {Host}:{Port}...", transportConfig.Host, transportConfig.Port);
                await client.ConnectAsync();
                logger.LogInformation("Connected successfully");

                // Initialize client
                await InitializeClient(client, logger);

                // Discover available tools
                await DiscoverTools(client, logger);

                // Perform calculator operations
                await PerformCalculatorOperations(client, logger);

                // Interactive mode
                await RunInteractiveMode(client, logger);

                // Disconnect
                logger.LogInformation("Disconnecting from server...");
                await client.DisconnectAsync();
                logger.LogInformation("Disconnected successfully");
            }
            catch (Exception ex)
            {
                logger.LogCritical(ex, "Fatal error occurred");
                Environment.Exit(1);
            }
        }

        static async Task InitializeClient(McpClient client, ILogger logger)
        {
            logger.LogInformation("Initializing client...");

            var initParams = new
            {
                protocolVersion = "2024-11-05",
                capabilities = new
                {
                    experimental = new { }
                },
                clientInfo = new
                {
                    name = "Calculator Client",
                    version = "1.0.0"
                }
            };

            var response = await client.InvokeAsync<InitializeResponse>("initialize", initParams);

            if (response != null)
            {
                logger.LogInformation("Server info: {Name} v{Version}", 
                    response.ServerInfo?.Name ?? "Unknown",
                    response.ServerInfo?.Version ?? "Unknown");
                logger.LogInformation("Protocol version: {Version}", response.ProtocolVersion);
            }
        }

        static async Task DiscoverTools(McpClient client, ILogger logger)
        {
            logger.LogInformation("Discovering available tools...");

            var tools = await client.DiscoverToolsAsync();

            if (tools.Tools.Count > 0)
            {
                logger.LogInformation("Available tools:");
                foreach (var tool in tools.Tools)
                {
                    logger.LogInformation("  - {Name}: {Description}", tool.Name, tool.Description ?? "No description");
                }
            }
            else
            {
                logger.LogWarning("No tools available from server");
            }
        }

        static async Task PerformCalculatorOperations(McpClient client, ILogger logger)
        {
            logger.LogInformation("\n=== Performing Calculator Operations ===\n");

            // Addition
            logger.LogInformation("Testing addition: 10 + 5");
            var addResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.add",
                new { a = 10, b = 5 });
            DisplayResult(addResult, logger);

            // Subtraction
            logger.LogInformation("Testing subtraction: 20 - 8");
            var subtractResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.subtract",
                new { a = 20, b = 8 });
            DisplayResult(subtractResult, logger);

            // Multiplication
            logger.LogInformation("Testing multiplication: 7 * 6");
            var multiplyResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.multiply",
                new { a = 7, b = 6 });
            DisplayResult(multiplyResult, logger);

            // Division
            logger.LogInformation("Testing division: 100 / 4");
            var divideResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.divide",
                new { dividend = 100, divisor = 4 });
            DisplayResult(divideResult, logger);

            // Power
            logger.LogInformation("Testing power: 2^8");
            var powerResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.power",
                new { @base = 2, exponent = 8 });
            DisplayResult(powerResult, logger);

            // Square root
            logger.LogInformation("Testing square root: √144");
            var sqrtResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.sqrt",
                new { value = 144 });
            DisplayResult(sqrtResult, logger);

            // Percentage
            logger.LogInformation("Testing percentage: 15% of 200");
            var percentResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.percentage",
                new { value = 200, percent = 15 });
            DisplayResult(percentResult, logger);

            // Average
            logger.LogInformation("Testing average: [10, 20, 30, 40, 50]");
            var avgResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.average",
                new { numbers = new[] { 10.0, 20.0, 30.0, 40.0, 50.0 } });
            DisplayResult(avgResult, logger);

            // Factorial
            logger.LogInformation("Testing factorial: 6!");
            var factorialResult = await client.CallToolAsync<CalculatorResult>(
                "calculator.factorial",
                new { n = 6 });
            DisplayResult(factorialResult, logger);

            // Error handling - Division by zero
            logger.LogInformation("Testing error handling: Division by zero");
            try
            {
                await client.CallToolAsync<CalculatorResult>(
                    "calculator.divide",
                    new { dividend = 10, divisor = 0 });
            }
            catch (Exception ex)
            {
                logger.LogWarning("Expected error: {Message}", ex.Message);
            }
        }

        static void DisplayResult(CalculatorResult? result, ILogger logger)
        {
            if (result != null)
            {
                if (result.Content != null && result.Content.Length > 0)
                {
                    var content = result.Content[0];
                    logger.LogInformation("  Result: {Text}", content.Text);
                }
            }
            else
            {
                logger.LogWarning("  No result received");
            }
        }

        static async Task RunInteractiveMode(McpClient client, ILogger logger)
        {
            logger.LogInformation("\n=== Interactive Mode ===");
            logger.LogInformation("Enter calculations in format: operation arg1 arg2");
            logger.LogInformation("Examples: add 10 5, multiply 3 7, sqrt 16");
            logger.LogInformation("Type 'help' for available operations, 'quit' to exit\n");

            while (true)
            {
                Console.Write("> ");
                var input = Console.ReadLine();

                if (string.IsNullOrWhiteSpace(input))
                    continue;

                input = input.Trim().ToLower();

                if (input == "quit" || input == "exit")
                    break;

                if (input == "help")
                {
                    ShowHelp();
                    continue;
                }

                try
                {
                    await ProcessCommand(client, input, logger);
                }
                catch (Exception ex)
                {
                    logger.LogError("Error: {Message}", ex.Message);
                }
            }
        }

        static void ShowHelp()
        {
            Console.WriteLine("\nAvailable operations:");
            Console.WriteLine("  add <a> <b>         - Addition");
            Console.WriteLine("  subtract <a> <b>    - Subtraction");
            Console.WriteLine("  multiply <a> <b>    - Multiplication");
            Console.WriteLine("  divide <a> <b>      - Division");
            Console.WriteLine("  power <base> <exp>  - Power");
            Console.WriteLine("  sqrt <value>        - Square root");
            Console.WriteLine("  percentage <v> <p>  - Percentage");
            Console.WriteLine("  factorial <n>       - Factorial");
            Console.WriteLine("  average <n1> <n2>.. - Average of numbers");
            Console.WriteLine("  sum <n1> <n2>..     - Sum of numbers");
            Console.WriteLine("  min <n1> <n2>..     - Minimum value");
            Console.WriteLine("  max <n1> <n2>..     - Maximum value");
            Console.WriteLine("  abs <value>         - Absolute value");
            Console.WriteLine("  round <value> <dp>  - Round to decimal places");
            Console.WriteLine("  sin <angle>         - Sine (radians)");
            Console.WriteLine("  cos <angle>         - Cosine (radians)");
            Console.WriteLine("  tan <angle>         - Tangent (radians)");
            Console.WriteLine("  log <value> <base>  - Logarithm");
            Console.WriteLine("  ln <value>          - Natural logarithm");
            Console.WriteLine();
        }

        static async Task ProcessCommand(McpClient client, string input, ILogger logger)
        {
            var parts = input.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0)
                return;

            var operation = parts[0];
            object? arguments = null;

            switch (operation)
            {
                case "add":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: add <a> <b>");
                    arguments = new { a = double.Parse(parts[1]), b = double.Parse(parts[2]) };
                    break;

                case "subtract":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: subtract <a> <b>");
                    arguments = new { a = double.Parse(parts[1]), b = double.Parse(parts[2]) };
                    break;

                case "multiply":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: multiply <a> <b>");
                    arguments = new { a = double.Parse(parts[1]), b = double.Parse(parts[2]) };
                    break;

                case "divide":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: divide <dividend> <divisor>");
                    arguments = new { dividend = double.Parse(parts[1]), divisor = double.Parse(parts[2]) };
                    break;

                case "power":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: power <base> <exponent>");
                    arguments = new { @base = double.Parse(parts[1]), exponent = double.Parse(parts[2]) };
                    break;

                case "sqrt":
                    if (parts.Length != 2)
                        throw new ArgumentException("Usage: sqrt <value>");
                    arguments = new { value = double.Parse(parts[1]) };
                    break;

                case "percentage":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: percentage <value> <percent>");
                    arguments = new { value = double.Parse(parts[1]), percent = double.Parse(parts[2]) };
                    break;

                case "factorial":
                    if (parts.Length != 2)
                        throw new ArgumentException("Usage: factorial <n>");
                    arguments = new { n = int.Parse(parts[1]) };
                    break;

                case "average":
                case "sum":
                case "min":
                case "max":
                    if (parts.Length < 2)
                        throw new ArgumentException($"Usage: {operation} <number1> <number2> ...");
                    var numbers = new double[parts.Length - 1];
                    for (int i = 1; i < parts.Length; i++)
                    {
                        numbers[i - 1] = double.Parse(parts[i]);
                    }
                    arguments = new { numbers };
                    break;

                case "abs":
                    if (parts.Length != 2)
                        throw new ArgumentException("Usage: abs <value>");
                    arguments = new { value = double.Parse(parts[1]) };
                    break;

                case "round":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: round <value> <decimalPlaces>");
                    arguments = new { value = double.Parse(parts[1]), decimalPlaces = int.Parse(parts[2]) };
                    break;

                case "sin":
                case "cos":
                case "tan":
                    if (parts.Length != 2)
                        throw new ArgumentException($"Usage: {operation} <angleRadians>");
                    arguments = new { angleRadians = double.Parse(parts[1]) };
                    break;

                case "log":
                    if (parts.Length != 3)
                        throw new ArgumentException("Usage: log <value> <base>");
                    arguments = new { value = double.Parse(parts[1]), baseNumber = double.Parse(parts[2]) };
                    break;

                case "ln":
                    if (parts.Length != 2)
                        throw new ArgumentException("Usage: ln <value>");
                    arguments = new { value = double.Parse(parts[1]) };
                    break;

                default:
                    throw new ArgumentException($"Unknown operation: {operation}");
            }

            var result = await client.CallToolAsync<CalculatorResult>(
                $"calculator.{operation}",
                arguments);

            if (result?.Content != null && result.Content.Length > 0)
            {
                Console.WriteLine($"Result: {result.Content[0].Text}");
            }
            else
            {
                Console.WriteLine("No result received");
            }
        }
    }

    // Response classes
    public class InitializeResponse
    {
        public string ProtocolVersion { get; set; } = string.Empty;
        public ServerInfo? ServerInfo { get; set; }
        public object? Capabilities { get; set; }
    }

    public class ServerInfo
    {
        public string Name { get; set; } = string.Empty;
        public string Version { get; set; } = string.Empty;
    }

    public class CalculatorResult
    {
        public ContentItem[] Content { get; set; } = Array.Empty<ContentItem>();
    }

    public class ContentItem
    {
        public string Type { get; set; } = string.Empty;
        public string Text { get; set; } = string.Empty;
    }
}