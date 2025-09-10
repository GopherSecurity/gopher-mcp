using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Server;
using System.ComponentModel;

var builder = Host.CreateApplicationBuilder(args);

// Configure MCP server with stdio transport
builder.Services
    .AddMcpServer()
    .WithStdioServerTransport()
    .WithToolsFromAssembly();

// Build and run the host
var host = builder.Build();

Console.Error.WriteLine("Simple Calculator Server started. Press Ctrl+C to exit.");
await host.RunAsync();

// Define calculator tools using attributes
[McpServerToolType]
public static class CalculatorTools
{
    [McpServerTool]
    [Description("Add two numbers")]
    public static double Add(double a, double b)
    {
        return a + b;
    }

    [McpServerTool]
    [Description("Subtract two numbers")]
    public static double Subtract(double a, double b)
    {
        return a - b;
    }

    [McpServerTool]
    [Description("Multiply two numbers")]
    public static double Multiply(double a, double b)
    {
        return a * b;
    }

    [McpServerTool]
    [Description("Divide two numbers")]
    public static double Divide(double a, double b)
    {
        if (b == 0)
            throw new ArgumentException("Cannot divide by zero");
        return a / b;
    }
}