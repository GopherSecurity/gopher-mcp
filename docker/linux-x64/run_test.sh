#!/bin/bash

# Simple test runner for libgopher_mcp_auth on Linux

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Copy library to test directory
if [ -f "$SCRIPT_DIR/../libgopher_mcp_auth.so.0.1.0" ]; then
    cp "$SCRIPT_DIR/../libgopher_mcp_auth.so.0.1.0" "$SCRIPT_DIR/libgopher_mcp_auth.so"
elif [ -f "$SCRIPT_DIR/../libgopher_mcp_auth.so" ]; then
    cp "$SCRIPT_DIR/../libgopher_mcp_auth.so" "$SCRIPT_DIR/libgopher_mcp_auth.so"
else
    echo "❌ Library not found in parent directory"
    exit 1
fi

# Run the test
cd "$SCRIPT_DIR"
node simple-test.js