// Windows compatibility layer for MCP Auth library
// Provides Windows-specific implementations and adaptations

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <cstring>

// Windows doesn't have some POSIX functions, provide alternatives
extern "C" {

// Provide a simple random bytes function using Windows CryptoAPI
int RAND_bytes(unsigned char* buf, int num) {
    // Use Windows CryptoAPI for secure random generation
    HCRYPTPROV hProvider = 0;
    
    if (!CryptAcquireContext(&hProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return 0;
    }
    
    BOOL result = CryptGenRandom(hProvider, num, buf);
    CryptReleaseContext(hProvider, 0);
    
    return result ? 1 : 0;
}

// Windows socket initialization
struct WSAInit {
    WSAInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~WSAInit() {
        WSACleanup();
    }
};

// Ensure Windows sockets are initialized
static WSAInit wsa_init;

} // extern "C"

// Windows-specific path handling
std::string normalize_path(const std::string& path) {
    std::string result = path;
    // Convert forward slashes to backslashes for Windows
    for (char& c : result) {
        if (c == '/') c = '\\';
    }
    return result;
}

// Windows-specific error handling
std::string get_last_error() {
    DWORD error = GetLastError();
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer, 0, NULL);
    
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    
    return message;
}

#endif // _WIN32