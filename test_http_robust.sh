#!/bin/bash

echo "==================================="
echo "MCP HTTP Transport Test Suite"
echo "==================================="
echo ""

SERVER_PORT=9092

# Start server
echo "1. Starting MCP server on port $SERVER_PORT..."
./build/examples/mcp/mcp_example_server --transport http --port $SERVER_PORT > server.log 2>&1 &
SERVER_PID=$!
echo "   Server PID: $SERVER_PID"
sleep 3

# Function to cleanup
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ ! -z "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null
        echo "   Server stopped"
    fi
}
trap cleanup EXIT

echo ""
echo "2. Testing server endpoints:"
echo ""

# Test health
echo "   a) Health endpoint:"
RESPONSE=$(curl -s http://localhost:$SERVER_PORT/health)
if echo "$RESPONSE" | grep -q "healthy"; then
    echo "      ✓ Health check passed"
else
    echo "      ✗ Health check failed"
fi

# Test initialize
echo ""
echo "   b) Initialize protocol:"
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05"},"id":1}')
if echo "$RESPONSE" | grep -q "serverInfo.name"; then
    echo "      ✓ Initialize successful"
    echo "      Server: $(echo $RESPONSE | grep -o '"serverInfo.name":"[^"]*"' | cut -d'"' -f4)"
else
    echo "      ✗ Initialize failed"
fi

# Test tools/list
echo ""
echo "   c) List tools:"
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/list","params":{},"id":2}')
if echo "$RESPONSE" | grep -q "calculator"; then
    echo "      ✓ Tools list successful"
    echo "      Found calculator tool"
else
    echo "      ✗ Tools list failed"
fi

# Test calculator
echo ""
echo "   d) Calculator tool (5 * 8):"
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"operation":"multiply","a":5,"b":8}},"id":3}')
if echo "$RESPONSE" | grep -q "40"; then
    echo "      ✓ Calculator successful: 5 * 8 = 40"
else
    echo "      ✗ Calculator failed"
    echo "      Response: $RESPONSE"
fi

# Test resources
echo ""
echo "   e) List resources:"
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"resources/list","params":{},"id":4}')
if echo "$RESPONSE" | grep -q "resources"; then
    echo "      ✓ Resources list successful"
else
    echo "      ✗ Resources list failed"
fi

# Test prompts
echo ""
echo "   f) List prompts:"
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"prompts/list","params":{},"id":5}')
if echo "$RESPONSE" | grep -q "prompts"; then
    echo "      ✓ Prompts list successful"
else
    echo "      ✗ Prompts list failed"
fi

echo ""
echo "3. Testing with demo mode client:"
echo ""

# Try to run client (it may fail due to connection issues, but we'll see the attempt)
echo "   Running client..."
(./build/examples/mcp/mcp_example_client --host localhost --port $SERVER_PORT --transport http --demo 2>&1 | head -20) || true

echo ""
echo "==================================="
echo "Test suite completed!"
echo "====================================="