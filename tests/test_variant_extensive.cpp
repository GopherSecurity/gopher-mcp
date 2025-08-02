#include <gtest/gtest.h>
#include "mcp/variant.h"
#include "mcp/memory_utils.h"
#include <string>
#include <vector>
#include <memory>
#include <type_traits>
#include <limits>
#include <algorithm>
#include <functional>

// Test suite inspired by C++17 std::variant test patterns
// Adapted for C++11 compatibility

// Helper types for testing
struct NonDefaultConstructible {
    NonDefaultConstructible() = delete;
    explicit NonDefaultConstructible(int v) : value(v) {}
    int value;
};

struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

struct NonMovable {
    NonMovable() = default;
    NonMovable(const NonMovable&) = default;
    NonMovable& operator=(const NonMovable&) = default;
    NonMovable(NonMovable&&) = delete;
    NonMovable& operator=(NonMovable&&) = delete;
};

struct ConvertibleToInt {
    operator int() const { return 42; }
};

struct ConvertibleFromInt {
    ConvertibleFromInt(int) {}
};

struct Counter {
    static int alive;
    Counter() { ++alive; }
    Counter(const Counter&) { ++alive; }
    Counter(Counter&&) { ++alive; }
    ~Counter() { --alive; }
};
int Counter::alive = 0;

// SFINAE test helpers
template<typename T, typename = void>
struct is_default_constructible : std::false_type {};

template<typename T>
struct is_default_constructible<T, 
    typename std::enable_if<
        std::is_same<decltype(T()), T>::value
    >::type> : std::true_type {};

// Test fixture
class VariantExtensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        Counter::alive = 0;
    }
    
    void TearDown() override {
        EXPECT_EQ(Counter::alive, 0);
    }
};

// Construction tests
TEST_F(VariantExtensiveTest, DefaultConstructionFirstAlternative) {
    // Default constructs the first alternative
    {
        mcp::variant<int, std::string> v;
        EXPECT_EQ(v.index(), 0u);
        EXPECT_EQ(v.get<int>(), 0);
    }
    
    {
        mcp::variant<std::string, int> v;
        EXPECT_EQ(v.index(), 0u);
        EXPECT_EQ(v.get<std::string>(), "");
    }
}

TEST_F(VariantExtensiveTest, DirectInitialization) {
    // Direct initialization selects the best match
    {
        mcp::variant<int, std::string> v(42);
        EXPECT_EQ(v.index(), 0u);
        EXPECT_EQ(v.get<int>(), 42);
    }
    
    {
        mcp::variant<int, std::string> v("hello");
        EXPECT_EQ(v.index(), 1u);
        EXPECT_EQ(v.get<std::string>(), "hello");
    }
    
    {
        mcp::variant<int, std::string> v(std::string("world"));
        EXPECT_EQ(v.index(), 1u);
        EXPECT_EQ(v.get<std::string>(), "world");
    }
}

TEST_F(VariantExtensiveTest, ConvertingConstruction) {
    // Test implicit conversions
    {
        mcp::variant<std::string, int> v("test");  // const char* -> std::string
        EXPECT_EQ(v.index(), 0u);
        EXPECT_EQ(v.get<std::string>(), "test");
    }
    
    {
        // Direct double construction to avoid ambiguity
        mcp::variant<int, double> v(3.14);  // double literal
        EXPECT_EQ(v.index(), 1u);
        EXPECT_DOUBLE_EQ(v.get<double>(), 3.14);
    }
    
    {
        ConvertibleToInt cti;
        mcp::variant<int, std::string> v(cti);
        EXPECT_EQ(v.index(), 0u);
        EXPECT_EQ(v.get<int>(), 42);
    }
}

// Copy/Move tests
TEST_F(VariantExtensiveTest, CopyConstructionPreservesValue) {
    {
        mcp::variant<int, std::string> v1(42);
        mcp::variant<int, std::string> v2(v1);
        
        EXPECT_EQ(v1.index(), v2.index());
        EXPECT_EQ(v1.get<int>(), v2.get<int>());
    }
    
    {
        mcp::variant<int, std::string> v1("test");
        mcp::variant<int, std::string> v2(v1);
        
        EXPECT_EQ(v1.index(), v2.index());
        EXPECT_EQ(v1.get<std::string>(), v2.get<std::string>());
    }
}

TEST_F(VariantExtensiveTest, MoveConstructionTransfersOwnership) {
    {
        std::vector<int> data = {1, 2, 3, 4, 5};
        mcp::variant<int, std::vector<int>> v1(data);
        mcp::variant<int, std::vector<int>> v2(std::move(v1));
        
        EXPECT_EQ(v2.index(), 1u);
        EXPECT_EQ(v2.get<std::vector<int>>(), data);
    }
    
    {
        auto ptr = mcp::make_unique<int>(42);
        mcp::variant<int, std::unique_ptr<int>> v1(std::move(ptr));
        mcp::variant<int, std::unique_ptr<int>> v2(std::move(v1));
        
        EXPECT_EQ(v2.index(), 1u);
        ASSERT_NE(v2.get<std::unique_ptr<int>>().get(), nullptr);
        EXPECT_EQ(*v2.get<std::unique_ptr<int>>(), 42);
    }
}

// Assignment tests
TEST_F(VariantExtensiveTest, CopyAssignmentSameIndex) {
    mcp::variant<int, std::string> v1(42);
    mcp::variant<int, std::string> v2(99);
    
    v2 = v1;
    EXPECT_EQ(v2.index(), 0u);
    EXPECT_EQ(v2.get<int>(), 42);
}

TEST_F(VariantExtensiveTest, CopyAssignmentDifferentIndex) {
    mcp::variant<int, std::string> v1(42);
    mcp::variant<int, std::string> v2("old");
    
    v2 = v1;
    EXPECT_EQ(v2.index(), 0u);
    EXPECT_EQ(v2.get<int>(), 42);
}

TEST_F(VariantExtensiveTest, MoveAssignment) {
    mcp::variant<int, std::vector<int>> v1(std::vector<int>{1, 2, 3});
    mcp::variant<int, std::vector<int>> v2(42);
    
    v2 = std::move(v1);
    EXPECT_EQ(v2.index(), 1u);
    EXPECT_EQ(v2.get<std::vector<int>>().size(), 3u);
}

TEST_F(VariantExtensiveTest, ConvertingAssignment) {
    mcp::variant<int, std::string> v(42);
    
    v = "hello";  // const char* -> std::string
    EXPECT_EQ(v.index(), 1u);
    EXPECT_EQ(v.get<std::string>(), "hello");
    
    v = 99;
    EXPECT_EQ(v.index(), 0u);
    EXPECT_EQ(v.get<int>(), 99);
}

// Visitor tests with various return types
TEST_F(VariantExtensiveTest, VisitorVoidReturn) {
    mcp::variant<int, double, std::string> v(3.14);
    
    bool visited = false;
    struct VoidVisitor {
        bool& flag;
        void operator()(int) { flag = false; }
        void operator()(double) { flag = true; }
        void operator()(const std::string&) { flag = false; }
    };
    
    mcp::visit(VoidVisitor{visited}, v);
    EXPECT_TRUE(visited);
}

TEST_F(VariantExtensiveTest, VisitorNonVoidReturn) {
    mcp::variant<int, double, std::string> v("hello");
    
    struct SizeVisitor {
        std::size_t operator()(int) const { return sizeof(int); }
        std::size_t operator()(double) const { return sizeof(double); }
        std::size_t operator()(const std::string& s) const { return s.size(); }
    };
    
    auto size = mcp::visit(SizeVisitor{}, v);
    EXPECT_EQ(size, 5u);
}

TEST_F(VariantExtensiveTest, VisitorWithLambdas) {
    mcp::variant<int, double> v(42);
    
    auto visitor = mcp::make_overload(
        [](int i) { return i * 2; },
        [](double d) { return static_cast<int>(d); }
    );
    
    EXPECT_EQ(mcp::visit(visitor, v), 84);
}

TEST_F(VariantExtensiveTest, VisitorModifyingValue) {
    mcp::variant<int, std::string> v(42);
    
    struct ModifyingVisitor {
        void operator()(int& i) { i *= 2; }
        void operator()(std::string& s) { s += s; }
    };
    
    mcp::visit(ModifyingVisitor{}, v);
    EXPECT_EQ(v.get<int>(), 84);
    
    v = std::string("test");
    mcp::visit(ModifyingVisitor{}, v);
    EXPECT_EQ(v.get<std::string>(), "testtest");
}

// Type traits tests
TEST_F(VariantExtensiveTest, TypeIndexing) {
    EXPECT_EQ((mcp::type_index<int, int, double, std::string>::value), 0u);
    EXPECT_EQ((mcp::type_index<double, int, double, std::string>::value), 1u);
    EXPECT_EQ((mcp::type_index<std::string, int, double, std::string>::value), 2u);
}

TEST_F(VariantExtensiveTest, ContainsType) {
    EXPECT_TRUE((mcp::contains_type<int, int, double, std::string>::value));
    EXPECT_TRUE((mcp::contains_type<double, int, double, std::string>::value));
    EXPECT_TRUE((mcp::contains_type<std::string, int, double, std::string>::value));
    EXPECT_FALSE((mcp::contains_type<float, int, double, std::string>::value));
    EXPECT_FALSE((mcp::contains_type<char, int, double, std::string>::value));
}

// Edge case tests
TEST_F(VariantExtensiveTest, EmptyVariantBehavior) {
    // Single alternative variant
    mcp::variant<int> v;
    EXPECT_EQ(v.index(), 0u);
    v = 42;
    EXPECT_EQ(v.get<int>(), 42);
}

TEST_F(VariantExtensiveTest, LargeVariant) {
    // Test with many alternatives
    using LargeVariant = mcp::variant<
        char, short, int, long, long long,
        unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long,
        float, double, long double,
        std::string, std::vector<int>, std::unique_ptr<double>
    >;
    
    LargeVariant v1('x');
    EXPECT_EQ(v1.index(), 0u);
    EXPECT_EQ(v1.get<char>(), 'x');
    
    v1 = 3.14;
    EXPECT_EQ(v1.index(), 11u);
    EXPECT_DOUBLE_EQ(v1.get<double>(), 3.14);
    
    v1 = std::string("test");
    EXPECT_EQ(v1.index(), 13u);
    EXPECT_EQ(v1.get<std::string>(), "test");
}

TEST_F(VariantExtensiveTest, RecursiveVariantSupport) {
    // Test recursive variant through pointer/reference
    struct Tree {
        int value;
        std::vector<mcp::variant<int, Tree*>> children;
        
        Tree(int v) : value(v) {}
    };
    
    Tree root(1);
    Tree child1(2);
    Tree child2(3);
    
    root.children.push_back(&child1);
    root.children.push_back(&child2);
    root.children.push_back(42);
    
    EXPECT_EQ(root.children.size(), 3u);
    EXPECT_TRUE(root.children[0].holds_alternative<Tree*>());
    EXPECT_TRUE(root.children[1].holds_alternative<Tree*>());
    EXPECT_TRUE(root.children[2].holds_alternative<int>());
}

// Exception safety tests
TEST_F(VariantExtensiveTest, ExceptionNeutralConstruction) {
    struct ThrowOnCopy {
        ThrowOnCopy() = default;
        ThrowOnCopy(const ThrowOnCopy&) { throw std::runtime_error("copy"); }
        ThrowOnCopy(ThrowOnCopy&&) noexcept = default;
    };
    
    // Move construction should not throw
    ThrowOnCopy toc;
    bool threw = false;
    try {
        mcp::variant<int, ThrowOnCopy> v(std::move(toc));
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw);
}

TEST_F(VariantExtensiveTest, StrongExceptionGuarantee) {
    // Define static member outside of local struct
    static int construct_count = 0;
    
    struct ThrowOnConstruct {
        ThrowOnConstruct() {
            if (++construct_count > 1) {
                throw std::runtime_error("construct");
            }
        }
    };
    construct_count = 0;
    
    mcp::variant<int, ThrowOnConstruct> v(42);
    construct_count = 1;  // Next construction will throw
    
    // Assignment that throws should leave v unchanged
    try {
        v = ThrowOnConstruct();
        FAIL() << "Expected exception";
    } catch (...) {
        EXPECT_EQ(v.index(), 0u);
        EXPECT_EQ(v.get<int>(), 42);
    }
}

// get_if tests
TEST_F(VariantExtensiveTest, GetIfCorrectType) {
    mcp::variant<int, double, std::string> v(42);
    
    auto* pi = v.get_if<int>();
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(*pi, 42);
    
    auto* pd = v.get_if<double>();
    EXPECT_EQ(pd, nullptr);
    
    auto* ps = v.get_if<std::string>();
    EXPECT_EQ(ps, nullptr);
}

TEST_F(VariantExtensiveTest, GetIfConst) {
    const mcp::variant<int, double, std::string> v(3.14);
    
    auto* pd = v.get_if<double>();
    ASSERT_NE(pd, nullptr);
    EXPECT_DOUBLE_EQ(*pd, 3.14);
    
    // Ensure const correctness
    static_assert(std::is_same<decltype(pd), const double*>::value, 
                  "get_if on const variant should return const pointer");
}

// Lifetime tests
TEST_F(VariantExtensiveTest, ObjectLifetime) {
    {
        mcp::variant<Counter, int> v;
        EXPECT_EQ(Counter::alive, 1);  // Default constructed Counter
        
        v = 42;
        EXPECT_EQ(Counter::alive, 0);  // Counter destroyed
        
        v = Counter();
        EXPECT_EQ(Counter::alive, 1);  // New Counter created
    }
    EXPECT_EQ(Counter::alive, 0);  // All destroyed
}

TEST_F(VariantExtensiveTest, MoveOnlyTypes) {
    mcp::variant<int, std::unique_ptr<int>> v;
    
    v = mcp::make_unique<int>(42);
    EXPECT_EQ(v.index(), 1u);
    ASSERT_NE(v.get<std::unique_ptr<int>>().get(), nullptr);
    EXPECT_EQ(*v.get<std::unique_ptr<int>>(), 42);
    
    // Move to another variant
    mcp::variant<int, std::unique_ptr<int>> v2(std::move(v));
    EXPECT_EQ(v2.index(), 1u);
    ASSERT_NE(v2.get<std::unique_ptr<int>>().get(), nullptr);
    EXPECT_EQ(*v2.get<std::unique_ptr<int>>(), 42);
}

// Constexpr-like tests (compile-time behavior)
TEST_F(VariantExtensiveTest, CompileTimeTypeSelection) {
    // Test that variant correctly selects types at compile time
    static_assert(std::is_same<
        typename mcp::type_at_index<0, int, double, std::string>::type,
        int
    >::value, "type_at_index<0> should be int");
    
    static_assert(std::is_same<
        typename mcp::type_at_index<1, int, double, std::string>::type,
        double
    >::value, "type_at_index<1> should be double");
    
    static_assert(std::is_same<
        typename mcp::type_at_index<2, int, double, std::string>::type,
        std::string
    >::value, "type_at_index<2> should be std::string");
}

// Performance and stress tests
TEST_F(VariantExtensiveTest, LargeObjectStorage) {
    struct LargeObject {
        char data[1024];
        LargeObject() { std::fill(data, data + 1024, 'x'); }
    };
    
    mcp::variant<int, LargeObject> v;
    v = LargeObject();
    
    EXPECT_EQ(v.index(), 1u);
    EXPECT_EQ(v.get<LargeObject>().data[0], 'x');
    EXPECT_EQ(v.get<LargeObject>().data[1023], 'x');
}

TEST_F(VariantExtensiveTest, ManyAlternativesPerformance) {
    const int iterations = 1000;
    
    using ManyTypes = mcp::variant<
        char, short, int, long,
        float, double,
        std::string, std::vector<int>
    >;
    
    std::vector<ManyTypes> variants;
    variants.reserve(iterations);
    
    // Create many variants of different types
    for (int i = 0; i < iterations; ++i) {
        switch (i % 8) {
            case 0: variants.emplace_back(static_cast<char>(i)); break;
            case 1: variants.emplace_back(static_cast<short>(i)); break;
            case 2: variants.emplace_back(i); break;
            case 3: variants.emplace_back(static_cast<long>(i)); break;
            case 4: variants.emplace_back(static_cast<float>(i)); break;
            case 5: variants.emplace_back(static_cast<double>(i)); break;
            case 6: variants.emplace_back(std::to_string(i)); break;
            case 7: variants.emplace_back(std::vector<int>{i, i+1}); break;
        }
    }
    
    // Visit all variants
    int sum = 0;
    struct SumVisitor {
        int& sum;
        void operator()(char c) { sum += c; }
        void operator()(short s) { sum += s; }
        void operator()(int i) { sum += i; }
        void operator()(long l) { sum += static_cast<int>(l); }
        void operator()(float f) { sum += static_cast<int>(f); }
        void operator()(double d) { sum += static_cast<int>(d); }
        void operator()(const std::string& s) { sum += static_cast<int>(s.length()); }
        void operator()(const std::vector<int>& v) { sum += static_cast<int>(v.size()); }
    };
    
    for (const auto& v : variants) {
        mcp::visit(SumVisitor{sum}, v);
    }
    
    EXPECT_GT(sum, 0);
}

// Special member function tests
TEST_F(VariantExtensiveTest, NoCopyConstructibleAlternative) {
    // Variant with non-copyable type should still be move-constructible
    using V = mcp::variant<int, NonCopyable>;
    
    V v1(42);
    V v2(std::move(v1));
    EXPECT_EQ(v2.index(), 0u);
    EXPECT_EQ(v2.get<int>(), 42);
    
    v2 = NonCopyable();
    EXPECT_EQ(v2.index(), 1u);
    
    V v3(std::move(v2));
    EXPECT_EQ(v3.index(), 1u);
}

// Value category tests
TEST_F(VariantExtensiveTest, PerfectForwarding) {
    struct TrackValueCategory {
        enum Category { LValue, RValue };
        Category cat;
        
        TrackValueCategory(int&) : cat(LValue) {}
        TrackValueCategory(int&&) : cat(RValue) {}
    };
    
    int lvalue = 42;
    mcp::variant<int, TrackValueCategory> v1(lvalue);
    EXPECT_EQ(v1.index(), 0u);  // Should select int
    
    // Force conversion to TrackValueCategory
    mcp::variant<TrackValueCategory> v2(lvalue);
    EXPECT_EQ(v2.get<TrackValueCategory>().cat, TrackValueCategory::LValue);
    
    mcp::variant<TrackValueCategory> v3(42);
    EXPECT_EQ(v3.get<TrackValueCategory>().cat, TrackValueCategory::RValue);
}

// Test helper for compile-time checks
namespace {
    template<typename T>
    struct always_false : std::false_type {};
}

TEST_F(VariantExtensiveTest, CompileTimeConstraints) {
    // These should compile
    mcp::variant<int> v1;
    mcp::variant<int, double> v2;
    mcp::variant<int, double, std::string> v3;
    
    // Test that we can store various types
    mcp::variant<void*, std::nullptr_t> v4(nullptr);
    mcp::variant<bool, char, int> v5(true);
    
    SUCCEED() << "All compile-time constraints passed";
}