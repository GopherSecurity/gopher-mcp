cmake_minimum_required(VERSION 3.10)

# Set policy for FetchContent timestamp extraction
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

#Prevent in - source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed.
Please create a build directory and run cmake from there:
    mkdir build
    cd build
    cmake ..
You may need to remove CMakeCache.txt and CMakeFiles/")
endif()

project(gopher-mcp VERSION 0.1.0 LANGUAGES C CXX)

# Set library version for shared libraries
set(GOPHER_MCP_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(GOPHER_MCP_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(GOPHER_MCP_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(GOPHER_MCP_VERSION ${PROJECT_VERSION})

# Detect if we're being used as a submodule
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(GOPHER_MCP_IS_SUBMODULE TRUE)
    message(STATUS "gopher-mcp: Building as submodule")
else()
    set(GOPHER_MCP_IS_SUBMODULE FALSE)
    message(STATUS "gopher-mcp: Building as standalone project")
endif()

# Set the root directory for gopher-mcp (use current directory for proper submodule support)
set(GOPHER_MCP_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

#Set C++ standard
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14)
endif()
# When used as submodule, inherit parent's C++ standard
if(GOPHER_MCP_IS_SUBMODULE)
    message(STATUS "gopher-mcp: Inheriting parent C++ standard: ${CMAKE_CXX_STANDARD}")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Option to use std::optional / variant when available
option(MCP_USE_STD_TYPES "Use std::optional and std::variant when available (C++17)" ON)

#Option to enable strict warnings(disable for development)
option(MCP_STRICT_WARNINGS "Enable strict compiler warnings" OFF)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_STATIC_LIBS "Build static libraries" ON)
option(GOPHER_MCP_INSTALL "Generate installation targets" ON)

# Option to build C API bindings (for FFI language bindings)
option(BUILD_C_API "Build C API bindings for FFI" ON)

#Option to build language binding examples
if(GOPHER_MCP_IS_SUBMODULE)
    option(BUILD_BINDINGS_EXAMPLES "Build language binding examples" OFF)
else()
    option(BUILD_BINDINGS_EXAMPLES "Build language binding examples" ON)
endif()

# Option to build tests when used as submodule (defaults to OFF for submodule)
if(GOPHER_MCP_IS_SUBMODULE)
    option(GOPHER_MCP_BUILD_TESTS "Build gopher-mcp tests when used as submodule" OFF)
    # Honor parent BUILD_TESTS setting if not explicitly set
    if(NOT DEFINED BUILD_TESTS)
        set(BUILD_TESTS OFF)
    endif()
else()
    option(BUILD_TESTS "Build tests" ON)
endif()

#Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Set custom install prefix
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install_prefix_dir")

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
# Always set include directories for building gopher-mcp sources
# Use BEFORE to ensure our includes come before parent project's includes
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include)

#Configure based on C++ standard
if(CMAKE_CXX_STANDARD GREATER_EQUAL 17 AND MCP_USE_STD_TYPES)
    add_compile_definitions(MCP_USE_STD_OPTIONAL_VARIANT=1)
else()
    add_compile_definitions(MCP_USE_STD_OPTIONAL_VARIANT=0)
endif()

#Find packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(yaml-cpp REQUIRED)

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
message(STATUS "")
message(STATUS "==================================================")
message(STATUS "Checking and downloading dependencies if needed...")
message(STATUS "This may take several minutes on first build.")
message(STATUS "Subsequent builds will use cached downloads.")
message(STATUS "==================================================")
message(STATUS "")

#llhttp - High - performance HTTP parser
#Option to enable / disable llhttp(can be disabled if build issues)
option(MCP_USE_LLHTTP "Use llhttp for HTTP/1.x parsing" ON)

if(MCP_USE_LLHTTP)
    # Use a simpler approach - download just the release archive
    FetchContent_Declare(
        llhttp
        URL https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.1.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    
    message(STATUS "Downloading llhttp... This may take a moment depending on your connection speed.")
    FetchContent_MakeAvailable(llhttp)
    message(STATUS "llhttp download complete.")
    
    # Build llhttp as a simple static library
    # The release archive contains pre-generated C files
    file(GLOB LLHTTP_SOURCES ${llhttp_SOURCE_DIR}/src/*.c)
    
    if(NOT TARGET llhttp)
        add_library(llhttp STATIC ${LLHTTP_SOURCES})
        target_include_directories(llhttp PUBLIC ${llhttp_SOURCE_DIR}/include)

        # Enable position-independent code for shared library linking
        set_target_properties(llhttp PROPERTIES POSITION_INDEPENDENT_CODE ON)

        # Disable warnings for third-party code
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(llhttp PRIVATE -w)
        endif()
    endif()
    
    set(LLHTTP_FOUND TRUE)
    message(STATUS "llhttp successfully configured for build from source")
else()
    set(LLHTTP_FOUND FALSE)
    message(STATUS "llhttp support disabled")
endif()

# nghttp2 - HTTP/2 C library
# Option to enable/disable nghttp2 (can be disabled if build issues)
option(MCP_USE_NGHTTP2 "Use nghttp2 for HTTP/2 parsing" ON)

if(MCP_USE_NGHTTP2)
    # Try to find system nghttp2 first
    message(STATUS "Looking for system nghttp2...")
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
        
        message(STATUS "Downloading nghttp2... This may take a few minutes depending on your connection speed.")
        message(STATUS "nghttp2 is a larger dependency and requires compilation. Please be patient.")
        FetchContent_MakeAvailable(nghttp2)
        message(STATUS "nghttp2 download and configuration complete.")

        # Enable position-independent code for shared library linking
        if(TARGET nghttp2_static)
            set_target_properties(nghttp2_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()

        set(NGHTTP2_FOUND TRUE)
        set(NGHTTP2_INCLUDE_DIRS 
            ${nghttp2_SOURCE_DIR}/lib/includes 
            ${nghttp2_BINARY_DIR}/lib/includes)
        set(NGHTTP2_LIBRARIES nghttp2_static)
        
        # Propagate to parent scope only when used as submodule
        if(GOPHER_MCP_IS_SUBMODULE)
            set(NGHTTP2_FOUND TRUE PARENT_SCOPE)
            set(NGHTTP2_INCLUDE_DIRS 
                ${nghttp2_SOURCE_DIR}/lib/includes 
                ${nghttp2_BINARY_DIR}/lib/includes PARENT_SCOPE)
            set(NGHTTP2_LIBRARIES nghttp2_static PARENT_SCOPE)
        endif()
        
        message(STATUS "nghttp2 successfully configured for build from source")
    else()
        message(STATUS "Found system nghttp2")
        # Propagate to parent scope only when used as submodule
        if(GOPHER_MCP_IS_SUBMODULE)
            set(NGHTTP2_FOUND TRUE PARENT_SCOPE)
            set(NGHTTP2_INCLUDE_DIRS ${NGHTTP2_INCLUDE_DIRS} PARENT_SCOPE)
            set(NGHTTP2_LIBRARIES ${NGHTTP2_LIBRARIES} PARENT_SCOPE)
            set(NGHTTP2_LIBRARY_DIRS ${NGHTTP2_LIBRARY_DIRS} PARENT_SCOPE)
        endif()
    endif()
else()
    set(NGHTTP2_FOUND FALSE)
    if(GOPHER_MCP_IS_SUBMODULE)
        set(NGHTTP2_FOUND FALSE PARENT_SCOPE)
    endif()
    message(STATUS "nghttp2 support disabled")
endif()

# fmt library for formatting - use system-installed version
find_package(fmt REQUIRED)
message(STATUS "Found fmt: ${fmt_VERSION}")

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
message(STATUS "Downloading nlohmann/json... This may take a moment depending on your connection speed.")
FetchContent_MakeAvailable(nlohmann_json)
message(STATUS "nlohmann/json download complete.")

# json-schema-validator (header-only) for schema validation
if(NOT TARGET nlohmann_json_schema_validator)
    FetchContent_Declare(
        json_schema_validator
        GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
        GIT_TAG 2.3.0
    )
    message(STATUS "Downloading json-schema-validator... This may take a moment depending on your connection speed.")
    FetchContent_MakeAvailable(json_schema_validator)
    message(STATUS "json-schema-validator download complete.")
endif()

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
message(STATUS "Downloading Google Test... This may take a moment depending on your connection speed.")
FetchContent_MakeAvailable(googletest)
message(STATUS "Google Test download complete.")

message(STATUS "")
message(STATUS "==================================================")
message(STATUS "All dependencies ready. Configuring build...")
message(STATUS "==================================================")
message(STATUS "")

# Note: nlohmann/json already fetched above (lines 298-305)
# Note: Google Test already fetched above (lines 319-329)

# Source files - split core from client/server to avoid circular deps
set(MCP_CORE_SOURCES
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
    src/filter/http_sse_filter_chain_factory.cc
    src/filter/stdio_filter_chain_factory.cc
    src/filter/json_rpc_protocol_filter.cc
    src/filter/protocol_detection_filter_chain_factory.cc
    src/filter/http_routing_filter.cc
    src/filter/protocol_detection_filter.cc
    src/filter/filter_registry.cc
    src/filter/http_codec_filter_factory.cc
    src/filter/sse_codec_filter_factory.cc
    src/filter/json_rpc_dispatcher_filter_factory.cc
    src/filter/rate_limit_factory.cc
    src/filter/circuit_breaker_factory.cc
    src/filter/metrics_factory.cc
    src/filter/request_logger_filter.cc
    src/filter/request_logger_factory.cc
    src/filter/core_filter_factories.cc
    src/filter/filter_chain_assembler.cc
    src/mcp_connection_manager.cc
    src/protocol/mcp_protocol_state_machine.cc
    src/config/config_manager.cc
    src/config/file_config_source.cc
    src/config/config_merger.cc
    src/config/config_validator.cc
    src/config/units.cc
    # TODO : Re-enable filter order validator for new architecture
    # src/config/filter_order_validator.cc
    src/config/predicate_evaluator.cc
)

# Client and server sources that need event library
set(MCP_CLIENT_SERVER_SOURCES
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
        src/transport/http_sse_transport_socket.cc  # HTTP+SSE with layered architecture
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

# Create Gopher MCP libraries (both static and shared)
# Combine all sources for the main library (including event sources)
set(MCP_SDK_SOURCES ${MCP_CORE_SOURCES} ${MCP_CLIENT_SERVER_SOURCES} ${MCP_EVENT_SOURCES})

if(BUILD_STATIC_LIBS)
    add_library(gopher-mcp-static STATIC ${MCP_SDK_SOURCES} ${MCP_HTTP_SOURCES})
    set_target_properties(gopher-mcp-static PROPERTIES
        OUTPUT_NAME gopher-mcp
        POSITION_INDEPENDENT_CODE ON
        VERSION ${GOPHER_MCP_VERSION}
        SOVERSION ${GOPHER_MCP_VERSION_MAJOR}
    )
endif()

if(BUILD_SHARED_LIBS)
    add_library(gopher-mcp SHARED ${MCP_SDK_SOURCES} ${MCP_HTTP_SOURCES})
    set_target_properties(gopher-mcp PROPERTIES
        VERSION ${GOPHER_MCP_VERSION}
        SOVERSION ${GOPHER_MCP_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Platform-specific shared library settings
    if(WIN32)
        set_target_properties(gopher-mcp PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            PREFIX ""
        )
    elseif(APPLE)
        set_target_properties(gopher-mcp PROPERTIES
            INSTALL_RPATH "@loader_path"
            BUILD_WITH_INSTALL_RPATH OFF
            MACOSX_RPATH ON
        )
    else()  # Linux/Unix
        set_target_properties(gopher-mcp PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            BUILD_WITH_INSTALL_RPATH OFF
        )
    endif()
else()
    # If only static is built, create alias
    add_library(gopher-mcp ALIAS gopher-mcp-static)
endif()
# Configure include directories and linking for both library types
# Only configure real targets, not aliases
set(REAL_TARGETS)
if(TARGET gopher-mcp-static)
    list(APPEND REAL_TARGETS gopher-mcp-static)
endif()
if(BUILD_SHARED_LIBS AND TARGET gopher-mcp)
    list(APPEND REAL_TARGETS gopher-mcp)
endif()

foreach(lib_target ${REAL_TARGETS})
    if(TARGET ${lib_target})
        target_include_directories(${lib_target}
            PUBLIC 
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include/gopher-mcp>
            PRIVATE
                $<BUILD_INTERFACE:$<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>>
                ${LIBEVENT_INCLUDE_DIRS}
        )
        target_link_libraries(${lib_target} 
            PUBLIC 
                Threads::Threads 
                OpenSSL::SSL 
                OpenSSL::Crypto
            PRIVATE
                nlohmann_json::nlohmann_json
                ${LIBEVENT_LIBRARIES}
                yaml-cpp::yaml-cpp
                $<$<TARGET_EXISTS:gopher-mcp-logging>:gopher-mcp-logging>
                $<$<AND:$<NOT:$<TARGET_EXISTS:gopher-mcp-logging>>,$<TARGET_EXISTS:gopher-mcp-logging-static>>:gopher-mcp-logging-static>
        )
        # Disable json-schema-validator by default to avoid export/link issues
        target_compile_definitions(${lib_target} PUBLIC MCP_HAS_JSON_SCHEMA_VALIDATOR=0)
        if(LIBEVENT_LIBRARY_DIRS)
            target_link_directories(${lib_target} PRIVATE ${LIBEVENT_LIBRARY_DIRS})
        endif()
        # Add compile definitions for nlohmann_json
        target_compile_definitions(${lib_target} PUBLIC
            $<BUILD_INTERFACE:NLOHMANN_JSON_NAMESPACE_NO_VERSION=1>
        )
    endif()
endforeach()

# Add llhttp if found
if(LLHTTP_FOUND)
    foreach(lib_target ${REAL_TARGETS})
        if(TARGET ${lib_target})
            target_link_libraries(${lib_target} PRIVATE llhttp)
            target_compile_definitions(${lib_target} PUBLIC MCP_HAS_LLHTTP=1)
            target_include_directories(${lib_target} PRIVATE 
                $<BUILD_INTERFACE:$<TARGET_PROPERTY:llhttp,INTERFACE_INCLUDE_DIRECTORIES>>
            )
        endif()
    endforeach()
else()
    foreach(lib_target ${REAL_TARGETS})
        if(TARGET ${lib_target})
            target_compile_definitions(${lib_target} PUBLIC MCP_HAS_LLHTTP=0)
        endif()
    endforeach()
endif()

# Add nghttp2 if found
if(NGHTTP2_FOUND)
    foreach(lib_target ${REAL_TARGETS})
        if(TARGET ${lib_target})
            target_include_directories(${lib_target} PRIVATE ${NGHTTP2_INCLUDE_DIRS})
            if(NGHTTP2_LIBRARY_DIRS)
                # For static libraries, make link directories PUBLIC so they propagate
                if("${lib_target}" STREQUAL "gopher-mcp-static" OR "${lib_target}" STREQUAL "gopher-mcp-event-static")
                    target_link_directories(${lib_target} PUBLIC ${NGHTTP2_LIBRARY_DIRS})
                else()
                    target_link_directories(${lib_target} PRIVATE ${NGHTTP2_LIBRARY_DIRS})
                endif()
            endif()
            if(NGHTTP2_LIBRARIES)
                # For static libraries, make nghttp2 a PUBLIC dependency so it propagates
                if("${lib_target}" STREQUAL "gopher-mcp-static" OR "${lib_target}" STREQUAL "gopher-mcp-event-static")
                    target_link_libraries(${lib_target} PUBLIC ${NGHTTP2_LIBRARIES})
                else()
                    target_link_libraries(${lib_target} PRIVATE ${NGHTTP2_LIBRARIES})
                endif()
            else()
                if("${lib_target}" STREQUAL "gopher-mcp-static" OR "${lib_target}" STREQUAL "gopher-mcp-event-static")
                    target_link_libraries(${lib_target} PUBLIC nghttp2)
                else()
                    target_link_libraries(${lib_target} PRIVATE nghttp2)
                endif()
            endif()
            target_compile_definitions(${lib_target} PUBLIC MCP_HAS_NGHTTP2=1)
        endif()
    endforeach()
else()
    foreach(lib_target ${REAL_TARGETS})
        if(TARGET ${lib_target})
            target_compile_definitions(${lib_target} PUBLIC MCP_HAS_NGHTTP2=0)
        endif()
    endforeach()
endif()

# Create Gopher MCP Echo Advanced libraries
if(BUILD_STATIC_LIBS)
    add_library(gopher-mcp-echo-advanced-static STATIC ${MCP_ECHO_ADVANCED_SOURCES})
    set_target_properties(gopher-mcp-echo-advanced-static PROPERTIES
        OUTPUT_NAME gopher-mcp-echo-advanced
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(BUILD_SHARED_LIBS)
    add_library(gopher-mcp-echo-advanced SHARED ${MCP_ECHO_ADVANCED_SOURCES})
    set_target_properties(gopher-mcp-echo-advanced PROPERTIES
        VERSION ${GOPHER_MCP_VERSION}
        SOVERSION ${GOPHER_MCP_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
    )
else()
    add_library(gopher-mcp-echo-advanced ALIAS gopher-mcp-echo-advanced-static)
endif()
# Configure echo advanced library
# Only configure real targets, not aliases
set(ECHO_REAL_TARGETS)
if(TARGET gopher-mcp-echo-advanced-static)
    list(APPEND ECHO_REAL_TARGETS gopher-mcp-echo-advanced-static)
endif()
if(BUILD_SHARED_LIBS AND TARGET gopher-mcp-echo-advanced)
    list(APPEND ECHO_REAL_TARGETS gopher-mcp-echo-advanced)
endif()

foreach(lib_target ${ECHO_REAL_TARGETS})
    if(TARGET ${lib_target})
        target_include_directories(${lib_target}
            PUBLIC 
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include/gopher-mcp>
        )
        # Link to appropriate main library
        if(TARGET gopher-mcp)
            target_link_libraries(${lib_target} PUBLIC gopher-mcp)
        elseif(TARGET gopher-mcp-static)
            target_link_libraries(${lib_target} PUBLIC gopher-mcp-static)
        endif()
        target_link_libraries(${lib_target} PUBLIC 
            Threads::Threads
        )
    endif()
endforeach()

# Create Gopher MCP Event libraries
if(BUILD_STATIC_LIBS)
    add_library(gopher-mcp-event-static STATIC ${MCP_EVENT_SOURCES})
    set_target_properties(gopher-mcp-event-static PROPERTIES
        OUTPUT_NAME gopher-mcp-event
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if(BUILD_SHARED_LIBS)
    add_library(gopher-mcp-event SHARED ${MCP_EVENT_SOURCES})
    set_target_properties(gopher-mcp-event PROPERTIES
        VERSION ${GOPHER_MCP_VERSION}
        SOVERSION ${GOPHER_MCP_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
    )
else()
    add_library(gopher-mcp-event ALIAS gopher-mcp-event-static)
endif()
# Configure event library
# Only configure real targets, not aliases
set(EVENT_REAL_TARGETS)
if(TARGET gopher-mcp-event-static)
    list(APPEND EVENT_REAL_TARGETS gopher-mcp-event-static)
endif()
if(BUILD_SHARED_LIBS AND TARGET gopher-mcp-event)
    list(APPEND EVENT_REAL_TARGETS gopher-mcp-event)
endif()

foreach(lib_target ${EVENT_REAL_TARGETS})
    if(TARGET ${lib_target})
        target_include_directories(${lib_target}
            PUBLIC 
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<BUILD_INTERFACE:${LIBEVENT_INCLUDE_DIRS}>
                $<INSTALL_INTERFACE:include/gopher-mcp>
        )
        # Link to appropriate main library
        if(TARGET gopher-mcp)
            target_link_libraries(${lib_target} PUBLIC gopher-mcp)
        elseif(TARGET gopher-mcp-static)
            target_link_libraries(${lib_target} PUBLIC gopher-mcp-static)
        endif()
        target_link_libraries(${lib_target} PUBLIC Threads::Threads)
        
        if(LIBEVENT_FOUND)
            if(LIBEVENT_LIBRARY_DIRS)
                target_link_directories(${lib_target} PUBLIC ${LIBEVENT_LIBRARY_DIRS})
            endif()
            target_link_libraries(${lib_target} PUBLIC ${LIBEVENT_LIBRARIES})
        endif()
    endif()
endforeach()

# Enable testing only if not a submodule or explicitly requested
if(NOT GOPHER_MCP_IS_SUBMODULE OR GOPHER_MCP_BUILD_TESTS)
    enable_testing()
    
    # Test targets:
    # - make test          : Standard CMake test target (shows progress)
    # - make check         : Run tests with minimal output (recommended)
    # - make check-verbose : Run tests with detailed output
    # - make check-parallel: Run tests in parallel with minimal output
    # - ctest             : Direct CTest invocation (customizable)
    
    # Add test subdirectory only if tests should be built
    if(BUILD_TESTS)
        add_subdirectory(tests)
    endif()
endif()

# Add logging subdirectory
add_subdirectory(src/logging)
message(STATUS "Building logging framework")

# Add C API subdirectory (if enabled)
if(BUILD_C_API)
    add_subdirectory(src/c_api)
    message(STATUS "Building C API bindings")
endif()

# Build examples (optional) - default OFF for submodule
if(GOPHER_MCP_IS_SUBMODULE)
    option(BUILD_EXAMPLES "Build example programs" OFF)
else()
    option(BUILD_EXAMPLES "Build example programs" ON)
endif()

if(BUILD_EXAMPLES AND NOT GOPHER_MCP_IS_SUBMODULE)
    add_executable(event_loop_example examples/event_loop_example.cc)
    target_link_libraries(event_loop_example 
        gopher-mcp-event 
        gopher-mcp 
        Threads::Threads
    )
    
    # Basic stdio echo examples are now built in examples/stdio_echo/CMakeLists.txt
    
    add_executable(test_echo_integration examples/test_echo_integration.cc)
    target_link_libraries(test_echo_integration
        gopher-mcp
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

# Add custom targets only if not a submodule
if(NOT GOPHER_MCP_IS_SUBMODULE)
    # Add custom target for formatting
    add_custom_target(format
    COMMAND find ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests
            -name "*.h" -o -name "*.cc" | xargs clang-format -i
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Formatting all source files with clang-format"
)

# Add custom target to check formatting without modifying files
add_custom_target(check-format
    COMMAND find ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests
            -name "*.h" -o -name "*.cc" | xargs clang-format --dry-run --Werror
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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
endif() # NOT GOPHER_MCP_IS_SUBMODULE

# ============================================================================
# Installation Support
# ============================================================================

if(GOPHER_MCP_INSTALL AND NOT GOPHER_MCP_IS_SUBMODULE)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    
    # Set installation directories based on OS
    if(WIN32)
        set(GOPHER_MCP_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
        set(GOPHER_MCP_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
        set(GOPHER_MCP_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/gopher-mcp)
        set(GOPHER_MCP_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/gopher-mcp)
    else()
        set(GOPHER_MCP_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
        set(GOPHER_MCP_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
        set(GOPHER_MCP_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/gopher-mcp)
        set(GOPHER_MCP_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/gopher-mcp)
    endif()
    
    # Export targets for installation - separate shared and static
    # to avoid dependency issues with static libraries
    set(GOPHER_MCP_SHARED_TARGETS "")
    set(GOPHER_MCP_STATIC_TARGETS "")
    
    # Collect shared library targets
    if(BUILD_SHARED_LIBS)
        if(TARGET gopher-mcp)
            list(APPEND GOPHER_MCP_SHARED_TARGETS gopher-mcp)
        endif()
        if(TARGET gopher-mcp-echo-advanced)
            list(APPEND GOPHER_MCP_SHARED_TARGETS gopher-mcp-echo-advanced)
        endif()
        if(TARGET gopher-mcp-event)
            list(APPEND GOPHER_MCP_SHARED_TARGETS gopher-mcp-event)
        endif()
        if(BUILD_C_API AND TARGET gopher_mcp_c)
            list(APPEND GOPHER_MCP_SHARED_TARGETS gopher_mcp_c)
        endif()
    endif()
    
    # Collect static library targets
    if(BUILD_STATIC_LIBS)
        if(TARGET gopher-mcp-static)
            list(APPEND GOPHER_MCP_STATIC_TARGETS gopher-mcp-static)
        endif()
        if(TARGET gopher-mcp-echo-advanced-static)
            list(APPEND GOPHER_MCP_STATIC_TARGETS gopher-mcp-echo-advanced-static)
        endif()
        if(TARGET gopher-mcp-event-static)
            list(APPEND GOPHER_MCP_STATIC_TARGETS gopher-mcp-event-static)
        endif()
        if(BUILD_C_API AND BUILD_C_API_STATIC AND TARGET gopher_mcp_c_static)
            list(APPEND GOPHER_MCP_STATIC_TARGETS gopher_mcp_c_static)
        endif()
    endif()
    
    # Install shared libraries with export
    if(GOPHER_MCP_SHARED_TARGETS)
        install(TARGETS ${GOPHER_MCP_SHARED_TARGETS}
            EXPORT gopher-mcp-targets
            LIBRARY DESTINATION ${GOPHER_MCP_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${GOPHER_MCP_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${GOPHER_MCP_INSTALL_BINDIR}
            INCLUDES DESTINATION ${GOPHER_MCP_INSTALL_INCLUDEDIR}
        )
    endif()
    
    # Install static libraries without export (to avoid dependency issues)
    if(GOPHER_MCP_STATIC_TARGETS)
        install(TARGETS ${GOPHER_MCP_STATIC_TARGETS}
            LIBRARY DESTINATION ${GOPHER_MCP_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${GOPHER_MCP_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${GOPHER_MCP_INSTALL_BINDIR}
        )
    endif()
    
    # Install C++ SDK header files
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${GOPHER_MCP_INSTALL_INCLUDEDIR}
        FILES_MATCHING 
        PATTERN "*.h"
        PATTERN "*.hpp"
    )
    
    # Install C API headers (if C API is enabled)
    if(BUILD_C_API)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/mcp/c_api/
            DESTINATION ${GOPHER_MCP_INSTALL_INCLUDEDIR}/mcp/c_api
            FILES_MATCHING 
            PATTERN "*.h"
        )
    endif()
    
    # Generate and install package configuration files
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gopher-mcp-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-config.cmake
        INSTALL_DESTINATION ${GOPHER_MCP_INSTALL_CMAKEDIR}
    )
    
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-config-version.cmake
        DESTINATION ${GOPHER_MCP_INSTALL_CMAKEDIR}
    )
    
    # Install export targets (only if shared libraries exist)
    if(GOPHER_MCP_SHARED_TARGETS)
        install(EXPORT gopher-mcp-targets
            FILE gopher-mcp-targets.cmake
            NAMESPACE gopher-mcp::
            DESTINATION ${GOPHER_MCP_INSTALL_CMAKEDIR}
        )
    endif()
    
    # Generate pkg-config files
    # C++ SDK pkg-config
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gopher-mcp.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp.pc
        @ONLY
    )
    
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
    
    # C API pkg-config (if enabled)
    if(BUILD_C_API)
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gopher-mcp-c.pc.in
            ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-c.pc
            @ONLY
        )
        
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gopher-mcp-c.pc
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        )
    endif()
    
    # Create uninstall target
    if(NOT TARGET uninstall)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
            IMMEDIATE @ONLY
        )
        
        add_custom_target(uninstall
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
            COMMENT "Uninstalling gopher-mcp..."
        )
    endif()
    
    # Print installation summary
    message(STATUS "")
    message(STATUS "Installation Configuration:")
    message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "  Libraries: ${GOPHER_MCP_INSTALL_LIBDIR}")
    message(STATUS "  Headers: ${GOPHER_MCP_INSTALL_INCLUDEDIR}")
    message(STATUS "  CMake modules: ${GOPHER_MCP_INSTALL_CMAKEDIR}")
    message(STATUS "  Components:")
    message(STATUS "    C++ SDK: YES")
    message(STATUS "    C API: ${BUILD_C_API}")
    message(STATUS "    Shared libraries: ${BUILD_SHARED_LIBS}")
    message(STATUS "    Static libraries: ${BUILD_STATIC_LIBS}")
    if(BUILD_C_API)
        message(STATUS "    C API static: ${BUILD_C_API_STATIC}")
    endif()
    message(STATUS "")
    message(STATUS "To install all components: make install")
    message(STATUS "To uninstall: make uninstall")
endif()
