cmake_minimum_required(VERSION 3.16)

# Allow VERSION in project() command
cmake_policy(SET CMP0048 NEW)

project(gopher-mcp-auth VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard to C++11 for maximum compatibility
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Auth library source files
set(AUTH_SOURCES
    mcp_auth_implementation.cc
    mcp_auth_crypto_optimized.cc
    mcp_auth_network_optimized.cc
)

# Auth library header files  
set(AUTH_HEADERS
    ${CMAKE_SOURCE_DIR}/../../include/mcp/auth/auth_c_api.h
    ${CMAKE_SOURCE_DIR}/../../include/mcp/auth/auth_types.h
    ${CMAKE_SOURCE_DIR}/../../include/mcp/auth/memory_cache.h
)

# Create shared library
add_library(gopher-mcp-auth SHARED ${AUTH_SOURCES})

# Set library properties
set_target_properties(gopher-mcp-auth PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${AUTH_HEADERS}"
    OUTPUT_NAME "gopher_mcp_auth"
)

# Include directories
target_include_directories(gopher-mcp-auth PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
)

# C++11 compatibility: Add preprocessor define to handle make_unique
target_compile_definitions(gopher-mcp-auth PRIVATE USE_CPP11_COMPAT=1)

# Link libraries
target_link_libraries(gopher-mcp-auth
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CURL_LIBRARIES}
    PRIVATE
        pthread
)

# Create static library
add_library(gopher-mcp-auth-static STATIC ${AUTH_SOURCES})

set_target_properties(gopher-mcp-auth-static PROPERTIES
    VERSION ${PROJECT_VERSION}
    OUTPUT_NAME "gopher_mcp_auth"
)

target_include_directories(gopher-mcp-auth-static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
)

# C++11 compatibility for static library too
target_compile_definitions(gopher-mcp-auth-static PRIVATE USE_CPP11_COMPAT=1)

target_link_libraries(gopher-mcp-auth-static
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CURL_LIBRARIES}
    PRIVATE
        pthread
)

# Installation rules
install(TARGETS gopher-mcp-auth gopher-mcp-auth-static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/mcp/auth
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/gopher-mcp-auth-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-auth-config.cmake"
    INSTALL_DESTINATION lib/cmake/gopher-mcp-auth
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-auth-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-auth-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-auth-config-version.cmake"
    DESTINATION lib/cmake/gopher-mcp-auth
)

# Build verification executable
add_executable(verify_auth verify_auth.cc)

# Set properties for macOS 10.14 compatibility
set_target_properties(verify_auth PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "verify_auth"
)

# Link only with system libraries (no dependency on our auth library)
target_link_libraries(verify_auth PRIVATE ${CMAKE_DL_LIBS})

# Install verification app
install(TARGETS verify_auth
    RUNTIME DESTINATION bin
)

# Export compile commands for development
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional: Build tests
option(BUILD_AUTH_TESTS "Build auth library tests" OFF)

if(BUILD_AUTH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()