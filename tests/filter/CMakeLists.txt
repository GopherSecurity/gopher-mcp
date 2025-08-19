# Filter unit tests
cmake_minimum_required(VERSION 3.14)

# Circuit Breaker Filter Test
add_executable(test_circuit_breaker_filter
  test_circuit_breaker_filter.cc
)
target_link_libraries(test_circuit_breaker_filter
  PRIVATE
  mcp_core
  mcp_test_lib
  GTest::gtest_main
  GTest::gmock
)
add_test(NAME CircuitBreakerFilterTest COMMAND test_circuit_breaker_filter)

# Rate Limiting Filter Test
add_executable(test_rate_limit_filter
  test_rate_limit_filter.cc
)
target_link_libraries(test_rate_limit_filter
  PRIVATE
  mcp_core
  mcp_test_lib
  GTest::gtest_main
  GTest::gmock
)
add_test(NAME RateLimitFilterTest COMMAND test_rate_limit_filter)

# Backpressure Filter Test
add_executable(test_backpressure_filter
  test_backpressure_filter.cc
)
target_link_libraries(test_backpressure_filter
  PRIVATE
  mcp_core
  mcp_test_lib
  GTest::gtest_main
  GTest::gmock
)
add_test(NAME BackpressureFilterTest COMMAND test_backpressure_filter)

# Metrics Filter Test
add_executable(test_metrics_filter
  test_metrics_filter.cc
)
target_link_libraries(test_metrics_filter
  PRIVATE
  mcp_core
  mcp_test_lib
  GTest::gtest_main
  GTest::gmock
)
add_test(NAME MetricsFilterTest COMMAND test_metrics_filter)

# Request Validation Filter Test
add_executable(test_request_validation_filter
  test_request_validation_filter.cc
)
target_link_libraries(test_request_validation_filter
  PRIVATE
  mcp_core
  mcp_test_lib
  GTest::gtest_main
  GTest::gmock
)
add_test(NAME RequestValidationFilterTest COMMAND test_request_validation_filter)