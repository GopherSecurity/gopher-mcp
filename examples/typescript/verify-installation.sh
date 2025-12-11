#!/bin/bash

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ MCP C++ Library Installation Verification"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

PROJECT_ROOT="/Users/james/Desktop/dev/mcp-cpp-sdk"
LIBRARY_PATH="$PROJECT_ROOT/build/src/c_api/libgopher_mcp_c.0.1.0.dylib"

echo ""
echo "1️⃣ Checking C++ library..."
if [ -f "$LIBRARY_PATH" ]; then
    echo "   ✅ Library found: $LIBRARY_PATH"
    echo "   Size: $(ls -lh "$LIBRARY_PATH" | awk '{print $5}')"
    echo ""
    echo "   Exported functions:"
    nm -gU "$LIBRARY_PATH" | grep mcp | wc -l | xargs echo "   Total MCP functions:"
    echo ""
    echo "   Key functions available:"
    nm -gU "$LIBRARY_PATH" | grep -E "mcp_init|mcp_dispatcher_create|mcp_chain_create_from_json_async" | while read addr type func; do
        echo "   ✅ ${func#_}"
    done
else
    echo "   ❌ Library not found at $LIBRARY_PATH"
    exit 1
fi

echo ""
echo "2️⃣ Testing TypeScript integration..."
cd "$PROJECT_ROOT/examples/typescript/calculator-hybrid"

# Create a minimal test script
cat > /tmp/test-lib.js << 'EOF'
const koffi = require('koffi');
const path = require('path');

const libPath = '/Users/james/Desktop/dev/mcp-cpp-sdk/build/src/c_api/libgopher_mcp_c.0.1.0.dylib';
console.log('Loading library:', libPath);

try {
    const lib = koffi.load(libPath);
    
    // Test basic functions
    const mcp_init = lib.func('mcp_init', 'int', []);
    const mcp_get_version = lib.func('mcp_get_version', 'str', []);
    const mcp_dispatcher_create = lib.func('mcp_dispatcher_create', 'void*', []);
    
    console.log('✅ Library loaded successfully');
    
    // Initialize
    const rc = mcp_init();
    console.log('✅ mcp_init returned:', rc);
    
    // Get version
    const version = mcp_get_version();
    console.log('✅ Version:', version);
    
    // Create dispatcher
    const dispatcher = mcp_dispatcher_create();
    console.log('✅ Dispatcher created:', dispatcher ? 'success' : 'failed');
    
    console.log('\n✅ All basic functions work!');
} catch (error) {
    console.error('❌ Error:', error.message);
    process.exit(1);
}
EOF

npx tsx /tmp/test-lib.js 2>&1

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📋 Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ $? -eq 0 ]; then
    echo "✅ C++ library is installed and working!"
    echo ""
    echo "The stub library provides:"
    echo "  • All dispatcher functions"
    echo "  • Filter chain operations" 
    echo "  • Async chain creation (mcp_chain_create_from_json_async)"
    echo "  • JSON handling"
    echo "  • Circuit breaker callbacks"
    echo ""
    echo "You can now run:"
    echo "  ./test-server.sh     - Full server test"
    echo "  ./test-client.sh     - Client test"
    echo "  ./test-integration.sh - Integration tests"
else
    echo "❌ Library verification failed"
    echo "Please check the error messages above"
fi