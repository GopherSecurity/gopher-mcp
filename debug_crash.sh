#!/bin/bash

# Start server in background
echo "Starting server on port 3011..."
./build/examples/mcp/mcp_example_server --port 3011 > /tmp/debug_server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Run client with LLDB and capture the exact crash
echo "Running client in LLDB to capture crash..."

lldb -b -o "run --port 3011 --quiet" \
     -o "thread backtrace all" \
     -o "frame variable" \
     -o "quit" \
     ./build/examples/mcp/mcp_example_client 2>&1 | tee /tmp/lldb_output.txt

# Show the backtrace
echo ""
echo "=== Crash Backtrace ==="
grep -A 50 "stop reason = signal" /tmp/lldb_output.txt || grep -A 50 "Assertion failed" /tmp/lldb_output.txt

# Kill server
kill $SERVER_PID 2>/dev/null