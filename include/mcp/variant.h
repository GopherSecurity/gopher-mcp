#ifndef MCP_VARIANT_H
#define MCP_VARIANT_H

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace mcp {

// Forward declaration for union storage
namespace detail {
template <typename... Types>
union variant_union;
}  // namespace detail

// Forward declarations
template <typename... Types>
class variant;

// Helper to get the index of a type in a type list
// Use enum to avoid ODR violations in C++14
template <typename T, typename... Types>
struct type_index;

template <typename T, typename First, typename... Rest>
struct type_index<T, First, Rest...> {
  enum {
    value =
        std::is_same<T, First>::value ? 0 : 1 + type_index<T, Rest...>::value
  };
};

template <typename T, typename Last>
struct type_index<T, Last> {
  enum { value = std::is_same<T, Last>::value ? 0 : 1 };
};

// Helper to check if a type is in a type list
template <typename T, typename... Types>
struct contains_type;

template <typename T>
struct contains_type<T> : std::false_type {};

template <typename T, typename First, typename... Rest>
struct contains_type<T, First, Rest...>
    : std::conditional<std::is_same<T, First>::value,
                       std::true_type,
                       contains_type<T, Rest...>>::type {};

// Helper to check if a type is convertible to any type in the list
template <typename T, typename... Types>
struct is_convertible_to_any;

template <typename T>
struct is_convertible_to_any<T> : std::false_type {};

template <typename T, typename First, typename... Rest>
struct is_convertible_to_any<T, First, Rest...>
    : std::conditional<std::is_convertible<T, First>::value,
                       std::true_type,
                       is_convertible_to_any<T, Rest...>>::type {};

// Helper to detect if type is a C-style string
template <typename T>
struct is_c_string : std::false_type {};

template <std::size_t N>
struct is_c_string<const char[N]> : std::true_type {};

template <std::size_t N>
struct is_c_string<char[N]> : std::true_type {};

template <std::size_t N>
struct is_c_string<const char (&)[N]> : std::true_type {};

template <std::size_t N>
struct is_c_string<char (&)[N]> : std::true_type {};

template <>
struct is_c_string<const char*> : std::true_type {};

template <>
struct is_c_string<char*> : std::true_type {};

// Helper to find the first type that T is convertible to
// For C-style strings, prefer std::string if available
template <typename T, std::size_t I, typename... Types>
struct find_convertible_type;

template <typename T, std::size_t I>
struct find_convertible_type<T, I> {
  enum { index = static_cast<std::size_t>(-1) };
  using type = void;
};

template <typename T, std::size_t I, typename First, typename... Rest>
struct find_convertible_type<T, I, First, Rest...> {
 private:
  // For C-style strings, only consider std::string as convertible
  static constexpr bool is_conv =
      is_c_string<typename std::decay<T>::type>::value
          ? std::is_same<First, std::string>::value
          : std::is_convertible<T, First>::value;
  using rest_result = find_convertible_type<T, I + 1, Rest...>;

 public:
  enum { index = is_conv ? I : rest_result::index };
  using type = typename std::
      conditional<is_conv, First, typename rest_result::type>::type;
};

// Helper to get the type at a given index
template <std::size_t I, typename... Types>
struct type_at_index;

template <std::size_t I, typename First, typename... Rest>
struct type_at_index<I, First, Rest...> {
  using type = typename type_at_index<I - 1, Rest...>::type;
};

template <typename First, typename... Rest>
struct type_at_index<0, First, Rest...> {
  using type = First;
};

// Helper to get the maximum size and alignment
// Use enum to avoid ODR violations
template <typename... Types>
struct variant_storage_traits;

template <typename T>
struct variant_storage_traits<T> {
  enum { size = sizeof(T) };
  enum { alignment = alignof(T) };
};

template <typename T, typename... Rest>
struct variant_storage_traits<T, Rest...> {
  enum {
    size = sizeof(T) > variant_storage_traits<Rest...>::size
               ? sizeof(T)
               : variant_storage_traits<Rest...>::size
  };
  enum {
    alignment = alignof(T) > variant_storage_traits<Rest...>::alignment
                    ? alignof(T)
                    : variant_storage_traits<Rest...>::alignment
  };
};

// Check if all types have nothrow move constructors
template <typename... Types>
struct all_nothrow_move_constructible;

template <>
struct all_nothrow_move_constructible<> : std::true_type {};

template <typename T, typename... Rest>
struct all_nothrow_move_constructible<T, Rest...>
    : std::conditional<std::is_nothrow_move_constructible<T>::value,
                       all_nothrow_move_constructible<Rest...>,
                       std::false_type>::type {};

// Check if all types have nothrow destructors
template <typename... Types>
struct all_nothrow_destructible;

template <>
struct all_nothrow_destructible<> : std::true_type {};

template <typename T, typename... Rest>
struct all_nothrow_destructible<T, Rest...>
    : std::conditional<std::is_nothrow_destructible<T>::value,
                       all_nothrow_destructible<Rest...>,
                       std::false_type>::type {};

// Visitor helper
template <typename Visitor, typename... Types>
struct visitor_helper;

// Bad variant access exception
class bad_variant_access : public std::exception {
 public:
  const char* what() const noexcept override { return "bad variant access"; }
};

// Union-based storage implementation
namespace detail {

// Base case - single type
template <typename T>
union variant_union<T> {
  char dummy;
  T value;

  // Constructors/destructor must be explicit for unions
  constexpr variant_union() : dummy() {}
  ~variant_union() {}
};

// Recursive case - multiple types
template <typename T, typename... Rest>
union variant_union<T, Rest...> {
  char dummy;
  T value;
  variant_union<Rest...> rest;

  constexpr variant_union() : dummy() {}
  ~variant_union() {}
};

// Helper to access union element by index
template <std::size_t I, typename Union>
struct union_accessor;

// Access element at index 0
template <typename T, typename... Rest>
struct union_accessor<0, variant_union<T, Rest...>> {
  using type = T;

  static T& get(variant_union<T, Rest...>& u) { return u.value; }

  static const T& get(const variant_union<T, Rest...>& u) { return u.value; }

  static T* get_ptr(variant_union<T, Rest...>& u) {
    return std::addressof(u.value);
  }

  static const T* get_ptr(const variant_union<T, Rest...>& u) {
    return std::addressof(u.value);
  }
};

// Access element at index > 0
template <std::size_t I, typename T, typename... Rest>
struct union_accessor<I, variant_union<T, Rest...>> {
  using type = typename union_accessor<I - 1, variant_union<Rest...>>::type;

  static typename union_accessor<I - 1, variant_union<Rest...>>::type& get(
      variant_union<T, Rest...>& u) {
    return union_accessor<I - 1, variant_union<Rest...>>::get(u.rest);
  }

  static const typename union_accessor<I - 1, variant_union<Rest...>>::type&
  get(const variant_union<T, Rest...>& u) {
    return union_accessor<I - 1, variant_union<Rest...>>::get(u.rest);
  }

  static typename union_accessor<I - 1, variant_union<Rest...>>::type* get_ptr(
      variant_union<T, Rest...>& u) {
    return union_accessor<I - 1, variant_union<Rest...>>::get_ptr(u.rest);
  }

  static const typename union_accessor<I - 1, variant_union<Rest...>>::type*
  get_ptr(const variant_union<T, Rest...>& u) {
    return union_accessor<I - 1, variant_union<Rest...>>::get_ptr(u.rest);
  }
};

// Specialization for single type union
template <typename T>
struct union_accessor<0, variant_union<T>> {
  using type = T;

  static T& get(variant_union<T>& u) { return u.value; }

  static const T& get(const variant_union<T>& u) { return u.value; }

  static T* get_ptr(variant_union<T>& u) { return std::addressof(u.value); }

  static const T* get_ptr(const variant_union<T>& u) {
    return std::addressof(u.value);
  }
};

}  // namespace detail

// Main variant class
template <typename... Types>
class variant {
 private:
  // Use union storage instead of aligned_storage
  detail::variant_union<Types...> storage_;
  std::size_t type_index_;

  // Helper to construct at specific index
  template <typename T>
  void construct_at_index(std::size_t index, T&& value) {
    construct_at_index_impl<0>(index, std::forward<T>(value));
  }

  template <std::size_t I, typename T>
  typename std::enable_if<I == sizeof...(Types)>::type construct_at_index_impl(
      std::size_t, T&&) {
    throw bad_variant_access();
  }

  template <std::size_t I, typename T>
      typename std::enable_if <
      I<sizeof...(Types)>::type construct_at_index_impl(std::size_t index,
                                                        T&& value) {
    if (index == I) {
      using TargetType = typename type_at_index<I, Types...>::type;
      new (detail::union_accessor<I, detail::variant_union<Types...>>::get_ptr(
          storage_)) TargetType(std::forward<T>(value));
    } else {
      construct_at_index_impl<I + 1>(index, std::forward<T>(value));
    }
  }

  // Helper to construct with conversion at specific index
  template <std::size_t I, typename T>
  typename std::enable_if<I == sizeof...(Types)>::type construct_at_index_conv(
      std::size_t, T&&) {
    throw bad_variant_access();
  }

  template <std::size_t I, typename T>
      typename std::enable_if <
      I<sizeof...(Types)>::type construct_at_index_conv(std::size_t index,
                                                        T&& value) {
    if (index == I) {
      using TargetType = typename type_at_index<I, Types...>::type;
      new (detail::union_accessor<I, detail::variant_union<Types...>>::get_ptr(
          storage_)) TargetType(std::forward<T>(value));
    } else {
      construct_at_index_conv<I + 1>(index, std::forward<T>(value));
    }
  }

  // Direct construction helper
  template <typename TargetType, typename T>
  void construct_direct(std::size_t index, T&& value) {
    construct_direct_impl<TargetType, 0>(index, std::forward<T>(value));
  }

  template <typename TargetType, std::size_t I, typename T>
  typename std::enable_if<I == sizeof...(Types)>::type construct_direct_impl(
      std::size_t, T&&) {
    throw bad_variant_access();
  }

  template <typename TargetType, std::size_t I, typename T>
      typename std::enable_if <
      I<sizeof...(Types)>::type construct_direct_impl(std::size_t index,
                                                      T&& value) {
    if (index == I) {
      using IndexType = typename type_at_index<I, Types...>::type;
      // Only construct if types match
      construct_if_same<TargetType, IndexType, I>(std::forward<T>(value));
    } else {
      construct_direct_impl<TargetType, I + 1>(index, std::forward<T>(value));
    }
  }

  template <typename TargetType, typename IndexType, std::size_t I, typename T>
  typename std::enable_if<std::is_same<TargetType, IndexType>::value>::type
  construct_if_same(T&& value) {
    new (detail::union_accessor<I, detail::variant_union<Types...>>::get_ptr(
        storage_)) TargetType(std::forward<T>(value));
  }

  template <typename TargetType, typename IndexType, std::size_t I, typename T>
  typename std::enable_if<!std::is_same<TargetType, IndexType>::value>::type
  construct_if_same(T&&) {
    // Type mismatch - this should never be called
    throw bad_variant_access();
  }

  // Destructor dispatcher
  template <std::size_t I = 0>
  typename std::enable_if<I == sizeof...(Types)>::type destroy_impl() {}

  template <std::size_t I = 0>
      typename std::enable_if < I<sizeof...(Types)>::type destroy_impl() {
    if (type_index_ == I) {
      using T = typename type_at_index<I, Types...>::type;
      detail::union_accessor<I, detail::variant_union<Types...>>::get(storage_)
          .~T();
    } else {
      destroy_impl<I + 1>();
    }
  }

  // Copy constructor dispatcher - exception safe
  template <std::size_t I = 0>
  typename std::enable_if<I == sizeof...(Types)>::type copy_construct_impl(
      const variant&) {
    // Should never reach here if variant is valid
    throw bad_variant_access();
  }

  template <std::size_t I = 0>
      typename std::enable_if <
      I<sizeof...(Types)>::type copy_construct_impl(const variant& other) {
    if (other.type_index_ == I) {
      using T = typename type_at_index<I, Types...>::type;
      new (detail::union_accessor<I, detail::variant_union<Types...>>::get_ptr(
          storage_))
          T(detail::union_accessor<I, detail::variant_union<Types...>>::get(
              other.storage_));
      type_index_ = I;  // Set index AFTER successful construction
    } else {
      copy_construct_impl<I + 1>(other);
    }
  }

  // Move constructor dispatcher - exception safe
  template <std::size_t I = 0>
  typename std::enable_if<I == sizeof...(Types)>::type move_construct_impl(
      variant&&) {
    // Should never reach here if variant is valid
    throw bad_variant_access();
  }

  template <std::size_t I = 0>
      typename std::enable_if <
      I<sizeof...(Types)>::type move_construct_impl(variant&& other) {
    if (other.type_index_ == I) {
      using T = typename type_at_index<I, Types...>::type;
      new (detail::union_accessor<I, detail::variant_union<Types...>>::get_ptr(
          storage_))
          T(std::move(
              detail::union_accessor<I, detail::variant_union<Types...>>::get(
                  other.storage_)));
      type_index_ = I;  // Set index AFTER successful construction
    } else {
      move_construct_impl<I + 1>(std::move(other));
    }
  }

 public:
  // Default constructor - constructs the first alternative
  variant() : type_index_(0) {
    using T = typename type_at_index<0, Types...>::type;
    new (detail::union_accessor<0, detail::variant_union<Types...>>::get_ptr(
        storage_)) T();
  }

  // Constructor from a value - exact match
  template <
      typename T,
      typename = typename std::enable_if<
          contains_type<typename std::decay<T>::type, Types...>::value>::type>
  variant(T&& value)
      : type_index_(type_index<typename std::decay<T>::type, Types...>::value) {
    using DecayedT = typename std::decay<T>::type;
    constexpr std::size_t idx = type_index<DecayedT, Types...>::value;
    new (detail::union_accessor<idx, detail::variant_union<Types...>>::get_ptr(
        storage_)) DecayedT(std::forward<T>(value));
  }

  // Constructor from a value - implicit conversion (with disambiguation)
  template <
      typename T,
      typename = typename std::enable_if<
          !contains_type<typename std::decay<T>::type, Types...>::value>::type,
      typename = typename std::enable_if<
          is_convertible_to_any<T, Types...>::value>::type,
      int = 0>  // Disambiguation parameter
  variant(T&& value) {
    using Helper = find_convertible_type<T, 0, Types...>;
    using TargetType = typename Helper::type;
    type_index_ = Helper::index;
    // Directly construct the target type without going through index dispatch
    construct_direct<TargetType>(Helper::index, std::forward<T>(value));
  }

  // Copy constructor
  variant(const variant& other) : type_index_(static_cast<std::size_t>(-1)) {
    copy_construct_impl(other);
  }

  // Move constructor - conditional noexcept based on contained types
  variant(variant&& other) noexcept(
      all_nothrow_move_constructible<Types...>::value&&
          all_nothrow_destructible<Types...>::value)
      : type_index_(static_cast<std::size_t>(-1)) {
    move_construct_impl(std::move(other));
  }

  // Destructor
  ~variant() { destroy_impl(); }

  // Copy assignment - exception safe using copy-and-swap idiom
  variant& operator=(const variant& other) {
    if (this != &other) {
      variant tmp(other);
      swap(tmp);
    }
    return *this;
  }

  // Move assignment - conditional noexcept
  variant& operator=(variant&& other) noexcept(
      all_nothrow_move_constructible<Types...>::value&&
          all_nothrow_destructible<Types...>::value) {
    if (this != &other) {
      destroy_impl();
      move_construct_impl(std::move(other));
    }
    return *this;
  }

  // Assignment from value - exact match
  template <
      typename T,
      typename = typename std::enable_if<
          contains_type<typename std::decay<T>::type, Types...>::value>::type>
  variant& operator=(T&& value) {
    using DecayedT = typename std::decay<T>::type;
    destroy_impl();
    constexpr std::size_t idx = type_index<DecayedT, Types...>::value;
    new (detail::union_accessor<idx, detail::variant_union<Types...>>::get_ptr(
        storage_)) DecayedT(std::forward<T>(value));
    type_index_ = idx;
    return *this;
  }

  // Assignment from value - implicit conversion
  template <
      typename T,
      typename = typename std::enable_if<
          !contains_type<typename std::decay<T>::type, Types...>::value>::type,
      typename = typename std::enable_if<
          is_convertible_to_any<T, Types...>::value>::type,
      typename = void>
  variant& operator=(T&& value) {
    using Helper = find_convertible_type<T, 0, Types...>;
    using TargetType = typename Helper::type;
    destroy_impl();
    construct_direct<TargetType>(Helper::index, std::forward<T>(value));
    type_index_ = Helper::index;
    return *this;
  }

  // Get the index of the current alternative
  std::size_t index() const noexcept { return type_index_; }

  // Check if the variant holds a specific type
  template <typename T>
  bool holds_alternative() const noexcept {
    return type_index_ == type_index<T, Types...>::value;
  }

  // Get a reference to the stored value
  template <typename T>
  T& get() {
    if (!holds_alternative<T>()) {
      throw bad_variant_access();
    }
    constexpr std::size_t idx = type_index<T, Types...>::value;
    return detail::union_accessor<idx, detail::variant_union<Types...>>::get(
        storage_);
  }

  template <typename T>
  const T& get() const {
    if (!holds_alternative<T>()) {
      throw bad_variant_access();
    }
    constexpr std::size_t idx = type_index<T, Types...>::value;
    return detail::union_accessor<idx, detail::variant_union<Types...>>::get(
        storage_);
  }

  // Get a pointer to the stored value (returns nullptr if wrong type)
  template <typename T>
  T* get_if() noexcept {
    if (!holds_alternative<T>()) {
      return nullptr;
    }
    constexpr std::size_t idx = type_index<T, Types...>::value;
    return detail::union_accessor<
        idx, detail::variant_union<Types...>>::get_ptr(storage_);
  }

  template <typename T>
  const T* get_if() const noexcept {
    if (!holds_alternative<T>()) {
      return nullptr;
    }
    constexpr std::size_t idx = type_index<T, Types...>::value;
    return detail::union_accessor<
        idx, detail::variant_union<Types...>>::get_ptr(storage_);
  }

  // Swap implementation
  void swap(variant& other) {
    if (this == &other)
      return;

    if (type_index_ == other.type_index_) {
      swap_same_type(other);
    } else {
      // Use three-way swap for different types
      variant tmp(std::move(other));
      other = std::move(*this);
      *this = std::move(tmp);
    }
  }

 private:
  // Helper to swap same type
  template <std::size_t I = 0>
  typename std::enable_if<I == sizeof...(Types)>::type swap_same_type(
      variant&) {}

  template <std::size_t I = 0>
      typename std::enable_if <
      I<sizeof...(Types)>::type swap_same_type(variant& other) {
    if (type_index_ == I) {
      using std::swap;
      swap(detail::union_accessor<I, detail::variant_union<Types...>>::get(
               storage_),
           detail::union_accessor<I, detail::variant_union<Types...>>::get(
               other.storage_));
    } else {
      swap_same_type<I + 1>(other);
    }
  }

 public:
  // Get by index helper for visitor
  template <std::size_t I>
  typename type_at_index<I, Types...>::type& get_by_index() {
    if (type_index_ != I) {
      throw bad_variant_access();
    }
    return detail::union_accessor<I, detail::variant_union<Types...>>::get(
        storage_);
  }

  template <std::size_t I>
  const typename type_at_index<I, Types...>::type& get_by_index() const {
    if (type_index_ != I) {
      throw bad_variant_access();
    }
    return detail::union_accessor<I, detail::variant_union<Types...>>::get(
        storage_);
  }
  // Visit helper - moved outside class to avoid incomplete type issues
};

// Visit functions - defined outside class
template <typename Visitor, typename... Types>
auto visit(Visitor&& vis, variant<Types...>& v)
    -> decltype(vis(v.template get_by_index<0>())) {
  return visitor_helper<Visitor, Types...>::visit(std::forward<Visitor>(vis), v,
                                                  v.index());
}

template <typename Visitor, typename... Types>
auto visit(Visitor&& vis, const variant<Types...>& v)
    -> decltype(vis(v.template get_by_index<0>())) {
  return visitor_helper<Visitor, Types...>::visit(std::forward<Visitor>(vis), v,
                                                  v.index());
}

// Visitor implementation
template <typename Visitor, typename... Types>
struct visitor_helper {
  template <typename Variant, std::size_t I = 0>
  static typename std::enable_if<
      I == sizeof...(Types) - 1,
      decltype(std::declval<Visitor>()(
          std::declval<Variant>().template get_by_index<I>()))>::type
  visit(Visitor&& vis, Variant&& v, std::size_t) {
    return vis(std::forward<Variant>(v).template get_by_index<I>());
  }

  template <typename Variant, std::size_t I = 0>
      static typename std::enable_if <
      I<sizeof...(Types) - 1,
        decltype(std::declval<Visitor>()(
            std::declval<Variant>().template get_by_index<0>()))>::type
      visit(Visitor&& vis, Variant&& v, std::size_t index) {
    if (index == I) {
      return vis(std::forward<Variant>(v).template get_by_index<I>());
    }
    return visit<Variant, I + 1>(std::forward<Visitor>(vis),
                                 std::forward<Variant>(v), index);
  }
};

// Helper function to create a variant (similar to std::make_variant)
template <typename T, typename... Types>
variant<Types...> make_variant(T&& value) {
  return variant<Types...>(std::forward<T>(value));
}

// Overload pattern for visitor - C++11 compatible version
template <typename F, typename... Fs>
struct overload_impl : F, overload_impl<Fs...> {
  overload_impl(F f, Fs... fs) : F(f), overload_impl<Fs...>(fs...) {}
  using F::operator();
  using overload_impl<Fs...>::operator();
};

template <typename F>
struct overload_impl<F> : F {
  overload_impl(F f) : F(f) {}
  using F::operator();
};

template <typename... Fs>
overload_impl<Fs...> make_overload(Fs... fs) {
  return overload_impl<Fs...>(fs...);
}

// swap free function
template <typename... Types>
void swap(variant<Types...>& lhs,
          variant<Types...>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// No out-of-class definitions needed with enum approach

}  // namespace mcp

#endif  // MCP_VARIANT_H
