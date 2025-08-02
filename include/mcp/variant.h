#ifndef MCP_VARIANT_H
#define MCP_VARIANT_H

#include <type_traits>
#include <utility>
#include <typeinfo>
#include <stdexcept>
#include <new>

namespace mcp {

// Forward declarations
template<typename... Types>
class variant;

// Helper to get the index of a type in a type list
template<typename T, typename... Types>
struct type_index;

template<typename T, typename First, typename... Rest>
struct type_index<T, First, Rest...> {
    static constexpr std::size_t value = std::is_same<T, First>::value ? 0 : 1 + type_index<T, Rest...>::value;
};

template<typename T, typename Last>
struct type_index<T, Last> {
    static constexpr std::size_t value = std::is_same<T, Last>::value ? 0 : 1;
};

// Helper to check if a type is in a type list
template<typename T, typename... Types>
struct contains_type;

template<typename T>
struct contains_type<T> : std::false_type {};

template<typename T, typename First, typename... Rest>
struct contains_type<T, First, Rest...> 
    : std::conditional<std::is_same<T, First>::value, std::true_type, contains_type<T, Rest...>>::type {};

// Helper to check if a type is convertible to any type in the list
template<typename T, typename... Types>
struct is_convertible_to_any;

template<typename T>
struct is_convertible_to_any<T> : std::false_type {};

template<typename T, typename First, typename... Rest>
struct is_convertible_to_any<T, First, Rest...> 
    : std::conditional<std::is_convertible<T, First>::value, std::true_type, is_convertible_to_any<T, Rest...>>::type {};

// Helper to find the first type that T is convertible to
template<typename T, std::size_t I, typename... Types>
struct find_convertible_type;

template<typename T, std::size_t I>
struct find_convertible_type<T, I> {
    static constexpr std::size_t index = static_cast<std::size_t>(-1);
    using type = void;
};

template<typename T, std::size_t I, typename First, typename... Rest>
struct find_convertible_type<T, I, First, Rest...> {
private:
    static constexpr bool is_conv = std::is_convertible<T, First>::value;
    using rest_result = find_convertible_type<T, I + 1, Rest...>;
    
public:
    static constexpr std::size_t index = is_conv ? I : rest_result::index;
    using type = typename std::conditional<is_conv, First, typename rest_result::type>::type;
};

// Helper to get the type at a given index
template<std::size_t I, typename... Types>
struct type_at_index;

template<std::size_t I, typename First, typename... Rest>
struct type_at_index<I, First, Rest...> {
    using type = typename type_at_index<I - 1, Rest...>::type;
};

template<typename First, typename... Rest>
struct type_at_index<0, First, Rest...> {
    using type = First;
};

// Helper to get the maximum size and alignment
template<typename... Types>
struct variant_storage_traits;

template<typename T>
struct variant_storage_traits<T> {
    static constexpr std::size_t size = sizeof(T);
    static constexpr std::size_t alignment = alignof(T);
};

template<typename T, typename... Rest>
struct variant_storage_traits<T, Rest...> {
    static constexpr std::size_t size = sizeof(T) > variant_storage_traits<Rest...>::size 
        ? sizeof(T) : variant_storage_traits<Rest...>::size;
    static constexpr std::size_t alignment = alignof(T) > variant_storage_traits<Rest...>::alignment 
        ? alignof(T) : variant_storage_traits<Rest...>::alignment;
};

// Visitor helper
template<typename Visitor, typename... Types>
struct visitor_helper;

// Bad variant access exception
class bad_variant_access : public std::exception {
public:
    const char* what() const noexcept override {
        return "bad variant access";
    }
};

// Main variant class
template<typename... Types>
class variant {
private:
    static constexpr std::size_t storage_size = variant_storage_traits<Types...>::size;
    static constexpr std::size_t storage_alignment = variant_storage_traits<Types...>::alignment;
    
    typename std::aligned_storage<storage_size, storage_alignment>::type storage_;
    std::size_t type_index_;
    
    // Destructor dispatcher
    template<std::size_t I = 0>
    typename std::enable_if<I == sizeof...(Types)>::type
    destroy_impl() {}
    
    template<std::size_t I = 0>
    typename std::enable_if<I < sizeof...(Types)>::type
    destroy_impl() {
        if (type_index_ == I) {
            using T = typename type_at_index<I, Types...>::type;
            reinterpret_cast<T*>(&storage_)->~T();
        } else {
            destroy_impl<I + 1>();
        }
    }
    
    // Copy constructor dispatcher
    template<std::size_t I = 0>
    typename std::enable_if<I == sizeof...(Types)>::type
    copy_construct_impl(const variant&) {}
    
    template<std::size_t I = 0>
    typename std::enable_if<I < sizeof...(Types)>::type
    copy_construct_impl(const variant& other) {
        if (other.type_index_ == I) {
            using T = typename type_at_index<I, Types...>::type;
            new (&storage_) T(*reinterpret_cast<const T*>(&other.storage_));
            type_index_ = I;
        } else {
            copy_construct_impl<I + 1>(other);
        }
    }
    
    // Move constructor dispatcher
    template<std::size_t I = 0>
    typename std::enable_if<I == sizeof...(Types)>::type
    move_construct_impl(variant&&) {}
    
    template<std::size_t I = 0>
    typename std::enable_if<I < sizeof...(Types)>::type
    move_construct_impl(variant&& other) {
        if (other.type_index_ == I) {
            using T = typename type_at_index<I, Types...>::type;
            new (&storage_) T(std::move(*reinterpret_cast<T*>(&other.storage_)));
            type_index_ = I;
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
    template<typename T, typename = typename std::enable_if<contains_type<typename std::decay<T>::type, Types...>::value>::type>
    variant(T&& value) : type_index_(type_index<typename std::decay<T>::type, Types...>::value) {
        using DecayedT = typename std::decay<T>::type;
        new (&storage_) DecayedT(std::forward<T>(value));
    }
    
    // Constructor from a value - implicit conversion
    template<typename T, 
             typename = typename std::enable_if<!contains_type<typename std::decay<T>::type, Types...>::value>::type,
             typename = typename std::enable_if<is_convertible_to_any<T, Types...>::value>::type>
    variant(T&& value) : type_index_(find_convertible_type<T, 0, Types...>::index) {
        using TargetType = typename find_convertible_type<T, 0, Types...>::type;
        new (&storage_) TargetType(std::forward<T>(value));
    }
    
    // Copy constructor
    variant(const variant& other) : type_index_(static_cast<std::size_t>(-1)) {
        copy_construct_impl(other);
    }
    
    // Move constructor
    variant(variant&& other) noexcept : type_index_(static_cast<std::size_t>(-1)) {
        move_construct_impl(std::move(other));
    }
    
    // Destructor
    ~variant() {
        destroy_impl();
    }
    
    // Copy assignment
    variant& operator=(const variant& other) {
        if (this != &other) {
            destroy_impl();
            copy_construct_impl(other);
        }
        return *this;
    }
    
    // Move assignment
    variant& operator=(variant&& other) noexcept {
        if (this != &other) {
            destroy_impl();
            move_construct_impl(std::move(other));
        }
        return *this;
    }
    
    // Assignment from value - exact match
    template<typename T, typename = typename std::enable_if<contains_type<typename std::decay<T>::type, Types...>::value>::type>
    variant& operator=(T&& value) {
        using DecayedT = typename std::decay<T>::type;
        destroy_impl();
        new (&storage_) DecayedT(std::forward<T>(value));
        type_index_ = type_index<DecayedT, Types...>::value;
        return *this;
    }
    
    // Assignment from value - implicit conversion
    template<typename T,
             typename = typename std::enable_if<!contains_type<typename std::decay<T>::type, Types...>::value>::type,
             typename = typename std::enable_if<is_convertible_to_any<T, Types...>::value>::type,
             typename = void>
    variant& operator=(T&& value) {
        using TargetType = typename find_convertible_type<T, 0, Types...>::type;
        destroy_impl();
        new (&storage_) TargetType(std::forward<T>(value));
        type_index_ = find_convertible_type<T, 0, Types...>::index;
        return *this;
    }
    
    // Get the index of the current alternative
    std::size_t index() const noexcept {
        return type_index_;
    }
    
    // Check if the variant holds a specific type
    template<typename T>
    bool holds_alternative() const noexcept {
        return type_index_ == type_index<T, Types...>::value;
    }
    
    // Get a reference to the stored value
    template<typename T>
    T& get() {
        if (!holds_alternative<T>()) {
            throw bad_variant_access();
        }
        return *reinterpret_cast<T*>(&storage_);
    }
    
    template<typename T>
    const T& get() const {
        if (!holds_alternative<T>()) {
            throw bad_variant_access();
        }
        return *reinterpret_cast<const T*>(&storage_);
    }
    
    // Get a pointer to the stored value (returns nullptr if wrong type)
    template<typename T>
    T* get_if() noexcept {
        if (!holds_alternative<T>()) {
            return nullptr;
        }
        return reinterpret_cast<T*>(&storage_);
    }
    
    template<typename T>
    const T* get_if() const noexcept {
        if (!holds_alternative<T>()) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(&storage_);
    }
    
    // Visit helper - moved outside class to avoid incomplete type issues
};

// Visit functions - defined outside class
template<typename Visitor, typename... Types>
auto visit(Visitor&& vis, variant<Types...>& v) -> decltype(vis(v.template get<typename type_at_index<0, Types...>::type>())) {
    return visitor_helper<Visitor, Types...>::visit(std::forward<Visitor>(vis), v, v.index());
}

template<typename Visitor, typename... Types>
auto visit(Visitor&& vis, const variant<Types...>& v) -> decltype(vis(v.template get<typename type_at_index<0, Types...>::type>())) {
    return visitor_helper<Visitor, Types...>::visit(std::forward<Visitor>(vis), v, v.index());
}

// Visitor implementation
template<typename Visitor, typename... Types>
struct visitor_helper {
    template<typename Variant, std::size_t I = 0>
    static typename std::enable_if<I == sizeof...(Types) - 1, 
        decltype(std::declval<Visitor>()(std::declval<Variant>().template get<typename type_at_index<I, Types...>::type>()))>::type
    visit(Visitor&& vis, Variant&& v, std::size_t) {
        using T = typename type_at_index<I, Types...>::type;
        return vis(std::forward<Variant>(v).template get<T>());
    }
    
    template<typename Variant, std::size_t I = 0>
    static typename std::enable_if<I < sizeof...(Types) - 1,
        decltype(std::declval<Visitor>()(std::declval<Variant>().template get<typename type_at_index<0, Types...>::type>()))>::type
    visit(Visitor&& vis, Variant&& v, std::size_t index) {
        if (index == I) {
            using T = typename type_at_index<I, Types...>::type;
            return vis(std::forward<Variant>(v).template get<T>());
        }
        return visit<Variant, I + 1>(std::forward<Visitor>(vis), std::forward<Variant>(v), index);
    }
};

// Helper function to create a variant (similar to std::make_variant)
template<typename T, typename... Types>
variant<Types...> make_variant(T&& value) {
    return variant<Types...>(std::forward<T>(value));
}

// Overload pattern for visitor - C++11 compatible version
template<typename F, typename... Fs>
struct overload_impl : F, overload_impl<Fs...> {
    overload_impl(F f, Fs... fs) : F(f), overload_impl<Fs...>(fs...) {}
    using F::operator();
    using overload_impl<Fs...>::operator();
};

template<typename F>
struct overload_impl<F> : F {
    overload_impl(F f) : F(f) {}
    using F::operator();
};

template<typename... Fs>
overload_impl<Fs...> make_overload(Fs... fs) {
    return overload_impl<Fs...>(fs...);
}

// C++11 requires out-of-class definitions for static constexpr members
template<typename T, typename First, typename... Rest>
constexpr std::size_t type_index<T, First, Rest...>::value;

template<typename T, typename Last>
constexpr std::size_t type_index<T, Last>::value;

template<typename T, std::size_t I>
constexpr std::size_t find_convertible_type<T, I>::index;

template<typename T, std::size_t I, typename First, typename... Rest>
constexpr std::size_t find_convertible_type<T, I, First, Rest...>::index;

template<typename T>
constexpr std::size_t variant_storage_traits<T>::size;

template<typename T>
constexpr std::size_t variant_storage_traits<T>::alignment;

template<typename T, typename... Rest>
constexpr std::size_t variant_storage_traits<T, Rest...>::size;

template<typename T, typename... Rest>
constexpr std::size_t variant_storage_traits<T, Rest...>::alignment;

} // namespace mcp

#endif // MCP_VARIANT_H