# MCP Go SDK Examples

This directory contains example implementations of MCP (Model Context Protocol) server and client using the official Go SDK.

## Prerequisites

- Go 1.21 or later
- The official MCP Go SDK

## Structure

```
examples/
├── go.mod       # Go module definition
├── server.go    # MCP server implementation
├── client.go    # MCP client implementation
└── README.md    # This file
```

## MCP Server Example

The server example demonstrates:
- Tool registration and handling (get_time, echo, calculate)
- Prompt management (greeting, system_info)  
- Resource serving (config, stats)
- Stdio transport for communication

### Running the Server

```bash
go run server.go
```

The server will start and listen on stdio for MCP protocol messages.

### Available Tools

1. **get_time** - Returns current time in specified format
   - Parameters: `format` (string) - Time format (RFC3339, Unix, or custom)

2. **echo** - Echoes back the provided message
   - Parameters: `message` (string) - Message to echo

3. **calculate** - Performs basic arithmetic operations
   - Parameters: 
     - `operation` (string) - Operation (add, subtract, multiply, divide)
     - `a` (number) - First operand
     - `b` (number) - Second operand

### Available Prompts

1. **greeting** - Generates a personalized greeting
   - Arguments: `name` (string, required)

2. **system_info** - Returns system information

### Available Resources

1. **config://server** - Server configuration (JSON)
2. **stats://requests** - Request statistics (JSON)

## MCP Client Example

The client example demonstrates:
- Connecting to an MCP server via stdio transport
- Listing and calling tools
- Retrieving and using prompts
- Reading resources
- Interactive demo mode

### Running the Client

```bash
# Run with default server (starts server.go example)
go run client.go

# Run with custom server command
go run client.go -server "node custom-server.js"

# Run specific tool
go run client.go -tool calculate -args '{"operation":"add","a":10,"b":20}'

# Run non-interactive mode (just list tools)
go run client.go -interactive=false
```

### Command Line Options

- `-server` - Server command to execute (default: runs the example server)
- `-interactive` - Run interactive demo (default: true)
- `-tool` - Call specific tool by name
- `-args` - Tool arguments as JSON (default: "{}")

## Building

To build the examples:

```bash
# Build server
go build -o mcp-server server.go

# Build client  
go build -o mcp-client client.go
```

## Protocol Communication

The examples use stdio transport for communication:
- Server reads from stdin and writes to stdout
- Client spawns server process and communicates via pipes
- Messages are exchanged using JSON-RPC 2.0 protocol

## Extending the Examples

### Adding New Tools

In the server, add to `registerTools()` and `setupHandlers()`:

```go
// Register tool definition
s.tools["my_tool"] = ToolDefinition{
    Name:        "my_tool",
    Description: "My custom tool",
    Parameters:  map[string]interface{}{...},
}

// Add handler case
case "my_tool":
    return s.handleMyTool(arguments)
```

### Adding New Resources

In the server, update resource handlers:

```go
// Add to resources list
{
    URI:         "custom://resource",
    Name:        "Custom Resource",
    Description: "My custom resource",
    MimeType:    "text/plain",
}

// Add read handler case
case "custom://resource":
    return &server.ReadResourceResult{...}, nil
```

## Dependencies

Update dependencies:

```bash
go mod tidy
go mod download
```

## Troubleshooting

1. **Connection errors**: Ensure the server command is correct and the server is accessible
2. **Protocol errors**: Check that both client and server use compatible MCP versions
3. **Tool execution errors**: Verify tool arguments match the expected schema

## References

- [MCP Specification](https://github.com/modelcontextprotocol/specification)
- [MCP Go SDK](https://github.com/modelcontextprotocol/go-sdk)
- [MCP Documentation](https://modelcontextprotocol.io)