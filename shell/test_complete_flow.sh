#!/bin/bash

echo "==================================="
echo "MCP Complete Flow Test"
echo "==================================="
echo ""

SERVER_PORT=9095
SERVER_PID=""

# Function to cleanup
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ ! -z "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null
        echo "Server stopped"
    fi
    rm -f server.log client.log
}
trap cleanup EXIT

# Start server with verbose logging
echo "1. Starting MCP server on port $SERVER_PORT..."
../build/examples/mcp/mcp_example_server --transport http --port $SERVER_PORT --verbose > server.log 2>&1 &
SERVER_PID=$!
echo "   Server PID: $SERVER_PID"
sleep 3

# Test server is alive
echo ""
echo "2. Testing server health..."
HEALTH=$(curl -s http://localhost:$SERVER_PORT/health)
if echo "$HEALTH" | grep -q "healthy"; then
    echo "   ✓ Server is healthy"
else
    echo "   ✗ Server health check failed"
    exit 1
fi

# Test direct JSON-RPC
echo ""
echo "3. Testing direct JSON-RPC initialize..."
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"}},"id":1}')

if echo "$RESPONSE" | grep -q "serverInfo"; then
    echo "   ✓ Initialize successful via curl"
else
    echo "   ✗ Initialize failed via curl"
    echo "   Response: $RESPONSE"
fi

# Now test with the actual client (without demo mode first)
echo ""
echo "4. Testing MCP client (simple connection)..."
(./build/examples/mcp/mcp_example_client --host localhost --port $SERVER_PORT --transport http --verbose 2>&1 | tee client.log | head -40) &
CLIENT_PID=$!

# Wait a bit and check client log
sleep 5
kill $CLIENT_PID 2>/dev/null

if grep -q "Connected successfully" client.log; then
    echo "   ✓ Client connected successfully"
else
    echo "   ✗ Client failed to connect"
fi

if grep -q "Protocol initialized" client.log; then
    echo "   ✓ Client initialized protocol"
else
    echo "   ✗ Client failed to initialize protocol"
    echo ""
    echo "Client output:"
    tail -20 client.log
fi

# Check server log for requests
echo ""
echo "5. Checking server log for client requests..."
if grep -q "McpServer::onRequest called with method: initialize" server.log; then
    echo "   ✓ Server received initialize request from client"
else
    echo "   ✗ Server did not receive initialize request from client"
    echo ""
    echo "Last 20 lines of server log:"
    tail -20 server.log
fi

echo ""
echo "==================================="
echo "Test completed"
echo "==================================="