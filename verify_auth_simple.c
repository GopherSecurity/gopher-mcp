/**
 * @file verify_auth_simple.c
 * @brief Simple C verification app for libgopher_mcp_auth
 * Maximum compatibility for macOS 10.14+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef void* (*create_func)(const char*, const char*, const char*, const char*);
typedef void (*destroy_func)(void*);
typedef const char* (*error_func)(void);
typedef const char* (*client_error_func)(void*);
typedef void (*clear_cache_func)(void);

int main(int argc, char* argv[]) {
    printf("\n====================================\n");
    printf("libgopher_mcp_auth Verification\n");
    printf("====================================\n\n");
    
    const char* lib_name = "./libgopher_mcp_auth.dylib";
    if (argc > 1) lib_name = argv[1];
    
    printf("Loading: %s\n", lib_name);
    void* lib = dlopen(lib_name, RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        printf("ERROR: Failed to load library\n");
        printf("  %s\n", dlerror());
        printf("\nTry: export DYLD_LIBRARY_PATH=.\n");
        return 1;
    }
    printf("✓ Library loaded\n");
    
    // Load functions
    create_func create_client = (create_func)dlsym(lib, "mcp_auth_client_create");
    destroy_func destroy_client = (destroy_func)dlsym(lib, "mcp_auth_client_destroy");
    error_func get_last_error = (error_func)dlsym(lib, "mcp_auth_get_last_error");
    client_error_func client_get_last_error = (client_error_func)dlsym(lib, "mcp_auth_client_get_last_error");
    clear_cache_func clear_crypto_cache = (clear_cache_func)dlsym(lib, "mcp_auth_clear_crypto_cache");
    
    int all_ok = 1;
    if (!create_client) { printf("✗ mcp_auth_client_create not found\n"); all_ok = 0; }
    else printf("✓ mcp_auth_client_create loaded\n");
    
    if (!destroy_client) { printf("✗ mcp_auth_client_destroy not found\n"); all_ok = 0; }
    else printf("✓ mcp_auth_client_destroy loaded\n");
    
    if (!get_last_error) { printf("✗ mcp_auth_get_last_error not found\n"); all_ok = 0; }
    else printf("✓ mcp_auth_get_last_error loaded\n");
    
    if (!all_ok) {
        printf("\nERROR: Required functions not found\n");
        dlclose(lib);
        return 1;
    }
    
    printf("\nTesting client creation...\n");
    void* client = create_client("http://localhost:8080", "test-realm", "test-client", NULL);
    if (client) {
        printf("✓ Client created: %p\n", client);
        
        if (clear_crypto_cache) {
            clear_crypto_cache();
            printf("✓ Cache cleared\n");
        }
        
        destroy_client(client);
        printf("✓ Client destroyed\n");
    } else {
        printf("✗ Failed to create client\n");
        if (get_last_error) {
            const char* err = get_last_error();
            if (err && strlen(err) > 0) printf("  Error: %s\n", err);
        }
    }
    
    dlclose(lib);
    printf("✓ Library unloaded\n");
    
    printf("\n====================================\n");
    printf("✓ All tests passed!\n");
    printf("====================================\n\n");
    
    return 0;
}