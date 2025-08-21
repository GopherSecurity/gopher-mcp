#!/bin/bash
set -e

echo "Starting MCP server..."
./build/examples/mcp/mcp_example_server > test.log 2>&1 &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Give server time to start
sleep 2

echo "Testing /health endpoint..."
curl -v http://localhost:3000/health 2>&1 | grep -E "(HTTP|OK)"

echo "Testing /info endpoint..."
curl -v http://localhost:3000/info 2>&1 | grep -E "(HTTP|application/json)"

# Give time to see if it crashes
echo "Waiting to check for crash..."
sleep 2

# Check if server is still running
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server still running - PASS"
    kill $SERVER_PID
    wait $SERVER_PID 2>/dev/null || true
    echo "Server shut down cleanly"
else
    echo "Server crashed - FAIL"
    exit 1
fi

echo "Test completed successfully!"