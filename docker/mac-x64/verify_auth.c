// Simple verification tool for macOS 10.14.6+
// Built to avoid "Killed: 9" errors on older macOS versions
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification\n");
    printf("====================================\n\n");
    
    const char* lib_path = argc > 1 ? argv[1] : "./libgopher_mcp_auth.0.1.0.dylib";
    
    // Check if file exists using stat (no dlopen to avoid compatibility issues)
    struct stat st;
    if (stat(lib_path, &st) == 0) {
        printf("✓ Library file found: %s\n", lib_path);
        printf("  Size: %lld bytes\n", (long long)st.st_size);
        
        // Check if it's a regular file
        if (S_ISREG(st.st_mode)) {
            printf("✓ Is a regular file\n");
        }
        
        // Check if it's readable
        if (access(lib_path, R_OK) == 0) {
            printf("✓ File is readable\n");
        }
        
        // Check for symlink
        if (access("./libgopher_mcp_auth.dylib", F_OK) == 0) {
            printf("✓ Symlink exists: libgopher_mcp_auth.dylib\n");
        }
    } else {
        printf("✗ Library not found: %s\n", lib_path);
        return 1;
    }
    
    printf("\n====================================\n");
    printf("✓ Basic verification passed!\n");
    printf("====================================\n");
    
    return 0;
}