#!/bin/bash

# Build verification app locally on macOS 10.14.6
# Run this script directly on the target machine to avoid transfer issues

cat > verify_auth_local.c << 'EOF'
#include <stdio.h>
#include <dlfcn.h>

int main(void) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification\n");
    printf("====================================\n\n");
    
    printf("Loading library...\n");
    void* lib = dlopen("./libgopher_mcp_auth.dylib", 1);
    if (!lib) {
        printf("ERROR: %s\n", dlerror());
        return 1;
    }
    printf("OK: Library loaded\n");
    
    printf("Checking mcp_auth_client_create...\n");
    void* func = dlsym(lib, "mcp_auth_client_create");
    if (!func) {
        printf("ERROR: Not found\n");
        dlclose(lib);
        return 1;
    }
    printf("OK: Found\n");
    
    dlclose(lib);
    printf("\n====================================\n");
    printf("SUCCESS: Library verified\n");
    printf("====================================\n");
    
    return 0;
}
EOF

echo "Building verify_auth..."
# Use the most basic compilation possible
cc -o verify_auth verify_auth_local.c

echo "Removing temporary source..."
rm verify_auth_local.c

echo ""
echo "Build complete! Run ./verify_auth to test the library."
echo ""
echo "If you still get 'Killed: 9', try:"
echo "  1. xattr -d com.apple.quarantine verify_auth"
echo "  2. xattr -d com.apple.quarantine libgopher_mcp_auth.dylib"
echo "  3. codesign --remove-signature verify_auth"
echo "  4. Run with: DYLD_LIBRARY_PATH=. ./verify_auth"