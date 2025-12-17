cmake_minimum_required(VERSION 3.16)
project(auth_tests_standalone)

# Set C++ standard to C++17 for tests (auth library uses C++11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages for auth library
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Only fetch Google Test, nothing else
message(STATUS "Fetching Google Test framework only...")
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
)

# Path to standalone auth library
set(AUTH_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build-auth-only")
link_directories(${AUTH_LIB_DIR})

# Common libraries for auth tests
set(COMMON_LIBS
    gopher_mcp_auth
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
    GTest::gtest
    GTest::gtest_main
)

# Helper function to add auth tests
function(add_auth_test test_name)
    add_executable(${test_name} ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.cc)
    target_link_libraries(${test_name} ${COMMON_LIBS})
    # Set runtime path for finding the auth library
    set_target_properties(${test_name} PROPERTIES
        BUILD_RPATH "${AUTH_LIB_DIR}"
        INSTALL_RPATH "${AUTH_LIB_DIR}"
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Add all auth tests
enable_testing()
add_auth_test(test_auth_types)
add_auth_test(benchmark_jwt_validation)
add_auth_test(benchmark_crypto_optimization)
add_auth_test(benchmark_network_optimization)
add_auth_test(test_keycloak_integration)
add_auth_test(test_mcp_inspector_flow)
add_auth_test(test_complete_integration)
add_auth_test(test_token_exchange)

# Optional: Add test for memory cache if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_memory_cache.cc")
    add_auth_test(test_memory_cache)
endif()

message(STATUS "")
message(STATUS "Auth tests configuration complete!")
message(STATUS "  Auth library: ${AUTH_LIB_DIR}/libgopher_mcp_auth.dylib")
message(STATUS "  Google Test: Will be downloaded to build/_deps/googletest-*")
message(STATUS "  Other deps: NOT required (no llhttp, nghttp2, nlohmann_json)")
message(STATUS "")