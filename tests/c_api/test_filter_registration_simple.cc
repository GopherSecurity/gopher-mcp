/**
 * @file test_filter_registration_simple.cc
 * @brief Simple test to verify all core filters are registered
 */

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/filter/filter_registry.h"
#include <gtest/gtest.h>
#include <vector>
#include <string>

class FilterRegistrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize MCP
    mcp_result_t init_result = mcp_init(nullptr);
    ASSERT_EQ(init_result, MCP_OK) << "mcp_init failed";
  }

  void TearDown() override {
    mcp_shutdown();
  }
};

TEST_F(FilterRegistrationTest, AllCoreFiltersRegistered) {
  auto& registry = mcp::filter::FilterRegistry::instance();

  // Expected filters
  std::vector<std::string> expected = {
    "http.codec", "sse.codec", "json_rpc.dispatcher",
    "rate_limit", "circuit_breaker", "metrics"
  };

  // Check each filter
  for (const auto& name : expected) {
    bool has_context = registry.hasContextFactory(name);
    bool has_trad = registry.hasFactory(name);
    bool registered = has_context || has_trad;

    EXPECT_TRUE(registered) << name << " filter not registered";
  }

  // Verify counts
  auto context_factories = registry.listContextFactories();
  auto trad_factories = registry.listFactories();

  EXPECT_EQ(context_factories.size(), 3u)
      << "Expected 3 context factories (http.codec, sse.codec, json_rpc.dispatcher)";
  EXPECT_EQ(trad_factories.size(), 3u)
      << "Expected 3 traditional factories (rate_limit, circuit_breaker, metrics)";

  // Log what we found
  std::cout << "\n=== Context Factories ===" << std::endl;
  for (const auto& name : context_factories) {
    std::cout << "  - " << name << std::endl;
  }

  std::cout << "\n=== Traditional Factories ===" << std::endl;
  for (const auto& name : trad_factories) {
    std::cout << "  - " << name << std::endl;
  }
}

TEST_F(FilterRegistrationTest, ContextFactoriesCorrect) {
  auto& registry = mcp::filter::FilterRegistry::instance();

  EXPECT_TRUE(registry.hasContextFactory("http.codec"));
  EXPECT_TRUE(registry.hasContextFactory("sse.codec"));
  EXPECT_TRUE(registry.hasContextFactory("json_rpc.dispatcher"));
}

TEST_F(FilterRegistrationTest, TraditionalFactoriesCorrect) {
  auto& registry = mcp::filter::FilterRegistry::instance();

  EXPECT_TRUE(registry.hasFactory("rate_limit"));
  EXPECT_TRUE(registry.hasFactory("circuit_breaker"));
  EXPECT_TRUE(registry.hasFactory("metrics"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
