// Windows implementation of libgopher_mcp_auth
// Uses Windows native APIs as alternatives to OpenSSL/CURL
// This provides real cryptography and networking without external dependencies

#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "winhttp.lib")

// Export as C functions
extern "C" {

// Thread-local error storage
static __declspec(thread) char g_last_error[256] = {0};

// Auth client structure with Windows crypto context
struct mcp_auth_client {
    HCRYPTPROV hCryptProv;
    HCRYPTHASH hHash;
    HCRYPTKEY hKey;
    HINTERNET hSession;
    std::string last_error;
};

// Create a new auth client with Windows Crypto API
__declspec(dllexport) void* mcp_auth_client_create() {
    mcp_auth_client* client = new mcp_auth_client();
    
    // Initialize Windows Crypto API context
    if (!CryptAcquireContext(&client->hCryptProv, NULL, NULL, 
                             PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        // Try with different provider
        if (!CryptAcquireContext(&client->hCryptProv, NULL, NULL,
                                 PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            strcpy(g_last_error, "Failed to acquire crypto context");
            delete client;
            return nullptr;
        }
    }
    
    // Initialize WinHTTP for networking (replacement for CURL)
    client->hSession = WinHttpOpen(L"MCP-Auth-Client/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!client->hSession) {
        CryptReleaseContext(client->hCryptProv, 0);
        strcpy(g_last_error, "Failed to initialize WinHTTP");
        delete client;
        return nullptr;
    }
    
    client->hHash = 0;
    client->hKey = 0;
    return client;
}

// Destroy an auth client
__declspec(dllexport) void mcp_auth_client_destroy(void* client_ptr) {
    if (!client_ptr) return;
    
    mcp_auth_client* client = static_cast<mcp_auth_client*>(client_ptr);
    
    // Clean up crypto resources
    if (client->hKey) {
        CryptDestroyKey(client->hKey);
    }
    if (client->hHash) {
        CryptDestroyHash(client->hHash);
    }
    if (client->hCryptProv) {
        CryptReleaseContext(client->hCryptProv, 0);
    }
    
    // Clean up WinHTTP
    if (client->hSession) {
        WinHttpCloseHandle(client->hSession);
    }
    
    delete client;
}

// Generate random bytes using Windows Crypto API (replacement for OpenSSL RAND_bytes)
__declspec(dllexport) int mcp_auth_generate_random(void* client_ptr, unsigned char* buf, int len) {
    if (!client_ptr || !buf || len <= 0) {
        strcpy(g_last_error, "Invalid parameters");
        return 0;
    }
    
    mcp_auth_client* client = static_cast<mcp_auth_client*>(client_ptr);
    
    if (CryptGenRandom(client->hCryptProv, len, buf)) {
        return 1;
    }
    
    strcpy(g_last_error, "Failed to generate random bytes");
    return 0;
}

// Compute SHA256 hash using Windows Crypto API (replacement for OpenSSL EVP)
__declspec(dllexport) int mcp_auth_sha256(void* client_ptr, const unsigned char* data, 
                                          size_t len, unsigned char* hash) {
    if (!client_ptr || !data || !hash) {
        strcpy(g_last_error, "Invalid parameters");
        return 0;
    }
    
    mcp_auth_client* client = static_cast<mcp_auth_client*>(client_ptr);
    HCRYPTHASH hHash;
    
    // Create SHA256 hash object
    if (!CryptCreateHash(client->hCryptProv, CALG_SHA_256, 0, 0, &hHash)) {
        strcpy(g_last_error, "Failed to create hash object");
        return 0;
    }
    
    // Hash the data
    if (!CryptHashData(hHash, data, len, 0)) {
        CryptDestroyHash(hHash);
        strcpy(g_last_error, "Failed to hash data");
        return 0;
    }
    
    // Get the hash value
    DWORD hashLen = 32; // SHA256 is 32 bytes
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        CryptDestroyHash(hHash);
        strcpy(g_last_error, "Failed to get hash value");
        return 0;
    }
    
    CryptDestroyHash(hHash);
    return 1;
}

// Perform HTTPS request using WinHTTP (replacement for CURL)
__declspec(dllexport) int mcp_auth_https_request(void* client_ptr, const char* url,
                                                 const char* method, const char* data,
                                                 char* response, size_t* response_len) {
    if (!client_ptr || !url || !method || !response || !response_len) {
        strcpy(g_last_error, "Invalid parameters");
        return 0;
    }
    
    mcp_auth_client* client = static_cast<mcp_auth_client*>(client_ptr);
    
    // Convert URL to wide string
    int url_len = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    std::vector<wchar_t> wurl(url_len);
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl.data(), url_len);
    
    // Parse URL components
    URL_COMPONENTS urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    
    wchar_t hostname[256];
    wchar_t urlPath[1024];
    urlComp.lpszHostName = hostname;
    urlComp.dwHostNameLength = sizeof(hostname) / sizeof(wchar_t);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);
    
    if (!WinHttpCrackUrl(wurl.data(), 0, 0, &urlComp)) {
        strcpy(g_last_error, "Failed to parse URL");
        return 0;
    }
    
    // Connect to server
    HINTERNET hConnect = WinHttpConnect(client->hSession, hostname,
                                        urlComp.nPort, 0);
    if (!hConnect) {
        strcpy(g_last_error, "Failed to connect to server");
        return 0;
    }
    
    // Convert method to wide string
    int method_len = MultiByteToWideChar(CP_UTF8, 0, method, -1, NULL, 0);
    std::vector<wchar_t> wmethod(method_len);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod.data(), method_len);
    
    // Create request
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.data(), urlPath,
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        strcpy(g_last_error, "Failed to create request");
        return 0;
    }
    
    // Send request
    BOOL result = WinHttpSendRequest(hRequest,
                                     WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     (LPVOID)data, data ? strlen(data) : 0,
                                     data ? strlen(data) : 0, 0);
    
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        strcpy(g_last_error, "Failed to send request");
        return 0;
    }
    
    // Receive response
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        strcpy(g_last_error, "Failed to receive response");
        return 0;
    }
    
    // Read response data
    DWORD bytesRead = 0;
    size_t totalRead = 0;
    do {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
            break;
        }
        
        if (bytesAvailable == 0) {
            break;
        }
        
        if (totalRead + bytesAvailable > *response_len) {
            bytesAvailable = *response_len - totalRead;
        }
        
        if (!WinHttpReadData(hRequest, response + totalRead,
                            bytesAvailable, &bytesRead)) {
            break;
        }
        
        totalRead += bytesRead;
    } while (bytesRead > 0 && totalRead < *response_len);
    
    *response_len = totalRead;
    response[totalRead] = '\0';
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    
    return 1;
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
    mcp_auth_client* c = static_cast<mcp_auth_client*>(client);
    return c->last_error.c_str();
}

// Clear crypto cache
__declspec(dllexport) void mcp_auth_clear_crypto_cache() {
    // Clear any cached crypto data
    strcpy(g_last_error, "");
}

// DLL entry point for Windows
BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD  ul_reason_for_call,
                      LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            // Initialize any global resources
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            // Clean up global resources
            break;
    }
    return TRUE;
}

} // extern "C"