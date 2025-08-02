#ifndef MCP_VARIANT_H
#define MCP_VARIANT_H

#include <new>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace mcp {

// Forward declarations
template <typename... Types>
class variant;

// Helper to get the index of a type in a type list
// Use enum to avoid ODR violations in C++14
template <typename T, typename... Types>
struct type_index;

template <typename T, typename First, typename... Rest>
struct type_index<T, First, Rest...> {
  enum { value = std::is_same<T, First>::value ? 0 : 1 + type_index<T, Rest...>::value };
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

// Helper to find the first type that T is convertible to
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
  static constexpr bool is_conv = std::is_convertible<T, First>::value;
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
    : std::conditional<
          std::is_nothrow_move_constructible<T>::value,
          all_nothrow_move_constructible<Rest...>,
          std::false_type>::type {};

// Check if all types have nothrow destructors
template <typename... Types>
struct all_nothrow_destructible;

template <>
struct all_nothrow_destructible<> : std::true_type {};

template <typename T, typename... Rest>
struct all_nothrow_destructible<T, Rest...>
    : std::conditional<
          std::is_nothrow_destructible<T>::value,
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

// Main variant class
template <typename... Types>
class variant {
 private:
  static constexpr std::size_t storage_size =
      variant_storage_traits<Types...>::size;
  static constexpr std::size_t storage_alignment =
      variant_storage_traits<Types...>::alignment;

  typename std::aligned_storage<storage_size, storage_alignment>::type storage_;
  std::size_t type_index_;

  // Destructor dispatcher
  template <std::size_t I = 0>
  typename std::enable_if<I == sizeof...(Types)>::type destroy_impl() {}

  template <std::size_t I = 0>
      typename std::enable_if < I<sizeof...(Types)>::type destroy_impl() {
    if (type_index_ == I) {
      using T = typename type_at_index<I, Types...>::type;
      reinterpret_cast<T*>(&storage_)->~T();
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
      new (&storage_) T(*reinterpret_cast<const T*>(&other.storage_));
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
      new (&storage_) T(std::move(*reinterpret_cast<T*>(&other.storage_)));
      type_index_ = I;  // Set index AFTER successful construction
    } else {
      move_construct_impl<I + 1>(std::move(other));
    }
  }

 public:
  // Default constructor - constructs the first alternative
  variant() : type_index_(0) {
    using T = typename type_at_index<0, Types...>::type;
    new (&storage_) T();
  }

  // Constructor from a value - exact match
  template <
      typename T,
      typename = typename std::enable_if<
          contains_type<typename std::decay<T>::type, Types...>::value>::type>
  variant(T&& value)
      : type_index_(type_index<typename std::decay<T>::type, Types...>::value) {
    using DecayedT = typename std::decay<T>::type;
    new (&storage_) DecayedT(std::forward<T>(value));
  }

  // Constructor from a value - implicit conversion (with disambiguation)
  template <
      typename T,
      typename = typename std::enable_if<
          !contains_type<typename std::decay<T>::type, Types...>::value>::type,
      typename = typename std::enable_if<
          is_convertible_to_any<T, Types...>::value>::type,
      int = 0>  // Disambiguation parameter
  variant(T&& value)
      : type_index_(find_convertible_type<T, 0, Types...>::index) {
    using TargetType = typename find_convertible_type<T, 0, Types...>::type;
    new (&storage_) TargetType(std::forward<T>(value));
  }

  // Copy constructor
  variant(const variant& other) : type_index_(static_cast<std::size_t>(-1)) {
    copy_construct_impl(other);
  }

  // Move constructor - conditional noexcept based on contained types
  variant(variant&& other) noexcept(
      all_nothrow_move_constructible<Types...>::value &&
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
      all_nothrow_move_constructible<Types...>::value &&
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
    new (&storage_) DecayedT(std::forward<T>(value));
    type_index_ = type_index<DecayedT, Types...>::value;
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
    using TargetType = typename find_convertible_type<T, 0, Types...>::type;
    destroy_impl();
    new (&storage_) TargetType(std::forward<T>(value));
    type_index_ = find_convertible_type<T, 0, Types...>::index;
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
    return *reinterpret_cast<T*>(&storage_);
  }

  template <typename T>
  const T& get() const {
    if (!holds_alternative<T>()) {
      throw bad_variant_access();
    }
    return *reinterpret_cast<const T*>(&storage_);
  }

  // Get a pointer to the stored value (returns nullptr if wrong type)
  template <typename T>
  T* get_if() noexcept {
    if (!holds_alternative<T>()) {
      return nullptr;
    }
    return reinterpret_cast<T*>(&storage_);
  }

  template <typename T>
  const T* get_if() const noexcept {
    if (!holds_alternative<T>()) {
      return nullptr;
    }
    return reinterpret_cast<const T*>(&storage_);
  }

  // Swap implementation
  void swap(variant& other) {
    if (this == &other) return;
    
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
  typename std::enable_if<I == sizeof...(Types)>::type 
  swap_same_type(variant&) {}
  
  template <std::size_t I = 0>
  typename std::enable_if<I < sizeof...(Types)>::type 
  swap_same_type(variant& other) {
    if (type_index_ == I) {
      using std::swap;
      using T = typename type_at_index<I, Types...>::type;
      swap(*reinterpret_cast<T*>(&storage_), 
           *reinterpret_cast<T*>(&other.storage_));
    } else {
      swap_same_type<I + 1>(other);
    }
  }
  
 public:
  // Visit helper - moved outside class to avoid incomplete type issues
};

// Visit functions - defined outside class
template <typename Visitor, typename... Types>
auto visit(Visitor&& vis, variant<Types...>& v) -> decltype(vis(
    v.template get<typename type_at_index<0, Types...>::type>())) {
  return visitor_helper<Visitor, Types...>::visit(std::forward<Visitor>(vis), v,
                                                  v.index());
}

template <typename Visitor, typename... Types>
auto visit(Visitor&& vis, const variant<Types...>& v) -> decltype(vis(
    v.template get<typename type_at_index<0, Types...>::type>())) {
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
          std::declval<Variant>()
              .template get<typename type_at_index<I, Types...>::type>()))>::
      type
      visit(Visitor&& vis, Variant&& v, std::size_t) {
    using T = typename type_at_index<I, Types...>::type;
    return vis(std::forward<Variant>(v).template get<T>());
  }

  template <typename Variant, std::size_t I = 0>
      static typename std::enable_if <
      I<sizeof...(Types) - 1,
        decltype(std::declval<Visitor>()(
            std::declval<Variant>()
                .template get<typename type_at_index<0, Types...>::type>()))>::
          type
          visit(Visitor&& vis, Variant&& v, std::size_t index) {
    if (index == I) {
      using T = typename type_at_index<I, Types...>::type;
      return vis(std::forward<Variant>(v).template get<T>());
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
void swap(variant<Types...>& lhs, variant<Types...>& rhs) 
    noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// No out-of-class definitions needed with enum approach

}  // namespace mcp

#endif  // MCP_VARIANT_H