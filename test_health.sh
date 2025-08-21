#!/bin/bash
# Kill any existing servers
pkill -f mcp_example_server 2>/dev/null

# Start server
echo "Starting server on port 8081..."
./build/examples/mcp/mcp_example_server --port 8081 --verbose > health_test.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
sleep 3

# Check if server is running
if ! ps -p $SERVER_PID > /dev/null; then
    echo "Server failed to start"
    cat health_test.log
    exit 1
fi

echo "Server running with PID $SERVER_PID"

# Test health endpoint with timeout
echo "Testing health endpoint..."
if timeout 5 curl -v http://localhost:8081/health 2>&1 | tee health_response.txt | grep -q "200 OK"; then
    echo "SUCCESS: Health check returned 200 OK"
    cat health_response.txt
else
    echo "FAILED: Health check did not return 200 OK"
    echo "Response:"
    cat health_response.txt
    echo ""
    echo "Server log tail:"
    tail -50 health_test.log
fi

# Kill server
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "Test complete"
