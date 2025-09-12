using System;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp;
using GopherMcp.Filters;
using GopherMcp.Manager;
using GopherMcp.Types;
using GopherMcp.Integration;

namespace BasicUsage
{
    class Program
    {
        static async Task Main(string[] args)
        {
            Console.WriteLine("=== GopherMcp Basic Usage Example ===\n");

            try
            {
                // Create FilterManager
                var config = new FilterManagerConfig
                {
                    MaxConcurrency = 4,
                    DefaultTimeout = TimeSpan.FromSeconds(30),
                    LogLevel = McpLogLevel.Debug
                };

                using var manager = new FilterManager(config);

                // Create and process a test message
                var testMessage = new JsonRpcMessage
                {
                    JsonRpc = "2.0",
                    Id = 1,
                    Method = "test.echo",
                    Params = new { text = "Hello, MCP!" }
                };

                Console.WriteLine($"Processing message: {testMessage.Method}");

                var result = await manager.ProcessAsync(testMessage);

                if (result.IsSuccess)
                {
                    Console.WriteLine($"Success! Result: {result.Data?.Method ?? "OK"}");
                }
                else
                {
                    Console.WriteLine($"Failed: {result.ErrorMessage}");
                }

                // Display statistics
                var stats = manager.GetStatistics();
                Console.WriteLine($"\nStatistics:");
                Console.WriteLine($"  Total Bytes: {stats.TotalBytesProcessed}");

                Console.WriteLine("\n=== Example completed successfully ===");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"\n[ERROR] {ex.GetType().Name}: {ex.Message}");
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }
    }
}
