package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"
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

	// Start server on stdio transport with filters
	log.Println("Starting MCP server on stdio with filters...")
	
	// Create the base stdio transport
	stdioTransport := &mcp.StdioTransport{}
	
	// Create filtered transport wrapper
	filteredTransport := filters.NewFilteredTransport(stdioTransport)
	
	// Add logging filter for debugging
	loggingFilter := filters.NewLoggingFilter("[Server] ", false)
	filteredTransport.AddInboundFilter(filters.NewFilterAdapter(loggingFilter, "ServerLogging", "logging"))
	filteredTransport.AddOutboundFilter(filters.NewFilterAdapter(loggingFilter, "ServerLogging", "logging"))
	
	// Add validation filter
	validationFilter := filters.NewValidationFilter(1024 * 1024) // 1MB max message size
	filteredTransport.AddInboundFilter(filters.NewFilterAdapter(validationFilter, "ServerValidation", "validation"))
	
	// Add compression filter (optional, can be enabled based on config)
	if os.Getenv("MCP_ENABLE_COMPRESSION") == "true" {
		compressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
		filteredTransport.AddOutboundFilter(filters.NewFilterAdapter(compressionFilter, "ServerCompression", "compression"))
		log.Println("Compression enabled for outbound messages")
	}
	
	if err := server.Run(ctx, filteredTransport); err != nil {
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