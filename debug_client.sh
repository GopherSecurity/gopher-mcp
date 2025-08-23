#!/bin/bash

# Start server in background
echo "Starting server on port 3010..."
./build/examples/mcp/mcp_example_server --port 3010 > /tmp/debug_server.log 2>&1 &
SERVER_PID=$!

# Give server time to start
sleep 2

echo "Starting LLDB debugger for client..."
echo "Commands to run in LLDB:"
echo "  breakpoint set -n isThreadSafe"
echo "  breakpoint set -n createTimer"
echo "  breakpoint set -n schedulePeriodicTasks"
echo "  run --port 3010 --quiet"
echo ""

# Start LLDB
lldb ./build/examples/mcp/mcp_example_client -o "breakpoint set -n isThreadSafe" \
     -o "breakpoint set -n createTimer" \
     -o "breakpoint set -n schedulePeriodicTasks" \
     -o "breakpoint set -n LibeventDispatcher::run" \
     -o "run --port 3010 --quiet"

# Cleanup
kill $SERVER_PID 2>/dev/null