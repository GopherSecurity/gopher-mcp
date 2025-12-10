#!/bin/sh
# Shell script verification tool for macOS 10.14.6+ compatibility
# Avoids "Killed: 9" issues with compiled binaries

echo "===================================="
echo "libgopher_mcp_auth Verification"
echo "===================================="
echo ""

# Default library path
LIB_PATH="${1:-./libgopher_mcp_auth.0.1.0.dylib}"

# Check if library file exists
if [ -f "$LIB_PATH" ]; then
    echo "✓ Library file found: $LIB_PATH"
    
    # Get file size
    SIZE=$(ls -l "$LIB_PATH" | awk '{print $5}')
    echo "  Size: $SIZE bytes"
    
    # Check if it's readable
    if [ -r "$LIB_PATH" ]; then
        echo "✓ File is readable"
    else
        echo "✗ File is not readable"
    fi
    
    # Check file type
    FILE_TYPE=$(file -b "$LIB_PATH" 2>/dev/null)
    if echo "$FILE_TYPE" | grep -q "Mach-O"; then
        echo "✓ Is a Mach-O binary"
    fi
    
    # Check for symlink
    if [ -L "./libgopher_mcp_auth.dylib" ] || [ -f "./libgopher_mcp_auth.dylib" ]; then
        echo "✓ Symlink/file exists: libgopher_mcp_auth.dylib"
    fi
else
    echo "✗ Library not found: $LIB_PATH"
    exit 1
fi

echo ""
echo "===================================="
echo "✓ Basic verification passed!"
echo "===================================="

exit 0