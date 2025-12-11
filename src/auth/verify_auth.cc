/**
 * @file verify_auth.cc
 * @brief Verification tool for libgopher_mcp_auth
 * Built with CMake for consistency with library build
 */

#include <iostream>
#include <dlfcn.h>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "====================================" << std::endl;
    std::cout << "libgopher_mcp_auth Verification" << std::endl;
    std::cout << "====================================" << std::endl << std::endl;
    
    std::string lib_path = "./libgopher_mcp_auth.dylib";
    if (argc > 1) {
        lib_path = argv[1];
    }
    
    std::cout << "Loading: " << lib_path << std::endl;
    void* lib = dlopen(lib_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    
    if (!lib) {
        std::cerr << "ERROR: Failed to load library" << std::endl;
        std::cerr << "  " << dlerror() << std::endl;
        std::cerr << std::endl << "Try: export DYLD_LIBRARY_PATH=." << std::endl;
        return 1;
    }
    std::cout << "✓ Library loaded" << std::endl;
    
    // Check for key functions
    bool all_ok = true;
    
    void* func = dlsym(lib, "mcp_auth_client_create");
    if (!func) {
        std::cerr << "✗ mcp_auth_client_create not found" << std::endl;
        all_ok = false;
    } else {
        std::cout << "✓ mcp_auth_client_create found" << std::endl;
    }
    
    func = dlsym(lib, "mcp_auth_client_destroy");
    if (!func) {
        std::cerr << "✗ mcp_auth_client_destroy not found" << std::endl;
        all_ok = false;
    } else {
        std::cout << "✓ mcp_auth_client_destroy found" << std::endl;
    }
    
    func = dlsym(lib, "mcp_auth_get_last_error");
    if (!func) {
        std::cerr << "✗ mcp_auth_get_last_error not found" << std::endl;
        all_ok = false;
    } else {
        std::cout << "✓ mcp_auth_get_last_error found" << std::endl;
    }
    
    dlclose(lib);
    std::cout << "✓ Library unloaded" << std::endl;
    
    std::cout << std::endl << "====================================" << std::endl;
    if (all_ok) {
        std::cout << "✓ All tests passed!" << std::endl;
    } else {
        std::cout << "✗ Some tests failed" << std::endl;
    }
    std::cout << "====================================" << std::endl;
    
    return all_ok ? 0 : 1;
}