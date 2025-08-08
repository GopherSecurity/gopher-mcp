#!/bin/bash

echo "Testing stdio echo server..."

# Create named pipes
PIPE_IN=$(mktemp -u)
PIPE_OUT=$(mktemp -u)
mkfifo "$PIPE_IN" "$PIPE_OUT"

# Start server with visible stderr
echo "Starting server..."
./stdio_echo_server < "$PIPE_IN" > "$PIPE_OUT" 2>&1 &
SERVER_PID=$!

# Give server time to start
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server failed to start or crashed immediately"
    cat "$PIPE_OUT"
    rm -f "$PIPE_IN" "$PIPE_OUT"
    exit 1
fi

echo "Server running with PID $SERVER_PID"

# Read server output in background
cat "$PIPE_OUT" &
CAT_PID=$!

# Send test message
echo "Sending test message..."
echo '{"jsonrpc":"2.0","id":1,"method":"test","params":{"hello":"world"}}' > "$PIPE_IN"

# Wait a bit for response
sleep 2

# Clean up
kill $SERVER_PID 2>/dev/null
kill $CAT_PID 2>/dev/null
rm -f "$PIPE_IN" "$PIPE_OUT"

echo "Test complete"