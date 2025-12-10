#!/bin/bash
# Script to run MCP Inspector OAuth flow tests

# Configuration
MCP_SERVER_URL=${MCP_SERVER_URL:-"http://localhost:3000"}
AUTH_SERVER_URL=${AUTH_SERVER_URL:-"http://localhost:8080/realms/master"}
OAUTH_CLIENT_ID=${OAUTH_CLIENT_ID:-"mcp-inspector"}
OAUTH_CLIENT_SECRET=${OAUTH_CLIENT_SECRET:-"mcp-secret"}
OAUTH_REDIRECT_URI=${OAUTH_REDIRECT_URI:-"http://localhost:5173/auth/callback"}
REQUIRE_AUTH_ON_CONNECT=${REQUIRE_AUTH_ON_CONNECT:-"true"}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "========================================="
echo "MCP Inspector OAuth Flow Test Runner"
echo "========================================="
echo ""
echo "Configuration:"
echo "  MCP Server URL: $MCP_SERVER_URL"
echo "  Auth Server URL: $AUTH_SERVER_URL"
echo "  Client ID: $OAUTH_CLIENT_ID"
echo "  Redirect URI: $OAUTH_REDIRECT_URI"
echo "  Require Auth on Connect: $REQUIRE_AUTH_ON_CONNECT"
echo ""

# Export environment variables
export MCP_SERVER_URL
export AUTH_SERVER_URL
export OAUTH_CLIENT_ID
export OAUTH_CLIENT_SECRET
export OAUTH_REDIRECT_URI
export REQUIRE_AUTH_ON_CONNECT

# Run test executable
TEST_BINARY="../../build/tests/test_mcp_inspector_flow"

if [ ! -f "$TEST_BINARY" ]; then
    echo -e "${RED}Test binary not found at $TEST_BINARY${NC}"
    echo "Please build the tests first:"
    echo "  cd ../../build && make test_mcp_inspector_flow"
    exit 1
fi

echo "Running OAuth flow tests..."
echo "========================================="

# Run tests
$TEST_BINARY --gtest_color=yes

TEST_RESULT=$?

echo ""
echo "========================================="
echo "Test Summary:"
echo ""

if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}✅ All OAuth flow tests passed!${NC}"
    echo ""
    echo "The authentication flow is working correctly:"
    echo "  • Connect triggers authentication when required"
    echo "  • Tokens are properly validated"
    echo "  • Authorization headers are correctly formatted"
    echo "  • Tool scopes are enforced"
    echo "  • Token expiration is handled"
else
    echo -e "${RED}❌ Some OAuth flow tests failed!${NC}"
    echo ""
    echo "Please check:"
    echo "  • OAuth server configuration"
    echo "  • Client credentials"
    echo "  • JWKS endpoint accessibility"
fi

echo "========================================="

# Additional validation info
if [ "$REQUIRE_AUTH_ON_CONNECT" == "true" ]; then
    echo ""
    echo -e "${BLUE}ℹ️  Authentication Required Mode${NC}"
    echo "MCP Inspector will require authentication on connect."
    echo "This ensures all tools are protected by default."
else
    echo ""
    echo -e "${YELLOW}⚠️  Authentication Optional Mode${NC}"
    echo "MCP Inspector allows connection without authentication."
    echo "Individual tools may still require authentication."
fi

exit $TEST_RESULT