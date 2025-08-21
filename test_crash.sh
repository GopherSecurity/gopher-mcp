#!/bin/bash

# Start server in background
echo "Starting server..."
./build/examples/mcp/mcp_example_server --port 3333 2>&1 | tee server.log &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Send request that causes crash
echo "Sending health request..."
curl http://localhost:3333/health

# Give it time to crash
sleep 1

# Check if server is still running
if ps -p $SERVER_PID > /dev/null; then
    echo "Server is still running (PID: $SERVER_PID)"
    kill $SERVER_PID
else
    echo "Server crashed!"
fi