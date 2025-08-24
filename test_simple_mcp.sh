#!/bin/bash

echo "============================================="
echo "Simple MCP Client-Server Test"
echo "============================================="

# Clean up any existing processes
pkill -f mcp_example_server 2>/dev/null
pkill -f mcp_example_client 2>/dev/null
sleep 1

# Start server in background
echo "Starting server on port 3000..."
./build/examples/mcp/mcp_example_server --port 3000 --transport http &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start"
    exit 1
fi

echo "Server started with PID: $SERVER_PID"

# Test with curl to verify server is responding
echo ""
echo "Testing server health endpoint..."
curl -s http://localhost:3000/health | head -20
echo ""

# Test JSON-RPC endpoint
echo "Testing JSON-RPC endpoint with initialize..."
curl -X POST http://localhost:3000/rpc \
     -H "Content-Type: application/json" \
     -H "Accept: text/event-stream" \
     -d '{"id":1,"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{"roots":{"listChanged":true},"sampling":{}}}}'
echo ""
echo ""

# Test with ping
echo "Testing ping..."
curl -X POST http://localhost:3000/rpc \
     -H "Content-Type: application/json" \
     -d '{"id":2,"jsonrpc":"2.0","method":"ping","params":{}}'
echo ""
echo ""

# Kill server
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "Test completed"