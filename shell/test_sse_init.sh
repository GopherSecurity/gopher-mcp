#!/bin/bash

# Test SSE initialization flow
echo "Testing SSE Client-Server Communication"
echo "========================================"

# Clean up any previous runs
pkill -f mcp_example 2>/dev/null
rm -f /tmp/server_sse.log /tmp/client_sse.log

# Start server with SSE support
echo "Starting server..."
../build/examples/mcp/mcp_example_server --port 3002 > /tmp/server_sse.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check if server started
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start"
    cat /tmp/server_sse.log
    exit 1
fi

echo "Server started with PID: $SERVER_PID"

# Start client with SSE request
echo "Starting client with SSE mode..."
timeout 5 ./build/examples/mcp/mcp_example_client --host localhost --port 3002 > /tmp/client_sse.log 2>&1

CLIENT_EXIT=$?

echo ""
echo "Client exit code: $CLIENT_EXIT"
echo ""

# Check results
echo "=== Server Response Check ==="
if grep -q "SSE mode activated" /tmp/server_sse.log; then
    echo "✓ Server detected SSE mode"
else
    echo "✗ Server did NOT detect SSE mode"
fi

if grep -q "First SSE write, adding HTTP headers" /tmp/server_sse.log; then
    echo "✓ Server sent SSE headers"
else
    echo "✗ Server did NOT send SSE headers"
fi

if grep -q "data:" /tmp/server_sse.log; then
    echo "✓ Server formatted SSE events"
else
    echo "✗ Server did NOT format SSE events"
fi

echo ""
echo "=== Client Response Check ==="
if grep -q "Content-Type: text/event-stream" /tmp/client_sse.log; then
    echo "✓ Client received SSE content type"
else
    echo "✗ Client did NOT receive SSE content type"
fi

if grep -q "Initialize response received" /tmp/client_sse.log; then
    echo "✓ Client received initialize response"
else
    echo "✗ Client did NOT receive initialize response"
fi

if grep -q "Protocol initialized successfully" /tmp/client_sse.log; then
    echo "✓ Client initialized successfully"
else
    echo "✗ Client did NOT initialize successfully"
fi

echo ""
echo "=== Debug Output ==="
echo "Last 20 lines of server log:"
tail -20 /tmp/server_sse.log

echo ""
echo "Last 20 lines of client log:"
tail -20 /tmp/client_sse.log

# Cleanup
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

exit $CLIENT_EXIT