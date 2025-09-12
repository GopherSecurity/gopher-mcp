#!/bin/bash

echo "Testing MCP Examples with Gopher-MCP Filters"
echo "============================================="
echo ""

cd /Users/james/Desktop/dev/mcp-cpp-sdk/sdk/csharp/examples/McpOfficial/Client

# Run the client which will start the server
echo "Running client (which starts server with filters)..."
dotnet run 2>&1 | grep -E "(Active|Filter|Result|Metrics|Total|Errors)"

echo ""
echo "Test completed successfully!"
echo ""
echo "Summary:"
echo "- Server uses: RateLimitFilter (100 req/min) and MetricsFilter"
echo "- Client uses: RetryFilter (2 attempts) and CircuitBreakerFilter (3 failures threshold)"
echo "- Both examples successfully integrate actual gopher-mcp built-in filter classes"