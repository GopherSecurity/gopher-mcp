#ifndef MCP_TYPE_HELPERS_H
#define MCP_TYPE_HELPERS_H

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "mcp/compat.h"

namespace mcp {

// Forward declarations for MCP types
struct TextContent;
struct ImageContent;
struct ResourceContent;
struct Resource;
struct Tool;
struct Prompt;
struct Error;

// Protocol types are defined in types.h

// CTAD alternatives - Factory functions that deduce template parameters

// For optional types - already have make_optional in optional.h
// Additional helpers for common MCP patterns
template <typename T>
constexpr optional<typename std::decay<T>::type> opt(T&& value) {
  return mcp::make_optional(std::forward<T>(value));
}

// Shorthand for nullopt construction
template <typename T>
constexpr optional<T> none() {
  return nullopt;
}

// For variant types - extend make_variant for common MCP patterns
// Protocol type factory functions moved to types.h

// Content block factories - these will be defined in types.h after the types
// are complete
template <typename T>
variant<TextContent, ImageContent, ResourceContent> make_content_block(
    T&& content);

// Discriminated union helpers for type-based discrimination
template <typename... Types>
struct TypeDiscriminator {
  using VariantType = variant<Types...>;

  template <typename T>
  static VariantType create(T&& value) {
    return VariantType(std::forward<T>(value));
  }

  template <typename T>
  static bool is_type(const VariantType& v) {
    return v.template holds_alternative<T>();
  }

  template <typename T>
  static const T* get_if(const VariantType& v) {
    return v.template get_if<T>();
  }

  template <typename T>
  static T* get_if(VariantType& v) {
    return v.template get_if<T>();
  }
};

// Method-based discriminator for JSON-RPC messages
template <typename... Types>
struct MethodDiscriminator {
  using VariantType = variant<Types...>;
  std::string method;
  VariantType data;

  template <typename T>
  MethodDiscriminator(const std::string& m, T&& d)
      : method(m), data(std::forward<T>(d)) {}

  template <typename T>
  static MethodDiscriminator create(const std::string& method, T&& value) {
    return MethodDiscriminator(method, std::forward<T>(value));
  }

  bool has_method(const std::string& m) const { return method == m; }

  template <typename T>
  bool is_type() const {
    return data.template holds_alternative<T>();
  }

  template <typename T>
  const T* get_if() const {
    return data.template get_if<T>();
  }

  template <typename T>
  T* get_if() {
    return data.template get_if<T>();
  }
};

// Helper for creating method-based messages
template <typename T>
auto make_method_notification(const std::string& method, T&& params)
    -> MethodDiscriminator<typename std::decay<T>::type> {
  return MethodDiscriminator<typename std::decay<T>::type>::create(
      method, std::forward<T>(params));
}

// make_method_request moved to types.h as it depends on RequestId

// Enum helpers moved to types.h for protocol consistency

// Extensible metadata pattern for [key: string]: unknown
using Metadata = std::map<
    std::string,
    variant<
        std::nullptr_t,  // null
        bool,
        int,
        double,
        std::string,
        std::vector<variant<std::nullptr_t, bool, int, double, std::string>>,
        std::map<std::string,
                 variant<std::nullptr_t, bool, int, double, std::string>>>>;

// Factory for metadata
inline Metadata make_metadata() { return Metadata(); }

template <typename... Args>
Metadata make_metadata(Args&&... args) {
  return Metadata{std::forward<Args>(args)...};
}

// Helper to add metadata entries with type deduction
template <typename T>
void add_metadata(Metadata& m, const std::string& key, T&& value) {
  m[key] = std::forward<T>(value);
}

// Capability helpers with nested optionals
template <typename T>
struct Capability {
  optional<T> experimental;

  Capability() = default;

  explicit Capability(T&& exp)
      : experimental(make_optional(std::forward<T>(exp))) {}

  static Capability with_experimental(T&& value) {
    return Capability(std::forward<T>(value));
  }

  static Capability without_experimental() { return Capability(); }
};

// Factory for capabilities
template <typename T>
Capability<T> make_capability() {
  return Capability<T>::without_experimental();
}

template <typename T>
Capability<T> make_capability(T&& experimental) {
  return Capability<T>::with_experimental(std::forward<T>(experimental));
}

// Result/Error pattern helpers - defined after Error type is complete
template <typename T>
struct ResultType {
  using type = variant<T, Error>;
};

template <typename T>
using Result = typename ResultType<T>::type;

// Array factory helpers
template <typename T>
std::vector<T> make_array() {
  return std::vector<T>();
}

template <typename T>
std::vector<T> make_array(std::initializer_list<T> init) {
  return std::vector<T>(init);
}

template <typename T, typename... Args>
std::vector<T> make_array(Args&&... args) {
  return std::vector<T>{std::forward<Args>(args)...};
}

// Object factory helpers for complex nested structures
template <typename T>
class ObjectBuilder {
 public:
  T object;

  ObjectBuilder() = default;

  template <typename U>
  ObjectBuilder& set(U T::*member, U&& value) {
    object.*member = std::forward<U>(value);
    return *this;
  }

  template <typename U>
  ObjectBuilder& set_optional(optional<U> T::*member, U&& value) {
    object.*member = make_optional(std::forward<U>(value));
    return *this;
  }

  T build() && { return std::move(object); }

  T build() const& { return object; }
};

template <typename T>
ObjectBuilder<T> make_object() {
  return ObjectBuilder<T>();
}

// String literal helper for compile-time string matching
template <size_t N>
struct string_literal {
  char value[N];

  constexpr string_literal(const char (&str)[N]) {
    for (size_t i = 0; i < N; ++i) {
      value[i] = str[i];
    }
  }

  constexpr const char* c_str() const { return value; }
  constexpr size_t size() const { return N - 1; }

  constexpr bool operator==(const string_literal& other) const {
    for (size_t i = 0; i < N; ++i) {
      if (value[i] != other.value[i])
        return false;
    }
    return true;
  }
};

// Deduction guide for C++17, but we'll use make function for C++14
template <size_t N>
constexpr string_literal<N> make_string_literal(const char (&str)[N]) {
  return string_literal<N>(str);
}

// Type trait helpers for SFINAE
template <typename T>
using remove_cvref_t =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <typename T, typename U>
using is_same_decayed = std::is_same<remove_cvref_t<T>, remove_cvref_t<U>>;

// Helper for variant visitation with type safety
template <typename Variant, typename... Visitors>
auto match(Variant&& v, Visitors&&... visitors)
    -> decltype(visit(make_overload(std::forward<Visitors>(visitors)...),
                      std::forward<Variant>(v))) {
  return visit(make_overload(std::forward<Visitors>(visitors)...),
               std::forward<Variant>(v));
}

}  // namespace mcp

#endif  // MCP_TYPE_HELPERS_H
