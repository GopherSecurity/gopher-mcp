#!/bin/bash

echo "Starting MCP server..."
../build/examples/mcp/mcp_example_server --port 3001 --transport http --verbose > server.log 2>&1 &
SERVER_PID=$!

sleep 2

echo "Starting MCP client with verbose logging..."

# Run client with timeout and capture output
(
    ../build/examples/mcp/mcp_example_client \
        --host localhost \
        --port 3001 \
        --transport http \
        --verbose \
        --demo 2>&1 | tee client.log &
    
    CLIENT_PID=$!
    
    # Wait for 10 seconds then kill
    sleep 10
    kill $CLIENT_PID 2>/dev/null
) 

# Kill server
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=== Server log tail ==="
tail -50 server.log

echo ""
echo "=== Client log tail ==="
tail -50 client.log

# Clean up
rm -f server.log client.log