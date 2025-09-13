package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/modelcontextprotocol/go-sdk/pkg/server"
)

type MCPServer struct {
	server *server.MCPServer
	tools  map[string]ToolDefinition
}

type ToolDefinition struct {
	Name        string                 `json:"name"`
	Description string                 `json:"description"`
	Parameters  map[string]interface{} `json:"parameters"`
}

func NewMCPServer() *MCPServer {
	return &MCPServer{
		tools: make(map[string]ToolDefinition),
	}
}

func (s *MCPServer) Initialize() error {
	serverOpts := []server.ServerOption{
		server.WithName("example-mcp-server"),
		server.WithVersion("1.0.0"),
	}

	mcpServer, err := server.NewMCPServer(serverOpts...)
	if err != nil {
		return fmt.Errorf("failed to create MCP server: %w", err)
	}

	s.server = mcpServer

	// Register tools
	s.registerTools()

	// Set up handlers
	s.setupHandlers()

	return nil
}

func (s *MCPServer) registerTools() {
	// Register example tools
	s.tools["get_time"] = ToolDefinition{
		Name:        "get_time",
		Description: "Get the current time",
		Parameters: map[string]interface{}{
			"format": map[string]interface{}{
				"type":        "string",
				"description": "Time format (e.g., RFC3339, Unix)",
				"default":     "RFC3339",
			},
		},
	}

	s.tools["echo"] = ToolDefinition{
		Name:        "echo",
		Description: "Echo back the provided message",
		Parameters: map[string]interface{}{
			"message": map[string]interface{}{
				"type":        "string",
				"description": "Message to echo",
				"required":    true,
			},
		},
	}

	s.tools["calculate"] = ToolDefinition{
		Name:        "calculate",
		Description: "Perform basic calculations",
		Parameters: map[string]interface{}{
			"operation": map[string]interface{}{
				"type":        "string",
				"description": "Operation to perform (add, subtract, multiply, divide)",
				"required":    true,
			},
			"a": map[string]interface{}{
				"type":        "number",
				"description": "First operand",
				"required":    true,
			},
			"b": map[string]interface{}{
				"type":        "number",
				"description": "Second operand",
				"required":    true,
			},
		},
	}
}

func (s *MCPServer) setupHandlers() {
	// Handle tool listing
	s.server.SetToolsListHandler(func(ctx context.Context) ([]server.Tool, error) {
		tools := make([]server.Tool, 0, len(s.tools))
		for _, tool := range s.tools {
			tools = append(tools, server.Tool{
				Name:        tool.Name,
				Description: tool.Description,
				InputSchema: tool.Parameters,
			})
		}
		return tools, nil
	})

	// Handle tool execution
	s.server.SetToolCallHandler(func(ctx context.Context, name string, arguments json.RawMessage) (interface{}, error) {
		switch name {
		case "get_time":
			return s.handleGetTime(arguments)
		case "echo":
			return s.handleEcho(arguments)
		case "calculate":
			return s.handleCalculate(arguments)
		default:
			return nil, fmt.Errorf("unknown tool: %s", name)
		}
	})

	// Handle prompts
	s.server.SetPromptsListHandler(func(ctx context.Context) ([]server.Prompt, error) {
		return []server.Prompt{
			{
				Name:        "greeting",
				Description: "Generate a greeting message",
				Arguments: []server.PromptArgument{
					{
						Name:        "name",
						Description: "Name to greet",
						Required:    true,
					},
				},
			},
			{
				Name:        "system_info",
				Description: "Get system information",
			},
		}, nil
	})

	s.server.SetPromptGetHandler(func(ctx context.Context, name string, arguments map[string]string) (*server.GetPromptResult, error) {
		switch name {
		case "greeting":
			userName := arguments["name"]
			if userName == "" {
				userName = "User"
			}
			return &server.GetPromptResult{
				Messages: []server.PromptMessage{
					{
						Role:    "user",
						Content: server.TextContent(fmt.Sprintf("Hello, %s! Welcome to the MCP server example.", userName)),
					},
				},
			}, nil
		case "system_info":
			return &server.GetPromptResult{
				Messages: []server.PromptMessage{
					{
						Role: "user",
						Content: server.TextContent(fmt.Sprintf(
							"System Information:\nServer: example-mcp-server v1.0.0\nTime: %s\nTools Available: %d",
							time.Now().Format(time.RFC3339),
							len(s.tools),
						)),
					},
				},
			}, nil
		default:
			return nil, fmt.Errorf("unknown prompt: %s", name)
		}
	})

	// Handle resources
	s.server.SetResourcesListHandler(func(ctx context.Context) ([]server.Resource, error) {
		return []server.Resource{
			{
				URI:         "config://server",
				Name:        "Server Configuration",
				Description: "Current server configuration",
				MimeType:    "application/json",
			},
			{
				URI:         "stats://requests",
				Name:        "Request Statistics",
				Description: "Server request statistics",
				MimeType:    "application/json",
			},
		}, nil
	})

	s.server.SetResourceReadHandler(func(ctx context.Context, uri string) (*server.ReadResourceResult, error) {
		switch uri {
		case "config://server":
			config := map[string]interface{}{
				"name":    "example-mcp-server",
				"version": "1.0.0",
				"tools":   len(s.tools),
			}
			data, _ := json.MarshalIndent(config, "", "  ")
			return &server.ReadResourceResult{
				Contents: []server.ResourceContent{
					{
						URI:      uri,
						MimeType: "application/json",
						Text:     string(data),
					},
				},
			}, nil
		case "stats://requests":
			stats := map[string]interface{}{
				"total_requests": 0,
				"uptime":        time.Since(time.Now()).String(),
			}
			data, _ := json.MarshalIndent(stats, "", "  ")
			return &server.ReadResourceResult{
				Contents: []server.ResourceContent{
					{
						URI:      uri,
						MimeType: "application/json",
						Text:     string(data),
					},
				},
			}, nil
		default:
			return nil, fmt.Errorf("unknown resource: %s", uri)
		}
	})
}

func (s *MCPServer) handleGetTime(arguments json.RawMessage) (interface{}, error) {
	var args struct {
		Format string `json:"format"`
	}
	if err := json.Unmarshal(arguments, &args); err != nil {
		return nil, err
	}

	if args.Format == "" {
		args.Format = "RFC3339"
	}

	now := time.Now()
	switch args.Format {
	case "Unix":
		return map[string]interface{}{
			"time": now.Unix(),
		}, nil
	case "RFC3339":
		return map[string]interface{}{
			"time": now.Format(time.RFC3339),
		}, nil
	default:
		return map[string]interface{}{
			"time": now.Format(args.Format),
		}, nil
	}
}

func (s *MCPServer) handleEcho(arguments json.RawMessage) (interface{}, error) {
	var args struct {
		Message string `json:"message"`
	}
	if err := json.Unmarshal(arguments, &args); err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"echo": args.Message,
	}, nil
}

func (s *MCPServer) handleCalculate(arguments json.RawMessage) (interface{}, error) {
	var args struct {
		Operation string  `json:"operation"`
		A         float64 `json:"a"`
		B         float64 `json:"b"`
	}
	if err := json.Unmarshal(arguments, &args); err != nil {
		return nil, err
	}

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
			return nil, fmt.Errorf("division by zero")
		}
		result = args.A / args.B
	default:
		return nil, fmt.Errorf("unknown operation: %s", args.Operation)
	}

	return map[string]interface{}{
		"result": result,
	}, nil
}

func (s *MCPServer) Start() error {
	// Start server with stdio transport by default
	transport := server.NewStdioTransport()
	
	log.Println("Starting MCP server on stdio...")
	return s.server.Serve(transport)
}

func main() {
	// Set up logging
	log.SetPrefix("[MCP Server] ")
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)

	// Create and initialize server
	mcpServer := NewMCPServer()
	if err := mcpServer.Initialize(); err != nil {
		log.Fatalf("Failed to initialize server: %v", err)
	}

	// Set up signal handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Start server in goroutine
	errChan := make(chan error, 1)
	go func() {
		if err := mcpServer.Start(); err != nil {
			errChan <- err
		}
	}()

	log.Println("MCP server started. Press Ctrl+C to stop.")

	// Wait for signal or error
	select {
	case sig := <-sigChan:
		log.Printf("Received signal: %v. Shutting down...", sig)
	case err := <-errChan:
		log.Printf("Server error: %v", err)
	}

	log.Println("Server stopped.")
}