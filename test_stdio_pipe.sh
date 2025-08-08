#!/bin/bash

# Test stdio pipe bridge with echo server

echo "Testing stdio pipe bridge with echo server..."

# Create a test request
REQUEST='{"jsonrpc":"2.0","id":1,"method":"test.echo","params":{"message":"Hello Pipe Bridge!"}}'

# Send request to server and capture response
RESPONSE=$(echo "$REQUEST" | ./stdio_echo_server 2>/dev/null | grep -a '"jsonrpc"' | head -1)

if [ -n "$RESPONSE" ]; then
    echo "SUCCESS: Received response through pipe bridge"
    echo "Request:  $REQUEST"
    echo "Response: $RESPONSE"
    
    # Check if response contains expected fields
    if echo "$RESPONSE" | grep -q '"id":1' && echo "$RESPONSE" | grep -q '"result"'; then
        echo "✓ Response structure is valid"
        exit 0
    else
        echo "✗ Response structure is invalid"
        exit 1
    fi
else
    echo "FAILED: No response received"
    exit 1
fi