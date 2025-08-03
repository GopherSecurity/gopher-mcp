#ifndef MCP_COMPAT_H
#define MCP_COMPAT_H

// Compatibility layer to use std::optional/variant in C++17 or later,
// falling back to mcp:: implementations for C++14

#include <cstddef>

// Check C++ version and feature availability
// Can be overridden by CMake definition
#ifndef MCP_USE_STD_OPTIONAL_VARIANT
  #if defined(__cplusplus) && __cplusplus >= 201703L
    #define MCP_USE_STD_OPTIONAL_VARIANT 1
  #else
    #define MCP_USE_STD_OPTIONAL_VARIANT 0
  #endif
#endif

#define MCP_HAS_STD_OPTIONAL MCP_USE_STD_OPTIONAL_VARIANT
#define MCP_HAS_STD_VARIANT MCP_USE_STD_OPTIONAL_VARIANT

// Include appropriate headers based on availability
#if MCP_HAS_STD_OPTIONAL
  #include <optional>
#else
  #include "optional.h"
#endif

#if MCP_HAS_STD_VARIANT
  #include <variant>
#else
  #include "variant.h"
#endif

namespace mcp {

// Type aliases that resolve to either std:: or mcp:: versions
#if MCP_HAS_STD_OPTIONAL
  template <typename T>
  using optional = std::optional<T>;
  
  using nullopt_t = std::nullopt_t;
  inline constexpr auto nullopt = std::nullopt;
  
  using in_place_t = std::in_place_t;
  inline constexpr auto in_place = std::in_place;
  
  using bad_optional_access = std::bad_optional_access;
  
  // make_optional helpers
  template <typename T>
  constexpr optional<typename std::decay<T>::type> make_optional(T&& value) {
    return std::make_optional(std::forward<T>(value));
  }
  
  template <typename T, typename... Args>
  constexpr optional<T> make_optional(Args&&... args) {
    return std::make_optional<T>(std::forward<Args>(args)...);
  }
  
  template <typename T, typename U, typename... Args>
  constexpr optional<T> make_optional(std::initializer_list<U> il, Args&&... args) {
    return std::make_optional<T>(il, std::forward<Args>(args)...);
  }
#else
  // Use mcp:: implementations (already defined in optional.h)
  // Just need to ensure they're in the mcp namespace
#endif

#if MCP_HAS_STD_VARIANT
  template <typename... Types>
  using variant = std::variant<Types...>;
  
  using bad_variant_access = std::bad_variant_access;
  
  template <typename T, typename... Types>
  constexpr T* get_if(variant<Types...>* v) noexcept {
    return std::get_if<T>(v);
  }
  
  template <typename T, typename... Types>
  constexpr const T* get_if(const variant<Types...>* v) noexcept {
    return std::get_if<T>(v);
  }
  
  template <typename T, typename... Types>
  constexpr T& get(variant<Types...>& v) {
    return std::get<T>(v);
  }
  
  template <typename T, typename... Types>
  constexpr const T& get(const variant<Types...>& v) {
    return std::get<T>(v);
  }
  
  template <typename T, typename... Types>
  constexpr T&& get(variant<Types...>&& v) {
    return std::get<T>(std::move(v));
  }
  
  template <typename T, typename... Types>
  constexpr const T&& get(const variant<Types...>&& v) {
    return std::get<T>(std::move(v));
  }
  
  template <typename T, typename... Types>
  constexpr bool holds_alternative(const variant<Types...>& v) noexcept {
    return std::holds_alternative<T>(v);
  }
  
  template <typename Visitor, typename... Variants>
  constexpr decltype(auto) visit(Visitor&& vis, Variants&&... vars) {
    return std::visit(std::forward<Visitor>(vis), std::forward<Variants>(vars)...);
  }
  
  // Helper for index-based operations
  template <std::size_t I, typename... Types>
  constexpr auto& get(variant<Types...>& v) {
    return std::get<I>(v);
  }
  
  template <std::size_t I, typename... Types>
  constexpr const auto& get(const variant<Types...>& v) {
    return std::get<I>(v);
  }
  
  template <std::size_t I, typename... Types>
  constexpr auto&& get(variant<Types...>&& v) {
    return std::get<I>(std::move(v));
  }
  
  template <std::size_t I, typename... Types>
  constexpr const auto&& get(const variant<Types...>&& v) {
    return std::get<I>(std::move(v));
  }
  
  template <std::size_t I, typename... Types>
  constexpr auto* get_if(variant<Types...>* v) noexcept {
    return std::get_if<I>(v);
  }
  
  template <std::size_t I, typename... Types>
  constexpr const auto* get_if(const variant<Types...>* v) noexcept {
    return std::get_if<I>(v);
  }
#else
  // Use mcp:: implementations and provide std-like free functions
  
  // holds_alternative
  template <typename T, typename... Types>
  constexpr bool holds_alternative(const variant<Types...>& v) noexcept {
    return v.template holds_alternative<T>();
  }
  
  // get_if
  template <typename T, typename... Types>
  constexpr T* get_if(variant<Types...>* v) noexcept {
    return v ? v->template get_if<T>() : nullptr;
  }
  
  template <typename T, typename... Types>
  constexpr const T* get_if(const variant<Types...>* v) noexcept {
    return v ? v->template get_if<T>() : nullptr;
  }
  
  // get
  template <typename T, typename... Types>
  constexpr T& get(variant<Types...>& v) {
    auto* ptr = v.template get_if<T>();
    if (!ptr) {
      throw bad_variant_access();
    }
    return *ptr;
  }
  
  template <typename T, typename... Types>
  constexpr const T& get(const variant<Types...>& v) {
    auto* ptr = v.template get_if<T>();
    if (!ptr) {
      throw bad_variant_access();
    }
    return *ptr;
  }
  
  template <typename T, typename... Types>
  constexpr T&& get(variant<Types...>&& v) {
    auto* ptr = v.template get_if<T>();
    if (!ptr) {
      throw bad_variant_access();
    }
    return std::move(*ptr);
  }
  
  template <typename T, typename... Types>
  constexpr const T&& get(const variant<Types...>&& v) {
    auto* ptr = v.template get_if<T>();
    if (!ptr) {
      throw bad_variant_access();
    }
    return std::move(*ptr);
  }
  
  // Note: Index-based get/get_if are not provided for C++14 as they require
  // complex template metaprogramming to deduce the return type
  
  // visit - only single variant supported in C++14
  template <typename Visitor, typename Variant>
  constexpr decltype(auto) visit(Visitor&& vis, Variant&& var) {
    return var.visit(std::forward<Visitor>(vis));
  }
#endif

} // namespace mcp

#endif // MCP_COMPAT_H