#!/bin/bash
# test-server.sh - Start and test the MCP Calculator Server

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK_DIR="$PROJECT_ROOT/sdk/typescript"
EXAMPLE_DIR="$PROJECT_ROOT/examples/typescript/calculator-hybrid"
SERVER_PORT="${PORT:-8080}"
SERVER_HOST="${HOST:-127.0.0.1}"
SERVER_MODE="${MODE:-stateless}"  # stateless or stateful
LOG_FILE="/tmp/mcp-server-$$.log"
PID_FILE="/tmp/mcp-server-$$.pid"

echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}🚀 MCP Calculator Server Test Script${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Function to cleanup on exit
cleanup() {
    echo -e "\n${YELLOW}🧹 Cleaning up...${NC}"
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo -e "${YELLOW}   Stopping server (PID: $PID)...${NC}"
            kill "$PID" 2>/dev/null || true
            sleep 1
            kill -9 "$PID" 2>/dev/null || true
        fi
        rm -f "$PID_FILE"
    fi
    rm -f "$LOG_FILE"
    echo -e "${GREEN}✅ Cleanup complete${NC}"
}

# Set trap for cleanup
trap cleanup EXIT INT TERM

# Check prerequisites
echo -e "\n${BLUE}📋 Checking prerequisites...${NC}"

# Check if C++ library exists
if [ -f "$PROJECT_ROOT/build/src/c_api/libgopher_mcp_c.0.1.0.dylib" ] || \
   [ -f "$PROJECT_ROOT/build/src/c_api/libgopher_mcp_c.so.0.1.0" ]; then
    echo -e "${GREEN}   ✅ C++ library found${NC}"
else
    echo -e "${YELLOW}   ⚠️  C++ library not found (server may fail to start with filters)${NC}"
    echo -e "${YELLOW}   Run 'make build' in project root to build the library${NC}"
fi

# Check if TypeScript dependencies are installed
if [ -d "$SDK_DIR/node_modules" ]; then
    echo -e "${GREEN}   ✅ TypeScript dependencies installed${NC}"
else
    echo -e "${RED}   ❌ TypeScript dependencies not installed${NC}"
    echo -e "${YELLOW}   Installing dependencies...${NC}"
    cd "$SDK_DIR" && npm install
fi

# Check if tsx is available
if [ -f "$SDK_DIR/node_modules/.bin/tsx" ]; then
    echo -e "${GREEN}   ✅ tsx executor found${NC}"
else
    echo -e "${YELLOW}   ⚠️  tsx not found, installing...${NC}"
    cd "$SDK_DIR" && npm install --save-dev tsx
fi

# Start the server
echo -e "\n${BLUE}🚀 Starting Calculator Server...${NC}"
echo -e "${BLUE}   Mode: $SERVER_MODE${NC}"
echo -e "${BLUE}   Host: $SERVER_HOST${NC}"
echo -e "${BLUE}   Port: $SERVER_PORT${NC}"
echo -e "${BLUE}   Log: $LOG_FILE${NC}"

cd "$EXAMPLE_DIR"

# Build server command
if [ "$SERVER_MODE" = "stateful" ]; then
    SERVER_ARGS="--stateful"
else
    SERVER_ARGS=""
fi

# Start server in background
echo -e "\n${YELLOW}Starting server...${NC}"
PORT=$SERVER_PORT HOST=$SERVER_HOST npx tsx calculator-server-hybrid.ts $SERVER_ARGS > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"

# Wait for server to start
echo -e "${YELLOW}Waiting for server to initialize...${NC}"
RETRY_COUNT=0
MAX_RETRIES=30
SERVER_STARTED=false

while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    # Check for successful start
    if grep -q "Server ready and waiting for connections\|Server is running\|MCP Calculator Server is running" "$LOG_FILE" 2>/dev/null; then
        SERVER_STARTED=true
        break
    fi
    
    # Check for known failure patterns
    if grep -q "Failed to start\|mcp_chain_create_from_json_async is not a function\|Failed to start server" "$LOG_FILE" 2>/dev/null; then
        break
    fi
    
    # Check if process is still alive
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    
    sleep 1
    RETRY_COUNT=$((RETRY_COUNT + 1))
    echo -n "."
done
echo ""

# Check if server started successfully
if [ "$SERVER_STARTED" = "true" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    echo -e "${GREEN}✅ Server started successfully (PID: $SERVER_PID)${NC}"
    
    # Show server info
    echo -e "\n${BLUE}📊 Server Information:${NC}"
    grep -E "Server ready|Server Address|Available Tools|Active Filters" "$LOG_FILE" | head -20
    
    # Test server endpoints
    echo -e "\n${BLUE}🧪 Testing server endpoints...${NC}"
    
    # Test health endpoint
    echo -e "\n${YELLOW}1. Testing health check...${NC}"
    HEALTH_RESPONSE=$(curl -s "http://$SERVER_HOST:$SERVER_PORT/health")
    if echo "$HEALTH_RESPONSE" | grep -q "ok"; then
        echo -e "${GREEN}   ✅ Health check passed: $HEALTH_RESPONSE${NC}"
    else
        echo -e "${RED}   ❌ Health check failed${NC}"
    fi
    
    # Test MCP endpoint with tools/list
    echo -e "\n${YELLOW}2. Testing tools/list...${NC}"
    TOOLS_RESPONSE=$(curl -s -X POST "http://$SERVER_HOST:$SERVER_PORT/mcp" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}')
    
    if echo "$TOOLS_RESPONSE" | grep -q "calculate"; then
        echo -e "${GREEN}   ✅ Tools list retrieved successfully${NC}"
        echo "$TOOLS_RESPONSE" | python3 -m json.tool 2>/dev/null | grep -A 2 '"name"' | head -10
    else
        echo -e "${RED}   ❌ Failed to retrieve tools list${NC}"
        echo "   Response: $TOOLS_RESPONSE"
    fi
    
    # Test calculation
    echo -e "\n${YELLOW}3. Testing calculation (5 + 3)...${NC}"
    CALC_RESPONSE=$(curl -s -X POST "http://$SERVER_HOST:$SERVER_PORT/mcp" \
        -H "Content-Type: application/json" \
        -d '{
            "jsonrpc":"2.0",
            "id":2,
            "method":"tools/call",
            "params":{
                "name":"calculate",
                "arguments":{"operation":"add","a":5,"b":3}
            }
        }')
    
    if echo "$CALC_RESPONSE" | grep -q "8\|result"; then
        echo -e "${GREEN}   ✅ Calculation successful${NC}"
        echo "   Result: $(echo "$CALC_RESPONSE" | grep -o '"text":"[^"]*"' | head -1)"
    else
        echo -e "${RED}   ❌ Calculation failed${NC}"
        echo "   Response: $CALC_RESPONSE"
    fi
    
    # Test memory operations
    echo -e "\n${YELLOW}4. Testing memory store...${NC}"
    MEMORY_RESPONSE=$(curl -s -X POST "http://$SERVER_HOST:$SERVER_PORT/mcp" \
        -H "Content-Type: application/json" \
        -d '{
            "jsonrpc":"2.0",
            "id":3,
            "method":"tools/call",
            "params":{
                "name":"memory",
                "arguments":{"action":"store","value":42}
            }
        }')
    
    if echo "$MEMORY_RESPONSE" | grep -q "42\|stored"; then
        echo -e "${GREEN}   ✅ Memory store successful${NC}"
    else
        echo -e "${RED}   ❌ Memory store failed${NC}"
    fi
    
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}✅ Server is running at http://$SERVER_HOST:$SERVER_PORT/mcp${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    echo -e "\n${YELLOW}Server Logs (last 20 lines):${NC}"
    tail -20 "$LOG_FILE"
    
    echo -e "\n${YELLOW}Commands:${NC}"
    echo -e "  View logs:    tail -f $LOG_FILE"
    echo -e "  Stop server:  kill $SERVER_PID"
    echo -e "  Test client:  ./test-client.sh"
    
    echo -e "\n${YELLOW}Press Ctrl+C to stop the server${NC}"
    
    # Keep script running
    wait $SERVER_PID
    
else
    echo -e "${RED}❌ Failed to start server${NC}"
    echo -e "\n${RED}Server logs (last 30 lines):${NC}"
    tail -30 "$LOG_FILE"
    
    # Check if it's the known C++ library issue
    if grep -q "mcp_chain_create_from_json_async is not a function" "$LOG_FILE"; then
        echo -e "\n${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${YELLOW}⚠️  Server failed due to missing C++ library functions${NC}"
        echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${YELLOW}This is expected if the C++ library wasn't built with all dependencies.${NC}"
        echo -e "${YELLOW}To fix this issue:${NC}"
        echo -e "${YELLOW}  1. Install dependencies: brew install yaml-cpp libevent openssl${NC}"
        echo -e "${YELLOW}  2. Rebuild: cd $PROJECT_ROOT && make clean && make build${NC}"
        echo -e "${YELLOW}  3. Try again: ./test-server.sh${NC}"
        echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        
        echo -e "\n${CYAN}Alternative: Use the simple test server${NC}"
        echo -e "  ${GREEN}./test-server-simple.sh${NC} - Works without C++ library"
        echo -e "  ${GREEN}./test-demo.sh${NC} - Demo of test capabilities"
    fi
    
    # Exit with status 2 to indicate known issue
    exit 2
fi