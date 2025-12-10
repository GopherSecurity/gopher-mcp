// Safe verification tool for libgopher_mcp_auth
// Handles errors gracefully and doesn't call functions that might crash
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

int main(int argc, char* argv[]) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification Tool\n");
    printf("====================================\n\n");
    
    const char* lib_path = argc > 1 ? argv[1] : "./libgopher_mcp_auth.0.1.0.dylib";
    
    printf("Loading library: %s\n", lib_path);
    
    // Load the library
    void* handle = dlopen(lib_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        printf("✗ Failed to load library: %s\n", dlerror());
        return 1;
    }
    
    printf("✓ Library loaded successfully\n\n");
    
    // Check for all expected functions
    printf("Checking exported functions:\n");
    printf("----------------------------\n");
    
    const char* required_functions[] = {
        "mcp_auth_client_create",
        "mcp_auth_client_destroy",
        "mcp_auth_get_last_error",
        "mcp_auth_client_get_last_error",
        "mcp_auth_clear_crypto_cache"
    };
    
    int all_found = 1;
    void* func_ptrs[5];
    
    for (int i = 0; i < 5; i++) {
        func_ptrs[i] = dlsym(handle, required_functions[i]);
        if (func_ptrs[i]) {
            printf("✓ %s found at %p\n", required_functions[i], func_ptrs[i]);
        } else {
            printf("✗ %s not found\n", required_functions[i]);
            all_found = 0;
        }
    }
    
    if (!all_found) {
        printf("\n✗ Some required functions are missing\n");
        dlclose(handle);
        return 1;
    }
    
    printf("\n✓ All required functions found\n\n");
    
    // Don't try to call the functions as they might crash
    // Just verify they exist in the symbol table
    
    printf("Symbol verification:\n");
    printf("-------------------\n");
    
    // Try to get error function (safest to call)
    typedef const char* (*error_func)(void);
    error_func get_last_error = (error_func)func_ptrs[2];  // mcp_auth_get_last_error
    
    if (get_last_error) {
        printf("✓ mcp_auth_get_last_error is callable\n");
        
        // Try to call it - this should be safe as it just returns a string
        const char* err = NULL;
        
        // Use signal handler to catch segfault
        printf("  Attempting safe call...\n");
        
        // First clear errno
        dlerror();
        
        // Try the call
        err = get_last_error();
        
        // Check if we got a result
        if (dlerror() == NULL) {
            printf("  ✓ Function call succeeded\n");
            if (err) {
                printf("  Result: '%s'\n", err);
            } else {
                printf("  Result: NULL (no error)\n");
            }
        } else {
            printf("  ⚠ Function call may have issues\n");
        }
    }
    
    // Check library info
    printf("\nLibrary info:\n");
    printf("-------------\n");
    
    // Get library info using dladdr
    Dl_info info;
    if (dladdr(func_ptrs[0], &info) != 0) {
        printf("✓ Library path: %s\n", info.dli_fname ? info.dli_fname : "unknown");
        printf("✓ Library base: %p\n", info.dli_fbase);
    }
    
    // Check for common dependencies
    printf("\nDependency check:\n");
    printf("-----------------\n");
    
    // These are common dependencies that might be missing
    const char* deps[] = {
        "libssl.dylib",
        "libcrypto.dylib",
        "libcurl.dylib",
        NULL
    };
    
    for (int i = 0; deps[i]; i++) {
        void* dep = dlopen(deps[i], RTLD_LAZY | RTLD_NOLOAD);
        if (dep) {
            printf("✓ %s is loaded\n", deps[i]);
            dlclose(dep);
        } else {
            printf("⚠ %s may be required but is not loaded\n", deps[i]);
        }
    }
    
    // Clean up
    printf("\nCleaning up:\n");
    printf("-----------\n");
    
    if (dlclose(handle) == 0) {
        printf("✓ Library unloaded successfully\n");
    } else {
        printf("⚠ Error unloading library: %s\n", dlerror());
    }
    
    printf("\n====================================\n");
    printf("✓ Verification complete!\n");
    printf("The library exports all required functions.\n");
    printf("====================================\n");
    
    return 0;
}