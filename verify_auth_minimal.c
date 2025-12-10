/**
 * Minimal verification for libgopher_mcp_auth
 * Built for maximum compatibility with macOS 10.14.6
 */

#include <stdio.h>
#include <dlfcn.h>

int main(void) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification\n");
    printf("====================================\n\n");
    
    /* Load library */
    printf("Loading library...\n");
    void* lib = dlopen("./libgopher_mcp_auth.dylib", RTLD_LAZY);
    if (!lib) {
        printf("ERROR: %s\n", dlerror());
        return 1;
    }
    printf("OK: Library loaded\n");
    
    /* Check for key function */
    printf("Checking functions...\n");
    void* func = dlsym(lib, "mcp_auth_client_create");
    if (!func) {
        printf("ERROR: mcp_auth_client_create not found\n");
        dlclose(lib);
        return 1;
    }
    printf("OK: mcp_auth_client_create found\n");
    
    func = dlsym(lib, "mcp_auth_client_destroy");
    if (!func) {
        printf("ERROR: mcp_auth_client_destroy not found\n");
        dlclose(lib);
        return 1;
    }
    printf("OK: mcp_auth_client_destroy found\n");
    
    /* Clean up */
    dlclose(lib);
    printf("\n====================================\n");
    printf("SUCCESS: Library verified\n");
    printf("====================================\n");
    
    return 0;
}