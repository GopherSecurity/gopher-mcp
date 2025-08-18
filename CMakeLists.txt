cmake_minimum_required(VERSION 3.10)

#Prevent in - source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed.
Please create a build directory and run cmake from there:
    mkdir build
    cd build
    cmake ..
You may need to remove CMakeCache.txt and CMakeFiles/")
endif()

project(mcp-cpp-sdk VERSION 0.1.0 LANGUAGES C CXX)

#Set C++ standard
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Option to use std::optional / variant when available
option(MCP_USE_STD_TYPES "Use std::optional and std::variant when available (C++17)" ON)

#Option to enable strict warnings(disable for development)
option(MCP_STRICT_WARNINGS "Enable strict compiler warnings" OFF)

#Option to build C API bindings
option(BUILD_C_API "Build C API bindings" OFF)  # TODO: Re-enable after fixing compilation issues

#Option to build language binding examples
option(BUILD_BINDINGS_EXAMPLES "Build language binding examples" ON)

#Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

#Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    if(MCP_STRICT_WARNINGS)
        add_compile_options(-Werror)
    else()
#Disable warnings for unused entities during development
        add_compile_options(-Wno-unused-parameter -Wno-unused-variable -Wno-unused-const-variable)
        add_compile_options(-Wno-unused-function -Wno-unused-private-field)
    endif()
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MCP_STRICT_WARNINGS)
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

#Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

#Configure based on C++ standard
if(CMAKE_CXX_STANDARD GREATER_EQUAL 17 AND MCP_USE_STD_TYPES)
    add_compile_definitions(MCP_USE_STD_OPTIONAL_VARIANT=1)
else()
    add_compile_definitions(MCP_USE_STD_OPTIONAL_VARIANT=0)
endif()

#Find packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

#Find libevent
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBEVENT libevent)
endif()

if(NOT LIBEVENT_FOUND)
#Try to find libevent manually
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
#If pkg - config found it, we need to add pthreads library manually
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

#Dependencies via FetchContent
include(FetchContent)

#llhttp - High - performance HTTP parser
#Option to enable / disable llhttp(can be disabled if build issues)
option(MCP_USE_LLHTTP "Use llhttp for HTTP/1.x parsing" ON)

if(MCP_USE_LLHTTP)
#Use a simpler approach - download just the release archive
    FetchContent_Declare(
        llhttp
        URL https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.1.3.tar.gz
    )
    
    FetchContent_GetProperties(llhttp)
    if(NOT llhttp_POPULATED)
        FetchContent_Populate(llhttp)

#Build llhttp as a simple static library
#The release archive contains pre - generated C files
        file(GLOB LLHTTP_SOURCES ${llhttp_SOURCE_DIR}/src/*.c)
        
        add_library(llhttp STATIC ${LLHTTP_SOURCES})
        target_include_directories(llhttp PUBLIC ${llhttp_SOURCE_DIR}/include)
        
        # Disable warnings for third-party code
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(llhttp PRIVATE -w)
        endif()
        
        set(LLHTTP_FOUND TRUE)
        message(STATUS "llhttp will be built from source")
    endif()
else()
    set(LLHTTP_FOUND FALSE)
    message(STATUS "llhttp support disabled")
endif()

# nghttp2 - HTTP/2 C library
# Option to enable/disable nghttp2 (can be disabled if build issues)
option(MCP_USE_NGHTTP2 "Use nghttp2 for HTTP/2 parsing" ON)

if(MCP_USE_NGHTTP2)
    # Try to find system nghttp2 first
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(NGHTTP2 libnghttp2)
    endif()
    
    if(NOT NGHTTP2_FOUND)
        # Download and build nghttp2
        FetchContent_Declare(
            nghttp2
            GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
            GIT_TAG v1.58.0
        )
        
        # Configure nghttp2 build options before fetching
        set(ENABLE_LIB_ONLY ON CACHE BOOL "Build only nghttp2 library")
        set(ENABLE_STATIC_LIB ON CACHE BOOL "Build static library")
        set(ENABLE_SHARED_LIB OFF CACHE BOOL "Don't build shared library")
        set(ENABLE_APP OFF CACHE BOOL "Don't build applications")
        set(ENABLE_HPACK_TOOLS OFF CACHE BOOL "Don't build HPACK tools")
        set(ENABLE_EXAMPLES OFF CACHE BOOL "Don't build examples")
        set(ENABLE_PYTHON_BINDINGS OFF CACHE BOOL "Don't build Python bindings")
        set(ENABLE_FAILMALLOC OFF CACHE BOOL "Disable failmalloc")
        set(ENABLE_THREADS ON CACHE BOOL "Enable threads")
        set(ENABLE_WERROR OFF CACHE BOOL "Disable -Werror")
        set(ENABLE_DEBUG OFF CACHE BOOL "Disable debug output")
        
        FetchContent_MakeAvailable(nghttp2)
        
        set(NGHTTP2_FOUND TRUE)
        set(NGHTTP2_INCLUDE_DIRS 
            ${nghttp2_SOURCE_DIR}/lib/includes 
            ${nghttp2_BINARY_DIR}/lib/includes)
        set(NGHTTP2_LIBRARIES nghttp2_static)
        
        message(STATUS "nghttp2 will be built from source")
    else()
        message(STATUS "Found system nghttp2")
    endif()
else()
    set(NGHTTP2_FOUND FALSE)
    message(STATUS "nghttp2 support disabled")
endif()

# nlohmann/json
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
    src/json/json_bridge.cc
    src/json/json_serialization.cc
    src/network/address_impl.cc
    src/network/io_socket_handle_impl.cc
    src/network/socket_impl.cc
    src/network/socket_interface_impl.cc
    src/network/socket_option_impl.cc
    src/network/transport_socket.cc
    src/network/filter_impl.cc
    src/network/connection_impl.cc
    src/network/connection_utility.cc
    src/network/connection_state_machine.cc
    src/network/filter_chain_state_machine.cc
    src/network/listener_impl.cc
    src/network/tcp_server_listener_impl.cc
    src/network/server_listener_manager_impl.cc
    src/network/connection_manager_impl.cc
    src/stream_info/stream_info_impl.cc
    src/transport/stdio_transport_socket.cc
    src/transport/stdio_pipe_transport.cc
    src/transport/ssl_context.cc
    src/transport/ssl_state_machine.cc
    src/transport/ssl_transport_socket.cc
    src/transport/https_sse_transport_factory.cc
    src/transport/transport_socket_state_machine.cc
    src/transport/tcp_transport_socket_state_machine.cc
    src/transport/tcp_transport_socket.cc
    src/filter/http_codec_filter.cc
    src/filter/sse_codec_filter.cc
    src/filter/http_codec_state_machine.cc
    src/filter/sse_codec_state_machine.cc
    src/filter/mcp_http_filter_chain_factory.cc
    src/mcp_connection_manager.cc
    src/client/mcp_client.cc
    src/server/mcp_server.cc
)

# HTTP parser sources (conditional on llhttp)
set(MCP_HTTP_SOURCES
    src/http/http_parser.cc
    src/http/sse_parser.cc
)

if(LLHTTP_FOUND)
    list(APPEND MCP_HTTP_SOURCES
        src/http/llhttp_parser.cc
        src/transport/http_sse_transport_socket.cc  # New llhttp-based implementation
        src/transport/http_sse_transport_socket_v2.cc  # V2 with layered architecture
        src/transport/http_sse_state_machine.cc     # HTTP+SSE state machine
    )
endif()

if(NGHTTP2_FOUND)
    list(APPEND MCP_HTTP_SOURCES
        src/http/nghttp2_parser.cc
    )
endif()

# Echo library source files (transport-agnostic advanced)
set(MCP_ECHO_ADVANCED_SOURCES
    src/echo/echo_client_advanced.cc
    src/echo/echo_server_advanced.cc
    src/echo/echo_stdio_transport_advanced.cc
)

# Event system source files
set(MCP_EVENT_SOURCES
    src/event/libevent_dispatcher.cc
    src/event/worker.cc
)

# Create MCP SDK library
add_library(mcp-sdk STATIC ${MCP_SDK_SOURCES} ${MCP_HTTP_SOURCES})
target_include_directories(mcp-sdk PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mcp-sdk PUBLIC Threads::Threads nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto)

# Add llhttp if found
if(LLHTTP_FOUND)
    target_link_libraries(mcp-sdk PUBLIC llhttp)
    target_compile_definitions(mcp-sdk PUBLIC MCP_HAS_LLHTTP=1)
else()
    target_compile_definitions(mcp-sdk PUBLIC MCP_HAS_LLHTTP=0)
endif()

# Add nghttp2 if found
if(NGHTTP2_FOUND)
    target_include_directories(mcp-sdk PUBLIC ${NGHTTP2_INCLUDE_DIRS})
    if(NGHTTP2_LIBRARY_DIRS)
        target_link_directories(mcp-sdk PUBLIC ${NGHTTP2_LIBRARY_DIRS})
    endif()
    if(NGHTTP2_LIBRARIES)
        target_link_libraries(mcp-sdk PUBLIC ${NGHTTP2_LIBRARIES})
    else()
        target_link_libraries(mcp-sdk PUBLIC nghttp2)
    endif()
    target_compile_definitions(mcp-sdk PUBLIC MCP_HAS_NGHTTP2=1)
else()
    target_compile_definitions(mcp-sdk PUBLIC MCP_HAS_NGHTTP2=0)
endif()

# Create MCP Echo Advanced library (transport-agnostic advanced echo components)
add_library(mcp-echo-advanced STATIC ${MCP_ECHO_ADVANCED_SOURCES})
target_include_directories(mcp-echo-advanced PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mcp-echo-advanced PUBLIC mcp-sdk Threads::Threads nlohmann_json::nlohmann_json)

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

# Test targets:
# - make test          : Standard CMake test target (shows progress)
# - make check         : Run tests with minimal output (recommended)
# - make check-verbose : Run tests with detailed output
# - make check-parallel: Run tests in parallel with minimal output
# - ctest             : Direct CTest invocation (customizable)

# Add test subdirectory
add_subdirectory(tests)

# Add C API subdirectory (if enabled)
if(BUILD_C_API)
    add_subdirectory(src/c_api)
    message(STATUS "Building C API bindings")
endif()

# Build examples (optional)
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_executable(event_loop_example examples/event_loop_example.cc)
    target_link_libraries(event_loop_example 
        mcp-event 
        mcp-sdk 
        Threads::Threads
    )
    
    # Basic stdio echo examples are now built in examples/stdio_echo/CMakeLists.txt
    
    add_executable(test_echo_integration examples/test_echo_integration.cc)
    target_link_libraries(test_echo_integration
        mcp-sdk
        Threads::Threads
    )
    
    # Add advanced stdio echo examples subdirectory
    add_subdirectory(examples/stdio_echo)
    
    # Add TCP echo examples subdirectory
    add_subdirectory(examples/tcp_echo)
    
    # Add MCP examples subdirectory
    add_subdirectory(examples/mcp)
    
    # Add transport examples subdirectory
    add_subdirectory(examples/transport)
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

# Add custom target for running tests (quiet by default)
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests..."
)

# Add custom target for verbose test output
add_custom_target(check-verbose
    COMMAND ${CMAKE_CTEST_COMMAND} -V
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}  
    COMMENT "Running tests with verbose output..."
)

# Add custom target that runs tests in parallel
add_custom_target(check-parallel
    COMMAND ${CMAKE_CTEST_COMMAND} -j8 --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests in parallel..."
)