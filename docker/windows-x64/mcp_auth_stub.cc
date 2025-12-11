// Stub implementation of libgopher_mcp_auth for Windows
// This is a minimal implementation for testing the build system
// A real implementation would use Windows crypto and networking APIs

#include <windows.h>
#include <cstring>
#include <cstdlib>

// Export as C functions
extern "C" {

// Thread-local error storage
static __declspec(thread) char g_last_error[256] = {0};

// Auth client structure (stub)
struct mcp_auth_client {
    int dummy;
};

// Create a new auth client
__declspec(dllexport) void* mcp_auth_client_create() {
    mcp_auth_client* client = new mcp_auth_client();
    client->dummy = 42;
    return client;
}

// Destroy an auth client
__declspec(dllexport) void mcp_auth_client_destroy(void* client) {
    if (client) {
        delete static_cast<mcp_auth_client*>(client);
    }
}

// Get last global error
__declspec(dllexport) const char* mcp_auth_get_last_error() {
    return g_last_error;
}

// Get last client error
__declspec(dllexport) const char* mcp_auth_client_get_last_error(void* client) {
    if (!client) {
        return "Invalid client";
    }
    return "No error";
}

// Clear crypto cache
__declspec(dllexport) void mcp_auth_clear_crypto_cache() {
    // Stub implementation
    strcpy(g_last_error, "");
}

// DLL entry point for Windows
BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD  ul_reason_for_call,
                      LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

} // extern "C"