#!/bin/bash

# Test MCP client and server with different modes

echo "============================================="
echo "Testing MCP Example Client and Server"
echo "============================================="

# Function to run a test
run_test() {
    local test_name="$1"
    local server_args="$2"
    local client_args="$3"
    local timeout="${4:-10}"
    
    echo ""
    echo "TEST: $test_name"
    echo "---------------------------------------------"
    echo "Server args: $server_args"
    echo "Client args: $client_args"
    echo ""
    
    # Start server
    ./build/examples/mcp/mcp_example_server $server_args &
    SERVER_PID=$!
    
    # Give server time to start
    sleep 2
    
    # Check if server is running
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "ERROR: Server failed to start"
        return 1
    fi
    
    # Run client with timeout
    (
        ./build/examples/mcp/mcp_example_client $client_args &
        CLIENT_PID=$!
        
        # Wait for client or timeout
        COUNT=0
        while [ $COUNT -lt $timeout ]; do
            if ! kill -0 $CLIENT_PID 2>/dev/null; then
                # Client finished
                wait $CLIENT_PID
                CLIENT_EXIT=$?
                echo "Client exited with code: $CLIENT_EXIT"
                break
            fi
            sleep 1
            COUNT=$((COUNT + 1))
        done
        
        # Kill client if still running
        if kill -0 $CLIENT_PID 2>/dev/null; then
            echo "Timeout reached, killing client..."
            kill $CLIENT_PID 2>/dev/null
        fi
    )
    
    # Kill server
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    
    echo "Test completed"
    echo ""
}

# Clean up any existing processes
pkill -f mcp_example_server 2>/dev/null
pkill -f mcp_example_client 2>/dev/null
sleep 1

# Test 1: HTTP transport without demo
run_test "HTTP Transport (No Demo)" \
    "--port 3001 --transport http" \
    "--host localhost --port 3001 --transport http --quiet" \
    5

# Test 2: HTTP transport with demo
run_test "HTTP Transport (With Demo)" \
    "--port 3002 --transport http" \
    "--host localhost --port 3002 --transport http --demo --quiet" \
    10

# Test 3: HTTP with verbose logging (to see protocol details)
run_test "HTTP Transport (Verbose)" \
    "--port 3003 --transport http --verbose" \
    "--host localhost --port 3003 --transport http --verbose" \
    5

# Clean up
pkill -f mcp_example_server 2>/dev/null
pkill -f mcp_example_client 2>/dev/null

echo "============================================="
echo "All tests completed"
echo "============================================="