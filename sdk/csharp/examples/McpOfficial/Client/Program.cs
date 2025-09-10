using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;
using System.Diagnostics;
using GopherMcp.Filters;
using GopherMcp.Filters.BuiltinFilters;
using GopherMcp.Types;
using GopherMcp.Utils;

// Create client transport to connect to our server
var clientTransport = new StdioClientTransport(new StdioClientTransportOptions
{
    Name = "SimpleCalculatorClient",
    Command = "dotnet",
    Arguments = ["run", "--project", "../Server/SimpleServer.csproj"]
});

// Create and connect the MCP client
Console.WriteLine("Creating MCP client and connecting to server...");
await using var client = await McpClientFactory.CreateAsync(clientTransport);

Console.WriteLine("Connected to server!");

// Create and initialize gopher-mcp filter chain for client
var clientFilterChain = new FilterChain(new ChainConfig 
{ 
    Name = "ClientFilterChain",
    ExecutionMode = ChainExecutionMode.Sequential 
});

// Add 2 gopher-mcp built-in filters
var retryConfig = new RetryConfig
{
    Name = "ClientRetry",
    Enabled = true,
    MaxAttempts = 2,
    InitialDelay = TimeSpan.FromMilliseconds(500)
};
clientFilterChain.AddFilter(new RetryFilter(retryConfig));

var circuitBreakerConfig = new CircuitBreakerConfig
{
    Name = "ClientCircuitBreaker",
    Enabled = true,
    FailureThreshold = 3,
    TimeoutDuration = TimeSpan.FromSeconds(10)
};
clientFilterChain.AddFilter(new CircuitBreakerFilter(circuitBreakerConfig));

// Initialize the filter chain
await clientFilterChain.InitializeAsync();

// Create a client wrapper using the filter chain
var filteredClient = new FilteredMcpClient(client, clientFilterChain);

Console.WriteLine("\nActive client filters:");
foreach (var filter in clientFilterChain.GetFilters())
{
    Console.WriteLine($"  - {filter.Name}");
}

// List available tools
Console.WriteLine("\nAvailable tools:");
var tools = await client.ListToolsAsync();
foreach (var tool in tools)
{
    Console.WriteLine($"  - {tool.Name}: {tool.Description}");
}

// Test the add tool with gopher-mcp filters
Console.WriteLine("\n=== Testing with Gopher-MCP Filters ===");

// Test various addition operations
await filteredClient.CallToolAsync("add", 5, 3);
await filteredClient.CallToolAsync("add", 10, 20);
await filteredClient.CallToolAsync("add", -5, 15);
await filteredClient.CallToolAsync("add", 100, 200);

// Display filter chain metrics
var chainStats = clientFilterChain.GetStatistics();
Console.WriteLine("\n=== Client-Side Filter Chain Metrics ===");
Console.WriteLine($"Total Packets Processed: {chainStats.TotalPacketsProcessed}");
Console.WriteLine($"Total Errors: {chainStats.TotalErrors}");
if (chainStats.AverageProcessingTimeUs > 0)
{
    Console.WriteLine($"Average Processing Time: {chainStats.AverageProcessingTimeUs:F0}μs");
}

Console.WriteLine("\nClient test completed!");

// Client wrapper using actual gopher-mcp filter chain
public class FilteredMcpClient
{
    private readonly IMcpClient _client;
    private readonly FilterChain _filterChain;
    private int _requestId = 0;

    public FilteredMcpClient(IMcpClient client, FilterChain filterChain)
    {
        _client = client;
        _filterChain = filterChain;
    }

    public async Task CallToolAsync(string toolName, params object[] parameters)
    {
        var requestId = ++_requestId;
        Console.WriteLine($"[Request #{requestId}] Calling {toolName} with params: {string.Join(", ", parameters)}");
        
        // Create processing context
        var context = new ProcessingContext();
        context.SetProperty("ToolName", toolName);
        context.SetProperty("Parameters", parameters);
        context.SetProperty("RequestId", requestId);
        
        // Create a byte array representing the request (filters work with byte arrays)
        var requestData = System.Text.Encoding.UTF8.GetBytes(
            $"{{\"method\":\"{toolName}\",\"params\":{System.Text.Json.JsonSerializer.Serialize(parameters)},\"id\":{requestId}}}"
        );
        
        try
        {
            // Process through filter chain
            var filterResult = await _filterChain.ProcessAsync(requestData, context);
            
            // Check filter processing result
            if (filterResult.IsError)
            {
                Console.WriteLine($"[FilterChain] Request failed: {filterResult.ErrorMessage}");
                return;
            }
            
            // Build parameters dictionary for actual call
            var paramDict = new Dictionary<string, object?>();
            if (parameters.Length >= 1) paramDict["a"] = parameters[0];
            if (parameters.Length >= 2) paramDict["b"] = parameters[1];
            
            // Call the tool through MCP client
            var result = await _client.CallToolAsync(toolName, paramDict);
            
            // Process result
            if (result.Content != null && result.Content.Count > 0)
            {
                if (result.Content[0] is TextContentBlock textBlock)
                {
                    Console.WriteLine($"[Request #{requestId}] Result: {textBlock.Text}");
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[Request #{requestId}] Error: {ex.Message}");
        }
    }
}