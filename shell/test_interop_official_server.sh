#!/bin/bash
#
# Run this project's client against a server built on the official MCP
# TypeScript SDK.
#
# Everything else in the test tree checks this project against itself.
# Two halves written from one reading of the spec agree with each other
# by construction, including where the reading was wrong. This is the
# other side of that question, so it needs a toolchain that is not this
# project's: Node, and an install of the reference server.
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
SERVER_DIR="$REPO_ROOT/tests/interop/reference-server-ts"
TEST_BIN="$REPO_ROOT/build/tests/test_client_vs_official_server"

cleanup() {
    # The suite starts each server in its own process group and stops it
    # again, so there is normally nothing here. This is for the run that
    # was interrupted partway.
    pkill -f "node server.ts --port" 2>/dev/null
}
trap cleanup EXIT

echo "====================================="
echo "Interop: this client vs the official SDK's server"
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
echo -e "${GREEN}Installing the reference server (npm ci)...${NC}"
if ! (cd "$SERVER_DIR" && npm ci --no-audit --no-fund); then
    echo -e "${RED}Could not install the reference server.${NC}"
    exit 1
fi

echo -e "${GREEN}Building the interop suite...${NC}"
if ! cmake --build "$REPO_ROOT/build" --target test_client_vs_official_server; then
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
echo "reference server: it is the implementation neither of us wrote."
exit 1
