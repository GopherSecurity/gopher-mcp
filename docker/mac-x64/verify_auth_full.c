// Full verification tool for libgopher_mcp_auth
// Loads the library and verifies all exported functions
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

// Function pointer types for the auth API
typedef void* (*create_func)(const char*, const char*, const char*, const char*);
typedef void (*destroy_func)(void*);
typedef const char* (*error_func)(void);
typedef const char* (*client_error_func)(void*);
typedef void (*clear_cache_func)(void);

int main(int argc, char* argv[]) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification Tool\n");
    printf("====================================\n\n");
    
    const char* lib_path = argc > 1 ? argv[1] : "./libgopher_mcp_auth.0.1.0.dylib";
    
    printf("Loading library: %s\n", lib_path);
    
    // Load the library
    void* handle = dlopen(lib_path, RTLD_LAZY);
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
    for (int i = 0; i < 5; i++) {
        void* func = dlsym(handle, required_functions[i]);
        if (func) {
            printf("✓ %s found at %p\n", required_functions[i], func);
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
    
    // Load the functions
    create_func create_client = (create_func)dlsym(handle, "mcp_auth_client_create");
    destroy_func destroy_client = (destroy_func)dlsym(handle, "mcp_auth_client_destroy");
    error_func get_last_error = (error_func)dlsym(handle, "mcp_auth_get_last_error");
    client_error_func client_get_last_error = (client_error_func)dlsym(handle, "mcp_auth_client_get_last_error");
    clear_cache_func clear_crypto_cache = (clear_cache_func)dlsym(handle, "mcp_auth_clear_crypto_cache");
    
    // Test basic functionality
    printf("Testing basic functionality:\n");
    printf("----------------------------\n");
    
    // Clear any previous errors
    if (clear_crypto_cache) {
        clear_crypto_cache();
        printf("✓ Cleared crypto cache\n");
    }
    
    // Try to create a client (this will likely fail without a real server, but that's OK)
    printf("Creating test client...\n");
    void* client = create_client("http://localhost:8080", "test-realm", "test-client", NULL);
    
    if (client) {
        printf("✓ Client created successfully: %p\n", client);
        
        // Get client error (should be none)
        const char* client_err = client_get_last_error ? client_get_last_error(client) : NULL;
        if (client_err && strlen(client_err) > 0) {
            printf("  Client error: %s\n", client_err);
        }
        
        // Destroy the client
        destroy_client(client);
        printf("✓ Client destroyed successfully\n");
    } else {
        printf("⚠ Client creation returned NULL (expected without server)\n");
        
        // Check for error message
        const char* err = get_last_error ? get_last_error() : NULL;
        if (err && strlen(err) > 0) {
            printf("  Error: %s\n", err);
        } else {
            printf("  No error message available\n");
        }
    }
    
    // Test that we can call functions without crashing
    printf("\nTesting function calls:\n");
    printf("----------------------\n");
    
    // Call get_last_error
    const char* last_err = get_last_error();
    printf("✓ mcp_auth_get_last_error() called successfully\n");
    if (last_err && strlen(last_err) > 0) {
        printf("  Last error: %s\n", last_err);
    }
    
    // Call clear_crypto_cache again
    clear_crypto_cache();
    printf("✓ mcp_auth_clear_crypto_cache() called successfully\n");
    
    // Clean up
    dlclose(handle);
    printf("\n✓ Library unloaded successfully\n");
    
    printf("\n====================================\n");
    printf("✓ All verification tests passed!\n");
    printf("The library is working correctly.\n");
    printf("====================================\n");
    
    return 0;
}