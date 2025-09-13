package main

import (
	"compress/gzip"
	"fmt"
	"log"

	"github.com/GopherSecurity/gopher-mcp/src/filters"
)

func main() {
	log.Println("Testing filter integration...")
	
	// Test compression filter
	compressionFilter := filters.NewCompressionFilter(gzip.DefaultCompression)
	testData := []byte("Hello, this is a test message for the filter system!")
	
	compressed, err := compressionFilter.Process(testData)
	if err != nil {
		log.Fatalf("Compression failed: %v", err)
	}
	
	fmt.Printf("Original size: %d bytes\n", len(testData))
	fmt.Printf("Compressed size: %d bytes\n", len(compressed))
	fmt.Printf("Compression ratio: %.2f%%\n", float64(len(compressed))/float64(len(testData))*100)
	
	// Test decompression
	decompressed, err := compressionFilter.Decompress(compressed)
	if err != nil {
		log.Fatalf("Decompression failed: %v", err)
	}
	
	if string(decompressed) != string(testData) {
		log.Fatalf("Data mismatch after decompression")
	}
	
	fmt.Println("Compression/decompression test passed!")
	
	// Test validation filter
	validationFilter := filters.NewValidationFilter(100) // 100 bytes max
	
	// Test valid JSON-RPC message
	validMessage := []byte(`{"jsonrpc":"2.0","method":"test","id":1}`)
	_, err = validationFilter.Process(validMessage)
	if err != nil {
		log.Fatalf("Valid message rejected: %v", err)
	}
	fmt.Println("Validation test passed for valid message")
	
	// Test oversized message
	oversizedMessage := make([]byte, 200)
	_, err = validationFilter.Process(oversizedMessage)
	if err == nil {
		log.Fatalf("Oversized message should have been rejected")
	}
	fmt.Println("Validation test passed for oversized message")
	
	// Test logging filter
	loggingFilter := filters.NewLoggingFilter("[Test] ", true)
	loggingFilter.SetLogPayload(true)
	
	_, err = loggingFilter.Process(testData)
	if err != nil {
		log.Fatalf("Logging filter failed: %v", err)
	}
	
	stats := loggingFilter.GetStats()
	fmt.Printf("Logging filter stats: ProcessedCount=%d, BytesIn=%d, BytesOut=%d\n",
		stats.ProcessedCount, stats.BytesIn, stats.BytesOut)
	
	fmt.Println("\nAll filter tests passed successfully!")
}