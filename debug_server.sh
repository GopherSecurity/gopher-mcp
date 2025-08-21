#!/bin/bash

# Debug script for MCP server segfault
echo "Starting MCP server under lldb..."
echo "Commands to run:"
echo "  r           - run the server"
echo "  bt          - backtrace after crash"
echo "  frame 0     - examine crash frame"
echo ""

cat > /tmp/lldb_commands.txt << 'EOF'
r
bt
frame 0
q
EOF

# Run lldb with commands
lldb -s /tmp/lldb_commands.txt ./build/examples/mcp/mcp_example_server -- --port 3333