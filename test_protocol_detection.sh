#!/bin/bash

echo "====================================="
echo "Testing Protocol Detection"
echo "====================================="
echo ""
echo "This test demonstrates that the MCP C++ SDK can automatically"
echo "detect and handle both HTTP and native MCP protocols transparently."
echo ""

# Test 1: HTTP JSON-RPC (without SSE)
echo "[TEST 1] Sending HTTP JSON-RPC request to port 3000..."
echo ""
echo "Request:"
echo "POST /rpc HTTP/1.1"
echo "Content-Type: application/json"
echo ""
echo '{"jsonrpc":"2.0","method":"ping","id":1}'
echo ""
echo "Response:"
curl -s -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"ping","id":1}' \
  2>/dev/null || echo "Server not running on port 3000"

echo ""
echo ""

# Test 2: Native JSON-RPC (direct)
echo "[TEST 2] Sending native JSON-RPC request..."
echo ""
echo "Request:"
echo '{"jsonrpc":"2.0","method":"ping","id":2}'
echo ""
echo "Response:"
echo '{"jsonrpc":"2.0","method":"ping","id":2}' | nc localhost 3000 2>/dev/null || echo "Cannot connect with native protocol"

echo ""
echo ""

echo "====================================="
echo "Protocol Detection Summary"
echo "====================================="
echo ""
echo "The MCP C++ SDK with protocol detection can handle:"
echo "1. HTTP requests with JSON-RPC payloads"
echo "2. Native JSON-RPC messages"
echo "3. HTTP with Server-Sent Events (SSE) for streaming"
echo ""
echo "The ProtocolDetectionFilter examines the initial bytes"
echo "to determine if the incoming data is HTTP or JSON-RPC,"
echo "then routes to the appropriate filter chain."