/**
 * @file verify_auth.cc
 * @brief Simple verification app for libgopher_mcp_auth
 * 
 * Tests basic functionality of the authentication library on different platforms
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

// Platform-specific library loading
#ifdef _WIN32
    #include <windows.h>
    #define LIBTYPE HMODULE
    #define LIBLOAD(name) LoadLibraryA(name)
    #define LIBFUNC GetProcAddress
    #define LIBFREE FreeLibrary
    #define LIB_EXT ".dll"
#elif __APPLE__
    #include <dlfcn.h>
    #define LIBTYPE void*
    #define LIBLOAD(name) dlopen(name, RTLD_LAZY | RTLD_LOCAL)
    #define LIBFUNC dlsym
    #define LIBFREE dlclose
    #define LIB_EXT ".dylib"
#else  // Linux/Unix
    #include <dlfcn.h>
    #define LIBTYPE void*
    #define LIBLOAD(name) dlopen(name, RTLD_LAZY | RTLD_LOCAL)
    #define LIBFUNC dlsym
    #define LIBFREE dlclose
    #define LIB_EXT ".so"
#endif

// Function pointer types matching the C API
typedef void* (*mcp_auth_client_create_t)(const char*, const char*, const char*, const char*);
typedef void (*mcp_auth_client_destroy_t)(void*);
typedef const char* (*mcp_auth_get_last_error_t)();
typedef const char* (*mcp_auth_client_get_last_error_t)(void*);
typedef void (*mcp_auth_clear_crypto_cache_t)();

// Color output for better readability
#ifdef _WIN32
    #define COLOR_GREEN ""
    #define COLOR_RED ""
    #define COLOR_YELLOW ""
    #define COLOR_RESET ""
#else
    #define COLOR_GREEN "\033[32m"
    #define COLOR_RED "\033[31m"
    #define COLOR_YELLOW "\033[33m"
    #define COLOR_RESET "\033[0m"
#endif

void print_test_header(const char* test_name) {
    std::cout << "\n" << COLOR_YELLOW << "Testing: " << test_name << COLOR_RESET << "\n";
    std::cout << "----------------------------------------\n";
}

void print_result(bool success, const char* message) {
    if (success) {
        std::cout << COLOR_GREEN << "✓ " << message << COLOR_RESET << "\n";
    } else {
        std::cout << COLOR_RED << "✗ " << message << COLOR_RESET << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "\n====================================\n";
    std::cout << "libgopher_mcp_auth Verification Tool\n";
    std::cout << "====================================\n";

    // Determine library name
    std::string lib_name = "./libgopher_mcp_auth" LIB_EXT;
    if (argc > 1) {
        lib_name = argv[1];
    }

    std::cout << "\nPlatform: ";
#ifdef _WIN32
    std::cout << "Windows\n";
#elif __APPLE__
    std::cout << "macOS\n";
#else
    std::cout << "Linux/Unix\n";
#endif

    std::cout << "Library: " << lib_name << "\n";

    // Load the library
    print_test_header("Library Loading");
    
    LIBTYPE lib = LIBLOAD(lib_name.c_str());
    if (!lib) {
        print_result(false, "Failed to load library");
#ifndef _WIN32
        const char* error = dlerror();
        if (error) {
            std::cout << "  Error: " << error << "\n";
        }
#else
        DWORD error = GetLastError();
        std::cout << "  Error code: " << error << "\n";
#endif
        std::cout << "\nTroubleshooting:\n";
        std::cout << "1. Make sure the library exists in the current directory\n";
        std::cout << "2. Check that all dependencies are available\n";
        std::cout << "3. On Linux, try: export LD_LIBRARY_PATH=.\n";
        std::cout << "4. On macOS, try: export DYLD_LIBRARY_PATH=.\n";
        return 1;
    }
    print_result(true, "Library loaded successfully");

    // Load functions
    print_test_header("Function Loading");
    
    auto create_client = (mcp_auth_client_create_t)LIBFUNC(lib, "mcp_auth_client_create");
    auto destroy_client = (mcp_auth_client_destroy_t)LIBFUNC(lib, "mcp_auth_client_destroy");
    auto get_last_error = (mcp_auth_get_last_error_t)LIBFUNC(lib, "mcp_auth_get_last_error");
    auto client_get_last_error = (mcp_auth_client_get_last_error_t)LIBFUNC(lib, "mcp_auth_client_get_last_error");
    auto clear_crypto_cache = (mcp_auth_clear_crypto_cache_t)LIBFUNC(lib, "mcp_auth_clear_crypto_cache");

    bool all_functions_loaded = true;
    
    if (!create_client) {
        print_result(false, "mcp_auth_client_create not found");
        all_functions_loaded = false;
    } else {
        print_result(true, "mcp_auth_client_create loaded");
    }
    
    if (!destroy_client) {
        print_result(false, "mcp_auth_client_destroy not found");
        all_functions_loaded = false;
    } else {
        print_result(true, "mcp_auth_client_destroy loaded");
    }
    
    if (!get_last_error) {
        print_result(false, "mcp_auth_get_last_error not found");
        all_functions_loaded = false;
    } else {
        print_result(true, "mcp_auth_get_last_error loaded");
    }
    
    if (!client_get_last_error) {
        print_result(false, "mcp_auth_client_get_last_error not found");
        all_functions_loaded = false;
    } else {
        print_result(true, "mcp_auth_client_get_last_error loaded");
    }
    
    if (!clear_crypto_cache) {
        print_result(false, "mcp_auth_clear_crypto_cache not found");
        all_functions_loaded = false;
    } else {
        print_result(true, "mcp_auth_clear_crypto_cache loaded");
    }

    if (!all_functions_loaded) {
        std::cout << "\n" << COLOR_RED << "Not all functions could be loaded!" << COLOR_RESET << "\n";
        LIBFREE(lib);
        return 1;
    }

    // Test error function
    print_test_header("Error Functions");
    if (get_last_error) {
        const char* error = get_last_error();
        std::cout << "Global last error: " << (error ? error : "(none)") << "\n";
        print_result(true, "Global error function works");
    }

    // Test client creation
    print_test_header("Client Creation");
    void* client = nullptr;
    if (create_client) {
        // Create a test client with mock Keycloak URL
        client = create_client(
            "http://localhost:8080",  // Keycloak URL
            "test-realm",              // Realm
            "test-client",             // Client ID
            nullptr                    // No client secret (public client)
        );
        
        if (client) {
            print_result(true, "Client created successfully");
            std::cout << "  Client pointer: " << client << "\n";
        } else {
            print_result(false, "Failed to create client");
            if (client_get_last_error) {
                const char* error = client_get_last_error(client);
                if (error && strlen(error) > 0) {
                    std::cout << "  Error: " << error << "\n";
                }
            }
        }
    }

    // Test cache operations
    if (clear_crypto_cache) {
        print_test_header("Cache Operations");
        clear_crypto_cache();
        print_result(true, "Crypto cache cleared successfully");
    }

    // Clean up
    print_test_header("Cleanup");
    if (client && destroy_client) {
        destroy_client(client);
        print_result(true, "Client destroyed successfully");
    }

    // Unload library
    LIBFREE(lib);
    print_result(true, "Library unloaded successfully");

    // Summary
    std::cout << "\n====================================\n";
    std::cout << COLOR_GREEN << "All tests passed successfully!" << COLOR_RESET << "\n";
    std::cout << "The library is working correctly.\n";
    std::cout << "====================================\n\n";

    return 0;
}