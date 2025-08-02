# Test executable
add_executable(test_variant test_variant.cpp)

# Link libraries
target_link_libraries(test_variant 
    gtest 
    gtest_main
    Threads::Threads
)

# Add test
add_test(NAME VariantTest COMMAND test_variant)

# Set test properties
set_tests_properties(VariantTest PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "GTEST_COLOR=1"
)

# Custom target to run tests with output
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    DEPENDS test_variant
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests..."
)