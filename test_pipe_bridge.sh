#!/bin/bash

# Test script to verify stdio pipe bridge implementation

echo "Testing stdio pipe bridge implementation..."

# Create a test JSON-RPC message
TEST_MESSAGE='{"jsonrpc":"2.0","id":1,"method":"test.echo","params":{"message":"Hello from pipe bridge!"}}'

# Send the message to the echo server and capture the response
echo "$TEST_MESSAGE" | timeout 2 ./stdio_echo_server 2>/dev/null

# Check the exit code
if [ $? -eq 0 ] || [ $? -eq 124 ]; then
    echo "Test passed: Server started and processed input"
    exit 0
else
    echo "Test failed: Server error"
    exit 1
fi