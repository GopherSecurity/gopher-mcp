# Test executables
add_executable(test_variant test_variant.cpp)
add_executable(test_variant_extensive test_variant_extensive.cpp)
add_executable(test_variant_advanced test_variant_advanced.cpp)
add_executable(test_optional test_optional.cpp)
add_executable(test_optional_extensive test_optional_extensive.cpp)
add_executable(test_optional_advanced test_optional_advanced.cpp)
add_executable(test_mcp_type_helpers test_mcp_type_helpers.cpp)
add_executable(test_mcp_types test_mcp_types.cpp)
add_executable(test_compat test_compat.cpp)
add_executable(test_buffer test_buffer.cpp)
add_executable(test_json test_json.cpp)

# Link libraries
target_link_libraries(test_variant 
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_variant_extensive
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_variant_advanced
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_optional
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_optional_extensive
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_optional_advanced
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_mcp_type_helpers
    gtest 
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_mcp_types
    gtest 
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_compat
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_buffer
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_json
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

# Add tests
add_test(NAME VariantTest COMMAND test_variant)
add_test(NAME VariantExtensiveTest COMMAND test_variant_extensive)
add_test(NAME VariantAdvancedTest COMMAND test_variant_advanced)
add_test(NAME OptionalTest COMMAND test_optional)
add_test(NAME OptionalExtensiveTest COMMAND test_optional_extensive)
add_test(NAME OptionalAdvancedTest COMMAND test_optional_advanced)
add_test(NAME MCPTypeHelpersTest COMMAND test_mcp_type_helpers)
add_test(NAME MCPTypesTest COMMAND test_mcp_types)
add_test(NAME CompatTest COMMAND test_compat)
add_test(NAME BufferTest COMMAND test_buffer)
add_test(NAME JsonTest COMMAND test_json)

# Set test properties
set_tests_properties(VariantTest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "GTEST_COLOR=1"
)

# Custom target to run tests with output
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    DEPENDS test_variant test_variant_extensive test_variant_advanced 
            test_optional test_optional_extensive test_optional_advanced
            test_mcp_type_helpers test_mcp_types test_compat
            test_buffer test_json
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests..."
)