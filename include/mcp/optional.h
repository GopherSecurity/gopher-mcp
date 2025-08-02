#ifndef MCP_OPTIONAL_H
#define MCP_OPTIONAL_H

#include <algorithm>
#include <cassert>
#include <functional>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mcp {

// nullopt_t - a type to indicate an uninitialized optional
struct nullopt_t {
  // Explicit constructor to prevent implicit conversions
  explicit constexpr nullopt_t(int) noexcept {}
};

// Single instance of nullopt
constexpr nullopt_t nullopt{0};

// in_place_t - a type to enable in-place construction
struct in_place_t {
  explicit constexpr in_place_t() = default;
};

constexpr in_place_t in_place{};

// bad_optional_access exception
class bad_optional_access : public std::exception {
 public:
  const char* what() const noexcept override { return "bad optional access"; }
};

// Forward declaration
template <typename T>
class optional;

// Helper to detect if type is an optional
template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<optional<T>> : std::true_type {};

// Storage for optional - handles trivial and non-trivial types differently
template <typename T, bool = std::is_trivially_destructible<T>::value>
class optional_storage {
 protected:
  union {
    char dummy_;
    T value_;
  };
  bool has_value_;

  constexpr optional_storage() noexcept : dummy_(), has_value_(false) {}

  template <typename... Args>
  constexpr optional_storage(in_place_t, Args&&... args)
      : value_(std::forward<Args>(args)...), has_value_(true) {}

  ~optional_storage() {
    if (has_value_) {
      value_.~T();
    }
  }
};

// Specialization for trivially destructible types
template <typename T>
class optional_storage<T, true> {
 protected:
  union {
    char dummy_;
    T value_;
  };
  bool has_value_;

  constexpr optional_storage() noexcept : dummy_(), has_value_(false) {}

  template <typename... Args>
  constexpr optional_storage(in_place_t, Args&&... args)
      : value_(std::forward<Args>(args)...), has_value_(true) {}

  // Trivial destructor
  ~optional_storage() = default;
};

// Main optional class
template <typename T>
class optional : private optional_storage<T> {
  using base = optional_storage<T>;
  using base::has_value_;
  using base::value_;

  // Helper to check if U is constructible and not the same optional type
  template <typename U>
  using enable_if_constructible_t = typename std::enable_if<
      std::is_constructible<T, U>::value &&
      !std::is_same<optional, typename std::decay<U>::type>::value>::type;

 public:
  using value_type = T;

  // Constructors
  constexpr optional() noexcept : base() {}
  constexpr optional(nullopt_t) noexcept : base() {}

  // Copy constructor
  optional(const optional& other) : base() {
    if (other.has_value_) {
      construct(other.value_);
    }
  }

  // Move constructor
  optional(optional&& other) noexcept(
      std::is_nothrow_move_constructible<T>::value)
      : base() {
    if (other.has_value_) {
      construct(std::move(other.value_));
    }
  }

  // Converting constructors
  template <typename U = T, typename = enable_if_constructible_t<U&&>>
  constexpr optional(U&& value) : base(in_place, std::forward<U>(value)) {}

  // In-place construction
  template <typename... Args,
            typename = typename std::enable_if<
                std::is_constructible<T, Args...>::value>::type>
  constexpr explicit optional(in_place_t, Args&&... args)
      : base(in_place, std::forward<Args>(args)...) {}

  // In-place construction with initializer list
  template <typename U,
            typename... Args,
            typename = typename std::enable_if<
                std::is_constructible<T, std::initializer_list<U>&, Args...>::
                    value>::type>
  constexpr explicit optional(in_place_t,
                              std::initializer_list<U> il,
                              Args&&... args)
      : base(in_place, il, std::forward<Args>(args)...) {}

  // Destructor
  ~optional() = default;

  // Assignment operators
  optional& operator=(nullopt_t) noexcept {
    reset();
    return *this;
  }

  // Copy assignment
  optional& operator=(const optional& other) {
    if (this != &other) {
      if (other.has_value_) {
        if (has_value_) {
          value_ = other.value_;
        } else {
          construct(other.value_);
        }
      } else {
        reset();
      }
    }
    return *this;
  }

  // Move assignment
  optional& operator=(optional&& other) noexcept(
      std::is_nothrow_move_assignable<T>::value&&
          std::is_nothrow_move_constructible<T>::value) {
    if (this != &other) {
      if (other.has_value_) {
        if (has_value_) {
          value_ = std::move(other.value_);
        } else {
          construct(std::move(other.value_));
        }
      } else {
        reset();
      }
    }
    return *this;
  }

  // Converting assignment
  template <typename U = T, typename = enable_if_constructible_t<U&&>>
  optional& operator=(U&& value) {
    if (has_value_) {
      value_ = std::forward<U>(value);
    } else {
      construct(std::forward<U>(value));
    }
    return *this;
  }

  // Observers
  const T* operator->() const { return &value_; }

  T* operator->() { return &value_; }

  const T& operator*() const& { return value_; }

  T& operator*() & { return value_; }

  T&& operator*() && { return std::move(value_); }

  const T&& operator*() const&& { return std::move(value_); }

  constexpr explicit operator bool() const noexcept { return has_value_; }

  constexpr bool has_value() const noexcept { return has_value_; }

  // Checked access
  T& value() & {
    if (!has_value_) {
      throw bad_optional_access();
    }
    return value_;
  }

  const T& value() const& {
    if (!has_value_) {
      throw bad_optional_access();
    }
    return value_;
  }

  T&& value() && {
    if (!has_value_) {
      throw bad_optional_access();
    }
    return std::move(value_);
  }

  const T&& value() const&& {
    if (!has_value_) {
      throw bad_optional_access();
    }
    return std::move(value_);
  }

  // value_or
  template <typename U>
  constexpr T value_or(U&& default_value) const& {
    return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  T value_or(U&& default_value) && {
    return has_value_ ? std::move(value_)
                      : static_cast<T>(std::forward<U>(default_value));
  }

  // Modifiers
  void swap(optional& other) noexcept(
      std::is_nothrow_move_constructible<T>::value) {
    if (has_value_ && other.has_value_) {
      using std::swap;
      swap(value_, other.value_);
    } else if (has_value_ && !other.has_value_) {
      other.construct(std::move(value_));
      reset();
    } else if (!has_value_ && other.has_value_) {
      construct(std::move(other.value_));
      other.reset();
    }
  }

  void reset() noexcept {
    if (has_value_) {
      value_.~T();
      has_value_ = false;
    }
  }

  template <typename... Args>
  T& emplace(Args&&... args) {
    reset();
    construct(std::forward<Args>(args)...);
    return value_;
  }

  template <typename U, typename... Args>
  T& emplace(std::initializer_list<U> il, Args&&... args) {
    reset();
    construct(il, std::forward<Args>(args)...);
    return value_;
  }

 private:
  template <typename... Args>
  void construct(Args&&... args) {
    new (&value_) T(std::forward<Args>(args)...);
    has_value_ = true;
  }
};

// Relational operators
template <typename T, typename U>
bool operator==(const optional<T>& lhs, const optional<U>& rhs) {
  if (bool(lhs) != bool(rhs))
    return false;
  if (!bool(lhs))
    return true;
  return *lhs == *rhs;
}

template <typename T, typename U>
constexpr bool operator!=(const optional<T>& lhs, const optional<U>& rhs) {
  return !(lhs == rhs);
}

template <typename T, typename U>
bool operator<(const optional<T>& lhs, const optional<U>& rhs) {
  if (!rhs)
    return false;
  if (!lhs)
    return true;
  return *lhs < *rhs;
}

template <typename T, typename U>
constexpr bool operator<=(const optional<T>& lhs, const optional<U>& rhs) {
  return !(rhs < lhs);
}

template <typename T, typename U>
constexpr bool operator>(const optional<T>& lhs, const optional<U>& rhs) {
  return rhs < lhs;
}

template <typename T, typename U>
constexpr bool operator>=(const optional<T>& lhs, const optional<U>& rhs) {
  return !(lhs < rhs);
}

// Comparison with nullopt
template <typename T>
constexpr bool operator==(const optional<T>& opt, nullopt_t) noexcept {
  return !opt;
}

template <typename T>
constexpr bool operator==(nullopt_t, const optional<T>& opt) noexcept {
  return !opt;
}

template <typename T>
constexpr bool operator!=(const optional<T>& opt, nullopt_t) noexcept {
  return bool(opt);
}

template <typename T>
constexpr bool operator!=(nullopt_t, const optional<T>& opt) noexcept {
  return bool(opt);
}

template <typename T>
constexpr bool operator<(const optional<T>&, nullopt_t) noexcept {
  return false;
}

template <typename T>
constexpr bool operator<(nullopt_t, const optional<T>& opt) noexcept {
  return bool(opt);
}

template <typename T>
constexpr bool operator<=(const optional<T>& opt, nullopt_t) noexcept {
  return !opt;
}

template <typename T>
constexpr bool operator<=(nullopt_t, const optional<T>&) noexcept {
  return true;
}

template <typename T>
constexpr bool operator>(const optional<T>& opt, nullopt_t) noexcept {
  return bool(opt);
}

template <typename T>
constexpr bool operator>(nullopt_t, const optional<T>&) noexcept {
  return false;
}

template <typename T>
constexpr bool operator>=(const optional<T>&, nullopt_t) noexcept {
  return true;
}

template <typename T>
constexpr bool operator>=(nullopt_t, const optional<T>& opt) noexcept {
  return !opt;
}

// Comparison with T
template <typename T, typename U>
constexpr bool operator==(const optional<T>& opt, const U& value) {
  return bool(opt) ? *opt == value : false;
}

template <typename T, typename U>
constexpr bool operator==(const U& value, const optional<T>& opt) {
  return bool(opt) ? value == *opt : false;
}

template <typename T, typename U>
constexpr bool operator!=(const optional<T>& opt, const U& value) {
  return bool(opt) ? *opt != value : true;
}

template <typename T, typename U>
constexpr bool operator!=(const U& value, const optional<T>& opt) {
  return bool(opt) ? value != *opt : true;
}

template <typename T, typename U>
constexpr bool operator<(const optional<T>& opt, const U& value) {
  return bool(opt) ? *opt < value : true;
}

template <typename T, typename U>
constexpr bool operator<(const U& value, const optional<T>& opt) {
  return bool(opt) ? value < *opt : false;
}

template <typename T, typename U>
constexpr bool operator<=(const optional<T>& opt, const U& value) {
  return !(opt > value);
}

template <typename T, typename U>
constexpr bool operator<=(const U& value, const optional<T>& opt) {
  return !(value > opt);
}

template <typename T, typename U>
constexpr bool operator>(const optional<T>& opt, const U& value) {
  return bool(opt) ? *opt > value : false;
}

template <typename T, typename U>
constexpr bool operator>(const U& value, const optional<T>& opt) {
  return bool(opt) ? value > *opt : true;
}

template <typename T, typename U>
constexpr bool operator>=(const optional<T>& opt, const U& value) {
  return !(opt < value);
}

template <typename T, typename U>
constexpr bool operator>=(const U& value, const optional<T>& opt) {
  return !(value < opt);
}

// swap
template <typename T>
void swap(optional<T>& lhs,
          optional<T>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// make_optional
template <typename T>
constexpr optional<typename std::decay<T>::type> make_optional(T&& value) {
  return optional<typename std::decay<T>::type>(std::forward<T>(value));
}

template <typename T, typename... Args>
constexpr optional<T> make_optional(Args&&... args) {
  return optional<T>(in_place, std::forward<Args>(args)...);
}

template <typename T, typename U, typename... Args>
constexpr optional<T> make_optional(std::initializer_list<U> il,
                                    Args&&... args) {
  return optional<T>(in_place, il, std::forward<Args>(args)...);
}

// Hash support
template <typename T>
struct hash;

template <typename T>
struct hash<optional<T>> {
  size_t operator()(const optional<T>& opt) const {
    if (!opt)
      return 0;
    return std::hash<T>()(*opt);
  }
};

// C++11 requires out-of-class definitions for static constexpr members

}  // namespace mcp

#endif  // MCP_OPTIONAL_H