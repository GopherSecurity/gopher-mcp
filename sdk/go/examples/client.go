package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os/exec"
	"strings"
	"time"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type MCPClient struct {
	client  *mcp.Client
	session *mcp.ClientSession
	ctx     context.Context
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

	// Create command
	cmd := exec.Command(parts[0], parts[1:]...)
	
	// Create command transport
	transport := &mcp.CommandTransport{Command: cmd}

	// Create client implementation
	impl := &mcp.Implementation{
		Name:    "example-mcp-client",
		Version: "1.0.0",
	}

	// Create client
	c.client = mcp.NewClient(impl, nil)

	// Connect to server
	session, err := c.client.Connect(c.ctx, transport, nil)
	if err != nil {
		return fmt.Errorf("failed to connect to server: %w", err)
	}

	c.session = session
	
	log.Println("Connected to MCP server")

	// Get server info
	initResult := session.InitializeResult()
	if initResult != nil && initResult.ServerInfo != nil {
		log.Printf("Server info: %s v%s", initResult.ServerInfo.Name, initResult.ServerInfo.Version)
		
		if initResult.Capabilities != nil {
			caps := []string{}
			if initResult.Capabilities.Tools != nil {
				caps = append(caps, "tools")
			}
			if initResult.Capabilities.Prompts != nil {
				caps = append(caps, "prompts")
			}
			if initResult.Capabilities.Resources != nil {
				caps = append(caps, "resources")
			}
			log.Printf("Capabilities: %v", caps)
		}
	}

	return nil
}

func (c *MCPClient) ListTools() error {
	if c.session == nil {
		return fmt.Errorf("not connected")
	}

	result, err := c.session.ListTools(c.ctx, &mcp.ListToolsParams{})
	if err != nil {
		return fmt.Errorf("failed to list tools: %w", err)
	}

	fmt.Println("\nAvailable Tools:")
	fmt.Println("================")
	for _, tool := range result.Tools {
		fmt.Printf("- %s: %s\n", tool.Name, tool.Description)
	}

	return nil
}

func (c *MCPClient) CallTool(name string, arguments map[string]interface{}) error {
	if c.session == nil {
		return fmt.Errorf("not connected")
	}

	result, err := c.session.CallTool(c.ctx, &mcp.CallToolParams{
		Name:      name,
		Arguments: arguments,
	})
	if err != nil {
		return fmt.Errorf("failed to call tool: %w", err)
	}

	fmt.Printf("\nTool '%s' Result:\n", name)
	fmt.Println("==================")
	
	for _, content := range result.Content {
		switch v := content.(type) {
		case *mcp.TextContent:
			fmt.Println(v.Text)
		case *mcp.ImageContent:
			preview := "<binary>"
			if len(v.Data) > 20 {
				preview = string(v.Data[:20]) + "..."
			}
			fmt.Printf("Image: %s (MIME: %s)\n", preview, v.MIMEType)
		default:
			fmt.Printf("%v\n", content)
		}
	}

	return nil
}

func (c *MCPClient) ListPrompts() error {
	if c.session == nil {
		return fmt.Errorf("not connected")
	}

	result, err := c.session.ListPrompts(c.ctx, &mcp.ListPromptsParams{})
	if err != nil {
		return fmt.Errorf("failed to list prompts: %w", err)
	}

	fmt.Println("\nAvailable Prompts:")
	fmt.Println("==================")
	for _, prompt := range result.Prompts {
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
	if c.session == nil {
		return fmt.Errorf("not connected")
	}

	result, err := c.session.GetPrompt(c.ctx, &mcp.GetPromptParams{
		Name:      name,
		Arguments: arguments,
	})
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
		switch v := msg.Content.(type) {
		case *mcp.TextContent:
			fmt.Println(v.Text)
		case *mcp.ImageContent:
			preview := "<binary>"
			if len(v.Data) > 20 {
				preview = string(v.Data[:20]) + "..."
			}
			fmt.Printf("Image: %s (MIME: %s)\n", preview, v.MIMEType)
		default:
			fmt.Printf("%v\n", msg.Content)
		}
	}

	return nil
}

func (c *MCPClient) ListRoots() error {
	if c.session == nil {
		return fmt.Errorf("not connected")
	}

	result, err := c.session.ListResources(c.ctx, &mcp.ListResourcesParams{})
	if err != nil {
		return fmt.Errorf("failed to list roots: %w", err)
	}

	fmt.Println("\nAvailable Resources:")
	fmt.Println("====================")
	for _, resource := range result.Resources {
		fmt.Printf("- %s\n", resource.URI)
		if resource.Name != "" {
			fmt.Printf("  Name: %s\n", resource.Name)
		}
		if resource.Description != "" {
			fmt.Printf("  Description: %s\n", resource.Description)
		}
	}

	return nil
}

func (c *MCPClient) ReadResource(uri string) error {
	if c.session == nil {
		return fmt.Errorf("not connected")
	}

	result, err := c.session.ReadResource(c.ctx, &mcp.ReadResourceParams{
		URI: uri,
	})
	if err != nil {
		return fmt.Errorf("failed to read resource: %w", err)
	}

	fmt.Printf("\nResource '%s' Contents:\n", uri)
	fmt.Println("=======================")
	
	for _, content := range result.Contents {
		if content.Text != "" {
			fmt.Println(content.Text)
		} else if content.Blob != nil {
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
		"a":         42.0,
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

	// List resources (roots)
	if err := c.ListRoots(); err != nil {
		log.Printf("Error listing roots: %v", err)
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
	if c.session != nil {
		return c.session.Close()
	}
	return nil
}

func main() {
	// Command line flags
	var (
		serverCmd   = flag.String("server", "", "Server command to execute (e.g., 'go run server.go')")
		interactive = flag.Bool("interactive", true, "Run interactive demo")
		toolName    = flag.String("tool", "", "Call specific tool")
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
		// Call tool with default arguments
		args := map[string]interface{}{}
		if *toolName == "echo" {
			args["message"] = "Test message"
		} else if *toolName == "calculate" {
			args["operation"] = "add"
			args["a"] = 10.0
			args["b"] = 20.0
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