/**
 * @file test_filter_only_api_minimal.cc
 * @brief Minimal tests for Filter-Only C API
 *
 * Tests absolute minimal functionality without any JSON or dispatcher dependencies.
 */

#include "mcp/c_api/mcp_c_filter_only_api.h"

#include <gtest/gtest.h>
#include <iostream>

// Test that we can include the header and use basic null-safe functions
TEST(FilterOnlyAPIMinimal, NullSafety) {
  // These should all be no-ops and not crash
  mcp_filter_only_chain_release(0);
  mcp_filter_only_chain_retain(0);
  mcp_filter_only_validation_result_free(nullptr);
  mcp_filter_only_assembly_result_free(nullptr);

  std::cout << "Null safety tests passed\n";
  SUCCEED();
}

// Test that handle constants are available
TEST(FilterOnlyAPIMinimal, HandleConstants) {
  mcp_filter_only_chain_handle_t invalid_handle = 0;
  EXPECT_EQ(invalid_handle, 0u);

  // Just verify we can use the type
  mcp_filter_only_chain_handle_t test_handle = 12345;
  EXPECT_EQ(test_handle, 12345u);

  std::cout << "Handle constant tests passed\n";
  SUCCEED();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}