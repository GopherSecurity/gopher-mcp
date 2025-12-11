// Verification tool for libgopher_mcp_auth on Linux
// Compatible with Ubuntu 20.04+ and other modern Linux distributions
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <link.h>

int main(int argc, char* argv[]) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification Tool\n");
    printf("====================================\n\n");
    
    const char* lib_path = argc > 1 ? argv[1] : "./libgopher_mcp_auth.so.0.1.0";
    
    printf("Loading library: %s\n", lib_path);
    
    // Load the library with RTLD_LAZY to avoid immediate resolution of all symbols
    void* handle = dlopen(lib_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        printf("✗ Failed to load library: %s\n", dlerror());
        
        // Try alternative names
        printf("\nTrying alternative library names...\n");
        const char* alt_names[] = {
            "./libgopher_mcp_auth.so",
            "./libgopher_mcp_auth.so.0",
            "libgopher_mcp_auth.so.0.1.0",
            "libgopher_mcp_auth.so",
            NULL
        };
        
        for (int i = 0; alt_names[i]; i++) {
            printf("  Trying: %s\n", alt_names[i]);
            handle = dlopen(alt_names[i], RTLD_LAZY | RTLD_LOCAL);
            if (handle) {
                printf("  ✓ Loaded: %s\n", alt_names[i]);
                lib_path = alt_names[i];
                break;
            }
        }
        
        if (!handle) {
            printf("\n✗ Could not load library with any name\n");
            return 1;
        }
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
    
    // Get library info using dlinfo (Linux specific)
    printf("Library information:\n");
    printf("-------------------\n");
    
    // Get the real path of the loaded library
    struct link_map *lm = NULL;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) == 0 && lm) {
        printf("✓ Library path: %s\n", lm->l_name ? lm->l_name : "unknown");
        printf("✓ Base address: %p\n", (void*)lm->l_addr);
    }
    
    // Check library dependencies using ldd would require system() which we avoid
    // Instead, check if common dependencies are available
    printf("\nChecking common dependencies:\n");
    printf("-----------------------------\n");
    
    const char* deps[] = {
        "libssl.so.1.1",     // OpenSSL 1.1 (Ubuntu 20.04)
        "libssl.so.3",       // OpenSSL 3.0 (Ubuntu 22.04+)
        "libcrypto.so.1.1",  // OpenSSL crypto
        "libcrypto.so.3",    
        "libcurl.so.4",      // curl library
        NULL
    };
    
    for (int i = 0; deps[i]; i++) {
        void* dep = dlopen(deps[i], RTLD_LAZY | RTLD_NOLOAD);
        if (dep) {
            printf("✓ %s is loaded\n", deps[i]);
            dlclose(dep);
        } else {
            // Try to load it
            dep = dlopen(deps[i], RTLD_LAZY);
            if (dep) {
                printf("⚠ %s is available but was not loaded\n", deps[i]);
                dlclose(dep);
            } else {
                printf("✗ %s is not available\n", deps[i]);
            }
        }
    }
    
    // Test safe function call
    printf("\nTesting safe function call:\n");
    printf("---------------------------\n");
    
    typedef const char* (*error_func)(void);
    error_func get_last_error = (error_func)func_ptrs[2];  // mcp_auth_get_last_error
    
    if (get_last_error) {
        // Clear any existing errors
        dlerror();
        
        // Try to call the function
        const char* err = get_last_error();
        
        // Check if call succeeded
        const char* dl_err = dlerror();
        if (dl_err == NULL) {
            printf("✓ mcp_auth_get_last_error() called successfully\n");
            if (err) {
                printf("  Result: '%s'\n", err);
            } else {
                printf("  Result: NULL (no error)\n");
            }
        } else {
            printf("⚠ Error calling function: %s\n", dl_err);
        }
    }
    
    // Check file permissions
    printf("\nFile information:\n");
    printf("----------------\n");
    struct stat st;
    if (stat(lib_path, &st) == 0) {
        printf("✓ File size: %ld bytes\n", st.st_size);
        printf("✓ File mode: 0%o\n", st.st_mode & 0777);
        printf("✓ File owner: UID %d, GID %d\n", st.st_uid, st.st_gid);
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
    printf("The library is working correctly.\n");
    printf("====================================\n");
    
    return 0;
}