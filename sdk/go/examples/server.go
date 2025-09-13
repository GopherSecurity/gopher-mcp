package main

import (
	"bufio"
	"compress/gzip"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/filters"
	"github.com/GopherSecurity/gopher-mcp/src/integration"
)

// MockMCPServer simulates an MCP server with filtered transport
type MockMCPServer struct {
	transport *filters.FilteredTransport
	scanner   *bufio.Scanner
	writer    *bufio.Writer
}

// NewMockMCPServer creates a new mock MCP server
func NewMockMCPServer() *MockMCPServer {
	// Create filtered transport wrapper around stdio
	stdioTransport := &StdioTransport{
		Reader: os.Stdin,
		Writer: os.Stdout,
	}
	
	filteredTransport := filters.NewFilteredTransport(stdioTransport)
	
	// Add filters
	setupFilters(filteredTransport)
	
	return &MockMCPServer{
		transport: filteredTransport,
		scanner:   bufio.NewScanner(filteredTransport),
		writer:    bufio.NewWriter(filteredTransport),
	}
}

// StdioTransport implements io.ReadWriteCloser for stdio
type StdioTransport struct {
	Reader *os.File
	Writer *os.File
}

func (st *StdioTransport) Read(p []byte) (n int, err error) {
	return st.Reader.Read(p)
}

func (st *StdioTransport) Write(p []byte) (n int, err error) {
	return st.Writer.Write(p)
}

func (st *StdioTransport) Close() error {
	// Don't close stdio
	return nil
}

func setupFilters(transport *filters.FilteredTransport) {
	// Add logging filter
	loggingFilter := filters.NewLoggingFilter("[Server] ", true)
	transport.AddInboundFilter(filters.NewFilterAdapter(loggingFilter, "ServerLogging", "logging"))
	transport.AddOutboundFilter(filters.NewFilterAdapter(loggingFilter, "ServerLogging", "logging"))
	
	// Add validation filter
	validationFilter := filters.NewValidationFilter(1024 * 1024) // 1MB max
	transport.AddInboundFilter(filters.NewFilterAdapter(validationFilter, "ServerValidation", "validation"))
	
	// Add compression if enabled
	if os.Getenv("MCP_ENABLE_COMPRESSION") == "true" {
		compressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
		transport.AddOutboundFilter(filters.NewFilterAdapter(compressionFilter, "ServerCompression", "compression"))
		
		// Add decompression for inbound
		decompressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
		transport.AddInboundFilter(&DecompressionAdapter{filter: decompressionFilter})
		
		log.Println("Compression enabled for server")
	}
	
	log.Println("Filters configured: logging, validation, optional compression")
}

// DecompressionAdapter adapts CompressionFilter for decompression
type DecompressionAdapter struct {
	filter *filters.CompressionFilter
}

func (da *DecompressionAdapter) GetID() string {
	return "decompression"
}

func (da *DecompressionAdapter) GetName() string {
	return "DecompressionAdapter"
}

func (da *DecompressionAdapter) GetType() string {
	return "decompression"
}

func (da *DecompressionAdapter) GetVersion() string {
	return "1.0.0"
}

func (da *DecompressionAdapter) GetDescription() string {
	return "Decompression adapter"
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

// Run starts the server
func (s *MockMCPServer) Run() error {
	log.Println("Mock MCP Server with filters started")
	log.Println("Waiting for JSON-RPC messages...")
	
	// Send initialization response
	initResponse := map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      1,
		"result": map[string]interface{}{
			"serverInfo": map[string]interface{}{
				"name":    "filtered-mcp-server",
				"version": "1.0.0",
			},
			"capabilities": map[string]interface{}{
				"tools": map[string]interface{}{
					"supported": true,
				},
			},
		},
	}
	
	if err := s.sendMessage(initResponse); err != nil {
		return fmt.Errorf("failed to send init response: %w", err)
	}
	
	// Process incoming messages
	for s.scanner.Scan() {
		line := s.scanner.Text()
		
		var msg map[string]interface{}
		if err := json.Unmarshal([]byte(line), &msg); err != nil {
			log.Printf("Failed to parse message: %v", err)
			continue
		}
		
		// Handle different message types
		if method, ok := msg["method"].(string); ok {
			switch method {
			case "tools/list":
				s.handleListTools(msg)
			case "tools/call":
				s.handleCallTool(msg)
			default:
				log.Printf("Unknown method: %s", method)
			}
		}
	}
	
	if err := s.scanner.Err(); err != nil {
		return fmt.Errorf("scanner error: %w", err)
	}
	
	return nil
}

func (s *MockMCPServer) sendMessage(msg interface{}) error {
	data, err := json.Marshal(msg)
	if err != nil {
		return err
	}
	
	if _, err := s.writer.Write(data); err != nil {
		return err
	}
	
	if _, err := s.writer.Write([]byte("\n")); err != nil {
		return err
	}
	
	return s.writer.Flush()
}

func (s *MockMCPServer) handleListTools(msg map[string]interface{}) {
	response := map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      msg["id"],
		"result": map[string]interface{}{
			"tools": []map[string]interface{}{
				{
					"name":        "echo",
					"description": "Echo a message",
				},
				{
					"name":        "get_time",
					"description": "Get current time",
				},
			},
		},
	}
	
	if err := s.sendMessage(response); err != nil {
		log.Printf("Failed to send tools list: %v", err)
	}
}

func (s *MockMCPServer) handleCallTool(msg map[string]interface{}) {
	params, _ := msg["params"].(map[string]interface{})
	toolName, _ := params["name"].(string)
	arguments, _ := params["arguments"].(map[string]interface{})
	
	var result string
	switch toolName {
	case "echo":
		message, _ := arguments["message"].(string)
		result = fmt.Sprintf("Echo: %s", message)
	case "get_time":
		result = fmt.Sprintf("Current time: %s", time.Now().Format(time.RFC3339))
	default:
		result = "Unknown tool"
	}
	
	response := map[string]interface{}{
		"jsonrpc": "2.0",
		"id":      msg["id"],
		"result": map[string]interface{}{
			"content": []map[string]interface{}{
				{
					"type": "text",
					"text": result,
				},
			},
		},
	}
	
	if err := s.sendMessage(response); err != nil {
		log.Printf("Failed to send tool result: %v", err)
	}
}

func main() {
	log.SetPrefix("[Filtered Server] ")
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)
	
	// Set up signal handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	
	// Create and run server
	server := NewMockMCPServer()
	
	go func() {
		<-sigChan
		log.Println("Received interrupt signal, shutting down...")
		os.Exit(0)
	}()
	
	if err := server.Run(); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}