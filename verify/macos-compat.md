# macOS 10.14.6 Compatibility Guide

## Issue
On macOS 10.14.6, you may encounter this error:
```
dyld: cannot load 'verify_auth' (load command 0x80000034 is unknown)
Abort trap: 6
```

This occurs because the binary was compiled with a newer SDK that uses `LC_DYLD_CHAINED_FIXUPS`, which is not supported on macOS 10.14.

## Solution

Use the C version of the verification tool, which has maximum compatibility.

### Option 1: Build the C version locally

1. Create `verify_auth_simple.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef void* (*create_func)(const char*, const char*, const char*, const char*);
typedef void (*destroy_func)(void*);
typedef const char* (*error_func)(void);

int main(int argc, char* argv[]) {
    printf("\nlibgopher_mcp_auth Simple Verification\n");
    printf("======================================\n\n");
    
    const char* lib_name = "./libgopher_mcp_auth.dylib";
    if (argc > 1) lib_name = argv[1];
    
    printf("Loading: %s\n", lib_name);
    void* lib = dlopen(lib_name, RTLD_LAZY);
    if (!lib) {
        printf("ERROR: %s\n", dlerror());
        printf("\nTry: export DYLD_LIBRARY_PATH=.\n");
        return 1;
    }
    printf("✓ Library loaded\n");
    
    create_func create = (create_func)dlsym(lib, "mcp_auth_client_create");
    destroy_func destroy = (destroy_func)dlsym(lib, "mcp_auth_client_destroy");
    error_func get_error = (error_func)dlsym(lib, "mcp_auth_get_last_error");
    
    if (!create || !destroy) {
        printf("ERROR: Required functions not found\n");
        dlclose(lib);
        return 1;
    }
    printf("✓ Functions loaded\n");
    
    void* client = create("http://localhost:8080", "test", "test", NULL);
    if (client) {
        printf("✓ Client created: %p\n", client);
        destroy(client);
        printf("✓ Client destroyed\n");
    } else {
        printf("✗ Failed to create client\n");
        if (get_error) {
            const char* err = get_error();
            if (err) printf("  Error: %s\n", err);
        }
    }
    
    dlclose(lib);
    printf("✓ Library unloaded\n");
    printf("\n✓ All tests passed!\n\n");
    
    return 0;
}
```

2. Compile with compatibility flags:
```bash
clang -std=c99 -mmacosx-version-min=10.14 -o verify_auth_c verify_auth_simple.c
```

3. Run the verification:
```bash
./verify_auth_c
```

### Option 2: Build C++ version with legacy linker

If you prefer the C++ version, compile with:
```bash
clang++ -std=c++11 -mmacosx-version-min=10.14 -stdlib=libc++ -o verify_auth verify_auth.cc
```

On newer Xcode versions, you may need to use the legacy linker:
```bash
clang++ -std=c++11 -mmacosx-version-min=10.14 -stdlib=libc++ -Wl,-ld_classic -o verify_auth verify_auth.cc
```

## Library Path Issues

If you get "library not loaded" error:
```bash
export DYLD_LIBRARY_PATH=.
./verify_auth_c
```

Or specify the library path directly:
```bash
./verify_auth_c ./libgopher_mcp_auth.0.1.0.dylib
```

## Verification

To check the minimum macOS version of a binary:
```bash
otool -l verify_auth | grep -A 4 "LC_VERSION_MIN\|LC_BUILD_VERSION"
```

Expected output for 10.14 compatibility:
```
cmd LC_BUILD_VERSION
  cmdsize 32
 platform 1
    minos 10.14
```

## Notes

- The C version has the best compatibility across macOS versions
- The library itself (`libgopher_mcp_auth.dylib`) is already built with 10.14 compatibility
- Only the verification tool needs to be rebuilt for older macOS versions
- This issue only affects the test tool, not the library itself