#!/bin/bash
# test-integration.sh - Full integration test for MCP Calculator

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK_DIR="$PROJECT_ROOT/sdk/typescript"
EXAMPLE_DIR="$PROJECT_ROOT/examples/typescript/calculator-hybrid"
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_PORT="${PORT:-8080}"
SERVER_HOST="${HOST:-127.0.0.1}"
SERVER_URL="http://$SERVER_HOST:$SERVER_PORT/mcp"
LOG_DIR="/tmp/mcp-integration-test-$$"
VERBOSE="${VERBOSE:-false}"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

echo -e "${MAGENTA}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${MAGENTA}🧪 MCP Calculator Integration Test Suite${NC}"
echo -e "${MAGENTA}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Project Root: $PROJECT_ROOT${NC}"
echo -e "${BLUE}Test Time: $(date)${NC}"

# Create log directory
mkdir -p "$LOG_DIR"

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}🧹 Cleaning up...${NC}"
    
    # Stop server if running
    if [ -f "$LOG_DIR/server.pid" ]; then
        SERVER_PID=$(cat "$LOG_DIR/server.pid")
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            echo -e "${YELLOW}   Stopping server (PID: $SERVER_PID)...${NC}"
            kill "$SERVER_PID" 2>/dev/null || true
            sleep 1
            kill -9 "$SERVER_PID" 2>/dev/null || true
        fi
    fi
    
    # Clean up temp files
    if [ "$VERBOSE" != "true" ]; then
        rm -rf "$LOG_DIR"
    else
        echo -e "${YELLOW}   Logs preserved at: $LOG_DIR${NC}"
    fi
    
    echo -e "${GREEN}✅ Cleanup complete${NC}"
}

trap cleanup EXIT INT TERM

# Test result tracking
record_test() {
    local TEST_NAME="$1"
    local RESULT="$2"  # PASS or FAIL
    local DETAILS="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [ "$RESULT" = "PASS" ]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        echo -e "${GREEN}✅ $TEST_NAME${NC}"
        [ -n "$DETAILS" ] && echo -e "   ${CYAN}$DETAILS${NC}"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        echo -e "${RED}❌ $TEST_NAME${NC}"
        [ -n "$DETAILS" ] && echo -e "   ${YELLOW}$DETAILS${NC}"
    fi
}

# Phase 1: Build and Dependencies Check
echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Phase 1: Build and Dependencies${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Check C++ library
echo -e "\n${YELLOW}Checking C++ library...${NC}"
if [ -f "$BUILD_DIR/src/c_api/libgopher_mcp_c.dylib" ] || \
   [ -f "$BUILD_DIR/src/c_api/libgopher_mcp_c.so" ] || \
   [ -f "$BUILD_DIR/src/c_api/libgopher_mcp_c.0.1.0.dylib" ]; then
    record_test "C++ Library Check" "PASS" "Library found in build directory"
else
    record_test "C++ Library Check" "FAIL" "Library not found - run 'make build'"
    echo -e "${YELLOW}Attempting to build C++ library...${NC}"
    if (cd "$PROJECT_ROOT" && make build > "$LOG_DIR/build.log" 2>&1); then
        record_test "C++ Library Build" "PASS" "Successfully built library"
    else
        record_test "C++ Library Build" "FAIL" "Build failed - check $LOG_DIR/build.log"
    fi
fi

# Check TypeScript dependencies
echo -e "\n${YELLOW}Checking TypeScript dependencies...${NC}"
if [ -d "$SDK_DIR/node_modules" ]; then
    record_test "TypeScript Dependencies" "PASS" "node_modules exists"
else
    echo -e "${YELLOW}Installing TypeScript dependencies...${NC}"
    if (cd "$SDK_DIR" && npm install > "$LOG_DIR/npm-install.log" 2>&1); then
        record_test "TypeScript Dependencies Install" "PASS" "Dependencies installed"
    else
        record_test "TypeScript Dependencies Install" "FAIL" "Installation failed"
    fi
fi

# Phase 2: TypeScript SDK Tests
echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Phase 2: TypeScript SDK Tests${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Run basic usage test
echo -e "\n${YELLOW}Running basic usage test...${NC}"
if (cd "$SDK_DIR" && npx tsx examples/basic-usage.ts > "$LOG_DIR/basic-usage.log" 2>&1); then
    if grep -q "All examples completed successfully" "$LOG_DIR/basic-usage.log"; then
        record_test "Basic Usage Test" "PASS" "All examples completed"
    else
        record_test "Basic Usage Test" "FAIL" "Examples did not complete successfully"
    fi
else
    record_test "Basic Usage Test" "FAIL" "Script execution failed"
fi

# Run integration tests
echo -e "\n${YELLOW}Running SDK integration tests...${NC}"
if (cd "$SDK_DIR" && npx tsx examples/integration-test.ts > "$LOG_DIR/integration.log" 2>&1); then
    if grep -q "ALL TESTS PASSED" "$LOG_DIR/integration.log"; then
        PASS_COUNT=$(grep -c "✅ PASS" "$LOG_DIR/integration.log")
        record_test "SDK Integration Tests" "PASS" "$PASS_COUNT tests passed"
    else
        record_test "SDK Integration Tests" "FAIL" "Some tests failed"
    fi
else
    record_test "SDK Integration Tests" "FAIL" "Test execution failed"
fi

# Phase 3: Server Tests
echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Phase 3: Server Tests${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Start server
echo -e "\n${YELLOW}Starting calculator server...${NC}"
cd "$EXAMPLE_DIR"
PORT=$SERVER_PORT HOST=$SERVER_HOST npx tsx calculator-server-hybrid.ts > "$LOG_DIR/server.log" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$LOG_DIR/server.pid"

# Wait for server startup
RETRY_COUNT=0
MAX_RETRIES=30
SERVER_READY=false

while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    if curl -s -f "http://$SERVER_HOST:$SERVER_PORT/health" > /dev/null 2>&1; then
        SERVER_READY=true
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        break
    fi
    sleep 1
    RETRY_COUNT=$((RETRY_COUNT + 1))
done

if [ "$SERVER_READY" = "true" ]; then
    record_test "Server Startup" "PASS" "Server started on port $SERVER_PORT"
    
    # Test server endpoints
    echo -e "\n${YELLOW}Testing server endpoints...${NC}"
    
    # Health check
    if curl -s "http://$SERVER_HOST:$SERVER_PORT/health" | grep -q "ok"; then
        record_test "Health Endpoint" "PASS" "Returns {\"status\":\"ok\"}"
    else
        record_test "Health Endpoint" "FAIL" "Health check failed"
    fi
    
    # Tools list
    TOOLS_RESPONSE=$(curl -s -X POST "$SERVER_URL" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' 2>/dev/null)
    
    if echo "$TOOLS_RESPONSE" | grep -q "calculate.*memory.*history"; then
        record_test "Tools List" "PASS" "All 3 tools available"
    else
        record_test "Tools List" "FAIL" "Tools not properly listed"
    fi
    
    # Calculation test
    CALC_RESPONSE=$(curl -s -X POST "$SERVER_URL" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"calculate","arguments":{"operation":"add","a":10,"b":20}}}' 2>/dev/null)
    
    if echo "$CALC_RESPONSE" | grep -q "30"; then
        record_test "Calculation (10+20)" "PASS" "Result: 30"
    else
        record_test "Calculation (10+20)" "FAIL" "Incorrect result"
    fi
    
    # Memory test
    STORE_RESPONSE=$(curl -s -X POST "$SERVER_URL" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"memory","arguments":{"action":"store","value":123}}}' 2>/dev/null)
    
    RECALL_RESPONSE=$(curl -s -X POST "$SERVER_URL" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"memory","arguments":{"action":"recall"}}}' 2>/dev/null)
    
    if echo "$RECALL_RESPONSE" | grep -q "123"; then
        record_test "Memory Operations" "PASS" "Store/Recall working"
    else
        record_test "Memory Operations" "FAIL" "Memory not working correctly"
    fi
    
else
    record_test "Server Startup" "FAIL" "Server failed to start - check $LOG_DIR/server.log"
    if [ "$VERBOSE" = "true" ]; then
        echo -e "${RED}Server log tail:${NC}"
        tail -20 "$LOG_DIR/server.log"
    fi
fi

# Phase 4: Client Tests (if server is running)
if [ "$SERVER_READY" = "true" ]; then
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}Phase 4: Client Tests${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    echo -e "\n${YELLOW}Testing client operations...${NC}"
    
    # Test client connection
    CLIENT_TEST_SCRIPT="$LOG_DIR/client-test.txt"
    cat > "$CLIENT_TEST_SCRIPT" <<EOF
calc add 7 3
calc multiply 4 5
calc sqrt 64
memory store 100
memory recall
history 3
quit
EOF
    
    if (cd "$EXAMPLE_DIR" && cat "$CLIENT_TEST_SCRIPT" | timeout 10 npx tsx calculator-client-hybrid.ts "$SERVER_URL" > "$LOG_DIR/client.log" 2>&1); then
        if grep -q "10.*20.*8.*100" "$LOG_DIR/client.log"; then
            record_test "Client Operations" "PASS" "All operations completed"
        else
            record_test "Client Operations" "FAIL" "Some operations failed"
        fi
    else
        record_test "Client Operations" "FAIL" "Client execution failed"
    fi
fi

# Phase 5: Performance Tests
if [ "$SERVER_READY" = "true" ]; then
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}Phase 5: Performance Tests${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    echo -e "\n${YELLOW}Running performance benchmarks...${NC}"
    
    # Measure request latency
    START_TIME=$(date +%s%N)
    for i in {1..10}; do
        curl -s -X POST "$SERVER_URL" \
            -H "Content-Type: application/json" \
            -d "{\"jsonrpc\":\"2.0\",\"id\":$i,\"method\":\"tools/call\",\"params\":{\"name\":\"calculate\",\"arguments\":{\"operation\":\"add\",\"a\":$i,\"b\":$i}}}" \
            > /dev/null 2>&1
    done
    END_TIME=$(date +%s%N)
    
    DURATION=$((($END_TIME - $START_TIME) / 1000000))  # Convert to milliseconds
    AVG_LATENCY=$(($DURATION / 10))
    
    if [ $AVG_LATENCY -lt 100 ]; then
        record_test "Performance (Latency)" "PASS" "Average: ${AVG_LATENCY}ms per request"
    else
        record_test "Performance (Latency)" "FAIL" "Average: ${AVG_LATENCY}ms (>100ms threshold)"
    fi
    
    # Test concurrent requests
    echo -e "\n${YELLOW}Testing concurrent requests...${NC}"
    for i in {1..5}; do
        curl -s -X POST "$SERVER_URL" \
            -H "Content-Type: application/json" \
            -d "{\"jsonrpc\":\"2.0\",\"id\":$((100+i)),\"method\":\"tools/call\",\"params\":{\"name\":\"calculate\",\"arguments\":{\"operation\":\"multiply\",\"a\":$i,\"b\":$i}}}" \
            > "$LOG_DIR/concurrent-$i.log" 2>&1 &
    done
    wait
    
    CONCURRENT_SUCCESS=0
    for i in {1..5}; do
        if [ -f "$LOG_DIR/concurrent-$i.log" ] && grep -q "result" "$LOG_DIR/concurrent-$i.log"; then
            CONCURRENT_SUCCESS=$((CONCURRENT_SUCCESS + 1))
        fi
    done
    
    if [ $CONCURRENT_SUCCESS -eq 5 ]; then
        record_test "Concurrent Requests" "PASS" "All 5 concurrent requests succeeded"
    else
        record_test "Concurrent Requests" "FAIL" "Only $CONCURRENT_SUCCESS/5 requests succeeded"
    fi
fi

# Final Report
echo -e "\n${MAGENTA}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${MAGENTA}📊 Integration Test Results${NC}"
echo -e "${MAGENTA}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

echo -e "\n${CYAN}Test Summary:${NC}"
echo -e "  Total Tests: ${TESTS_RUN}"
echo -e "  ${GREEN}Passed: ${TESTS_PASSED}${NC}"
echo -e "  ${RED}Failed: ${TESTS_FAILED}${NC}"

if [ $TESTS_FAILED -eq 0 ]; then
    SUCCESS_RATE=100
else
    SUCCESS_RATE=$((TESTS_PASSED * 100 / TESTS_RUN))
fi

echo -e "  Success Rate: ${SUCCESS_RATE}%"

echo -e "\n${CYAN}Test Categories:${NC}"
echo -e "  ✓ Build & Dependencies"
echo -e "  ✓ TypeScript SDK"
echo -e "  ✓ Server Endpoints"
[ "$SERVER_READY" = "true" ] && echo -e "  ✓ Client Operations"
[ "$SERVER_READY" = "true" ] && echo -e "  ✓ Performance Tests"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "\n${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}🎉 ALL INTEGRATION TESTS PASSED!${NC}"
    echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    exit 0
else
    echo -e "\n${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${RED}⚠️  SOME TESTS FAILED${NC}"
    echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    if [ "$VERBOSE" = "true" ]; then
        echo -e "\n${YELLOW}Check logs at: $LOG_DIR${NC}"
    else
        echo -e "\n${YELLOW}Run with VERBOSE=true for detailed logs${NC}"
    fi
    exit 1
fi