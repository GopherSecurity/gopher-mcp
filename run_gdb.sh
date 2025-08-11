#\!/bin/bash
# Start server in background
./build/examples/mcp/mcp_example_server "http://localhost:8080" > server.log 2>&1 &
SERVER_PID=$\!
sleep 2

# Run client under gdb
gdb -batch -ex "run http://localhost:8080 --demo" -ex "bt" -ex "quit" ./build/examples/mcp/mcp_example_client 2>&1

# Kill server
kill $SERVER_PID 2>/dev/null
