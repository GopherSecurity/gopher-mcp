#include <array>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/memory_utils.h"
#include "mcp/variant.h"

// Advanced test scenarios for mcp::variant
// Focus on edge cases, SFINAE, and complex type interactions

// Helper to test if a type is constructible
template <typename T, typename... Args>
class is_constructible_test {
  template <typename U, typename... As>
  static auto test(int) -> decltype(U(std::declval<As>()...), std::true_type{});

  template <typename U, typename... As>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T, Args...>(0))::value;
};

// Test types with special characteristics
struct ExplicitConversion {
  explicit ExplicitConversion(int) {}
};

struct MultipleConversions {
  MultipleConversions(int) {}
  MultipleConversions(double) {}
  MultipleConversions(const std::string&) {}
};

struct BaseClass {
  virtual ~BaseClass() = default;
  virtual int value() const { return 1; }
};

struct DerivedClass : BaseClass {
  int value() const override { return 2; }
};

struct AbstractBase {
  virtual ~AbstractBase() = default;
  virtual void pure() = 0;
};

struct ConcreteImpl : AbstractBase {
  void pure() override {}
  int data = 42;
};

// Complex visitor scenarios
class AdvancedVariantTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test variant with inheritance hierarchies
TEST_F(AdvancedVariantTest, PolymorphicTypes) {
  // Store pointers to polymorphic types
  mcp::variant<BaseClass*, DerivedClass*> v;

  BaseClass base;
  DerivedClass derived;

  v = &base;
  EXPECT_EQ(v.index(), 0u);
  EXPECT_EQ(v.get<BaseClass*>()->value(), 1);

  v = &derived;
  EXPECT_EQ(v.index(), 1u);
  EXPECT_EQ(v.get<DerivedClass*>()->value(), 2);

  // Store by value using smart pointers
  mcp::variant<std::unique_ptr<BaseClass>, std::unique_ptr<DerivedClass>> v2;

  v2 = mcp::make_unique<BaseClass>();
  EXPECT_EQ(v2.get<std::unique_ptr<BaseClass>>()->value(), 1);

  v2 = mcp::make_unique<DerivedClass>();
  EXPECT_EQ(v2.get<std::unique_ptr<DerivedClass>>()->value(), 2);
}

// Test SFINAE and overload resolution
TEST_F(AdvancedVariantTest, OverloadResolution) {
  // Test that variant correctly selects the best overload
  mcp::variant<std::string, std::vector<char>, int> v;

  // Should select std::string for const char*
  v = "hello";
  EXPECT_EQ(v.index(), 0u);
  EXPECT_EQ(v.get<std::string>(), "hello");

  // Should select int for numeric literals
  v = 42;
  EXPECT_EQ(v.index(), 2u);
  EXPECT_EQ(v.get<int>(), 42);

  // Should handle explicit construction
  v = std::vector<char>{'a', 'b', 'c'};
  EXPECT_EQ(v.index(), 1u);
  EXPECT_EQ(v.get<std::vector<char>>().size(), 3u);
}

// Test with function types and function pointers
TEST_F(AdvancedVariantTest, FunctionTypes) {
  using FuncPtr = int (*)(int);

  auto square = [](int x) -> int { return x * x; };
  auto cube = [](int x) -> int { return x * x * x; };

  // Function pointer variant
  mcp::variant<FuncPtr, std::function<int(int)>> v;

  v = +square;  // Convert lambda to function pointer
  EXPECT_EQ(v.index(), 0u);
  EXPECT_EQ(v.get<FuncPtr>()(5), 25);

  v = std::function<int(int)>(cube);
  EXPECT_EQ(v.index(), 1u);
  EXPECT_EQ(v.get<std::function<int(int)>>()(3), 27);
}

// Test variant with array types
TEST_F(AdvancedVariantTest, ArrayTypes) {
  // C-style arrays decay to pointers, so we use std::array
  mcp::variant<std::array<int, 3>, std::array<double, 3>> v;

  v = std::array<int, 3>{{1, 2, 3}};
  EXPECT_EQ(v.index(), 0u);
  EXPECT_EQ((v.get<std::array<int, 3>>()[1]), 2);

  v = std::array<double, 3>{{1.1, 2.2, 3.3}};
  EXPECT_EQ(v.index(), 1u);
  EXPECT_DOUBLE_EQ((v.get<std::array<double, 3>>()[2]), 3.3);
}

// Test with nested variants
TEST_F(AdvancedVariantTest, NestedVariants) {
  using Inner = mcp::variant<int, std::string>;
  using Outer = mcp::variant<Inner, double, std::vector<Inner>>;

  Outer v;

  // Single nested variant
  v = Inner(42);
  EXPECT_EQ(v.index(), 0u);
  EXPECT_EQ(v.get<Inner>().get<int>(), 42);

  // Vector of variants
  std::vector<Inner> vec;
  vec.push_back(Inner(10));
  vec.push_back(Inner("test"));
  vec.push_back(Inner(20));

  v = vec;
  EXPECT_EQ(v.index(), 2u);
  auto& stored_vec = v.get<std::vector<Inner>>();
  EXPECT_EQ(stored_vec.size(), 3u);
  EXPECT_EQ(stored_vec[0].get<int>(), 10);
  EXPECT_EQ(stored_vec[1].get<std::string>(), "test");
  EXPECT_EQ(stored_vec[2].get<int>(), 20);
}

// Test with reference wrapper types
TEST_F(AdvancedVariantTest, ReferenceWrappers) {
  int x = 42;
  std::string s = "hello";

  // Note: std::reference_wrapper requires initialization, so we can't default
  // construct
  mcp::variant<int, std::reference_wrapper<int>,
               std::reference_wrapper<std::string>>
      v(0);

  v = std::ref(x);
  EXPECT_EQ(v.index(), 1u);
  EXPECT_EQ(v.get<std::reference_wrapper<int>>().get(), 42);

  // Modify through reference
  v.get<std::reference_wrapper<int>>().get() = 100;
  EXPECT_EQ(x, 100);

  v = std::ref(s);
  EXPECT_EQ(v.index(), 2u);
  EXPECT_EQ(v.get<std::reference_wrapper<std::string>>().get(), "hello");
}

// Test visitor with generic lambdas (C++11 style)
TEST_F(AdvancedVariantTest, GenericVisitor) {
  mcp::variant<int, double, std::string> v(3.14);

  // In C++11, we need explicit overloads instead of template member functions
  struct ToStringVisitor {
    std::string operator()(int value) const {
      std::stringstream ss;
      ss << value;
      return ss.str();
    }
    std::string operator()(double value) const {
      std::stringstream ss;
      ss << value;
      return ss.str();
    }
    std::string operator()(const std::string& value) const { return value; }
  };

  std::string result = mcp::visit(ToStringVisitor{}, v);
  EXPECT_EQ(result.substr(0, 4), "3.14");

  v = 42;
  result = mcp::visit(ToStringVisitor{}, v);
  EXPECT_EQ(result, "42");

  v = std::string("test");
  result = mcp::visit(ToStringVisitor{}, v);
  EXPECT_EQ(result, "test");
}

// Test with volatile and const volatile types
TEST_F(AdvancedVariantTest, CVQualifiedTypes) {
  // Note: storing cv-qualified types directly is unusual
  // More common is cv-qualified access to variant
  mcp::variant<int, double> v(42);

  const mcp::variant<int, double>& cv = v;
  EXPECT_EQ(cv.get<int>(), 42);

  // Test that const visitor works
  struct ConstVisitor {
    int operator()(const int& i) const { return i * 2; }
    int operator()(const double& d) const { return static_cast<int>(d * 2); }
  };

  EXPECT_EQ(mcp::visit(ConstVisitor{}, cv), 84);
}

// Test exception specifications
TEST_F(AdvancedVariantTest, NoexceptOperations) {
  using V = mcp::variant<int, double>;

  // Move construction should be noexcept for these types
  static_assert(noexcept(V(std::declval<V&&>())),
                "Move constructor should be noexcept");

  // Index and holds_alternative should be noexcept
  V v(42);
  static_assert(noexcept(v.index()), "index() should be noexcept");
  static_assert(noexcept(v.holds_alternative<int>()),
                "holds_alternative should be noexcept");
}

// Test with incomplete types (through pointers)
TEST_F(AdvancedVariantTest, IncompleteTypes) {
  struct Incomplete;  // Forward declaration

  // Can store pointers to incomplete types
  mcp::variant<Incomplete*, int> v;
  v = static_cast<Incomplete*>(nullptr);
  EXPECT_EQ(v.index(), 0u);
  EXPECT_EQ(v.get<Incomplete*>(), nullptr);

  v = 42;
  EXPECT_EQ(v.index(), 1u);
  EXPECT_EQ(v.get<int>(), 42);
}

// Test variant with empty types
TEST_F(AdvancedVariantTest, EmptyTypes) {
  struct Empty {};
  struct AlsoEmpty {};

  mcp::variant<Empty, AlsoEmpty, int> v;

  // Default constructs to first alternative
  EXPECT_EQ(v.index(), 0u);

  v = AlsoEmpty{};
  EXPECT_EQ(v.index(), 1u);

  v = 42;
  EXPECT_EQ(v.index(), 2u);
  EXPECT_EQ(v.get<int>(), 42);
}

// Test with types that have unusual alignment requirements
TEST_F(AdvancedVariantTest, AlignmentRequirements) {
  struct alignas(32) HighlyAligned {
    char data[32];
  };

  struct alignas(64) VeryHighlyAligned {
    char data[64];
  };

  mcp::variant<char, HighlyAligned, VeryHighlyAligned> v;

  // Check that variant respects alignment
  void* addr = &v;
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(addr) % alignof(decltype(v)), 0u);

  v = HighlyAligned{};
  EXPECT_EQ(v.index(), 1u);

  v = VeryHighlyAligned{};
  EXPECT_EQ(v.index(), 2u);
}

// Test with recursive data structures
TEST_F(AdvancedVariantTest, RecursiveDataStructure) {
  struct Node;
  using NodePtr = std::shared_ptr<Node>;

  struct Node {
    mcp::variant<int, NodePtr> data;

    explicit Node(int val) : data(val) {}
    explicit Node(NodePtr child) : data(child) {}
  };

  // Create a tree-like structure
  auto leaf1 = std::make_shared<Node>(10);
  auto leaf2 = std::make_shared<Node>(20);
  auto branch = std::make_shared<Node>(leaf1);
  auto root = std::make_shared<Node>(branch);

  // Navigate the structure
  EXPECT_TRUE(root->data.holds_alternative<NodePtr>());
  auto& child = root->data.get<NodePtr>();
  EXPECT_TRUE(child->data.holds_alternative<NodePtr>());
  auto& grandchild = child->data.get<NodePtr>();
  EXPECT_TRUE(grandchild->data.holds_alternative<int>());
  EXPECT_EQ(grandchild->data.get<int>(), 10);
}

// Test variant destruction order
TEST_F(AdvancedVariantTest, DestructionOrder) {
  static int destruction_count = 0;

  struct CountingDestructor {
    ~CountingDestructor() { destruction_count++; }
  };

  destruction_count = 0;

  {
    mcp::variant<CountingDestructor, int> v1;
    mcp::variant<CountingDestructor, int> v2;
    mcp::variant<CountingDestructor, int> v3;

    EXPECT_EQ(destruction_count, 0);  // No destructions yet

    v2 = 42;  // Should destroy the CountingDestructor in v2
    EXPECT_EQ(destruction_count, 1);

    // v3 and v1 still contain CountingDestructor
  }

  // After the scope, v3 and v1 should be destroyed
  EXPECT_EQ(destruction_count, 3);  // 1 from v2 assignment + 2 from scope exit
}

// Test with types that overload operator&
TEST_F(AdvancedVariantTest, OverloadedAddressOf) {
  struct EvilType {
    int value;
    explicit EvilType(int v) : value(v) {}

    // Overload operator& to return something unexpected
    int operator&() const { return 666; }
  };

  mcp::variant<int, EvilType> v(EvilType(42));

  // Should still work correctly despite overloaded operator&
  EXPECT_EQ(v.index(), 1u);
  EXPECT_EQ(v.get<EvilType>().value, 42);

  // get_if should work correctly
  auto* p = v.get_if<EvilType>();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->value, 42);
}

// Test variant with union-like types
TEST_F(AdvancedVariantTest, UnionLikeTypes) {
  union SimpleUnion {
    int i;
    float f;
    SimpleUnion() : i(0) {}
  };

  mcp::variant<SimpleUnion, double> v;

  SimpleUnion u;
  u.f = 3.14f;
  v = u;

  EXPECT_EQ(v.index(), 0u);
  // Note: accessing u.f after assignment is implementation-defined

  v = 2.718;
  EXPECT_EQ(v.index(), 1u);
  EXPECT_DOUBLE_EQ(v.get<double>(), 2.718);
}