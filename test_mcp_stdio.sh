#!/bin/bash

# Test MCP client-server communication over stdio

echo "Starting MCP stdio test..."

# Create named pipes for bidirectional communication
PIPE_DIR="/tmp/mcp_test_$$"
mkdir -p "$PIPE_DIR"
mkfifo "$PIPE_DIR/server_in"
mkfifo "$PIPE_DIR/server_out"

# Start server with stdio transport
echo "Starting server..."
./build/examples/mcp/mcp_example_server --transport stdio < "$PIPE_DIR/server_in" > "$PIPE_DIR/server_out" 2>&1 &
SERVER_PID=$!

# Give server time to start
sleep 1

# Connect the pipes in reverse for client (server's out is client's in)
echo "Starting client..."
./build/examples/mcp/mcp_example_client --transport stdio < "$PIPE_DIR/server_out" > "$PIPE_DIR/server_in" 2>&1 &
CLIENT_PID=$!

# Let them communicate
echo "Letting client and server communicate..."
sleep 5

# Check if processes are still running
if ps -p $SERVER_PID > /dev/null; then
    echo "Server is still running (PID $SERVER_PID)"
else
    echo "Server has exited"
fi

if ps -p $CLIENT_PID > /dev/null; then
    echo "Client is still running (PID $CLIENT_PID)"
else
    echo "Client has exited"
fi

# Clean up
echo "Cleaning up..."
kill $SERVER_PID $CLIENT_PID 2>/dev/null
wait $SERVER_PID $CLIENT_PID 2>/dev/null
rm -rf "$PIPE_DIR"

echo "Test complete"