package main

import (
	"bufio"
	"compress/gzip"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/filters"
	"github.com/GopherSecurity/gopher-mcp/src/integration"
)

// MockMCPClient simulates an MCP client with filtered transport
type MockMCPClient struct {
	transport *filters.FilteredTransport
	reader    *bufio.Reader
	writer    *bufio.Writer
	cmd       *exec.Cmd
	nextID    int
}

// NewMockMCPClient creates a new mock MCP client
func NewMockMCPClient(serverCommand string) (*MockMCPClient, error) {
	// Start the server process
	cmd := exec.Command("sh", "-c", serverCommand)
	
	// Get pipes for communication
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("failed to get stdin pipe: %w", err)
	}
	
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("failed to get stdout pipe: %w", err)
	}
	
	// Start the server
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start server: %w", err)
	}
	
	// Create transport wrapper
	transport := &ProcessTransport{
		stdin:  stdin,
		stdout: stdout,
	}
	
	// Create filtered transport
	filteredTransport := filters.NewFilteredTransport(transport)
	
	// Setup filters
	setupClientFilters(filteredTransport)
	
	return &MockMCPClient{
		transport: filteredTransport,
		reader:    bufio.NewReader(filteredTransport),
		writer:    bufio.NewWriter(filteredTransport),
		cmd:       cmd,
		nextID:    1,
	}, nil
}

// ProcessTransport wraps process pipes
type ProcessTransport struct {
	stdin  io.WriteCloser
	stdout io.ReadCloser
}

func (pt *ProcessTransport) Read(p []byte) (n int, err error) {
	return pt.stdout.Read(p)
}

func (pt *ProcessTransport) Write(p []byte) (n int, err error) {
	return pt.stdin.Write(p)
}

func (pt *ProcessTransport) Close() error {
	pt.stdin.Close()
	pt.stdout.Close()
	return nil
}

func setupClientFilters(transport *filters.FilteredTransport) {
	// Add logging filter
	loggingFilter := filters.NewLoggingFilter("[Client] ", true)
	transport.AddInboundFilter(filters.NewFilterAdapter(loggingFilter, "ClientLogging", "logging"))
	transport.AddOutboundFilter(filters.NewFilterAdapter(loggingFilter, "ClientLogging", "logging"))
	
	// Add validation filter for outbound
	validationFilter := filters.NewValidationFilter(1024 * 1024) // 1MB max
	transport.AddOutboundFilter(filters.NewFilterAdapter(validationFilter, "ClientValidation", "validation"))
	
	// Add compression if enabled
	if os.Getenv("MCP_ENABLE_COMPRESSION") == "true" {
		// For client: compress outbound, decompress inbound
		compressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
		transport.AddOutboundFilter(filters.NewFilterAdapter(compressionFilter, "ClientCompression", "compression"))
		
		// Add decompression for inbound
		decompressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
		transport.AddInboundFilter(&DecompressionAdapter{filter: decompressionFilter})
		
		log.Println("Compression enabled for client")
	}
	
	log.Println("Filters configured: logging, validation, optional compression")
}

// DecompressionAdapter adapts CompressionFilter for decompression
type DecompressionAdapter struct {
	filter *filters.CompressionFilter
}

func (da *DecompressionAdapter) GetID() string {
	return "client-decompression"
}

func (da *DecompressionAdapter) GetName() string {
	return "ClientDecompressionAdapter"
}

func (da *DecompressionAdapter) GetType() string {
	return "decompression"
}

func (da *DecompressionAdapter) GetVersion() string {
	return "1.0.0"
}

func (da *DecompressionAdapter) GetDescription() string {
	return "Client decompression adapter"
}

func (da *DecompressionAdapter) Process(data []byte) ([]byte, error) {
	// Try to decompress, if it fails assume it's not compressed
	decompressed, err := da.filter.Decompress(data)
	if err != nil {
		// Not compressed, return as-is
		return data, nil
	}
	return decompressed, nil
}

func (da *DecompressionAdapter) ValidateConfig() error {
	return nil
}

func (da *DecompressionAdapter) GetConfiguration() map[string]interface{} {
	return make(map[string]interface{})
}

func (da *DecompressionAdapter) UpdateConfig(config map[string]interface{}) {}

func (da *DecompressionAdapter) GetCapabilities() []string {
	return []string{"decompress"}
}

func (da *DecompressionAdapter) GetDependencies() []integration.FilterDependency {
	return []integration.FilterDependency{}
}

func (da *DecompressionAdapter) GetResourceRequirements() integration.ResourceRequirements {
	return integration.ResourceRequirements{}
}

func (da *DecompressionAdapter) GetTypeInfo() integration.TypeInfo {
	return integration.TypeInfo{}
}

func (da *DecompressionAdapter) EstimateLatency() time.Duration {
	return da.filter.EstimateLatency()
}

func (da *DecompressionAdapter) HasBlockingOperations() bool {
	return false
}

func (da *DecompressionAdapter) UsesDeprecatedFeatures() bool {
	return false
}

func (da *DecompressionAdapter) HasKnownVulnerabilities() bool {
	return false
}

func (da *DecompressionAdapter) IsStateless() bool {
	return true
}

func (da *DecompressionAdapter) Clone() integration.Filter {
	return &DecompressionAdapter{filter: da.filter}
}

func (da *DecompressionAdapter) SetID(id string) {}

// Connect initializes connection to the server
func (c *MockMCPClient) Connect() error {
	log.Println("Connecting to server...")
	
	// Read initialization response
	line, err := c.reader.ReadString('\n')
	if err != nil {
		return fmt.Errorf("failed to read init response: %w", err)
	}
	
	var initResponse map[string]interface{}
	if err := json.Unmarshal([]byte(line), &initResponse); err != nil {
		return fmt.Errorf("failed to parse init response: %w", err)
	}
	
	if result, ok := initResponse["result"].(map[string]interface{}); ok {
		if serverInfo, ok := result["serverInfo"].(map[string]interface{}); ok {
			name := serverInfo["name"]
			version := serverInfo["version"]
			log.Printf("Connected to server: %s v%s", name, version)
		}
	}
	
	return nil
}

// ListTools requests the list of available tools
func (c *MockMCPClient) ListTools() ([]map[string]interface{}, error) {
	request := map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "tools/list",
		"id":      c.nextID,
	}
	c.nextID++
	
	response, err := c.sendRequest(request)
	if err != nil {
		return nil, err
	}
	
	if result, ok := response["result"].(map[string]interface{}); ok {
		if tools, ok := result["tools"].([]interface{}); ok {
			var toolList []map[string]interface{}
			for _, tool := range tools {
				if t, ok := tool.(map[string]interface{}); ok {
					toolList = append(toolList, t)
				}
			}
			return toolList, nil
		}
	}
	
	return nil, fmt.Errorf("invalid response format")
}

// CallTool calls a specific tool with arguments
func (c *MockMCPClient) CallTool(name string, arguments map[string]interface{}) (string, error) {
	request := map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "tools/call",
		"params": map[string]interface{}{
			"name":      name,
			"arguments": arguments,
		},
		"id": c.nextID,
	}
	c.nextID++
	
	response, err := c.sendRequest(request)
	if err != nil {
		return "", err
	}
	
	if result, ok := response["result"].(map[string]interface{}); ok {
		if content, ok := result["content"].([]interface{}); ok && len(content) > 0 {
			if item, ok := content[0].(map[string]interface{}); ok {
				if text, ok := item["text"].(string); ok {
					return text, nil
				}
			}
		}
	}
	
	return "", fmt.Errorf("invalid response format")
}

// sendRequest sends a request and waits for response
func (c *MockMCPClient) sendRequest(request map[string]interface{}) (map[string]interface{}, error) {
	// Send request
	data, err := json.Marshal(request)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}
	
	if _, err := c.writer.Write(data); err != nil {
		return nil, fmt.Errorf("failed to write request: %w", err)
	}
	
	if _, err := c.writer.Write([]byte("\n")); err != nil {
		return nil, fmt.Errorf("failed to write newline: %w", err)
	}
	
	if err := c.writer.Flush(); err != nil {
		return nil, fmt.Errorf("failed to flush: %w", err)
	}
	
	// Read response
	line, err := c.reader.ReadString('\n')
	if err != nil {
		return nil, fmt.Errorf("failed to read response: %w", err)
	}
	
	var response map[string]interface{}
	if err := json.Unmarshal([]byte(line), &response); err != nil {
		return nil, fmt.Errorf("failed to parse response: %w", err)
	}
	
	return response, nil
}

// Close closes the client and stops the server
func (c *MockMCPClient) Close() error {
	c.transport.Close()
	if c.cmd != nil && c.cmd.Process != nil {
		c.cmd.Process.Kill()
		c.cmd.Wait()
	}
	return nil
}

// RunDemo runs an interactive demo
func (c *MockMCPClient) RunDemo() error {
	// List tools
	fmt.Println("\n=== Listing Available Tools ===")
	tools, err := c.ListTools()
	if err != nil {
		return fmt.Errorf("failed to list tools: %w", err)
	}
	
	for _, tool := range tools {
		fmt.Printf("- %s: %s\n", tool["name"], tool["description"])
	}
	
	// Call echo tool
	fmt.Println("\n=== Calling Echo Tool ===")
	result, err := c.CallTool("echo", map[string]interface{}{
		"message": "Hello from filtered MCP client!",
	})
	if err != nil {
		return fmt.Errorf("failed to call echo: %w", err)
	}
	fmt.Printf("Result: %s\n", result)
	
	// Call get_time tool
	fmt.Println("\n=== Calling Get Time Tool ===")
	result, err = c.CallTool("get_time", map[string]interface{}{})
	if err != nil {
		return fmt.Errorf("failed to call get_time: %w", err)
	}
	fmt.Printf("Result: %s\n", result)
	
	return nil
}

func main() {
	var (
		serverCmd   = flag.String("server", "./build/bin/server", "Path to server executable")
		interactive = flag.Bool("interactive", true, "Run interactive demo")
	)
	flag.Parse()
	
	log.SetPrefix("[Filtered Client] ")
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)
	
	// Create client
	client, err := NewMockMCPClient(*serverCmd)
	if err != nil {
		log.Fatalf("Failed to create client: %v", err)
	}
	defer client.Close()
	
	// Connect to server
	if err := client.Connect(); err != nil {
		log.Fatalf("Failed to connect: %v", err)
	}
	
	// Run demo
	if *interactive {
		if err := client.RunDemo(); err != nil {
			log.Fatalf("Demo failed: %v", err)
		}
	}
	
	fmt.Println("\nClient demo completed successfully!")
}