# MCP Official SDK with Gopher-MCP Filters

Examples demonstrating integration of the official [MCP C# SDK](https://github.com/modelcontextprotocol/csharp-sdk) with the **Gopher-MCP filter library** for advanced request/response processing.

## Overview

These examples show how to use the actual Gopher-MCP filter classes to add powerful middleware capabilities to MCP applications:

### Gopher-MCP Filters Used

#### Server-Side Filters
- **LoggingFilter**: Logs all incoming requests and responses
- **RateLimitFilter**: Limits requests to 100 per minute
- **RetryFilter**: Automatically retries failed operations up to 3 times
- **ValidationFilter**: Validates request parameters
- **MetricsFilter**: Collects performance metrics
- **CachingFilter**: Caches results for 10 seconds

#### Client-Side Filters  
- **LoggingFilter**: Logs outgoing requests
- **ValidationFilter**: Pre-validates parameters
- **RetryFilter**: Retries failed requests with backoff
- **CircuitBreakerFilter**: Prevents cascading failures
- **TimeoutFilter**: Enforces 5-second timeout
- **MetricsFilter**: Tracks client-side statistics

## Project Structure

```
McpOfficial/
├── Server/
│   ├── SimpleServer.csproj (references GopherMcp.csproj)
│   └── Program.cs (uses FilterChain with built-in filters)
└── Client/
    ├── SimpleClient.csproj (references GopherMcp.csproj)
    └── Program.cs (uses FilterChain with built-in filters)
```

## Quick Start

1. Build and run the server:
```bash
cd Server
dotnet build
dotnet run
```

2. In another terminal, run the client:
```bash
cd Client
dotnet build
dotnet run
```

## Implementation Details

### Server-Side Filter Chain
```csharp
// Create filter chain with gopher-mcp filters
var filterChain = new FilterChain();
filterChain.Add(new LoggingFilter(LogLevel.Information));
filterChain.Add(new RateLimitFilter(100, TimeSpan.FromMinutes(1)));
filterChain.Add(new RetryFilter(3, TimeSpan.FromMilliseconds(100)));
filterChain.Add(new ValidationFilter());
filterChain.Add(new MetricsFilter());
filterChain.Add(new CachingFilter(TimeSpan.FromSeconds(10)));
```

### Client-Side Filter Chain
```csharp
// Create client filter chain
var clientFilterChain = new FilterChain();
clientFilterChain.Add(new LoggingFilter(LogLevel.Information));
clientFilterChain.Add(new ValidationFilter());
clientFilterChain.Add(new RetryFilter(2, TimeSpan.FromMilliseconds(500)));
clientFilterChain.Add(new CircuitBreakerFilter(5, TimeSpan.FromSeconds(30)));
clientFilterChain.Add(new TimeoutFilter(TimeSpan.FromSeconds(5)));
clientFilterChain.Add(new MetricsFilter());
```

### Executing Through Filters
```csharp
// Execute operation through filter chain
await filterChain.ExecuteAsync(async (context) =>
{
    // Your actual operation
    var result = await DoWork();
    context.Set("result", result);
    return context;
}, filterContext);
```

## Features Demonstrated

1. **Request Logging**: All requests are logged with timing information
2. **Rate Limiting**: Server limits clients to 100 requests per minute
3. **Automatic Retry**: Failed operations are retried automatically
4. **Circuit Breaking**: Client stops calling failing services
5. **Response Caching**: Server caches repeated calculations
6. **Performance Metrics**: Both client and server track metrics
7. **Timeout Protection**: Client enforces timeouts on all calls
8. **Parameter Validation**: Both sides validate before processing

## Expected Output

```
=== Testing with Gopher-MCP Filters ===
Active filters: Logging, Validation, Retry, CircuitBreaker, Timeout, Metrics

[LoggingFilter] Request: add(5, 3)
[Client] Calling add with params: 5, 3
[ValidationFilter] Validated parameters
[MetricsFilter] Request completed in 15ms
[Client] Result: 8

=== Testing Caching Filter ===
[Client] Calling add with params: 5, 3
[CachingFilter] Cache hit for add(5, 3)
[Client] Result: 8

=== Client-Side Metrics (Gopher-MCP) ===
Total Requests: 8
Successful: 6
Failed: 2
Average Response Time: 12.5ms
Error Rate: 25%
```

## Benefits of Using Gopher-MCP Filters

1. **Production-Ready**: Battle-tested filter implementations
2. **Composable**: Mix and match filters as needed
3. **Configurable**: Each filter has configuration options
4. **Observable**: Built-in metrics and logging
5. **Resilient**: Circuit breakers, retries, and timeouts
6. **Performant**: Caching and rate limiting
7. **Extensible**: Easy to create custom filters

## Custom Filter Example

```csharp
public class CustomLoggingFilter : IFilter
{
    public string Name => "CustomLogging";
    
    public async Task<FilterContext> ExecuteAsync(
        FilterDelegate next, 
        FilterContext context)
    {
        // Pre-processing
        Console.WriteLine("Before operation");
        
        // Execute next filter/operation
        var result = await next(context);
        
        // Post-processing
        Console.WriteLine("After operation");
        
        return result;
    }
}
```

## Requirements

- .NET 8.0 or later
- ModelContextProtocol NuGet package (0.3.0-preview.4)
- GopherMcp library (local reference)

## See Also

- [Gopher-MCP Filters Documentation](../../src/Filters/README.md)
- [Built-in Filters Reference](../../src/Filters/BuiltinFilters/)
- [MCP C# SDK Documentation](https://github.com/modelcontextprotocol/csharp-sdk)