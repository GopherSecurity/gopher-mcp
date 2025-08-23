#!/bin/bash

# Start server
./build/examples/mcp/mcp_example_server --port 3012 > /tmp/debug_server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Get full backtrace
lldb -b ./build/examples/mcp/mcp_example_client \
  -o "run --port 3012 --quiet" \
  -o "bt" \
  -o "frame select 4" \
  -o "frame variable" \
  -o "thread list" \
  -o "thread backtrace all" \
  -o "quit" 2>&1 | grep -A 100 "thread #1"

# Cleanup
kill $SERVER_PID 2>/dev/null