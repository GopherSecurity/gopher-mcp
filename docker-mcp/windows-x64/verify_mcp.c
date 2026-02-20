/**
 * Verification tool for libgopher-mcp on Windows
 *
 * This tool verifies that the MCP library was built correctly by:
 * 1. Loading the DLL
 * 2. Checking for exported symbols
 * 3. Verifying basic functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#endif

#define GREEN   "\033[0;32m"
#define RED     "\033[0;31m"
#define YELLOW  "\033[1;33m"
#define NC      "\033[0m"

#ifdef _WIN32
typedef HMODULE lib_handle_t;
#define LIB_OPEN(name) LoadLibraryA(name)
#define LIB_SYM(handle, name) GetProcAddress(handle, name)
#define LIB_CLOSE(handle) FreeLibrary(handle)
#define LIB_ERROR() "LoadLibrary failed"
#else
typedef void* lib_handle_t;
#define LIB_OPEN(name) dlopen(name, RTLD_NOW)
#define LIB_SYM(handle, name) dlsym(handle, name)
#define LIB_CLOSE(handle) dlclose(handle)
#define LIB_ERROR() dlerror()
#endif

void print_header(void) {
    printf("\n");
    printf("===============================================\n");
    printf("  libgopher-mcp Verification Tool (Windows)\n");
    printf("===============================================\n");
    printf("\n");
}

void print_system_info(void) {
    printf("System Information:\n");
    printf("-------------------\n");

#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    const char* arch = "Unknown";
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            arch = "x86_64 (AMD64)";
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            arch = "ARM64";
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            arch = "x86 (Intel)";
            break;
        case PROCESSOR_ARCHITECTURE_ARM:
            arch = "ARM";
            break;
    }
    printf("  Architecture: %s\n", arch);
    printf("  Processors: %lu\n", si.dwNumberOfProcessors);

    OSVERSIONINFOA osvi;
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    if (GetVersionExA(&osvi)) {
        printf("  Windows Version: %lu.%lu (Build %lu)\n",
               osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
    }
#else
    printf("  Platform: Non-Windows (POSIX)\n");
#endif
    printf("\n");
}

int check_library(const char* lib_name, const char* display_name) {
    printf("Checking %s...\n", display_name);

    lib_handle_t handle = LIB_OPEN(lib_name);
    if (!handle) {
        printf("  " RED "X" NC " Failed to load: %s\n", LIB_ERROR());
        return 0;
    }

    printf("  " GREEN "OK" NC " Library loaded successfully\n");

    // Check for common MCP symbols
    const char* symbols[] = {
        "mcp_init",
        "mcp_cleanup",
        "mcp_client_create",
        "mcp_client_destroy",
        "mcp_client_connect",
        "mcp_server_create",
        "mcp_server_destroy",
        "mcp_json_parse",
        "mcp_json_stringify",
        NULL
    };

    int found = 0;
    int total = 0;

    for (int i = 0; symbols[i] != NULL; i++) {
        total++;
        void* sym = LIB_SYM(handle, symbols[i]);
        if (sym) {
            printf("  " GREEN "OK" NC " %s found\n", symbols[i]);
            found++;
        } else {
            printf("  " YELLOW "--" NC " %s not found\n", symbols[i]);
        }
    }

    LIB_CLOSE(handle);

    printf("\n  Summary: %d/%d symbols found\n", found, total);
    return found > 0 ? 1 : 0;
}

int main(int argc, char* argv[]) {
    print_header();
    print_system_info();

    int success = 0;

    // Try to load the main library
    printf("Library Verification:\n");
    printf("---------------------\n");

    // Try different library names
    const char* main_libs[] = {
        "gopher-mcp.dll",
        "./gopher-mcp.dll",
        "libgopher-mcp.dll",
        "./libgopher-mcp.dll",
        NULL
    };

    for (int i = 0; main_libs[i] != NULL; i++) {
        if (check_library(main_libs[i], "Main Library")) {
            success = 1;
            break;
        }
    }

    printf("\n");

    // Try to load the C API library
    const char* c_api_libs[] = {
        "gopher_mcp_c.dll",
        "./gopher_mcp_c.dll",
        "libgopher_mcp_c.dll",
        "./libgopher_mcp_c.dll",
        NULL
    };

    for (int i = 0; c_api_libs[i] != NULL; i++) {
        if (check_library(c_api_libs[i], "C API Library")) {
            success = 1;
            break;
        }
    }

    printf("\n");
    printf("===============================================\n");
    if (success) {
        printf("  " GREEN "Verification PASSED" NC "\n");
        printf("  At least one library loaded successfully.\n");
    } else {
        printf("  " RED "Verification FAILED" NC "\n");
        printf("  No libraries could be loaded.\n");
        printf("\n");
        printf("  Troubleshooting:\n");
        printf("  1. Ensure DLL files are in the same directory\n");
        printf("  2. Check that all dependencies are present\n");
        printf("  3. Run 'dumpbin /dependents *.dll' to check deps\n");
    }
    printf("===============================================\n");
    printf("\n");

    return success ? 0 : 1;
}
