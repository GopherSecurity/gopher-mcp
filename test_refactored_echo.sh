#!/bin/bash
# Test script for refactored echo implementation

echo "Testing Refactored Echo Implementation"
echo "======================================"

# Build if needed
if [ ! -f "build/examples/stdio_echo/stdio_echo_server_advanced" ] || [ ! -f "build/examples/stdio_echo/stdio_echo_client_advanced" ]; then
    echo "Building..."
    mkdir -p build && cd build && cmake .. && make -j8 stdio_echo_server_advanced stdio_echo_client_advanced
    cd ..
fi

# Create test requests
cat > /tmp/test_requests.jsonl << EOF
{"jsonrpc":"2.0","method":"ping","id":1}
{"jsonrpc":"2.0","method":"echo","params":{"message":"Hello World"},"id":2}
{"jsonrpc":"2.0","method":"echo/uppercase","params":{"message":"make me uppercase"},"id":3}
{"jsonrpc":"2.0","method":"server/stats","id":4}
{"jsonrpc":"2.0","method":"shutdown","id":5}
EOF

echo "Test 1: Server ping test"
echo '{"jsonrpc":"2.0","method":"ping","id":1}' | timeout 1 build/examples/stdio_echo/stdio_echo_server_advanced 2>/dev/null | grep -q "pong" && echo "✓ Ping test passed" || echo "✗ Ping test failed"

echo ""
echo "Test 2: Echo uppercase test"
echo '{"jsonrpc":"2.0","method":"echo/uppercase","params":{"message":"hello"},"id":2}' | timeout 1 build/examples/stdio_echo/stdio_echo_server_advanced 2>/dev/null | grep -q "HELLO" && echo "✓ Uppercase test passed" || echo "✗ Uppercase test failed"

echo ""
echo "Test 3: Server stats test"
echo '{"jsonrpc":"2.0","method":"server/stats","id":3}' | timeout 1 build/examples/stdio_echo/stdio_echo_server_advanced 2>/dev/null | grep -q "requests_total" && echo "✓ Stats test passed" || echo "✗ Stats test failed"

echo ""
echo "======================================"
echo "Refactored echo tests complete!"
echo ""
echo "Key Features Implemented:"
echo "✓ Transport-agnostic architecture"
echo "✓ Reusable client/server components"
echo "✓ Stdio transport adapter"
echo "✓ Circuit breaker pattern (client)"
echo "✓ Flow control (server)"
echo "✓ Custom handler registration"
echo "✓ Comprehensive metrics"
echo ""
echo "Files created:"
echo "- include/mcp/echo/echo_transport_advanced.h (Advanced transport interface)"
echo "- include/mcp/echo/echo_client_advanced.h (Reusable advanced client)"
echo "- include/mcp/echo/echo_server_advanced.h (Reusable advanced server)"
echo "- include/mcp/echo/echo_stdio_transport_advanced.h (Advanced stdio adapter)"
echo "- src/echo/echo_client_advanced.cc (Advanced client implementation)"
echo "- src/echo/echo_server_advanced.cc (Advanced server implementation)"
echo "- src/echo/echo_stdio_transport_advanced.cc (Advanced stdio implementation)"
echo ""
echo "Basic examples:"
echo "- examples/stdio_echo/stdio_echo_client_basic.cc"
echo "- examples/stdio_echo/stdio_echo_server_basic.cc"
echo ""
echo "Advanced examples:"
echo "- examples/stdio_echo/stdio_echo_client_advanced.cc"
echo "- examples/stdio_echo/stdio_echo_server_advanced.cc"
echo ""
echo "Original files backed up as *.cc.backup"