using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Server;
using System.ComponentModel;
using System.Diagnostics;
using GopherMcp.Filters;
using GopherMcp.Filters.BuiltinFilters;
using GopherMcp.Types;
using GopherMcp.Utils;

var builder = Host.CreateApplicationBuilder(args);

// Add logging
builder.Logging.SetMinimumLevel(LogLevel.Information);

// Configure MCP server with stdio transport
builder.Services
    .AddMcpServer()
    .WithStdioServerTransport()
    .WithToolsFromAssembly();

// Create and initialize gopher-mcp filter chain
var filterChain = new FilterChain(new ChainConfig 
{ 
    Name = "ServerFilterChain",
    ExecutionMode = ChainExecutionMode.Sequential 
});

// Add 2 gopher-mcp built-in filters
var rateLimitConfig = new RateLimitConfig
{
    Name = "ServerRateLimit",
    Enabled = true,
    RequestsPerMinute = 100,
    WindowSizeSeconds = 60
};
filterChain.AddFilter(new RateLimitFilter(rateLimitConfig));

var metricsConfig = new MetricsConfig
{
    Name = "ServerMetrics",
    Enabled = true,
    EnableHistograms = true
};
filterChain.AddFilter(new MetricsFilter(metricsConfig));

// Initialize the filter chain
await filterChain.InitializeAsync();
CalculatorTools.SetFilterChain(filterChain);

// Build and run the host
var host = builder.Build();

Console.Error.WriteLine("Calculator Server with Gopher-MCP Filters");
Console.Error.WriteLine("Active filters:");
foreach (var filter in filterChain.GetFilters())
{
    Console.Error.WriteLine($"  - {filter.Name}");
}
await host.RunAsync();

// Define calculator tool using gopher-mcp filters
[McpServerToolType]
public static class CalculatorTools
{
    private static FilterChain _filterChain;
    private static int _requestId = 0;
    
    public static void SetFilterChain(FilterChain chain)
    {
        _filterChain = chain;
    }
    
    private static async Task<T> ExecuteWithFiltersAsync<T>(string operation, Func<T> action, params object[] parameters)
    {
        var requestId = ++_requestId;
        
        // Create processing context
        var context = new ProcessingContext();
        context.SetProperty("Operation", operation);
        context.SetProperty("Parameters", parameters);
        context.SetProperty("RequestId", requestId);
        
        // Create a byte array representing the operation (filters work with byte arrays)
        var operationData = System.Text.Encoding.UTF8.GetBytes(
            $"{{\"method\":\"{operation}\",\"params\":{System.Text.Json.JsonSerializer.Serialize(parameters)},\"id\":{requestId}}}"
        );
        
        try
        {
            // Process through filter chain if available
            if (_filterChain != null && _filterChain.IsInitialized)
            {
                var result = await _filterChain.ProcessAsync(operationData, context);
                
                // Check if result was cached or processed
                if (context.Properties.TryGetValue("CachedResult", out var cached))
                {
                    Console.Error.WriteLine($"[FilterChain] Cache hit for {operation}");
                    return (T)cached;
                }
            }
            
            // Execute the actual operation
            var operationResult = action();
            
            // Store result in context for filters to cache
            context.SetProperty("Result", operationResult);
            
            // Display metrics from filter chain
            if (_filterChain != null)
            {
                var stats = _filterChain.GetStatistics();
                if (stats.TotalPacketsProcessed % 10 == 0) // Log every 10 requests
                {
                    Console.Error.WriteLine($"[FilterChain Metrics] Total: {stats.TotalPacketsProcessed}, " +
                        $"Errors: {stats.TotalErrors}, Avg Time: {stats.AverageProcessingTimeUs:F0}μs");
                }
            }
            
            return operationResult;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[FilterChain] Error in {operation}: {ex.Message}");
            throw;
        }
    }
    
    private static T ExecuteWithFilters<T>(string operation, Func<T> action, params object[] parameters)
    {
        // Synchronous wrapper for async filter processing
        return Task.Run(async () => await ExecuteWithFiltersAsync(operation, action, parameters)).Result;
    }
    
    [McpServerTool]
    [Description("Add two numbers")]
    public static double add(double a, double b)
    {
        return ExecuteWithFilters("add", () => a + b, a, b);
    }
}