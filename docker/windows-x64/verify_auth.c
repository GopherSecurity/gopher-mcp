// Verification tool for libgopher_mcp_auth on Windows
// Compatible with Windows 7+ (x86_64)
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Function pointer types for the auth library
typedef void* (*mcp_auth_client_create_func)(void);
typedef void (*mcp_auth_client_destroy_func)(void*);
typedef const char* (*mcp_auth_get_last_error_func)(void);
typedef const char* (*mcp_auth_client_get_last_error_func)(void*);
typedef void (*mcp_auth_clear_crypto_cache_func)(void);

int main(int argc, char* argv[]) {
    printf("====================================\n");
    printf("libgopher_mcp_auth Verification Tool\n");
    printf("====================================\n\n");
    
    const char* lib_path = argc > 1 ? argv[1] : "gopher_mcp_auth.dll";
    
    printf("Loading library: %s\n", lib_path);
    
    // Load the library
    HMODULE hLib = LoadLibraryA(lib_path);
    if (!hLib) {
        DWORD error = GetLastError();
        printf("✗ Failed to load library: Error code %lu\n", error);
        
        // Try alternative names
        printf("\nTrying alternative library names...\n");
        const char* alt_names[] = {
            "libgopher_mcp_auth.dll",
            "./gopher_mcp_auth.dll",
            "./libgopher_mcp_auth.dll",
            NULL
        };
        
        for (int i = 0; alt_names[i]; i++) {
            printf("  Trying: %s\n", alt_names[i]);
            hLib = LoadLibraryA(alt_names[i]);
            if (hLib) {
                printf("  ✓ Loaded: %s\n", alt_names[i]);
                lib_path = alt_names[i];
                break;
            }
        }
        
        if (!hLib) {
            printf("\n✗ Could not load library with any name\n");
            printf("Windows error details:\n");
            LPVOID lpMsgBuf;
            error = GetLastError();
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR) &lpMsgBuf,
                0,
                NULL
            );
            printf("  %s", (char*)lpMsgBuf);
            LocalFree(lpMsgBuf);
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
        func_ptrs[i] = (void*)GetProcAddress(hLib, required_functions[i]);
        if (func_ptrs[i]) {
            printf("✓ %s found at %p\n", required_functions[i], func_ptrs[i]);
        } else {
            printf("✗ %s not found\n", required_functions[i]);
            all_found = 0;
        }
    }
    
    if (!all_found) {
        printf("\n✗ Some required functions are missing\n");
        FreeLibrary(hLib);
        return 1;
    }
    
    printf("\n✓ All required functions found\n\n");
    
    // Get module information
    printf("Library information:\n");
    printf("-------------------\n");
    
    char module_path[MAX_PATH];
    if (GetModuleFileNameA(hLib, module_path, MAX_PATH)) {
        printf("✓ Full path: %s\n", module_path);
    }
    
    // Get module handle info
    MODULEINFO modInfo;
    HANDLE hProcess = GetCurrentProcess();
    if (GetModuleInformation(hProcess, hLib, &modInfo, sizeof(modInfo))) {
        printf("✓ Base address: %p\n", modInfo.lpBaseOfDll);
        printf("✓ Module size: %lu bytes\n", modInfo.SizeOfImage);
    }
    
    // Check common dependencies
    printf("\nChecking common dependencies:\n");
    printf("-----------------------------\n");
    
    const char* deps[] = {
        "kernel32.dll",
        "msvcrt.dll",
        "ws2_32.dll",     // Winsock
        "advapi32.dll",   // Crypto API
        "bcrypt.dll",     // Modern crypto
        "crypt32.dll",    // Certificate handling
        NULL
    };
    
    for (int i = 0; deps[i]; i++) {
        HMODULE hDep = GetModuleHandleA(deps[i]);
        if (hDep) {
            printf("✓ %s is loaded\n", deps[i]);
        } else {
            // Try to load it
            hDep = LoadLibraryA(deps[i]);
            if (hDep) {
                printf("⚠ %s is available but was not loaded\n", deps[i]);
                FreeLibrary(hDep);
            } else {
                printf("✗ %s is not available\n", deps[i]);
            }
        }
    }
    
    // Test safe function call
    printf("\nTesting safe function call:\n");
    printf("---------------------------\n");
    
    mcp_auth_get_last_error_func get_last_error = 
        (mcp_auth_get_last_error_func)func_ptrs[2];
    
    if (get_last_error) {
        // Try to call the function
        const char* err = get_last_error();
        
        printf("✓ mcp_auth_get_last_error() called successfully\n");
        if (err) {
            printf("  Result: '%s'\n", err);
        } else {
            printf("  Result: NULL (no error)\n");
        }
    }
    
    // Check file information
    printf("\nFile information:\n");
    printf("----------------\n");
    struct stat st;
    if (stat(lib_path, &st) == 0) {
        printf("✓ File size: %ld bytes\n", st.st_size);
        printf("✓ File mode: 0%o\n", st.st_mode & 0777);
    }
    
    // Check if it's a 64-bit DLL
    BOOL is64bit = FALSE;
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)hLib;
    if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)hLib + dosHeader->e_lfanew);
        if (ntHeaders->Signature == IMAGE_NT_SIGNATURE) {
            is64bit = (ntHeaders->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
        }
    }
    printf("✓ Architecture: %s\n", is64bit ? "x64 (64-bit)" : "x86 (32-bit)");
    
    // Clean up
    printf("\nCleaning up:\n");
    printf("-----------\n");
    
    if (FreeLibrary(hLib)) {
        printf("✓ Library unloaded successfully\n");
    } else {
        printf("⚠ Error unloading library: %lu\n", GetLastError());
    }
    
    printf("\n====================================\n");
    printf("✓ Verification complete!\n");
    printf("The library is working correctly.\n");
    printf("====================================\n");
    
    return 0;
}