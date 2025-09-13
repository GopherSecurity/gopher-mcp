package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	"github.com/modelcontextprotocol/go-sdk/pkg/client"
)

type MCPClient struct {
	client    *client.MCPClient
	transport client.Transport
	ctx       context.Context
}

func NewMCPClient(ctx context.Context) *MCPClient {
	return &MCPClient{
		ctx: ctx,
	}
}

func (c *MCPClient) Connect(serverCommand string) error {
	// Parse server command
	parts := strings.Fields(serverCommand)
	if len(parts) == 0 {
		return fmt.Errorf("invalid server command")
	}

	// Create stdio transport to communicate with server process
	transport := client.NewStdioTransport(parts[0], parts[1:]...)
	c.transport = transport

	// Create client with options
	clientOpts := []client.ClientOption{
		client.WithName("example-mcp-client"),
		client.WithVersion("1.0.0"),
	}

	mcpClient, err := client.NewMCPClient(clientOpts...)
	if err != nil {
		return fmt.Errorf("failed to create MCP client: %w", err)
	}

	c.client = mcpClient

	// Connect to server
	if err := c.client.Connect(c.ctx, c.transport); err != nil {
		return fmt.Errorf("failed to connect to server: %w", err)
	}

	log.Println("Connected to MCP server")

	// Initialize session
	initResult, err := c.client.Initialize(c.ctx)
	if err != nil {
		return fmt.Errorf("failed to initialize session: %w", err)
	}

	log.Printf("Server info: %s v%s", initResult.ServerInfo.Name, initResult.ServerInfo.Version)
	log.Printf("Capabilities: Tools=%v, Prompts=%v, Resources=%v",
		initResult.Capabilities.Tools != nil,
		initResult.Capabilities.Prompts != nil,
		initResult.Capabilities.Resources != nil)

	return nil
}

func (c *MCPClient) ListTools() error {
	tools, err := c.client.ListTools(c.ctx)
	if err != nil {
		return fmt.Errorf("failed to list tools: %w", err)
	}

	fmt.Println("\nAvailable Tools:")
	fmt.Println("================")
	for _, tool := range tools.Tools {
		fmt.Printf("- %s: %s\n", tool.Name, tool.Description)
		if tool.InputSchema != nil {
			schemaJSON, _ := json.MarshalIndent(tool.InputSchema, "  ", "  ")
			fmt.Printf("  Parameters: %s\n", schemaJSON)
		}
	}

	return nil
}

func (c *MCPClient) CallTool(name string, arguments map[string]interface{}) error {
	argsJSON, err := json.Marshal(arguments)
	if err != nil {
		return fmt.Errorf("failed to marshal arguments: %w", err)
	}

	result, err := c.client.CallTool(c.ctx, name, json.RawMessage(argsJSON))
	if err != nil {
		return fmt.Errorf("failed to call tool: %w", err)
	}

	fmt.Printf("\nTool '%s' Result:\n", name)
	fmt.Println("==================")
	
	for _, content := range result.Content {
		if content.Type == "text" {
			fmt.Println(content.Text)
		} else {
			resultJSON, _ := json.MarshalIndent(content, "", "  ")
			fmt.Println(string(resultJSON))
		}
	}

	return nil
}

func (c *MCPClient) ListPrompts() error {
	prompts, err := c.client.ListPrompts(c.ctx)
	if err != nil {
		return fmt.Errorf("failed to list prompts: %w", err)
	}

	fmt.Println("\nAvailable Prompts:")
	fmt.Println("==================")
	for _, prompt := range prompts.Prompts {
		fmt.Printf("- %s: %s\n", prompt.Name, prompt.Description)
		if len(prompt.Arguments) > 0 {
			fmt.Println("  Arguments:")
			for _, arg := range prompt.Arguments {
				required := ""
				if arg.Required {
					required = " (required)"
				}
				fmt.Printf("    - %s: %s%s\n", arg.Name, arg.Description, required)
			}
		}
	}

	return nil
}

func (c *MCPClient) GetPrompt(name string, arguments map[string]string) error {
	result, err := c.client.GetPrompt(c.ctx, name, arguments)
	if err != nil {
		return fmt.Errorf("failed to get prompt: %w", err)
	}

	fmt.Printf("\nPrompt '%s' Result:\n", name)
	fmt.Println("===================")
	
	if result.Description != "" {
		fmt.Printf("Description: %s\n", result.Description)
	}

	for _, msg := range result.Messages {
		fmt.Printf("\n[%s]:\n", msg.Role)
		switch content := msg.Content.(type) {
		case client.TextContent:
			fmt.Println(content.Text)
		case client.ImageContent:
			fmt.Printf("Image: %s (MIME: %s)\n", content.Data[:20]+"...", content.MimeType)
		case client.EmbeddedResourceContent:
			fmt.Printf("Resource: %s\n", content.Resource.URI)
		default:
			contentJSON, _ := json.MarshalIndent(content, "", "  ")
			fmt.Println(string(contentJSON))
		}
	}

	return nil
}

func (c *MCPClient) ListResources() error {
	resources, err := c.client.ListResources(c.ctx)
	if err != nil {
		return fmt.Errorf("failed to list resources: %w", err)
	}

	fmt.Println("\nAvailable Resources:")
	fmt.Println("====================")
	for _, resource := range resources.Resources {
		fmt.Printf("- %s\n", resource.URI)
		fmt.Printf("  Name: %s\n", resource.Name)
		fmt.Printf("  Description: %s\n", resource.Description)
		if resource.MimeType != "" {
			fmt.Printf("  MIME Type: %s\n", resource.MimeType)
		}
	}

	return nil
}

func (c *MCPClient) ReadResource(uri string) error {
	result, err := c.client.ReadResource(c.ctx, uri)
	if err != nil {
		return fmt.Errorf("failed to read resource: %w", err)
	}

	fmt.Printf("\nResource '%s' Contents:\n", uri)
	fmt.Println("=======================")
	
	for _, content := range result.Contents {
		if content.Text != "" {
			fmt.Println(content.Text)
		} else if content.Blob != "" {
			fmt.Printf("Binary data: %d bytes\n", len(content.Blob))
		}
	}

	return nil
}

func (c *MCPClient) InteractiveDemo() error {
	fmt.Println("\n=== MCP Client Interactive Demo ===\n")

	// List available tools
	if err := c.ListTools(); err != nil {
		log.Printf("Error listing tools: %v", err)
	}

	// Call some tools
	fmt.Println("\n--- Tool Demonstrations ---")

	// Get current time
	if err := c.CallTool("get_time", map[string]interface{}{
		"format": "RFC3339",
	}); err != nil {
		log.Printf("Error calling get_time: %v", err)
	}

	time.Sleep(1 * time.Second)

	// Echo message
	if err := c.CallTool("echo", map[string]interface{}{
		"message": "Hello from MCP client!",
	}); err != nil {
		log.Printf("Error calling echo: %v", err)
	}

	time.Sleep(1 * time.Second)

	// Calculate
	if err := c.CallTool("calculate", map[string]interface{}{
		"operation": "multiply",
		"a":         42,
		"b":         3.14,
	}); err != nil {
		log.Printf("Error calling calculate: %v", err)
	}

	// List prompts
	if err := c.ListPrompts(); err != nil {
		log.Printf("Error listing prompts: %v", err)
	}

	// Get prompts
	fmt.Println("\n--- Prompt Demonstrations ---")

	if err := c.GetPrompt("greeting", map[string]string{
		"name": "Alice",
	}); err != nil {
		log.Printf("Error getting greeting prompt: %v", err)
	}

	time.Sleep(1 * time.Second)

	if err := c.GetPrompt("system_info", nil); err != nil {
		log.Printf("Error getting system_info prompt: %v", err)
	}

	// List resources
	if err := c.ListResources(); err != nil {
		log.Printf("Error listing resources: %v", err)
	}

	// Read resources
	fmt.Println("\n--- Resource Demonstrations ---")

	if err := c.ReadResource("config://server"); err != nil {
		log.Printf("Error reading config resource: %v", err)
	}

	time.Sleep(1 * time.Second)

	if err := c.ReadResource("stats://requests"); err != nil {
		log.Printf("Error reading stats resource: %v", err)
	}

	return nil
}

func (c *MCPClient) Disconnect() error {
	if c.client != nil {
		return c.client.Close()
	}
	return nil
}

func main() {
	// Command line flags
	var (
		serverCmd   = flag.String("server", "", "Server command to execute (e.g., 'node server.js')")
		interactive = flag.Bool("interactive", true, "Run interactive demo")
		toolName    = flag.String("tool", "", "Call specific tool")
		toolArgs    = flag.String("args", "{}", "Tool arguments as JSON")
	)
	flag.Parse()

	// Set up logging
	log.SetPrefix("[MCP Client] ")
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)

	// Create context
	ctx := context.Background()

	// Create client
	client := NewMCPClient(ctx)

	// Determine server command
	serverCommand := *serverCmd
	if serverCommand == "" {
		// Default to the example server if it exists
		serverCommand = "go run server.go"
		log.Printf("No server specified, using default: %s", serverCommand)
	}

	// Connect to server
	if err := client.Connect(serverCommand); err != nil {
		log.Fatalf("Failed to connect: %v", err)
	}
	defer client.Disconnect()

	// Run demo or specific tool
	if *toolName != "" {
		// Parse tool arguments
		var args map[string]interface{}
		if err := json.Unmarshal([]byte(*toolArgs), &args); err != nil {
			log.Fatalf("Failed to parse tool arguments: %v", err)
		}

		// Call tool
		if err := client.CallTool(*toolName, args); err != nil {
			log.Fatalf("Failed to call tool: %v", err)
		}
	} else if *interactive {
		// Run interactive demo
		if err := client.InteractiveDemo(); err != nil {
			log.Fatalf("Demo failed: %v", err)
		}
	} else {
		// Just list available tools
		if err := client.ListTools(); err != nil {
			log.Fatalf("Failed to list tools: %v", err)
		}
	}

	fmt.Println("\nClient demo completed successfully!")
}