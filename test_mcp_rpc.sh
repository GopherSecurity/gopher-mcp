#!/bin/bash

echo "Testing MCP RPC functionality"
echo "=============================="

# Start server in background
echo "Starting MCP server on port 8080..."
./build/examples/mcp/mcp_example_server --port 8080 > server.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Check if server is running
if ! ps -p $SERVER_PID > /dev/null; then
    echo "ERROR: Server failed to start"
    cat server.log
    exit 1
fi

echo "Server started with PID $SERVER_PID"

# Test 1: Health check
echo ""
echo "Test 1: Health check"
curl -s http://localhost:8080/health && echo "" || echo "FAILED"

# Test 2: Initialize via RPC
echo ""
echo "Test 2: Initialize via RPC"
RESPONSE=$(curl -s -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{"roots":{"listChanged":true}}},"id":1}' \
  2>&1)
echo "Response: $RESPONSE"

# Test 3: Simple echo test
echo ""
echo "Test 3: Echo test"
RESPONSE=$(curl -s -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"echo","params":{"message":"Hello MCP"},"id":2}' \
  2>&1)
echo "Response: $RESPONSE"

# Test 4: Tool call (calculator)
echo ""
echo "Test 4: Calculator tool"
RESPONSE=$(curl -s -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"operation":"add","a":5,"b":3}},"id":3}' \
  2>&1)
echo "Response: $RESPONSE"

# Kill server
echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "Test complete. Check server.log for details."
