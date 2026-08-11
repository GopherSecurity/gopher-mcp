#!/bin/bash
#
# Run this project's server against the client of the official MCP
# TypeScript SDK.
#
# The other direction from test_interop_official_server.sh, and the
# stricter one: that client validates every message against the schema
# and is exact about statuses, headers and session semantics, so a
# disagreement it reports is evidence about this server.
#
#   PORT is not used here — the suite picks its own free ports, one per
#   scenario, so that a stale listener from a previous run cannot make a
#   test pass or fail for the wrong reason.

set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER_DIR="$REPO_ROOT/tests/interop/official-client-ts"
TEST_BIN="$REPO_ROOT/build/tests/test_official_client_vs_server"

cleanup() {
    # The suite starts each server in its own process group and stops it
    # again, so there is normally nothing here. This is for the run that
    # was interrupted partway.
    pkill -f "gopher_interop_server --port" 2>/dev/null
    pkill -f "node client.ts --url" 2>/dev/null
}
trap cleanup EXIT

echo "====================================="
echo "Interop: this server vs the official SDK's client"
echo "====================================="
echo ""

if ! command -v node > /dev/null 2>&1; then
    echo -e "${YELLOW}Skipping: node is not installed.${NC}"
    echo "The interop suite needs Node 22.6 or newer, which is where"
    echo "running TypeScript without a build step became possible."
    exit 0
fi

if ! command -v npm > /dev/null 2>&1; then
    echo -e "${YELLOW}Skipping: npm is not installed.${NC}"
    exit 0
fi

echo -e "${GREEN}Node $(node --version), npm $(npm --version)${NC}"

# npm ci, never npm install: the point of the pinned version and the
# committed lock is that this measures against a fixed thing. `npm
# install` would quietly move it.
echo -e "${GREEN}Installing the driver (npm ci)...${NC}"
if ! (cd "$DRIVER_DIR" && npm ci --no-audit --no-fund); then
    echo -e "${RED}Could not install the driver.${NC}"
    exit 1
fi

echo -e "${GREEN}Building the server and the suite...${NC}"
if ! cmake --build "$REPO_ROOT/build" --target test_official_client_vs_server; then
    echo -e "${RED}Could not build the interop suite.${NC}"
    echo "Configure the project first: cmake -B build"
    exit 1
fi

if [ ! -x "$TEST_BIN" ]; then
    echo -e "${RED}Error: the interop suite is not at $TEST_BIN${NC}"
    exit 1
fi

echo -e "${GREEN}Running...${NC}"
echo ""
if "$TEST_BIN" "$@"; then
    echo ""
    echo -e "${GREEN}Interop passed.${NC}"
    exit 0
fi

echo ""
echo -e "${RED}Interop failed.${NC}"
echo "A failure here is evidence about this project, not about the"
echo "official client: it is the implementation neither of us wrote."
exit 1
