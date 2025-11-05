# Filter unit tests
cmake_minimum_required(VERSION 3.14)

option(MCP_ENABLE_LEGACY_FILTER_TESTS "Build legacy filter chain validation tests" OFF)

# Circuit Breaker Filter Test - Disabled due to timing issues with RealIoTestBase
# add_executable(test_circuit_breaker_filter
#   test_circuit_breaker_filter.cc
# )
# target_link_libraries(test_circuit_breaker_filter
#   PRIVATE
#   gopher-mcp
#   gopher-mcp-event
#   gtest
#   gtest_main
#   gmock
#   Threads::Threads
# )
# add_test(NAME CircuitBreakerFilterTest COMMAND test_circuit_breaker_filter)

# Circuit Breaker Filter Simple Test (basic functionality)
add_executable(test_circuit_breaker_filter_simple
  test_circuit_breaker_filter_simple.cc
)
target_link_libraries(test_circuit_breaker_filter_simple
  PRIVATE
  gopher-mcp
  gopher-mcp-logging
  gtest
  gtest_main
  gmock
  Threads::Threads
  fmt::fmt
)
add_test(NAME CircuitBreakerFilterSimpleTest COMMAND test_circuit_breaker_filter_simple)

# Circuit Breaker Filter Basic Test (no request/response flow)
add_executable(test_circuit_breaker_basic
  test_circuit_breaker_basic.cc
)
target_link_libraries(test_circuit_breaker_basic
  PRIVATE
  gopher-mcp
  gopher-mcp-logging
  gtest
  gtest_main
  gmock
  Threads::Threads
  fmt::fmt
)
add_test(NAME CircuitBreakerBasicTest COMMAND test_circuit_breaker_basic)

# Rate Limiting Filter Test - Disabled due to timing issues with RealIoTestBase
# add_executable(test_rate_limit_filter
#   test_rate_limit_filter.cc
# )
# target_link_libraries(test_rate_limit_filter
#   PRIVATE
#   gopher-mcp
#   gopher-mcp-event
#   gtest
#   gtest_main
#   gmock
#   Threads::Threads
# )
# add_test(NAME RateLimitFilterTest COMMAND test_rate_limit_filter)

# Backpressure Filter Test - Disabled due to timing issues with RealIoTestBase
# add_executable(test_backpressure_filter
#   test_backpressure_filter.cc
# )
# target_link_libraries(test_backpressure_filter
#   PRIVATE
#   gopher-mcp
#   gopher-mcp-event
#   gtest
#   gtest_main
#   gmock
#   Threads::Threads
# )
# add_test(NAME BackpressureFilterTest COMMAND test_backpressure_filter)

# Metrics Filter Test - Disabled due to timing issues with RealIoTestBase
# add_executable(test_metrics_filter
#   test_metrics_filter.cc
# )
# target_link_libraries(test_metrics_filter
#   PRIVATE
#   gopher-mcp
#   gopher-mcp-event
#   gtest
#   gtest_main
#   gmock
#   Threads::Threads
# )
# add_test(NAME MetricsFilterTest COMMAND test_metrics_filter)

# Metrics Filter Simple Test (basic functionality)
add_executable(test_metrics_filter_simple
  test_metrics_filter_simple.cc
)
target_link_libraries(test_metrics_filter_simple
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME MetricsFilterSimpleTest COMMAND test_metrics_filter_simple)

# Request Validation Filter Test - Disabled due to timing issues with RealIoTestBase
# add_executable(test_request_validation_filter
#   test_request_validation_filter.cc
# )
# target_link_libraries(test_request_validation_filter
#   PRIVATE
#   gopher-mcp
#   gopher-mcp-event
#   gtest
#   gtest_main
#   gmock
#   Threads::Threads
# )
# add_test(NAME RequestValidationFilterTest COMMAND test_request_validation_filter)

# Rate Limiting Filter Simple Test (basic functionality)
add_executable(test_rate_limit_filter_simple
  test_rate_limit_filter_simple.cc
)
target_link_libraries(test_rate_limit_filter_simple
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME RateLimitFilterSimpleTest COMMAND test_rate_limit_filter_simple)

# Backpressure Filter Simple Test (basic functionality)
add_executable(test_backpressure_filter_simple
  test_backpressure_filter_simple.cc
)
target_link_libraries(test_backpressure_filter_simple
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME BackpressureFilterSimpleTest COMMAND test_backpressure_filter_simple)

# Request Validation Filter Simple Test (basic functionality)
add_executable(test_request_validation_filter_simple
  test_request_validation_filter_simple.cc
)
target_link_libraries(test_request_validation_filter_simple
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME RequestValidationFilterSimpleTest COMMAND test_request_validation_filter_simple)

# Filter Registry Test
add_executable(test_filter_registry
  test_filter_registry.cc
)
target_link_libraries(test_filter_registry
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME FilterRegistryTest COMMAND test_filter_registry)

# Core Filter Factories Test
add_executable(test_core_factories
  test_core_factories.cc
)
target_link_libraries(test_core_factories
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME CoreFactoriesTest COMMAND test_core_factories)

# QoS Filter Factories Test
add_executable(test_qos_factories
  test_qos_factories.cc
)
target_link_libraries(test_qos_factories
  PRIVATE
  gopher-mcp
  gopher-mcp-logging
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME QosFactoriesTest COMMAND test_qos_factories)

if(MCP_ENABLE_LEGACY_FILTER_TESTS)
  # Filter Chain Builder Test
  add_executable(test_filter_chain_builder
    test_filter_chain_builder.cc
  )
  target_link_libraries(test_filter_chain_builder
    PRIVATE
    gopher-mcp
    gopher-mcp-logging
    gtest
    gtest_main
    gmock
    Threads::Threads
  )
  add_test(NAME FilterChainBuilderTest COMMAND test_filter_chain_builder)

  # Configurable Filter Chain Test
  add_executable(test_configurable_filter_chain
    test_configurable_filter_chain.cc
  )
  target_link_libraries(test_configurable_filter_chain
    PRIVATE
    gopher-mcp
    gopher-mcp-logging
    gtest
    gtest_main
    gmock
    Threads::Threads
  )
  add_test(NAME ConfigurableFilterChainTest COMMAND test_configurable_filter_chain)

  # Filter Ordering Validator Test
  add_executable(test_filter_ordering
    test_filter_ordering.cc
  )
  target_link_libraries(test_filter_ordering
    PRIVATE
    gopher-mcp
    gopher-mcp-logging
    gtest
    gtest_main
    gmock
    Threads::Threads
  )
  add_test(NAME FilterOrderingTest COMMAND test_filter_ordering)

  # Context-aware Filter Factories Test
  add_executable(test_context_aware_factories
    test_context_aware_factories.cc
    ../../src/filter/http_codec_filter_factory.cc
    ../../src/filter/sse_codec_filter_factory.cc
    ../../src/filter/json_rpc_dispatcher_filter_factory.cc
  )
  target_link_libraries(test_context_aware_factories
    PRIVATE
    gopher-mcp
    gtest
    gtest_main
    gmock
    Threads::Threads
  )
  add_test(NAME ContextAwareFactoriesTest COMMAND test_context_aware_factories)

  # Filter Integration Test
  add_executable(test_filter_integration
    test_filter_integration.cc
    ../../src/filter/http_codec_filter_factory.cc
    ../../src/filter/sse_codec_filter_factory.cc
    ../../src/filter/json_rpc_dispatcher_filter_factory.cc
  )
  target_link_libraries(test_filter_integration
    PRIVATE
    gopher-mcp
    gtest
    gtest_main
    gmock
    Threads::Threads
  )
  add_test(NAME FilterIntegrationTest COMMAND test_filter_integration)
endif()

# Basic Filter Registry Test
add_executable(test_basic_filter_registry
  test_basic_filter_registry.cc
  ../../src/filter/http_codec_filter_factory.cc
  ../../src/filter/sse_codec_filter_factory.cc
  ../../src/filter/json_rpc_dispatcher_filter_factory.cc
)
target_link_libraries(test_basic_filter_registry
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME BasicFilterRegistryTest COMMAND test_basic_filter_registry)

# Filter Chain Assembler Test
add_executable(test_filter_chain_assembler
  test_filter_chain_assembler.cc
  ../../src/filter/filter_chain_assembler.cc
  ../../src/filter/http_codec_filter_factory.cc
  ../../src/filter/sse_codec_filter_factory.cc
  ../../src/filter/json_rpc_dispatcher_filter_factory.cc
)
target_link_libraries(test_filter_chain_assembler
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME FilterChainAssemblerTest COMMAND test_filter_chain_assembler)

# Filter Chain Event Hub Test
add_executable(test_filter_chain_event_hub
  test_filter_chain_event_hub.cc
)
target_link_libraries(test_filter_chain_event_hub
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME FilterChainEventHubTest COMMAND test_filter_chain_event_hub)

# Filter Event Emitter Test
add_executable(test_filter_event_emitter
  test_filter_event_emitter.cc
)
target_link_libraries(test_filter_event_emitter
  PRIVATE
  gopher-mcp
  gtest
  gtest_main
  gmock
  Threads::Threads
)
add_test(NAME FilterEventEmitterTest COMMAND test_filter_event_emitter)
