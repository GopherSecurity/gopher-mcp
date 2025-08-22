#!/bin/bash

echo "====================================="
echo "Testing Calculator Tool"
echo "====================================="
echo ""

# Test 1: List tools
echo "[TEST 1] Listing available tools..."
curl -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/list","params":{},"id":1}' \
  -s | python3 -m json.tool

echo ""
echo "[TEST 2] Calling calculator tool - Addition (5 + 3)..."
curl -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"a":5,"b":3,"operation":"add"}},"id":2}' \
  -s | python3 -m json.tool

echo ""
echo "[TEST 3] Calling calculator tool - Multiplication (7 * 6)..."
curl -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"a":7,"b":6,"operation":"multiply"}},"id":3}' \
  -s | python3 -m json.tool

echo ""
echo "[TEST 4] Calling calculator tool - Division (20 / 4)..."
curl -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"a":20,"b":4,"operation":"divide"}},"id":4}' \
  -s | python3 -m json.tool

echo ""
echo "[TEST 5] Calling calculator tool - Subtraction (15 - 8)..."
curl -X POST http://localhost:3000/rpc \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"calculator","arguments":{"a":15,"b":8,"operation":"subtract"}},"id":5}' \
  -s | python3 -m json.tool

echo ""
echo "====================================="
echo "Calculator Tool Test Complete"
echo "====================================="