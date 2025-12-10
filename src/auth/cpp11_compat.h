#ifndef MCP_AUTH_CPP11_COMPAT_H
#define MCP_AUTH_CPP11_COMPAT_H

#include <memory>
#include <utility>
#include <mutex>

// Provide std::make_unique for C++11 (it was added in C++14)
#if __cplusplus < 201402L

namespace std {
    template<typename T, typename... Args>
    unique_ptr<T> make_unique(Args&&... args) {
        return unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
    
    // Array version
    template<typename T>
    unique_ptr<T[]> make_unique(size_t size) {
        return unique_ptr<T[]>(new T[size]);
    }
}

#endif // __cplusplus < 201402L

// For C++11, use regular mutex instead of shared_mutex
#if __cplusplus < 201703L
    #define shared_mutex mutex
    #define shared_lock unique_lock
#endif

#endif // MCP_AUTH_CPP11_COMPAT_H