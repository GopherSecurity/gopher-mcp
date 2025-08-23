#!/bin/bash

# Test MCP client with HTTP/SSE transport

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "====================================="
echo "Testing MCP Client with HTTP Transport"
echo "====================================="
echo ""

# Start server first
echo -e "${GREEN}Starting MCP server on port 3000...${NC}"
./build/examples/mcp/mcp_example_server --transport http --port 3000 > /tmp/server_test.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server to start
sleep 2

# Check if server is running
if ! ps -p $SERVER_PID > /dev/null; then
    echo -e "${RED}Server failed to start!${NC}"
    cat /tmp/server_test.log
    exit 1
fi

# Test client connection
echo -e "${GREEN}Starting MCP client connecting to http://localhost:3000...${NC}"
./build/examples/mcp/mcp_example_client --transport http --host localhost --port 3000 > /tmp/client_test.log 2>&1 &
CLIENT_PID=$!
sleep 10
kill $CLIENT_PID 2>/dev/null
CLIENT_EXIT=$?

echo ""
echo "Client exit code: $CLIENT_EXIT"
echo ""

if [ $CLIENT_EXIT -eq 0 ]; then
    echo -e "${GREEN}✓ Client completed successfully${NC}"
else
    echo -e "${RED}✗ Client failed or timed out (exit code: $CLIENT_EXIT)${NC}"
fi

echo ""
echo "====================================="
echo "Log Analysis"
echo "====================================="
echo ""

# Check client logs
echo "Client Log Analysis:"
if grep -q "Protocol initialized successfully" /tmp/client_test.log; then
    echo -e "${GREEN}✓ Client initialized protocol${NC}"
else
    echo -e "${RED}✗ Client failed to initialize protocol${NC}"
fi

if grep -q "Connected successfully" /tmp/client_test.log; then
    echo -e "${GREEN}✓ Client connected to server${NC}"
else
    echo -e "${RED}✗ Client failed to connect${NC}"
fi

if grep -q "onReadReady called" /tmp/client_test.log; then
    echo -e "${GREEN}✓ Client received data from server${NC}"
else
    echo -e "${YELLOW}⚠ Client did not receive data from server${NC}"
fi

echo ""
echo "Server Log Analysis:"
if grep -q "Sending response for request" /tmp/server_test.log; then
    echo -e "${GREEN}✓ Server sent responses${NC}"
else
    echo -e "${RED}✗ Server did not send responses${NC}"
fi

echo ""
echo "Debug Output (last 20 lines of client log):"
echo "----------------------------------------"
tail -20 /tmp/client_test.log

echo ""
echo "====================================="
echo "Cleanup"
echo "====================================="
echo -e "${YELLOW}Stopping server (PID: $SERVER_PID)...${NC}"
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo -e "${GREEN}Test complete${NC}"