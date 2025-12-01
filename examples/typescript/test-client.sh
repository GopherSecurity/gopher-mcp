#\!/bin/bash

# Test client with proper timing
SERVER_URL="${1:-http://127.0.0.1:8080/mcp}"
OUTPUT_FILE="${2:-/tmp/client_test.log}"

echo "Testing client with server at: $SERVER_URL"
echo "Output will be saved to: $OUTPUT_FILE"

# Use a simpler approach - just send commands with delays
{
    sleep 3  # Wait for connection
    echo "calc add 7 3"
    sleep 1
    echo "calc multiply 4 5"
    sleep 1
    echo "calc sqrt 64"
    sleep 1
    echo "memory store 100"
    sleep 1
    echo "memory recall"
    sleep 1
    echo "history 3"
    sleep 1
    echo "quit"
} | npx tsx calculator-client-hybrid.ts "$SERVER_URL" > "$OUTPUT_FILE" 2>&1 &

CLIENT_PID=$\!

# Wait up to 15 seconds for completion
COUNTER=0
while [ $COUNTER -lt 15 ]; do
    if \! kill -0 $CLIENT_PID 2>/dev/null; then
        echo "Client process completed"
        break
    fi
    sleep 1
    COUNTER=$((COUNTER + 1))
done

# Kill if still running
if kill -0 $CLIENT_PID 2>/dev/null; then
    echo "Killing stuck client process"
    kill $CLIENT_PID 2>/dev/null
    sleep 1
    kill -9 $CLIENT_PID 2>/dev/null || true
fi

# Display output
echo "=== Client Output ==="
cat "$OUTPUT_FILE"
echo "===================="

# Check for expected results in output
FOUND_RESULTS=0
if grep -q "= 10" "$OUTPUT_FILE"; then
    echo "✅ Found: 7 + 3 = 10"
    FOUND_RESULTS=$((FOUND_RESULTS + 1))
fi
if grep -q "= 20" "$OUTPUT_FILE"; then
    echo "✅ Found: 4 * 5 = 20"
    FOUND_RESULTS=$((FOUND_RESULTS + 1))
fi
if grep -q "= 8" "$OUTPUT_FILE"; then
    echo "✅ Found: sqrt(64) = 8"
    FOUND_RESULTS=$((FOUND_RESULTS + 1))
fi
if grep -E -q "(Stored 100|Memory value: 100)" "$OUTPUT_FILE"; then
    echo "✅ Found: Memory operations with 100"
    FOUND_RESULTS=$((FOUND_RESULTS + 1))
fi

echo ""
echo "Results found: $FOUND_RESULTS / 4"

if [ $FOUND_RESULTS -ge 3 ]; then
    echo "✅ Client test PASSED\!"
    exit 0
else
    echo "❌ Client test FAILED - missing results"
    exit 1
fi
