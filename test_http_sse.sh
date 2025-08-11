#!/bin/bash

# Test MCP server and client with HTTP+SSE transport

echo "=== Testing MCP HTTP+SSE Transport ==="

# Start server on port 8080
echo "Starting MCP server on http://localhost:8080..."
./build/examples/mcp/mcp_example_server "http://localhost:8080" > server.log 2>&1 &
SERVER_PID=$!

echo "Server PID: $SERVER_PID"
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server failed to start. Last lines of server.log:"
    tail -20 server.log
    exit 1
fi

echo "Server is running. Starting client..."

# Start client connecting to server
./build/examples/mcp/mcp_example_client "http://localhost:8080" --demo > client.log 2>&1 &
CLIENT_PID=$!

echo "Client PID: $CLIENT_PID"
sleep 2

# Check if client is running
if ! kill -0 $CLIENT_PID 2>/dev/null; then
    echo "Client terminated. Last lines of client.log:"
    tail -20 client.log
fi

# Let them run for a bit
echo "Letting them communicate for 5 seconds..."
sleep 5

# Show logs
echo ""
echo "=== Server Log ==="
tail -30 server.log

echo ""
echo "=== Client Log ==="
tail -30 client.log

# Cleanup
echo ""
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
kill $CLIENT_PID 2>/dev/null

echo "Done!"