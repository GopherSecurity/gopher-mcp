using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;

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

// List available tools
Console.WriteLine("\nAvailable tools:");
var tools = await client.ListToolsAsync();
foreach (var tool in tools)
{
    Console.WriteLine($"  - {tool.Name}: {tool.Description}");
}

// Call the add tool
Console.WriteLine("\nCalling 'add' tool with a=5, b=3:");
var addResult = await client.CallToolAsync(
    "add",
    new Dictionary<string, object?> { ["a"] = 5, ["b"] = 3 }
);
if (addResult.Content != null && addResult.Content.Count > 0)
{
    if (addResult.Content[0] is TextContentBlock textBlock)
    {
        Console.WriteLine($"  Result: {textBlock.Text}");
    }
}

// Call the subtract tool
Console.WriteLine("\nCalling 'subtract' tool with a=10, b=4:");
var subtractResult = await client.CallToolAsync(
    "subtract",
    new Dictionary<string, object?> { ["a"] = 10, ["b"] = 4 }
);
if (subtractResult.Content != null && subtractResult.Content.Count > 0)
{
    if (subtractResult.Content[0] is TextContentBlock textBlock)
    {
        Console.WriteLine($"  Result: {textBlock.Text}");
    }
}

// Call the multiply tool
Console.WriteLine("\nCalling 'multiply' tool with a=6, b=7:");
var multiplyResult = await client.CallToolAsync(
    "multiply",
    new Dictionary<string, object?> { ["a"] = 6, ["b"] = 7 }
);
if (multiplyResult.Content != null && multiplyResult.Content.Count > 0)
{
    if (multiplyResult.Content[0] is TextContentBlock textBlock)
    {
        Console.WriteLine($"  Result: {textBlock.Text}");
    }
}

// Call the divide tool
Console.WriteLine("\nCalling 'divide' tool with a=20, b=4:");
var divideResult = await client.CallToolAsync(
    "divide",
    new Dictionary<string, object?> { ["a"] = 20, ["b"] = 4 }
);
if (divideResult.Content != null && divideResult.Content.Count > 0)
{
    if (divideResult.Content[0] is TextContentBlock textBlock)
    {
        Console.WriteLine($"  Result: {textBlock.Text}");
    }
}

Console.WriteLine("\nClient test completed successfully!");
Console.WriteLine("Note: The server will continue running. Press Ctrl+C in the server window to stop it.");