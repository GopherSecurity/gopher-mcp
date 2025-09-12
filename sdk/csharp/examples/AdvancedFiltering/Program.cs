using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp;
using GopherMcp.Filters;
using GopherMcp.Manager;
using GopherMcp.Types;
using GopherMcp.Integration;

namespace AdvancedFiltering
{
    class Program
    {
        static async Task Main(string[] args)
        {
            Console.WriteLine("=== Advanced Filtering Example ===\n");

            try
            {
                // Create FilterManager with configuration
                var config = new FilterManagerConfig
                {
                    MaxConcurrency = 8,
                    DefaultTimeout = TimeSpan.FromSeconds(60),
                    LogLevel = McpLogLevel.Info
                };

                using var manager = new FilterManager(config);

                // Process multiple messages
                await ProcessMultipleMessages(manager);

                Console.WriteLine("\n=== Advanced Filtering Example Completed ===");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"\n[ERROR] {ex.GetType().Name}: {ex.Message}");
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }

        static async Task ProcessMultipleMessages(FilterManager manager)
        {
            Console.WriteLine("Processing multiple messages...\n");

            var messages = new[]
            {
                new JsonRpcMessage { JsonRpc = "2.0", Id = 1, Method = "test.ping" },
                new JsonRpcMessage { JsonRpc = "2.0", Id = 2, Method = "test.echo", Params = new { data = "Echo this" } },
                new JsonRpcMessage { JsonRpc = "2.0", Id = 3, Method = "test.info" }
            };

            var sw = Stopwatch.StartNew();
            var tasks = messages.Select(msg => ProcessMessageAsync(manager, msg)).ToArray();
            var results = await Task.WhenAll(tasks);
            sw.Stop();

            var successCount = results.Count(r => r);
            Console.WriteLine($"\nCompleted in {sw.ElapsedMilliseconds}ms");
            Console.WriteLine($"Success rate: {successCount}/{messages.Length}");
        }

        static async Task<bool> ProcessMessageAsync(FilterManager manager, JsonRpcMessage message)
        {
            try
            {
                Console.WriteLine($"Processing: {message.Method}");
                var result = await manager.ProcessAsync(message);

                if (result.IsSuccess)
                {
                    Console.WriteLine($"  ✓ Success: {message.Method}");
                    return true;
                }
                else
                {
                    Console.WriteLine($"  ✗ Failed: {message.Method} - {result.ErrorMessage}");
                    return false;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Error: {message.Method} - {ex.Message}");
                return false;
            }
        }
    }
}
