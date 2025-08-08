#!/bin/bash
# Integration test for stdio echo server and client

echo "Starting stdio echo integration test..."

# Create a temporary FIFO for bidirectional communication
PIPE_DIR=$(mktemp -d)
SERVER_IN="$PIPE_DIR/server_in"
SERVER_OUT="$PIPE_DIR/server_out"

mkfifo "$SERVER_IN" "$SERVER_OUT"

# Start the server in background
echo "Starting echo server..."
./stdio_echo_server < "$SERVER_IN" > "$SERVER_OUT" 2>/dev/null &
SERVER_PID=$!

# Give server time to start
sleep 1

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start"
    rm -rf "$PIPE_DIR"
    exit 1
fi

echo "Server started with PID $SERVER_PID"

# Send a test message
echo "Sending test message to server..."
echo '{"jsonrpc":"2.0","id":1,"method":"test.echo","params":{"message":"Hello, Server!"}}' > "$SERVER_IN" &

# Read response with timeout
RESPONSE=$(timeout 2 head -n1 "$SERVER_OUT" 2>/dev/null)

if [ -z "$RESPONSE" ]; then
    echo "ERROR: No response from server"
    kill $SERVER_PID 2>/dev/null
    rm -rf "$PIPE_DIR"
    exit 1
fi

echo "Received response: $RESPONSE"

# Check if response contains expected fields
if echo "$RESPONSE" | grep -q '"id":1' && echo "$RESPONSE" | grep -q '"result"'; then
    echo "SUCCESS: Server responded correctly"
    EXIT_CODE=0
else
    echo "ERROR: Invalid response from server"
    EXIT_CODE=1
fi

# Send shutdown notification
echo "Sending shutdown notification..."
echo '{"jsonrpc":"2.0","method":"shutdown"}' > "$SERVER_IN"

# Wait for server to shut down
sleep 1

# Kill server if still running
kill $SERVER_PID 2>/dev/null

# Cleanup
rm -rf "$PIPE_DIR"

echo "Integration test completed"
exit $EXIT_CODE