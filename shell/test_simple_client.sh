#!/bin/bash

echo "Starting server..."
../build/examples/mcp/mcp_example_server --transport http --port 9096 > server_simple.log 2>&1 &
SERVER_PID=$!
sleep 3

echo "Starting client with timeout..."
timeout 10 ./build/examples/mcp/mcp_example_client --host localhost --port 9096 --transport http > client_simple.log 2>&1 || true

echo ""
echo "Client output:"
echo "=============="
cat client_simple.log

echo ""
echo "Server log (filtered for client connection):"
echo "============================================"
grep -E "session_1|request.*1|initialize" server_simple.log | head -20

kill $SERVER_PID 2>/dev/null
rm -f server_simple.log client_simple.log