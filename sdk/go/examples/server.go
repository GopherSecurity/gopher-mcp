package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// Tool argument types
type GetTimeArgs struct {
	Format string `json:"format,omitempty" jsonschema:"Time format (e.g. RFC3339 or Unix). Default: RFC3339"`
}

type EchoArgs struct {
	Message string `json:"message" jsonschema:"Message to echo"`
}

type CalculateArgs struct {
	Operation string  `json:"operation" jsonschema:"Operation to perform (add, subtract, multiply or divide)"`
	A         float64 `json:"a" jsonschema:"First operand"`
	B         float64 `json:"b" jsonschema:"Second operand"`
}

func main() {
	// Set up logging
	log.SetPrefix("[MCP Server] ")
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)

	// Create server implementation
	impl := &mcp.Implementation{
		Name:    "example-mcp-server",
		Version: "1.0.0",
	}

	// Create server with options
	server := mcp.NewServer(impl, nil)

	// Add tools
	registerTools(server)

	// Add prompts
	registerPrompts(server)

	// Add resources
	registerResources(server)

	// Set up signal handling
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	go func() {
		<-sigChan
		log.Println("Received interrupt signal, shutting down...")
		cancel()
	}()

	// Start server on stdio transport
	log.Println("Starting MCP server on stdio...")
	transport := &mcp.StdioTransport{}
	
	if err := server.Run(ctx, transport); err != nil {
		log.Printf("Server error: %v", err)
	}

	log.Println("Server stopped.")
}

func registerTools(server *mcp.Server) {
	// Register get_time tool
	mcp.AddTool(server, &mcp.Tool{
		Name:        "get_time",
		Description: "Get the current time",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args GetTimeArgs) (*mcp.CallToolResult, any, error) {
		format := args.Format
		if format == "" {
			format = "RFC3339"
		}

		now := time.Now()
		var result string
		switch format {
		case "Unix":
			result = fmt.Sprintf("%d", now.Unix())
		case "RFC3339":
			result = now.Format(time.RFC3339)
		default:
			result = now.Format(format)
		}

		return &mcp.CallToolResult{
			Content: []mcp.Content{
				&mcp.TextContent{Text: result},
			},
		}, nil, nil
	})

	// Register echo tool
	mcp.AddTool(server, &mcp.Tool{
		Name:        "echo",
		Description: "Echo back the provided message",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args EchoArgs) (*mcp.CallToolResult, any, error) {
		return &mcp.CallToolResult{
			Content: []mcp.Content{
				&mcp.TextContent{Text: args.Message},
			},
		}, nil, nil
	})

	// Register calculate tool
	mcp.AddTool(server, &mcp.Tool{
		Name:        "calculate",
		Description: "Perform basic calculations",
	}, func(ctx context.Context, req *mcp.CallToolRequest, args CalculateArgs) (*mcp.CallToolResult, any, error) {
		var result float64
		switch args.Operation {
		case "add":
			result = args.A + args.B
		case "subtract":
			result = args.A - args.B
		case "multiply":
			result = args.A * args.B
		case "divide":
			if args.B == 0 {
				return nil, nil, fmt.Errorf("division by zero")
			}
			result = args.A / args.B
		default:
			return nil, nil, fmt.Errorf("unknown operation: %s", args.Operation)
		}

		return &mcp.CallToolResult{
			Content: []mcp.Content{
				&mcp.TextContent{Text: fmt.Sprintf("%f", result)},
			},
		}, nil, nil
	})
}

func registerPrompts(server *mcp.Server) {
	// Register greeting prompt
	server.AddPrompt(&mcp.Prompt{
		Name:        "greeting",
		Description: "Generate a greeting message",
		Arguments: []*mcp.PromptArgument{
			{
				Name:        "name",
				Description: "Name to greet",
				Required:    true,
			},
		},
	}, func(ctx context.Context, req *mcp.GetPromptRequest) (*mcp.GetPromptResult, error) {
		userName := "User"
		if req.Params.Arguments != nil {
			if name, ok := req.Params.Arguments["name"]; ok && name != "" {
				userName = name
			}
		}
		
		return &mcp.GetPromptResult{
			Messages: []*mcp.PromptMessage{
				{
					Role: "user",
					Content: &mcp.TextContent{
						Text: fmt.Sprintf("Hello, %s! Welcome to the MCP server example.", userName),
					},
				},
			},
		}, nil
	})

	// Register system_info prompt
	server.AddPrompt(&mcp.Prompt{
		Name:        "system_info",
		Description: "Get system information",
	}, func(ctx context.Context, req *mcp.GetPromptRequest) (*mcp.GetPromptResult, error) {
		return &mcp.GetPromptResult{
			Messages: []*mcp.PromptMessage{
				{
					Role: "user",
					Content: &mcp.TextContent{
						Text: fmt.Sprintf(
							"System Information:\nServer: example-mcp-server v1.0.0\nTime: %s\nTools Available: 3",
							time.Now().Format(time.RFC3339),
						),
					},
				},
			},
		}, nil
	})
}

func registerResources(server *mcp.Server) {
	// Register config resource
	server.AddResource(&mcp.Resource{
		URI:         "config://server",
		Name:        "Server Configuration",
		Description: "Current server configuration",
		MIMEType:    "application/json",
	}, func(ctx context.Context, req *mcp.ReadResourceRequest) (*mcp.ReadResourceResult, error) {
		config := fmt.Sprintf(`{
  "name": "example-mcp-server",
  "version": "1.0.0",
  "tools": 3
}`)
		return &mcp.ReadResourceResult{
			Contents: []*mcp.ResourceContents{
				{
					URI:      req.Params.URI,
					MIMEType: "application/json",
					Text:     config,
				},
			},
		}, nil
	})

	// Register stats resource
	server.AddResource(&mcp.Resource{
		URI:         "stats://requests",
		Name:        "Request Statistics",
		Description: "Server request statistics",
		MIMEType:    "application/json",
	}, func(ctx context.Context, req *mcp.ReadResourceRequest) (*mcp.ReadResourceResult, error) {
		stats := fmt.Sprintf(`{
  "total_requests": 0,
  "uptime": "%s"
}`, time.Since(time.Now()).String())
		return &mcp.ReadResourceResult{
			Contents: []*mcp.ResourceContents{
				{
					URI:      req.Params.URI,
					MIMEType: "application/json",
					Text:     stats,
				},
			},
		}, nil
	})
}