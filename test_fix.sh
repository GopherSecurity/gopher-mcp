#!/bin/bash
# Test script to verify event handling fix

echo "Testing event handling fix for macOS..."
echo "Changed FileTriggerType to Level for macOS/BSD to avoid EV_CLEAR race conditions"
echo ""
echo "Attempting to rebuild and run tests..."

cd /Users/ganan/ws/mcpws/gopher-mcp

# Clean and rebuild
if [ -d "build" ]; then
    rm -rf build
fi
mkdir build
cd build

# Configure with debug mode
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . -j8

# Run specific tests that were hanging
echo ""
echo "Running tests that were previously hanging..."
ctest -R "StdioEchoServerTest|StdioEchoClientTest|HttpCodecFilterTest" --timeout 10 -V

echo "Test complete"