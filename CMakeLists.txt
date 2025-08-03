cmake_minimum_required(VERSION 3.10)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed.
Please create a build directory and run cmake from there:
    mkdir build
    cd build
    cmake ..
You may need to remove CMakeCache.txt and CMakeFiles/")
endif()

project(mcp-cpp-sdk VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to use std::optional/variant when available
option(MCP_USE_STD_TYPES "Use std::optional and std::variant when available (C++17)" ON)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Configure based on C++ standard
if(CMAKE_CXX_STANDARD GREATER_EQUAL 17 AND MCP_USE_STD_TYPES)
    add_compile_definitions(MCP_USE_STD_OPTIONAL_VARIANT=1)
else()
    add_compile_definitions(MCP_USE_STD_OPTIONAL_VARIANT=0)
endif()

# Find packages
find_package(Threads REQUIRED)

# Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Source files
set(MCP_SDK_SOURCES
    src/buffer/buffer_impl.cc
)

# Create MCP SDK library
add_library(mcp-sdk STATIC ${MCP_SDK_SOURCES})
target_include_directories(mcp-sdk PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mcp-sdk PUBLIC Threads::Threads)

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)

# Add custom target for formatting
add_custom_target(format
    COMMAND find ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
            -name "*.h" -o -name "*.cpp" -o -name "*.cc" | xargs clang-format -i
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting all source files with clang-format"
)

# Add custom target to check formatting without modifying files
add_custom_target(check-format
    COMMAND find ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
            -name "*.h" -o -name "*.cpp" -o -name "*.cc" | xargs clang-format --dry-run --Werror
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source file formatting with clang-format"
)