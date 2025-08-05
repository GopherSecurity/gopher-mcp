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
    # Disable warnings for unused variables and fields (common in platform-specific code)
    add_compile_options(-Wno-unused-variable -Wno-unused-const-variable)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-unused-private-field)
    endif()
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

# Find libevent
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBEVENT libevent)
endif()

if(NOT LIBEVENT_FOUND)
    # Try to find libevent manually
    find_path(LIBEVENT_INCLUDE_DIR event2/event.h
        PATHS /usr/local/include /usr/include
    )
    find_library(LIBEVENT_CORE_LIBRARY
        NAMES event_core event
        PATHS /usr/local/lib /usr/lib /usr/local/Cellar/libevent/2.1.12_1/lib
    )
    find_library(LIBEVENT_PTHREADS_LIBRARY
        NAMES event_pthreads
        PATHS /usr/local/lib /usr/lib /usr/local/Cellar/libevent/2.1.12_1/lib
    )
    
    if(LIBEVENT_INCLUDE_DIR AND LIBEVENT_CORE_LIBRARY)
        set(LIBEVENT_FOUND TRUE)
        set(LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})
        set(LIBEVENT_LIBRARIES ${LIBEVENT_CORE_LIBRARY})
        if(LIBEVENT_PTHREADS_LIBRARY)
            list(APPEND LIBEVENT_LIBRARIES ${LIBEVENT_PTHREADS_LIBRARY})
        endif()
    endif()
else()
    # If pkg-config found it, we need to add pthreads library manually
    find_library(LIBEVENT_PTHREADS_LIBRARY
        NAMES event_pthreads
        PATHS ${LIBEVENT_LIBRARY_DIRS} /usr/local/lib
    )
    if(LIBEVENT_PTHREADS_LIBRARY)
        set(LIBEVENT_LIBRARIES ${LIBEVENT_LIBRARIES} ${LIBEVENT_PTHREADS_LIBRARY})
    endif()
endif()

if(NOT LIBEVENT_FOUND)
    message(FATAL_ERROR "libevent not found. Please install libevent development package.")
endif()

# nlohmann/json
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Google Test
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
    src/json.cc
    src/network/address_impl.cc
    src/network/io_socket_handle_impl.cc
    src/network/socket_impl.cc
    src/network/socket_interface_impl.cc
    src/network/socket_option_impl.cc
    src/network/transport_socket.cc
    src/stream_info/stream_info_impl.cc
)

# Event system source files
set(MCP_EVENT_SOURCES
    src/event/libevent_dispatcher.cc
    src/event/worker.cc
)

# Create MCP SDK library
add_library(mcp-sdk STATIC ${MCP_SDK_SOURCES})
target_include_directories(mcp-sdk PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mcp-sdk PUBLIC Threads::Threads nlohmann_json::nlohmann_json)

# Create MCP Event library
add_library(mcp-event STATIC ${MCP_EVENT_SOURCES})
target_include_directories(mcp-event PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${LIBEVENT_INCLUDE_DIRS}
)
target_link_libraries(mcp-event PUBLIC 
    mcp-sdk
    Threads::Threads
)

if(LIBEVENT_FOUND)
    if(LIBEVENT_LIBRARY_DIRS)
        target_link_directories(mcp-event PUBLIC ${LIBEVENT_LIBRARY_DIRS})
    endif()
    target_link_libraries(mcp-event PUBLIC ${LIBEVENT_LIBRARIES})
endif()

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)

# Build examples (optional)
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_executable(event_loop_example examples/event_loop_example.cc)
    target_link_libraries(event_loop_example 
        mcp-event 
        mcp-sdk 
        Threads::Threads
    )
endif()

# Add custom target for formatting
add_custom_target(format
    COMMAND find ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
            -name "*.h" -o -name "*.cc" | xargs clang-format -i
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting all source files with clang-format"
)

# Add custom target to check formatting without modifying files
add_custom_target(check-format
    COMMAND find ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
            -name "*.h" -o -name "*.cc" | xargs clang-format --dry-run --Werror
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source file formatting with clang-format"
)