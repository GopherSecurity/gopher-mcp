#!/bin/bash
# Script to run Keycloak integration tests

# Configuration
KEYCLOAK_URL=${KEYCLOAK_URL:-"http://localhost:8080"}
KEYCLOAK_REALM=${KEYCLOAK_REALM:-"master"}
KEYCLOAK_CLIENT_ID=${KEYCLOAK_CLIENT_ID:-"test-client"}
KEYCLOAK_CLIENT_SECRET=${KEYCLOAK_CLIENT_SECRET:-"test-secret"}
KEYCLOAK_USERNAME=${KEYCLOAK_USERNAME:-"test-user"}
KEYCLOAK_PASSWORD=${KEYCLOAK_PASSWORD:-"test-password"}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "Keycloak Integration Test Runner"
echo "========================================="
echo ""
echo "Configuration:"
echo "  Keycloak URL: $KEYCLOAK_URL"
echo "  Realm: $KEYCLOAK_REALM"
echo "  Client ID: $KEYCLOAK_CLIENT_ID"
echo ""

# Check if Keycloak is running
echo -n "Checking Keycloak availability... "
if curl -s -o /dev/null -w "%{http_code}" "$KEYCLOAK_URL/health" | grep -q "200\|404"; then
    echo -e "${GREEN}Available${NC}"
else
    echo -e "${RED}Not available${NC}"
    echo ""
    echo -e "${YELLOW}Keycloak appears to be unavailable at $KEYCLOAK_URL${NC}"
    echo "To run these tests, you need a running Keycloak instance."
    echo ""
    echo "You can start a local Keycloak using Docker:"
    echo "  docker run -d --name keycloak -p 8080:8080 \\"
    echo "    -e KEYCLOAK_ADMIN=admin \\"
    echo "    -e KEYCLOAK_ADMIN_PASSWORD=admin \\"
    echo "    quay.io/keycloak/keycloak:latest start-dev"
    echo ""
    echo "Then create a test client and user in the Keycloak admin console."
    echo ""
    echo "Tests will be skipped."
    exit 0
fi

# Run the tests
echo ""
echo "Running integration tests..."
echo "========================================="

# Export environment variables
export KEYCLOAK_URL
export KEYCLOAK_REALM
export KEYCLOAK_CLIENT_ID
export KEYCLOAK_CLIENT_SECRET
export KEYCLOAK_USERNAME
export KEYCLOAK_PASSWORD

# Run test executable
TEST_BINARY="../../build/tests/test_keycloak_integration"

if [ ! -f "$TEST_BINARY" ]; then
    echo -e "${RED}Test binary not found at $TEST_BINARY${NC}"
    echo "Please build the tests first:"
    echo "  cd ../../build && make test_keycloak_integration"
    exit 1
fi

# Run with verbose output
$TEST_BINARY --gtest_color=yes

TEST_RESULT=$?

echo ""
echo "========================================="
if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
else
    echo -e "${RED}Some tests failed!${NC}"
fi

exit $TEST_RESULT