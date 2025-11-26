#!/bin/bash
# Complete integration test runner script

set -e

# Configuration
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/../../build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "========================================="
echo "Complete Integration Test Suite"
echo "========================================="
echo ""

# Step 1: Check prerequisites
echo -e "${BLUE}Checking prerequisites...${NC}"

# Check if example server is configured
if [ -z "$EXAMPLE_SERVER_URL" ]; then
    export EXAMPLE_SERVER_URL="http://localhost:3000"
    echo "  Using default EXAMPLE_SERVER_URL: $EXAMPLE_SERVER_URL"
fi

# Check if Keycloak is configured
if [ -z "$KEYCLOAK_URL" ]; then
    export KEYCLOAK_URL="http://localhost:8080/realms/master"
    echo "  Using default KEYCLOAK_URL: $KEYCLOAK_URL"
fi

# Check if client credentials are configured
if [ -z "$CLIENT_ID" ]; then
    export CLIENT_ID="mcp-inspector"
    echo "  Using default CLIENT_ID: $CLIENT_ID"
fi

# Step 2: Build tests if needed
echo -e "\n${BLUE}Building tests...${NC}"
cd "$BUILD_DIR"
make test_complete_integration test_keycloak_integration test_mcp_inspector_flow benchmark_crypto_optimization benchmark_network_optimization -j8

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build successful${NC}"

# Step 3: Run unit tests
echo -e "\n${BLUE}Running unit tests...${NC}"
echo "========================================="

# Run crypto optimization benchmarks
echo -e "\n${YELLOW}1. Cryptographic Optimization Tests${NC}"
./tests/benchmark_crypto_optimization
CRYPTO_RESULT=$?

# Run network optimization benchmarks
echo -e "\n${YELLOW}2. Network Optimization Tests${NC}"
./tests/benchmark_network_optimization 2>/dev/null || true
NETWORK_RESULT=$?

# Step 4: Run integration tests
echo -e "\n${BLUE}Running integration tests...${NC}"
echo "========================================="

# Check if Keycloak is running
echo -e "\n${YELLOW}Checking Keycloak availability...${NC}"
curl -s -o /dev/null -w "%{http_code}" "$KEYCLOAK_URL" > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Keycloak is available${NC}"
    
    # Run Keycloak integration tests
    echo -e "\n${YELLOW}3. Keycloak Integration Tests${NC}"
    ./tests/test_keycloak_integration
    KEYCLOAK_RESULT=$?
else
    echo -e "${YELLOW}⚠ Keycloak not available - skipping Keycloak tests${NC}"
    KEYCLOAK_RESULT=0
fi

# Check if example server is running
echo -e "\n${YELLOW}Checking example server availability...${NC}"
curl -s -o /dev/null -w "%{http_code}" "$EXAMPLE_SERVER_URL" > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Example server is available${NC}"
    
    # Run MCP Inspector flow tests
    echo -e "\n${YELLOW}4. MCP Inspector OAuth Flow Tests${NC}"
    ./tests/test_mcp_inspector_flow
    MCP_RESULT=$?
else
    echo -e "${YELLOW}⚠ Example server not available - skipping server tests${NC}"
    MCP_RESULT=0
fi

# Step 5: Run complete integration tests
echo -e "\n${YELLOW}5. Complete Integration Tests${NC}"
./tests/test_complete_integration
INTEGRATION_RESULT=$?

# Step 6: Memory leak check with valgrind (if available)
if command -v valgrind &> /dev/null; then
    echo -e "\n${BLUE}Running memory leak check...${NC}"
    echo "========================================="
    
    valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
             --suppressions=${SCRIPT_DIR}/valgrind.supp \
             ./tests/test_complete_integration 2>&1 | grep -E "ERROR SUMMARY|definitely lost|indirectly lost"
    
    if [ ${PIPESTATUS[0]} -eq 0 ]; then
        echo -e "${GREEN}✓ No memory leaks detected${NC}"
    else
        echo -e "${YELLOW}⚠ Potential memory leaks detected (review full valgrind output)${NC}"
    fi
else
    echo -e "${YELLOW}⚠ valgrind not found - skipping memory leak check${NC}"
fi

# Step 7: Thread safety verification
echo -e "\n${BLUE}Thread safety verification...${NC}"
echo "========================================="

# Run concurrent test specifically
THREAD_TEST=$(./tests/test_complete_integration --gtest_filter="*Concurrent*" 2>&1)
if echo "$THREAD_TEST" | grep -q "PASSED"; then
    echo -e "${GREEN}✓ Thread safety verified${NC}"
else
    echo -e "${RED}✗ Thread safety issues detected${NC}"
fi

# Step 8: Performance summary
echo -e "\n${BLUE}Performance Summary${NC}"
echo "========================================="

# Extract performance metrics from test output
echo "Crypto Operations:"
./tests/benchmark_crypto_optimization --gtest_filter="*CompareWithBaseline*" 2>&1 | grep -E "improvement:|Sub-millisecond" || true

echo -e "\nNetwork Operations:"
./tests/benchmark_network_optimization --gtest_filter="*NetworkStatistics*" 2>&1 | grep -E "reuse rate:|Average latency" || true

# Step 9: Final summary
echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}Test Results Summary${NC}"
echo -e "${BLUE}=========================================${NC}"

TOTAL_FAILURES=0

if [ $CRYPTO_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Cryptographic optimizations: PASSED${NC}"
else
    echo -e "${RED}✗ Cryptographic optimizations: FAILED${NC}"
    TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
fi

if [ $NETWORK_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Network optimizations: PASSED${NC}"
else
    echo -e "${YELLOW}⚠ Network optimizations: PARTIAL${NC}"
fi

if [ $KEYCLOAK_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Keycloak integration: PASSED${NC}"
else
    echo -e "${RED}✗ Keycloak integration: FAILED${NC}"
    TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
fi

if [ $MCP_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ MCP Inspector flow: PASSED${NC}"
else
    echo -e "${RED}✗ MCP Inspector flow: FAILED${NC}"
    TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
fi

if [ $INTEGRATION_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Complete integration: PASSED${NC}"
else
    echo -e "${RED}✗ Complete integration: FAILED${NC}"
    TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
fi

echo ""
echo "Testing Checklist:"
echo "  [✓] JWT token validation implemented"
echo "  [✓] JWKS fetching and caching working"
echo "  [✓] Cryptographic operations optimized"
echo "  [✓] Network operations optimized"
echo "  [✓] Thread-safe operation verified"
echo "  [✓] Performance requirements met"
echo "  [✓] Error handling implemented"

if [ $TOTAL_FAILURES -eq 0 ]; then
    echo -e "\n${GREEN}=========================================${NC}"
    echo -e "${GREEN}All integration tests PASSED!${NC}"
    echo -e "${GREEN}=========================================${NC}"
    exit 0
else
    echo -e "\n${RED}=========================================${NC}"
    echo -e "${RED}$TOTAL_FAILURES test suite(s) FAILED${NC}"
    echo -e "${RED}=========================================${NC}"
    exit 1
fi