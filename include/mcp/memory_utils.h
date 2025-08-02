#ifndef MCP_MEMORY_UTILS_H
#define MCP_MEMORY_UTILS_H

#include <memory>
#include <utility>

namespace mcp {

// C++11 compatible make_unique implementation
template <typename T, typename... Args>
typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type
make_unique(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// Array version for make_unique
template <typename T>
typename std::enable_if<std::is_array<T>::value && std::extent<T>::value == 0,
                        std::unique_ptr<T>>::type
make_unique(std::size_t size) {
  typedef typename std::remove_extent<T>::type element_type;
  return std::unique_ptr<T>(new element_type[size]());
}

// Disable make_unique for arrays with known bounds
template <typename T, typename... Args>
typename std::enable_if<std::extent<T>::value != 0, void>::type make_unique(
    Args&&...) = delete;

}  // namespace mcp

#endif  // MCP_MEMORY_UTILS_H