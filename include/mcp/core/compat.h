#ifndef MCP_COMPAT_H
#define MCP_COMPAT_H

// Compatibility layer to use std::optional/variant in C++17 or later,
// falling back to mcp:: implementations for C++14

#include <cstddef>
#include <type_traits>

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

// Import std functions into mcp namespace
using std::make_optional;
#else
// Use mcp:: implementations (already defined in optional.h)
// Just need to ensure they're in the mcp namespace
#endif

#if MCP_HAS_STD_VARIANT
template <typename... Types>
using variant = std::variant<Types...>;

using bad_variant_access = std::bad_variant_access;

// Import std functions into mcp namespace for ADL
using std::get;
using std::get_if;
using std::holds_alternative;
using std::visit;
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

// Type traits compatibility for C++14/17
// C++17 has std::is_same_v, C++14 needs ::value
#if __cplusplus >= 201703L
template <typename T, typename U>
inline constexpr bool is_same_v = std::is_same_v<T, U>;
#else
template <typename T, typename U>
struct is_same_v_helper {
  static constexpr bool value = std::is_same<T, U>::value;
};
template <typename T, typename U>
constexpr bool is_same_v = is_same_v_helper<T, U>::value;
#endif

}  // namespace mcp

#endif  // MCP_COMPAT_H