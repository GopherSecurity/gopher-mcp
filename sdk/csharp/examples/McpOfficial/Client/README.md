# Simple MCP Client Example

A minimal MCP client implementation using the official C# SDK that connects to the simple server and calls its calculator tools.

## Features

- Connects to the simple server via stdio
- Lists available tools
- Calls both `add` and `subtract` tools
- Clean and minimal implementation

## Prerequisites

- .NET 8.0 or later

## Running the Client

```bash
cd sdk/csharp/examples/McpOfficial/Client
dotnet run
```

The client will automatically start the server process and connect to it.

## Expected Output

```
Connecting to server...
Connected to: SimpleCalculator v1.0.0

Available tools:
  - add: Add two numbers
  - subtract: Subtract two numbers

Calling 'add' tool with a=5, b=3:
  Result: The sum of 5 and 3 is 8

Calling 'subtract' tool with a=10, b=4:
  Result: The difference of 10 and 4 is 6

Disconnecting...
Disconnected.
```