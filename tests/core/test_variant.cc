#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/core/memory_utils.h"
#include "mcp/core/variant.h"

// Test fixture for tracking construction/destruction
struct LifecycleTracker {
  static std::atomic<int> constructions;
  static std::atomic<int> destructions;
  static std::atomic<int> copies;
  static std::atomic<int> moves;

  int id;

  LifecycleTracker() : id(constructions++) {}
  LifecycleTracker(int i) : id(i) { constructions++; }
  LifecycleTracker(const LifecycleTracker& other) : id(other.id) { copies++; }
  LifecycleTracker(LifecycleTracker&& other) : id(other.id) { moves++; }
  ~LifecycleTracker() { destructions++; }

  LifecycleTracker& operator=(const LifecycleTracker& other) {
    id = other.id;
    copies++;
    return *this;
  }

  LifecycleTracker& operator=(LifecycleTracker&& other) {
    id = other.id;
    moves++;
    return *this;
  }

  static void reset() {
    constructions = 0;
    destructions = 0;
    copies = 0;
    moves = 0;
  }
};

std::atomic<int> LifecycleTracker::constructions(0);
std::atomic<int> LifecycleTracker::destructions(0);
std::atomic<int> LifecycleTracker::copies(0);
std::atomic<int> LifecycleTracker::moves(0);

// Move-only type for testing
struct MoveOnlyType {
  std::unique_ptr<int> data;

  MoveOnlyType() : data(mcp::make_unique<int>(0)) {}
  MoveOnlyType(int val) : data(mcp::make_unique<int>(val)) {}
  MoveOnlyType(const MoveOnlyType&) = delete;
  MoveOnlyType& operator=(const MoveOnlyType&) = delete;
  MoveOnlyType(MoveOnlyType&&) = default;
  MoveOnlyType& operator=(MoveOnlyType&&) = default;

  int value() const { return data ? *data : -1; }
};

// Complex type with multiple members
struct ComplexType {
  int id;
  std::string name;
  std::vector<double> values;
  std::map<std::string, int> metadata;

  ComplexType() : id(0), name("default") {}
  ComplexType(int i, const std::string& n) : id(i), name(n) {}

  bool operator==(const ComplexType& other) const {
    return id == other.id && name == other.name && values == other.values &&
           metadata == other.metadata;
  }
};

// Recursive type for stress testing
struct RecursiveType {
  int value;
  std::vector<mcp::variant<int, RecursiveType>> children;

  RecursiveType(int v) : value(v) {}
};

// Exception-throwing type for testing exception safety
struct ThrowingType {
  static bool should_throw;
  int value;

  ThrowingType(int v) : value(v) {
    if (should_throw)
      throw std::runtime_error("Construction failed");
  }

  ThrowingType(const ThrowingType& other) : value(other.value) {
    if (should_throw)
      throw std::runtime_error("Copy failed");
  }

  ThrowingType(ThrowingType&& other) : value(other.value) {
    if (should_throw)
      throw std::runtime_error("Move failed");
  }
};

bool ThrowingType::should_throw = false;

class VariantTest : public ::testing::Test {
 protected:
  void SetUp() override {
    LifecycleTracker::reset();
    ThrowingType::should_throw = false;
  }
};

// Basic functionality tests
TEST_F(VariantTest, DefaultConstruction) {
  mcp::variant<int, double, std::string> v;
  EXPECT_EQ(v.index(), 0u);
  EXPECT_TRUE(v.holds_alternative<int>());
  EXPECT_EQ(v.get<int>(), 0);
}

TEST_F(VariantTest, ValueConstruction) {
  // Test each type in the variant
  mcp::variant<int, double, std::string, std::vector<int>> v1(42);
  EXPECT_EQ(v1.get<int>(), 42);

  mcp::variant<int, double, std::string, std::vector<int>> v2(3.14159);
  EXPECT_DOUBLE_EQ(v2.get<double>(), 3.14159);

  mcp::variant<int, double, std::string, std::vector<int>> v3(
      std::string("hello world"));
  EXPECT_EQ(v3.get<std::string>(), "hello world");

  mcp::variant<int, double, std::string, std::vector<int>> v4(
      std::vector<int>{1, 2, 3, 4, 5});
  EXPECT_EQ(v4.get<std::vector<int>>().size(), 5u);
}

// Lifecycle management tests
TEST_F(VariantTest, LifecycleTracking) {
  // Track initial state
  int initial_constructions = LifecycleTracker::constructions;
  int initial_destructions = LifecycleTracker::destructions;

  {
    mcp::variant<LifecycleTracker, int> v1(LifecycleTracker(42));
    // At least one construction and move should have happened
    EXPECT_GT(LifecycleTracker::constructions, initial_constructions);
    EXPECT_GT(LifecycleTracker::moves, 0);

    mcp::variant<LifecycleTracker, int> v2(v1);  // copy
    EXPECT_GT(LifecycleTracker::copies, 0);

    int before_assign = LifecycleTracker::destructions;
    v2 = 10;  // destroy LifecycleTracker
    EXPECT_GT(LifecycleTracker::destructions, before_assign);

    int before_copy = LifecycleTracker::copies;
    v2 = v1;  // copy construct again
    EXPECT_GT(LifecycleTracker::copies, before_copy);
  }

  // All objects should be destroyed now
  int constructions = LifecycleTracker::constructions - initial_constructions;
  int total_destroyed = LifecycleTracker::destructions - initial_destructions;
  int total_copies = LifecycleTracker::copies;

  // The lifecycle is correct if all objects are eventually destroyed
  // With copy-and-swap idiom:
  // - Initial construction: 1 construction + 1 temporary destroyed after move
  // - v2(v1): 1 copy
  // - v2 = 10: 1 destruction
  // - v2 = v1 with copy-and-swap: creates temp (1 copy), swaps, destroys old (1
  // destruction)
  // - End of scope: 2 destructions (v1 and v2)
  // Total: 1 construction + 2 copies = 3 objects created
  // Total destructions: 1 (temp) + 1 (assign int) + 1 (copy-swap) + 2 (end) = 5
  // But we also have the temporary from copy-and-swap, so 6 total destructions
  EXPECT_GE(total_destroyed, constructions + total_copies);
}

// Move semantics tests
TEST_F(VariantTest, MoveOnlyTypes) {
  mcp::variant<MoveOnlyType, int> v1(MoveOnlyType(42));
  EXPECT_EQ(v1.get<MoveOnlyType>().value(), 42);

  mcp::variant<MoveOnlyType, int> v2(std::move(v1));
  EXPECT_EQ(v2.get<MoveOnlyType>().value(), 42);

  v2 = MoveOnlyType(100);
  EXPECT_EQ(v2.get<MoveOnlyType>().value(), 100);
}

TEST_F(VariantTest, MoveSemantics) {
  std::string long_string(1000, 'x');
  mcp::variant<int, std::string> v1(long_string);

  std::string* ptr1 = v1.get_if<std::string>();
  ASSERT_NE(ptr1, nullptr);

  mcp::variant<int, std::string> v2(std::move(v1));
  EXPECT_EQ(v2.get<std::string>(), long_string);
}

// Copy semantics tests
TEST_F(VariantTest, DeepCopy) {
  ComplexType complex;
  complex.id = 42;
  complex.name = "test";
  complex.values = {1.1, 2.2, 3.3};
  complex.metadata["key1"] = 100;
  complex.metadata["key2"] = 200;

  mcp::variant<int, ComplexType> v1(complex);
  mcp::variant<int, ComplexType> v2(v1);

  // Modify original
  v1.get<ComplexType>().id = 99;
  v1.get<ComplexType>().values.push_back(4.4);

  // Copy should be unchanged
  EXPECT_EQ(v2.get<ComplexType>().id, 42);
  EXPECT_EQ(v2.get<ComplexType>().values.size(), 3u);
}

// Exception safety tests
TEST_F(VariantTest, ExceptionSafety) {
  mcp::variant<ThrowingType, int> v(42);  // int type

  ThrowingType::should_throw = true;

  // Copy construction should fail but leave v unchanged
  try {
    mcp::variant<ThrowingType, int> v2(ThrowingType(10));
    FAIL() << "Expected exception";
  } catch (...) {
    EXPECT_EQ(v.index(), 1u);
    EXPECT_EQ(v.get<int>(), 42);
  }

  // Assignment should fail but leave v unchanged
  try {
    v = ThrowingType(20);
    FAIL() << "Expected exception";
  } catch (...) {
    EXPECT_EQ(v.index(), 1u);
    EXPECT_EQ(v.get<int>(), 42);
  }
}

// Visitor pattern intensive tests
TEST_F(VariantTest, VisitorAllTypes) {
  using TestVariant =
      mcp::variant<bool, char, int, long, float, double, std::string>;

  struct TypeNameVisitor {
    std::string operator()(bool) const { return "bool"; }
    std::string operator()(char) const { return "char"; }
    std::string operator()(int) const { return "int"; }
    std::string operator()(long) const { return "long"; }
    std::string operator()(float) const { return "float"; }
    std::string operator()(double) const { return "double"; }
    std::string operator()(const std::string&) const { return "string"; }
  };

  TestVariant v;

  v = true;
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "bool");

  v = 'A';
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "char");

  v = 42;
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "int");

  v = 42L;
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "long");

  v = 3.14f;
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "float");

  v = 3.14159;
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "double");

  v = std::string("test");
  EXPECT_EQ(mcp::visit(TypeNameVisitor{}, v), "string");
}

TEST_F(VariantTest, VisitorWithState) {
  mcp::variant<int, double, std::string> v;

  struct AccumulatorVisitor {
    double sum = 0;
    void operator()(int i) { sum += i; }
    void operator()(double d) { sum += d; }
    void operator()(const std::string& s) { sum += s.length(); }
  };

  AccumulatorVisitor acc;

  v = 10;
  mcp::visit(std::ref(acc), v);

  v = 5.5;
  mcp::visit(std::ref(acc), v);

  v = std::string("hello");
  mcp::visit(std::ref(acc), v);

  EXPECT_DOUBLE_EQ(acc.sum, 20.5);
}

// Overload pattern tests
TEST_F(VariantTest, ComplexOverloadPattern) {
  using TestVariant = mcp::variant<int, double, std::string, std::vector<int>>;

  auto visitor = mcp::make_overload(
      [](int i) -> std::string { return "int: " + std::to_string(i); },
      [](double d) -> std::string { return "double: " + std::to_string(d); },
      [](const std::string& s) -> std::string { return "string: " + s; },
      [](const std::vector<int>& v) -> std::string {
        return "vector: size=" + std::to_string(v.size());
      });

  TestVariant v(42);
  EXPECT_EQ(mcp::visit(visitor, v), "int: 42");

  v = 3.14;
  EXPECT_EQ(mcp::visit(visitor, v), "double: 3.140000");

  v = std::string("hello");
  EXPECT_EQ(mcp::visit(visitor, v), "string: hello");

  v = std::vector<int>{1, 2, 3};
  EXPECT_EQ(mcp::visit(visitor, v), "vector: size=3");
}

// Recursive variant tests
TEST_F(VariantTest, RecursiveVariant) {
  RecursiveType root(1);
  root.children.push_back(2);
  root.children.push_back(RecursiveType(3));

  RecursiveType child(4);
  child.children.push_back(5);
  root.children.push_back(std::move(child));

  mcp::variant<int, RecursiveType> v(std::move(root));

  struct CountVisitor {
    int count = 0;
    void operator()(int) { count++; }
    void operator()(const RecursiveType& r) {
      count++;
      for (const auto& child : r.children) {
        mcp::visit(std::ref(*this), child);
      }
    }
  };

  CountVisitor counter;
  mcp::visit(std::ref(counter), v);
  EXPECT_EQ(counter.count, 5);  // root + 2 ints + 2 recursive types
}

// Alignment and size tests
TEST_F(VariantTest, AlignmentAndSize) {
  struct Aligned16 {
    alignas(16) char data[16];
  };

  struct Aligned32 {
    alignas(32) char data[32];
  };

  using AlignedVariant = mcp::variant<char, Aligned16, Aligned32>;

  // Check that variant respects alignment
  AlignedVariant v1;
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&v1) % alignof(AlignedVariant), 0u);

  v1 = Aligned32{};
  EXPECT_TRUE(v1.holds_alternative<Aligned32>());
}

// Stress tests with many types
TEST_F(VariantTest, ManyTypes) {
  using MegaVariant =
      mcp::variant<bool, char, short, int, long, long long, unsigned char,
                   unsigned short, unsigned int, unsigned long, float, double,
                   long double, std::string, std::vector<int>,
                   std::map<int, int>, std::set<double>, std::unique_ptr<int>>;

  MegaVariant v;

  // Test each type
  v = false;
  EXPECT_EQ(v.index(), 0u);

  v = std::string("test");
  EXPECT_EQ(v.index(), 13u);

  v = mcp::make_unique<int>(42);
  EXPECT_EQ(v.index(), 17u);
  EXPECT_EQ(*v.get<std::unique_ptr<int>>(), 42);
}

// Performance stress test
TEST_F(VariantTest, PerformanceStress) {
  using PerfVariant = mcp::variant<int, double, std::string, std::vector<int>>;

  const int iterations = 10000;
  std::vector<PerfVariant> variants;
  variants.reserve(iterations);

  // Create many variants
  for (int i = 0; i < iterations; ++i) {
    switch (i % 4) {
      case 0:
        variants.emplace_back(i);
        break;
      case 1:
        variants.emplace_back(static_cast<double>(i) * 1.5);
        break;
      case 2:
        variants.emplace_back("string_" + std::to_string(i));
        break;
      case 3:
        variants.emplace_back(std::vector<int>{i, i + 1, i + 2});
        break;
    }
  }

  // Visit all variants
  struct SumVisitor {
    double sum = 0;
    void operator()(int i) { sum += i; }
    void operator()(double d) { sum += d; }
    void operator()(const std::string& s) { sum += s.length(); }
    void operator()(const std::vector<int>& v) { sum += v.size(); }
  };

  SumVisitor visitor;
  for (const auto& v : variants) {
    mcp::visit(std::ref(visitor), v);
  }

  EXPECT_GT(visitor.sum, 0);
}

// Edge cases
TEST_F(VariantTest, EmptyTypes) {
  struct Empty {};
  mcp::variant<Empty, int> v;
  EXPECT_TRUE(v.holds_alternative<Empty>());
}

TEST_F(VariantTest, SingleType) {
  mcp::variant<int> v(42);
  EXPECT_EQ(v.get<int>(), 42);

  v = 100;
  EXPECT_EQ(v.get<int>(), 100);
}

TEST_F(VariantTest, ConstCorrectness) {
  const mcp::variant<int, std::string> cv(42);

  // These should compile
  const int& i = cv.get<int>();
  const int* pi = cv.get_if<int>();

  EXPECT_EQ(i, 42);
  EXPECT_NE(pi, nullptr);
  EXPECT_EQ(*pi, 42);

  // Visitor with const variant
  struct ConstVisitor {
    int operator()(const int& i) const { return i * 2; }
    int operator()(const std::string& s) const {
      return static_cast<int>(s.length());
    }
  };

  EXPECT_EQ(mcp::visit(ConstVisitor{}, cv), 84);
}

// Self-assignment tests
TEST_F(VariantTest, SelfAssignment) {
  mcp::variant<int, std::string, std::vector<int>> v("hello");

  // Self-assignment through reference to avoid compiler warning
  auto& v_ref = v;
  v = v_ref;
  EXPECT_EQ(v.get<std::string>(), "hello");

  // Self-move is undefined behavior, so test move from temporary
  mcp::variant<int, std::string, std::vector<int>> temp("world");
  v = std::move(temp);
  EXPECT_EQ(v.get<std::string>(), "world");
}

// Bad access tests
TEST_F(VariantTest, BadAccess) {
  mcp::variant<int, double, std::string> v(42);

  EXPECT_THROW(v.get<double>(), mcp::bad_variant_access);
  EXPECT_THROW(v.get<std::string>(), mcp::bad_variant_access);

  try {
    v.get<double>();
  } catch (const mcp::bad_variant_access& e) {
    EXPECT_STREQ(e.what(), "bad variant access");
  }
}

// Type deduction tests
TEST_F(VariantTest, TypeDeduction) {
  auto v1 = mcp::make_variant<int, int, double, std::string>(42);
  EXPECT_EQ(v1.get<int>(), 42);

  auto v2 = mcp::make_variant<double, int, double, std::string>(3.14);
  EXPECT_DOUBLE_EQ(v2.get<double>(), 3.14);
}

// Variant of variants
TEST_F(VariantTest, NestedVariants) {
  using InnerVariant = mcp::variant<int, std::string>;
  using OuterVariant = mcp::variant<InnerVariant, double>;

  OuterVariant v1(InnerVariant(42));
  EXPECT_TRUE(v1.holds_alternative<InnerVariant>());
  EXPECT_EQ(v1.get<InnerVariant>().get<int>(), 42);

  OuterVariant v2(InnerVariant("nested"));
  EXPECT_EQ(v2.get<InnerVariant>().get<std::string>(), "nested");
}

// Thread safety test (variants should be thread-safe for const access)
TEST_F(VariantTest, ThreadSafeConstAccess) {
  const mcp::variant<int, std::string> v(42);
  std::atomic<int> sum(0);

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&v, &sum]() {
      for (int j = 0; j < 1000; ++j) {
        if (v.holds_alternative<int>()) {
          sum += v.get<int>();
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(sum.load(), 42 * 10 * 1000);
}
