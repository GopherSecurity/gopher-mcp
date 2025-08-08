#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/core/memory_utils.h"
#include "mcp/core/optional.h"

// Test helpers
struct NonTrivial {
  static int constructions;
  static int destructions;
  static int copies;
  static int moves;

  int value;

  NonTrivial() : value(0) { ++constructions; }
  explicit NonTrivial(int v) : value(v) { ++constructions; }
  NonTrivial(const NonTrivial& other) : value(other.value) {
    ++constructions;
    ++copies;
  }
  NonTrivial(NonTrivial&& other) noexcept : value(other.value) {
    ++constructions;
    ++moves;
    other.value = -1;
  }
  ~NonTrivial() { ++destructions; }

  NonTrivial& operator=(const NonTrivial& other) {
    value = other.value;
    ++copies;
    return *this;
  }

  NonTrivial& operator=(NonTrivial&& other) noexcept {
    value = other.value;
    other.value = -1;
    ++moves;
    return *this;
  }

  bool operator==(const NonTrivial& other) const {
    return value == other.value;
  }
  bool operator<(const NonTrivial& other) const { return value < other.value; }

  static void reset() {
    constructions = 0;
    destructions = 0;
    copies = 0;
    moves = 0;
  }
};

int NonTrivial::constructions = 0;
int NonTrivial::destructions = 0;
int NonTrivial::copies = 0;
int NonTrivial::moves = 0;

// Move-only type
struct MoveOnly {
  std::unique_ptr<int> ptr;

  MoveOnly() : ptr(mcp::make_unique<int>(0)) {}
  explicit MoveOnly(int v) : ptr(mcp::make_unique<int>(v)) {}
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;

  int value() const { return ptr ? *ptr : -1; }
};

// Throwing type for exception safety tests
struct ThrowingType {
  static bool should_throw;
  int value;

  ThrowingType() : value(0) {
    if (should_throw)
      throw std::runtime_error("ThrowingType()");
  }

  explicit ThrowingType(int v) : value(v) {
    if (should_throw)
      throw std::runtime_error("ThrowingType(int)");
  }

  ThrowingType(const ThrowingType& other) : value(other.value) {
    if (should_throw)
      throw std::runtime_error("ThrowingType(const&)");
  }

  ThrowingType(ThrowingType&& other) : value(other.value) {
    if (should_throw)
      throw std::runtime_error("ThrowingType(&&)");
  }

  ThrowingType& operator=(const ThrowingType& other) {
    value = other.value;
    if (should_throw)
      throw std::runtime_error("ThrowingType::operator=(const&)");
    return *this;
  }
};

bool ThrowingType::should_throw = false;

class OptionalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    NonTrivial::reset();
    ThrowingType::should_throw = false;
  }
};

// Basic construction tests
TEST_F(OptionalTest, DefaultConstruction) {
  mcp::optional<int> opt;
  EXPECT_FALSE(opt.has_value());
  EXPECT_FALSE(bool(opt));
}

TEST_F(OptionalTest, NulloptConstruction) {
  mcp::optional<int> opt(mcp::nullopt);
  EXPECT_FALSE(opt.has_value());
  EXPECT_FALSE(bool(opt));
}

TEST_F(OptionalTest, ValueConstruction) {
  mcp::optional<int> opt1(42);
  EXPECT_TRUE(opt1.has_value());
  EXPECT_EQ(*opt1, 42);

  mcp::optional<std::string> opt2("hello");
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, "hello");

  mcp::optional<std::vector<int>> opt3(std::vector<int>{1, 2, 3});
  EXPECT_TRUE(opt3);
  EXPECT_EQ(opt3->size(), 3u);
}

TEST_F(OptionalTest, InPlaceConstruction) {
  mcp::optional<std::string> opt1(mcp::in_place, "hello world");
  EXPECT_TRUE(opt1);
  EXPECT_EQ(*opt1, "hello world");

  mcp::optional<std::string> opt2(mcp::in_place, 10, 'x');
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, "xxxxxxxxxx");

  mcp::optional<std::vector<int>> opt3(mcp::in_place, {1, 2, 3, 4, 5});
  EXPECT_TRUE(opt3);
  EXPECT_EQ(opt3->size(), 5u);
}

// Copy/Move tests
TEST_F(OptionalTest, CopyConstruction) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2(opt1);
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, 42);

  mcp::optional<int> opt3;
  mcp::optional<int> opt4(opt3);
  EXPECT_FALSE(opt4);
}

TEST_F(OptionalTest, MoveConstruction) {
  mcp::optional<std::string> opt1("hello");
  mcp::optional<std::string> opt2(std::move(opt1));
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, "hello");

  mcp::optional<MoveOnly> opt3(MoveOnly(42));
  mcp::optional<MoveOnly> opt4(std::move(opt3));
  EXPECT_TRUE(opt4);
  EXPECT_EQ(opt4->value(), 42);
}

TEST_F(OptionalTest, CopyAssignment) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2(99);
  opt2 = opt1;
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, 42);

  mcp::optional<int> opt3;
  opt1 = opt3;
  EXPECT_FALSE(opt1);
}

TEST_F(OptionalTest, MoveAssignment) {
  mcp::optional<std::string> opt1("hello");
  mcp::optional<std::string> opt2("world");
  opt2 = std::move(opt1);
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, "hello");

  mcp::optional<MoveOnly> opt3(MoveOnly(42));
  mcp::optional<MoveOnly> opt4;
  opt4 = std::move(opt3);
  EXPECT_TRUE(opt4);
  EXPECT_EQ(opt4->value(), 42);
}

TEST_F(OptionalTest, ValueAssignment) {
  mcp::optional<int> opt;
  opt = 42;
  EXPECT_TRUE(opt);
  EXPECT_EQ(*opt, 42);

  opt = 99;
  EXPECT_EQ(*opt, 99);
}

TEST_F(OptionalTest, NulloptAssignment) {
  mcp::optional<int> opt(42);
  opt = mcp::nullopt;
  EXPECT_FALSE(opt);
}

// Observer tests
TEST_F(OptionalTest, Dereference) {
  mcp::optional<int> opt1(42);
  EXPECT_EQ(*opt1, 42);
  *opt1 = 99;
  EXPECT_EQ(*opt1, 99);

  const mcp::optional<int> opt2(42);
  EXPECT_EQ(*opt2, 42);
}

TEST_F(OptionalTest, ArrowOperator) {
  mcp::optional<std::string> opt("hello");
  EXPECT_EQ(opt->size(), 5u);
  opt->append(" world");
  EXPECT_EQ(*opt, "hello world");

  const mcp::optional<std::string> opt2("test");
  EXPECT_EQ(opt2->size(), 4u);
}

TEST_F(OptionalTest, Value) {
  mcp::optional<int> opt1(42);
  EXPECT_EQ(opt1.value(), 42);

  mcp::optional<int> opt2;
  EXPECT_THROW(opt2.value(), mcp::bad_optional_access);

  const mcp::optional<int> opt3(99);
  EXPECT_EQ(opt3.value(), 99);
}

TEST_F(OptionalTest, ValueOr) {
  mcp::optional<int> opt1(42);
  EXPECT_EQ(opt1.value_or(99), 42);

  mcp::optional<int> opt2;
  EXPECT_EQ(opt2.value_or(99), 99);

  // Test with different types
  mcp::optional<std::string> opt3;
  EXPECT_EQ(opt3.value_or("default"), "default");
}

// Modifier tests
TEST_F(OptionalTest, Reset) {
  mcp::optional<NonTrivial> opt(42);
  EXPECT_TRUE(opt);
  EXPECT_EQ(NonTrivial::constructions, 1);

  opt.reset();
  EXPECT_FALSE(opt);
  EXPECT_EQ(NonTrivial::destructions, 1);
}

TEST_F(OptionalTest, Emplace) {
  mcp::optional<std::string> opt;
  auto& ref = opt.emplace("hello");
  EXPECT_TRUE(opt);
  EXPECT_EQ(*opt, "hello");
  EXPECT_EQ(&ref, &*opt);

  opt.emplace(10, 'x');
  EXPECT_EQ(*opt, "xxxxxxxxxx");
}

TEST_F(OptionalTest, EmplaceInitializerList) {
  mcp::optional<std::vector<int>> opt;
  opt.emplace({1, 2, 3, 4, 5});
  EXPECT_TRUE(opt);
  EXPECT_EQ(opt->size(), 5u);
}

TEST_F(OptionalTest, Swap) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2(99);
  opt1.swap(opt2);
  EXPECT_EQ(*opt1, 99);
  EXPECT_EQ(*opt2, 42);

  mcp::optional<int> opt3(10);
  mcp::optional<int> opt4;
  opt3.swap(opt4);
  EXPECT_FALSE(opt3);
  EXPECT_TRUE(opt4);
  EXPECT_EQ(*opt4, 10);

  mcp::optional<int> opt5;
  mcp::optional<int> opt6;
  opt5.swap(opt6);
  EXPECT_FALSE(opt5);
  EXPECT_FALSE(opt6);
}

// Comparison tests
TEST_F(OptionalTest, CompareOptionals) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2(42);
  mcp::optional<int> opt3(99);
  mcp::optional<int> opt4;

  EXPECT_TRUE(opt1 == opt2);
  EXPECT_FALSE(opt1 == opt3);
  EXPECT_FALSE(opt1 == opt4);
  EXPECT_TRUE(opt1 != opt3);

  EXPECT_TRUE(opt1 < opt3);
  EXPECT_FALSE(opt3 < opt1);
  EXPECT_FALSE(opt1 < opt4);
  EXPECT_TRUE(opt4 < opt1);

  EXPECT_TRUE(opt1 <= opt2);
  EXPECT_TRUE(opt1 <= opt3);
  EXPECT_TRUE(opt3 > opt1);
  EXPECT_TRUE(opt3 >= opt1);
}

TEST_F(OptionalTest, CompareWithNullopt) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2;

  EXPECT_FALSE(opt1 == mcp::nullopt);
  EXPECT_TRUE(opt2 == mcp::nullopt);
  EXPECT_TRUE(opt1 != mcp::nullopt);
  EXPECT_FALSE(opt2 != mcp::nullopt);

  EXPECT_FALSE(opt1 < mcp::nullopt);
  EXPECT_FALSE(opt2 < mcp::nullopt);
  EXPECT_TRUE(mcp::nullopt < opt1);
  EXPECT_FALSE(mcp::nullopt < opt2);
}

TEST_F(OptionalTest, CompareWithValue) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2;

  EXPECT_TRUE(opt1 == 42);
  EXPECT_FALSE(opt1 == 99);
  EXPECT_FALSE(opt2 == 42);

  EXPECT_TRUE(opt1 < 99);
  EXPECT_FALSE(opt1 < 42);
  EXPECT_FALSE(opt1 < 10);
  EXPECT_TRUE(opt2 < 42);

  EXPECT_TRUE(42 == opt1);
  EXPECT_FALSE(99 == opt1);
  EXPECT_TRUE(10 < opt1);
}

// make_optional tests
TEST_F(OptionalTest, MakeOptional) {
  auto opt1 = mcp::make_optional(42);
  EXPECT_TRUE(opt1);
  EXPECT_EQ(*opt1, 42);

  auto opt2 = mcp::make_optional<std::string>("hello");
  EXPECT_TRUE(opt2);
  EXPECT_EQ(*opt2, "hello");

  auto opt3 = mcp::make_optional<std::string>(10, 'x');
  EXPECT_TRUE(opt3);
  EXPECT_EQ(*opt3, "xxxxxxxxxx");

  auto opt4 = mcp::make_optional<std::vector<int>>({1, 2, 3});
  EXPECT_TRUE(opt4);
  EXPECT_EQ(opt4->size(), 3u);
}

// Lifetime tests
TEST_F(OptionalTest, LifetimeTracking) {
  {
    mcp::optional<NonTrivial> opt1;
    EXPECT_EQ(NonTrivial::constructions, 0);

    opt1 = NonTrivial(42);
    // Temporary object is created and moved
    EXPECT_GE(NonTrivial::constructions, 1);
    EXPECT_GE(NonTrivial::moves, 1);

    mcp::optional<NonTrivial> opt2(opt1);
    EXPECT_GE(NonTrivial::copies, 1);

    opt2 = mcp::nullopt;
    EXPECT_GT(NonTrivial::destructions, 0);

    opt2 = std::move(opt1);
    EXPECT_GT(NonTrivial::moves, 1);
  }

  // All objects should be destroyed
  EXPECT_EQ(NonTrivial::constructions, NonTrivial::destructions);
}

// Exception safety tests
TEST_F(OptionalTest, ExceptionSafety) {
  mcp::optional<ThrowingType> opt(42);
  ThrowingType::should_throw = true;

  // Copy construction should fail
  try {
    mcp::optional<ThrowingType> opt2(ThrowingType(10));
    FAIL() << "Expected exception";
  } catch (...) {
    // Original should be unchanged
    EXPECT_TRUE(opt);
    EXPECT_EQ(opt->value, 42);
  }

  // Assignment should fail but leave opt unchanged
  try {
    opt = ThrowingType(99);
    FAIL() << "Expected exception";
  } catch (...) {
    EXPECT_TRUE(opt);
    EXPECT_EQ(opt->value, 42);
  }
}

// Constexpr tests (limited in C++11)
TEST_F(OptionalTest, ConstexprConstruction) {
  constexpr mcp::optional<int> opt1;
  constexpr mcp::optional<int> opt2(mcp::nullopt);
  constexpr mcp::optional<int> opt3(42);

  EXPECT_FALSE(opt1);
  EXPECT_FALSE(opt2);
  EXPECT_TRUE(opt3);
  EXPECT_EQ(*opt3, 42);
}

// Hash test
TEST_F(OptionalTest, Hash) {
  mcp::optional<int> opt1(42);
  mcp::optional<int> opt2(42);
  mcp::optional<int> opt3(99);
  mcp::optional<int> opt4;

  mcp::hash<mcp::optional<int>> hasher;

  EXPECT_EQ(hasher(opt1), hasher(opt2));
  EXPECT_NE(hasher(opt1), hasher(opt3));
  EXPECT_EQ(hasher(opt4), 0u);
}

// Edge cases
TEST_F(OptionalTest, SelfAssignment) {
  mcp::optional<int> opt(42);
  auto& opt_ref = opt;
  opt = opt_ref;
  EXPECT_TRUE(opt);
  EXPECT_EQ(*opt, 42);

  mcp::optional<int> opt2;
  auto& opt2_ref = opt2;
  opt2 = opt2_ref;
  EXPECT_FALSE(opt2);
}

TEST_F(OptionalTest, NestedOptionals) {
  using inner_optional = mcp::optional<int>;
  using outer_optional = mcp::optional<inner_optional>;

  outer_optional opt1;
  EXPECT_FALSE(opt1);

  outer_optional opt2((inner_optional()));
  EXPECT_TRUE(opt2);
  EXPECT_FALSE(*opt2);

  inner_optional inner(42);
  outer_optional opt3(inner);
  EXPECT_TRUE(opt3);
  EXPECT_TRUE(*opt3);
  EXPECT_EQ(**opt3, 42);
}

TEST_F(OptionalTest, OptionalReferences) {
  // Note: optional<T&> is not supported in C++17 std::optional
  // This test verifies we handle pointer types correctly
  int value = 42;
  mcp::optional<int*> opt(&value);
  EXPECT_TRUE(opt);
  EXPECT_EQ(**opt, 42);

  **opt = 99;
  EXPECT_EQ(value, 99);
}

TEST_F(OptionalTest, LargeObjects) {
  struct LargeObject {
    char data[1024];
    LargeObject() { std::fill(data, data + 1024, 'x'); }
  };

  mcp::optional<LargeObject> opt;
  EXPECT_FALSE(opt);

  opt.emplace();
  EXPECT_TRUE(opt);
  EXPECT_EQ(opt->data[0], 'x');
  EXPECT_EQ(opt->data[1023], 'x');
}

// Type deduction tests
TEST_F(OptionalTest, TypeDeduction) {
  auto opt1 = mcp::make_optional(42);
  static_assert(std::is_same<decltype(opt1), mcp::optional<int>>::value,
                "Should deduce optional<int>");

  auto opt2 = mcp::make_optional(std::string("hello"));
  static_assert(std::is_same<decltype(opt2), mcp::optional<std::string>>::value,
                "Should deduce optional<string>");
}
