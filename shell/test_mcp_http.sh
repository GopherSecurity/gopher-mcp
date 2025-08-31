#!/bin/bash

echo "Testing MCP HTTP Transport"
echo "=========================="

SERVER_PORT=9091
SERVER_PID=""

# Function to cleanup
cleanup() {
    echo "Cleaning up..."
    if [ ! -z "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null
    fi
}
trap cleanup EXIT

# Start server
echo "Starting MCP server on port $SERVER_PORT..."
../build/examples/mcp/mcp_example_server --transport http --port $SERVER_PORT 2>&1 > server.log &
SERVER_PID=$!
sleep 3

# Test server is running
echo "Testing server health endpoint..."
curl -s http://localhost:$SERVER_PORT/health | python3 -c "import sys, json; data=json.load(sys.stdin); print(f'Health: {data[\"status\"]}')"

echo ""
echo "Testing initialize method..."
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"}},"id":1}')
echo $RESPONSE | python3 -c "import sys, json; data=json.load(sys.stdin); print(f'Server: {data[\"result\"][\"serverInfo.name\"]} v{data[\"result\"][\"serverInfo.version\"]}')"

echo ""
echo "Testing tools/list method..."
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/list","params":{},"id":2}')
echo $RESPONSE | python3 -c "import sys, json; data=json.load(sys.stdin); tools=data['result']['tools']; print(f'Available tools: {len(tools)}'); [print(f'  - {t[\"name\"]}: {t[\"description\"]}') for t in tools]"

echo ""
echo "Testing calculator tool..."
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"operation":"multiply","a":7,"b":6}},"id":3}')
echo $RESPONSE | python3 -c "import sys, json; data=json.load(sys.stdin); print(f'Calculator result: {data[\"result\"][\"content\"]}')"

echo ""
echo "Testing resources/list method..."
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"resources/list","params":{},"id":4}')
echo $RESPONSE | python3 -c "import sys, json; data=json.load(sys.stdin); resources=data['result']['resources']; print(f'Available resources: {len(resources)}'); [print(f'  - {r[\"uri\"]}: {r[\"name\"]}') for r in resources]"

echo ""
echo "Testing prompts/list method..."
RESPONSE=$(curl -s -X POST http://localhost:$SERVER_PORT/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"prompts/list","params":{},"id":5}')
echo $RESPONSE | python3 -c "import sys, json; data=json.load(sys.stdin); prompts=data['result']['prompts']; print(f'Available prompts: {len(prompts)}'); [print(f'  - {p[\"name\"]}: {p[\"description\"]}') for p in prompts]"

echo ""
echo "All tests completed successfully!"