#include <iostream>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "mcp/core/compat.h"

class CompatTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test that we're using the correct implementation based on C++ version
TEST_F(CompatTest, ImplementationSelection) {
  std::cout << "__cplusplus = " << __cplusplus << std::endl;
  std::cout << "MCP_USE_STD_OPTIONAL_VARIANT = " << MCP_USE_STD_OPTIONAL_VARIANT
            << std::endl;

#if MCP_HAS_STD_OPTIONAL
  std::cout << "Using std::optional (C++17 or later)" << std::endl;
  // Verify we're using std::optional
  static_assert(std::is_same<mcp::optional<int>, std::optional<int>>::value,
                "mcp::optional should be std::optional in C++17");
#else
  std::cout << "Using mcp::optional (C++14)" << std::endl;
  // Verify we're NOT using std::optional
  // (Can't directly test this, but the fact it compiles in C++14 is the test)
#endif

#if MCP_HAS_STD_VARIANT
  std::cout << "Using std::variant (C++17 or later)" << std::endl;
  // Verify we're using std::variant
  static_assert(
      std::is_same<mcp::variant<int, double>, std::variant<int, double>>::value,
      "mcp::variant should be std::variant in C++17");
#else
  std::cout << "Using mcp::variant (C++14)" << std::endl;
#endif
}

// Test basic optional functionality through the compatibility layer
TEST_F(CompatTest, OptionalBasics) {
  // Default construction
  mcp::optional<int> opt1;
  EXPECT_FALSE(opt1.has_value());

  // Value construction
  mcp::optional<int> opt2(42);
  EXPECT_TRUE(opt2.has_value());
  EXPECT_EQ(*opt2, 42);

  // nullopt construction
  mcp::optional<int> opt3 = mcp::nullopt;
  EXPECT_FALSE(opt3.has_value());

  // make_optional
  auto opt4 = mcp::make_optional(100);
  EXPECT_TRUE(opt4.has_value());
  EXPECT_EQ(*opt4, 100);

  // in_place construction
  mcp::optional<std::string> opt5(mcp::in_place, "hello");
  EXPECT_TRUE(opt5.has_value());
  EXPECT_EQ(*opt5, "hello");
}

// Test basic variant functionality through the compatibility layer
TEST_F(CompatTest, VariantBasics) {
  // Default construction (first type)
  mcp::variant<int, double, std::string> var1;
  EXPECT_TRUE(mcp::holds_alternative<int>(var1));
  EXPECT_EQ(mcp::get<int>(var1), 0);

  // Value construction
  mcp::variant<int, double, std::string> var2(3.14);
  EXPECT_TRUE(mcp::holds_alternative<double>(var2));
  EXPECT_DOUBLE_EQ(mcp::get<double>(var2), 3.14);

  // String construction
  mcp::variant<int, double, std::string> var3("test");
  EXPECT_TRUE(mcp::holds_alternative<std::string>(var3));
  EXPECT_EQ(mcp::get<std::string>(var3), "test");

  // get_if
  auto* ptr = mcp::get_if<double>(&var2);
  ASSERT_NE(ptr, nullptr);
  EXPECT_DOUBLE_EQ(*ptr, 3.14);

  auto* null_ptr = mcp::get_if<int>(&var2);
  EXPECT_EQ(null_ptr, nullptr);
}

// Test visitor pattern
TEST_F(CompatTest, VariantVisitor) {
  mcp::variant<int, double, std::string> var(42);

  // Use overloaded lambdas for C++14 compatibility
  struct Visitor {
    std::string operator()(int arg) const {
      return "int: " + std::to_string(arg);
    }
    std::string operator()(double arg) const {
      return "double: " + std::to_string(arg);
    }
    std::string operator()(const std::string& arg) const {
      return "string: " + arg;
    }
  };

  auto result = mcp::visit(Visitor{}, var);
  EXPECT_EQ(result, "int: 42");

  // Test with double
  var = 3.14;
  result = mcp::visit(Visitor{}, var);
  EXPECT_EQ(result, "double: 3.140000");
}

// Test interoperability with MCP types
TEST_F(CompatTest, MCPTypeCompatibility) {
  // Test that MCP types work correctly with the compatibility layer
  struct TestStruct {
    mcp::optional<int> id;
    mcp::variant<std::string, int> value;
  };

  TestStruct test1{mcp::make_optional(123), std::string("test")};
  EXPECT_TRUE(test1.id.has_value());
  EXPECT_EQ(*test1.id, 123);
  EXPECT_TRUE(mcp::holds_alternative<std::string>(test1.value));

  TestStruct test2{mcp::nullopt, 456};
  EXPECT_FALSE(test2.id.has_value());
  EXPECT_TRUE(mcp::holds_alternative<int>(test2.value));
  EXPECT_EQ(mcp::get<int>(test2.value), 456);
}

// Test that the compatibility layer doesn't break existing code
TEST_F(CompatTest, BackwardCompatibility) {
  // Code that worked with mcp::optional should still work
  auto opt = mcp::make_optional(42);
  EXPECT_TRUE(opt);
  EXPECT_EQ(opt.value(), 42);

  opt = mcp::nullopt;
  EXPECT_FALSE(opt);

  // Code that worked with mcp::variant should still work
  mcp::variant<int, std::string> var(123);
  EXPECT_EQ(mcp::get<int>(var), 123);

  var = std::string("hello");
  EXPECT_EQ(mcp::get<std::string>(var), "hello");
}