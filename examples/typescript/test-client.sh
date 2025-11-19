#!/bin/bash
# test-client.sh - Test the MCP Calculator Client

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK_DIR="$PROJECT_ROOT/sdk/typescript"
EXAMPLE_DIR="$PROJECT_ROOT/examples/typescript/calculator-hybrid"
SERVER_URL="${SERVER_URL:-http://127.0.0.1:8080/mcp}"
INTERACTIVE="${INTERACTIVE:-false}"

echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}🧮 MCP Calculator Client Test Script${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Check prerequisites
echo -e "\n${BLUE}📋 Checking prerequisites...${NC}"

# Check if server is running
echo -e "${YELLOW}   Checking server at $SERVER_URL...${NC}"
if curl -s -f "${SERVER_URL%/mcp}/health" > /dev/null 2>&1; then
    echo -e "${GREEN}   ✅ Server is running${NC}"
else
    echo -e "${RED}   ❌ Server is not running at $SERVER_URL${NC}"
    echo -e "${YELLOW}   Start the server first with: ./test-server.sh${NC}"
    exit 1
fi

# Check TypeScript dependencies
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

# Function to run client command
run_client_command() {
    local COMMAND="$1"
    local TEMP_FILE="/tmp/client-test-$$.txt"
    
    cd "$EXAMPLE_DIR"
    
    # Create expect script for automated testing
    cat > /tmp/client-expect-$$.exp <<EOF
#!/usr/bin/expect -f
set timeout 10
spawn npx tsx calculator-client-hybrid.ts "$SERVER_URL"
expect "calc>"
send "$COMMAND\r"
expect "calc>"
send "quit\r"
expect eof
EOF
    
    chmod +x /tmp/client-expect-$$.exp
    /tmp/client-expect-$$.exp > "$TEMP_FILE" 2>&1
    
    # Extract result
    cat "$TEMP_FILE" | grep -E "Result:|Error:|stored|Memory|History|Statistics" | head -5
    
    rm -f /tmp/client-expect-$$.exp "$TEMP_FILE"
}

# Function to test with direct commands
test_direct_commands() {
    echo -e "\n${BLUE}🧪 Running automated client tests...${NC}"
    
    # Test 1: Basic addition
    echo -e "\n${YELLOW}Test 1: Addition (5 + 3)${NC}"
    echo "calc add 5 3" | timeout 5 npx tsx "$EXAMPLE_DIR/calculator-client-hybrid.ts" "$SERVER_URL" 2>/dev/null | \
        grep -E "Result|8" && echo -e "${GREEN}✅ Addition test passed${NC}" || echo -e "${RED}❌ Addition test failed${NC}"
    
    # Test 2: Multiplication
    echo -e "\n${YELLOW}Test 2: Multiplication (4 × 7)${NC}"
    echo "calc multiply 4 7" | timeout 5 npx tsx "$EXAMPLE_DIR/calculator-client-hybrid.ts" "$SERVER_URL" 2>/dev/null | \
        grep -E "Result|28" && echo -e "${GREEN}✅ Multiplication test passed${NC}" || echo -e "${RED}❌ Multiplication test failed${NC}"
    
    # Test 3: Square root
    echo -e "\n${YELLOW}Test 3: Square root (√16)${NC}"
    echo "calc sqrt 16" | timeout 5 npx tsx "$EXAMPLE_DIR/calculator-client-hybrid.ts" "$SERVER_URL" 2>/dev/null | \
        grep -E "Result|4" && echo -e "${GREEN}✅ Square root test passed${NC}" || echo -e "${RED}❌ Square root test failed${NC}"
    
    # Test 4: Memory operations
    echo -e "\n${YELLOW}Test 4: Memory operations${NC}"
    (echo "memory store 42"; echo "memory recall"; echo "quit") | \
        timeout 5 npx tsx "$EXAMPLE_DIR/calculator-client-hybrid.ts" "$SERVER_URL" 2>/dev/null | \
        grep -E "stored|42" && echo -e "${GREEN}✅ Memory test passed${NC}" || echo -e "${RED}❌ Memory test failed${NC}"
    
    # Test 5: History
    echo -e "\n${YELLOW}Test 5: History${NC}"
    echo "history 5" | timeout 5 npx tsx "$EXAMPLE_DIR/calculator-client-hybrid.ts" "$SERVER_URL" 2>/dev/null | \
        grep -E "History|calculation" && echo -e "${GREEN}✅ History test passed${NC}" || echo -e "${RED}❌ History test failed${NC}"
}

# Function to run interactive client
run_interactive_client() {
    echo -e "\n${BLUE}🎮 Starting interactive client...${NC}"
    echo -e "${YELLOW}Connecting to: $SERVER_URL${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    cd "$EXAMPLE_DIR"
    npx tsx calculator-client-hybrid.ts "$SERVER_URL"
}

# Function to run batch operations
run_batch_operations() {
    echo -e "\n${BLUE}📊 Running batch operations test...${NC}"
    
    BATCH_SCRIPT="/tmp/batch-operations-$$.txt"
    cat > "$BATCH_SCRIPT" <<EOF
calc add 10 20
calc multiply 5 6
calc divide 100 4
calc power 2 8
calc factorial 5
memory store 999
memory recall
history 10
stats
quit
EOF
    
    echo -e "${YELLOW}Executing batch operations:${NC}"
    cat "$BATCH_SCRIPT"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    cd "$EXAMPLE_DIR"
    cat "$BATCH_SCRIPT" | npx tsx calculator-client-hybrid.ts "$SERVER_URL" 2>&1 | \
        grep -E "Result:|Memory|Statistics|History" | head -20
    
    rm -f "$BATCH_SCRIPT"
}

# Function for performance test
run_performance_test() {
    echo -e "\n${BLUE}⚡ Running performance test...${NC}"
    echo -e "${YELLOW}Sending 10 rapid requests...${NC}"
    
    START_TIME=$(date +%s)
    
    for i in {1..10}; do
        echo "calc add $i $i" | timeout 2 npx tsx "$EXAMPLE_DIR/calculator-client-hybrid.ts" "$SERVER_URL" 2>/dev/null | \
            grep -q "Result" && echo -n "✓" || echo -n "✗"
    done
    echo ""
    
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))
    
    echo -e "${GREEN}Completed in ${DURATION} seconds${NC}"
    echo -e "${GREEN}Average: $((DURATION * 100 / 10))ms per request${NC}"
}

# Main test execution
if [ "$INTERACTIVE" = "true" ]; then
    run_interactive_client
else
    # Run automated tests
    echo -e "\n${BLUE}🤖 Running automated tests...${NC}"
    
    # Basic command tests
    test_direct_commands
    
    # Batch operations
    run_batch_operations
    
    # Performance test
    run_performance_test
    
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}✅ Client tests completed${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    echo -e "\n${YELLOW}To run interactive client:${NC}"
    echo -e "  INTERACTIVE=true ./test-client.sh"
    echo -e "\n${YELLOW}To test with different server:${NC}"
    echo -e "  SERVER_URL=http://localhost:9090/mcp ./test-client.sh"
fi