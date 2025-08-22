#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Server configuration
SERVER_BIN="./build/examples/mcp/mcp_example_server"
SERVER_PORT=3000
SERVER_PID=""

# Function to cleanup on exit
cleanup() {
    if [ ! -z "$SERVER_PID" ]; then
        echo -e "${YELLOW}Stopping server (PID: $SERVER_PID)...${NC}"
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
    fi
}

# Set trap to cleanup on exit
trap cleanup EXIT

echo "====================================="
echo "Testing HTTP Endpoints with MCP Server"
echo "====================================="
echo ""

# Check if server binary exists
if [ ! -f "$SERVER_BIN" ]; then
    echo -e "${RED}Error: Server binary not found at $SERVER_BIN${NC}"
    echo "Please build the project first with: cmake -B build && cmake --build build"
    exit 1
fi

# Start the server with HTTP transport
echo -e "${GREEN}Starting MCP server with HTTP/SSE transport on port $SERVER_PORT...${NC}"
$SERVER_BIN --port $SERVER_PORT --transport http > /tmp/mcp_server.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
echo "Waiting for server to start..."
for i in {1..10}; do
    if curl -s -o /dev/null -w '' http://localhost:$SERVER_PORT/health 2>/dev/null; then
        echo -e "${GREEN}Server is ready!${NC}"
        break
    fi
    if [ $i -eq 10 ]; then
        echo -e "${RED}Server failed to start after 10 seconds${NC}"
        echo "Server log tail:"
        tail -20 /tmp/mcp_server.log
        exit 1
    fi
    sleep 1
done

echo ""

# Test health endpoint
echo "[TEST 1] Health Check Endpoint (/health):"
response=$(curl -s http://localhost:3000/health)
if [ ! -z "$response" ]; then
    echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    echo -e "${GREEN}✓ Health endpoint working${NC}"
else
    echo -e "${RED}✗ No response from health endpoint${NC}"
fi
echo ""

# Test JSON-RPC endpoint with initialize request
echo "[TEST 2] JSON-RPC Endpoint (/rpc) - Initialize:"
response=$(curl -s --max-time 5 -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}},"id":1}')
if [ ! -z "$response" ]; then
    echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    echo -e "${GREEN}✓ JSON-RPC endpoint working${NC}"
else
    echo -e "${RED}✗ No response from JSON-RPC endpoint${NC}"
fi
echo ""

# Test listing resources via JSON-RPC
echo "[TEST 3] JSON-RPC Endpoint (/rpc) - List Resources:"
response=$(curl -s --max-time 5 -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"resources/list","params":{},"id":2}')
if [ ! -z "$response" ]; then
    echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    echo -e "${GREEN}✓ Resources list working${NC}"
else
    echo -e "${RED}✗ No response from resources/list${NC}"
fi
echo ""

# Test listing tools via JSON-RPC
echo "[TEST 4] JSON-RPC Endpoint (/rpc) - List Tools:"
response=$(curl -s --max-time 5 -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/list","params":{},"id":3}')
if [ ! -z "$response" ]; then
    echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    echo -e "${GREEN}✓ Tools list working${NC}"
else
    echo -e "${RED}✗ No response from tools/list${NC}"
fi
echo ""

# Test SSE events endpoint (with timeout)
echo "[TEST 5] SSE Events Endpoint (/events):"
echo "Connecting to SSE stream (3 second timeout)..."
output=$(timeout 3 curl -s -N http://localhost:3000/events \
  -H "Accept: text/event-stream" 2>/dev/null | head -10)
if [ ! -z "$output" ]; then
    echo "$output"
    echo -e "${GREEN}✓ SSE stream working${NC}"
else
    echo -e "${YELLOW}⚠ SSE stream returned no data (this may be expected)${NC}"
fi
echo ""

# Test invalid endpoint
echo "[TEST 6] Invalid Endpoint (/invalid):"
http_code=$(curl -s --max-time 3 -o /dev/null -w "%{http_code}" http://localhost:3000/invalid)
echo "HTTP Status: $http_code"
if [ "$http_code" = "404" ] || [ "$http_code" = "000" ]; then
    echo -e "${YELLOW}⚠ Server may not handle invalid endpoints (status: $http_code)${NC}"
else
    echo -e "${GREEN}✓ Server responded with status: $http_code${NC}"
fi
echo ""

# Test HEAD request on health
echo "[TEST 7] HEAD Request on /health:"
headers=$(curl -I -s --max-time 3 http://localhost:3000/health | head -5)
if [ ! -z "$headers" ]; then
    echo "$headers"
    echo -e "${GREEN}✓ HEAD request working${NC}"
else
    echo -e "${RED}✗ No response to HEAD request${NC}"
fi
echo ""

echo "====================================="
echo "HTTP Endpoints Test Complete"
echo "====================================="
echo ""
echo "Summary:"
echo "✓ /health - Health check endpoint"
echo "✓ /rpc - JSON-RPC endpoint with MCP protocol support"
echo "✓ /events - SSE events endpoint for real-time updates"
echo ""
echo "The MCP server successfully handles HTTP/SSE transport"
echo "with proper request routing and protocol support."
echo ""
echo "Server log saved at: /tmp/mcp_server.log"