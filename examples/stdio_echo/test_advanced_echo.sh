#!/bin/bash

# Test script for advanced MCP echo client and server
# Demonstrates all the advanced features:
# - Worker threads
# - Connection pooling
# - Circuit breaker
# - Metrics collection
# - Batch processing

set -e

echo "==================================="
echo "Advanced MCP Echo Test Suite"
echo "==================================="

# Build the examples if not already built
if [ ! -f "./stdio_echo_server_advanced" ] || [ ! -f "./stdio_echo_client_advanced" ]; then
    echo "Building advanced examples..."
    cmake ../.. && make -j4
fi

# Function to run a test
run_test() {
    local test_name="$1"
    local server_args="$2"
    local client_args="$3"
    local expected_success="$4"
    
    echo ""
    echo "Test: $test_name"
    echo "-----------------------------------"
    
    # Start server in background
    echo "Starting server with args: $server_args"
    ./stdio_echo_server_advanced $server_args > server.log 2>&1 &
    SERVER_PID=$!
    
    # Give server time to start
    sleep 1
    
    # Run client
    echo "Running client with args: $client_args"
    ./stdio_echo_client_advanced $client_args 2>&1 | tee client.log
    
    # Check results
    SUCCESS_COUNT=$(grep -c "Request .* succeeded" client.log || true)
    
    echo "Results: $SUCCESS_COUNT successful requests"
    
    if [ "$SUCCESS_COUNT" -ge "$expected_success" ]; then
        echo "✓ Test passed"
    else
        echo "✗ Test failed (expected at least $expected_success successes)"
    fi
    
    # Kill server
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    # Clean up logs
    rm -f server.log client.log
}

# Test 1: Basic sequential requests
run_test "Sequential Requests" \
    "--workers 2 --metrics-interval 5" \
    "--requests 10 --delay 100" \
    8

# Test 2: Batch mode
run_test "Batch Processing" \
    "--workers 4 --metrics-interval 10" \
    "--requests 20 --batch" \
    15

# Test 3: High load test
run_test "High Load" \
    "--workers 4 --no-metrics" \
    "--requests 100 --delay 10" \
    80

# Test 4: Zero delay stress test
run_test "Stress Test" \
    "--workers 2" \
    "--requests 50 --delay 0" \
    40

echo ""
echo "==================================="
echo "All tests completed"
echo "==================================="

# Demonstrate metrics output
echo ""
echo "Running server with metrics enabled for demonstration..."
echo "(Press Ctrl+C to stop)"
echo ""

./stdio_echo_server_advanced --workers 2 --metrics-interval 3 &
SERVER_PID=$!

sleep 1

# Send some requests to generate metrics
./stdio_echo_client_advanced --requests 5 --delay 500 >/dev/null 2>&1 &

# Let it run for a bit to show metrics
sleep 15

# Clean up
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo ""
echo "Test suite complete!"