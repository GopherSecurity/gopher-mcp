#!/bin/bash

echo "Testing signal handling for MCP server..."
echo

# Start server in background
./build/examples/mcp/mcp_example_server --port 3001 &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Wait for server to start
sleep 2

# Send SIGINT (Ctrl+C)
echo "Sending SIGINT to server..."
kill -INT $SERVER_PID

# Wait and check if process exists
sleep 2
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server still running after SIGINT"
    kill -9 $SERVER_PID
    exit 1
else
    echo "SUCCESS: Server shut down gracefully"
fi

echo
echo "Testing signal handling for MCP client..."
echo

# Start server first
./build/examples/mcp/mcp_example_server --port 3002 > /dev/null 2>&1 &
SERVER2_PID=$!
echo "Server started with PID: $SERVER2_PID for client test"
sleep 2

# Start client connecting to the server
./build/examples/mcp/mcp_example_client --port 3002 --quiet &
CLIENT_PID=$!
echo "Client started with PID: $CLIENT_PID"

# Wait a bit for connection
sleep 3

# Send SIGINT to client
echo "Sending SIGINT to client..."
kill -INT $CLIENT_PID

# Wait and check
sleep 2
if kill -0 $CLIENT_PID 2>/dev/null; then
    echo "ERROR: Client still running after SIGINT"
    kill -9 $CLIENT_PID
    kill -9 $SERVER2_PID 2>/dev/null || true
    exit 1
else
    echo "SUCCESS: Client shut down gracefully"
fi

# Clean up server
kill -INT $SERVER2_PID 2>/dev/null || true
sleep 1
kill -9 $SERVER2_PID 2>/dev/null || true

echo
echo "All signal handling tests passed!"