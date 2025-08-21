#!/bin/bash
set -e

echo "=== MCP HTTP Endpoints Robustness Test ==="
echo

# Clean up any existing server
pkill -f mcp_example_server 2>/dev/null || true
sleep 1

echo "Starting MCP server..."
./build/examples/mcp/mcp_example_server > test.log 2>&1 &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Give server time to start
sleep 2

echo
echo "Test 1: Multiple sequential requests..."
for i in {1..5}; do
    echo -n "  Request $i: "
    if curl -s http://localhost:3000/health | grep -q "healthy"; then
        echo "PASS"
    else
        echo "FAIL"
        kill $SERVER_PID 2>/dev/null || true
        exit 1
    fi
done

echo
echo "Test 2: Mixed endpoint requests..."
for endpoint in health info health info health; do
    echo -n "  /$endpoint: "
    if curl -s http://localhost:3000/$endpoint > /dev/null; then
        echo "OK"
    else
        echo "FAIL"
        kill $SERVER_PID 2>/dev/null || true
        exit 1
    fi
done

echo
echo "Test 3: Rapid fire requests..."
for i in {1..10}; do
    curl -s http://localhost:3000/health > /dev/null &
done
wait
echo "  10 concurrent requests: COMPLETE"

# Give time to see if it crashes
sleep 2

# Check if server is still running
if kill -0 $SERVER_PID 2>/dev/null; then
    echo
    echo "=== ALL TESTS PASSED ==="
    echo "Server remained stable throughout testing"
    kill $SERVER_PID
    wait $SERVER_PID 2>/dev/null || true
else
    echo
    echo "=== TEST FAILED ==="
    echo "Server crashed during testing"
    exit 1
fi