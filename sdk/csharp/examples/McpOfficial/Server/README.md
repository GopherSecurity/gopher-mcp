# Simple MCP Server Example

A minimal MCP server implementation using the official C# SDK that provides basic calculator functionality.

## Features

- Two simple tools: `add` and `subtract`
- Stdio transport for communication
- Clean and minimal implementation

## Prerequisites

- .NET 8.0 or later

## Running the Server

```bash
cd sdk/csharp/examples/McpOfficial/Server
dotnet run
```

The server will start and wait for client connections via stdio.

## Available Tools

- **add**: Adds two numbers (parameters: a, b)
- **subtract**: Subtracts two numbers (parameters: a, b)

## Testing

You can test the server using the simple client example:

```bash
cd ../Client
dotnet run
```