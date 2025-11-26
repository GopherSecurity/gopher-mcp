# CMake configuration for Gopher MCP C API bindings
# Updated to ensure proper symbol export for FFI usage
cmake_minimum_required(VERSION 3.14)

# Configuration option for symbol export
option(EXPORT_ALL_SYMBOLS "Export all symbols for FFI usage" ON)

# ============================================================================
# C API Library
# ============================================================================

# Collect C API source files
set(MCP_C_API_SOURCES
    # Core functionality
    mcp_c_api_core.cc          # Library init/shutdown, dispatcher
    mcp_c_api_connection.cc     # Connection management
    mcp_c_raii.cc                 # RAII utilities
    mcp_c_guard_transaction_impl.cc  # Guard and transaction implementations
    
    # New FFI-safe implementation with opaque handles
    mcp_c_types_impl.cc         # Type implementations
    mcp_c_memory_impl.cc        # Memory management
    mcp_c_collections_impl.cc   # Collections and JSON
    
    # Filter API and Buffer implementations
    mcp_c_filter_api.cc          # Filter API implementation
    mcp_c_filter_buffer.cc       # Buffer API implementation
    mcp_c_filter_chain.cc        # Filter chain implementation
    mcp_c_metrics_callbacks.cc   # Metrics callbacks API
    mcp_c_filter_events.cc       # Chain-level filter event callbacks API
    json_value_converter.cc      # JSON value conversion utilities
    unified_filter_chain.cc      # Unified filter chain wrapper
    filter_chain_stubs.cc        # Temporary stubs for Phase 1 (replaced in Phase 2)

    # Logging API with RAII
    mcp_c_logging_api.cc         # FFI-safe logging API with RAII
    
    # Authentication API
    mcp_c_auth_api.cc            # JWT validation and OAuth support
    
    # TODO: Update these to use new opaque handle API
    mcp_c_api_json.cc  # JSON conversion functions
    # mcp_c_api_client.cc
    # mcp_c_api_server.cc
)

# Create shared library for C bindings
add_library(gopher_mcp_c SHARED ${MCP_C_API_SOURCES})

# Set library properties
set_target_properties(gopher_mcp_c PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Ensure symbols are exported for FFI usage
if(EXPORT_ALL_SYMBOLS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # On macOS, ensure symbols are properly exported for dynamic loading
        set_target_properties(gopher_mcp_c PROPERTIES
            C_VISIBILITY_PRESET default
            CXX_VISIBILITY_PRESET default
        )
        message(STATUS "Symbol export enabled for FFI usage on macOS")
    endif()
else()
    message(STATUS "Symbol export disabled - library may not work with FFI")
endif()

# Export only C API symbols
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Windows: Use .def file or __declspec(dllexport)
    set_target_properties(gopher_mcp_c PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
    target_compile_definitions(gopher_mcp_c PRIVATE GOPHER_MCP_C_API_EXPORTS MCP_BUILD_LIBRARY MCP_BUILD_SHARED)
else()
    # Unix/Linux/macOS: Use visibility attributes
    if(EXPORT_ALL_SYMBOLS)
        target_compile_options(gopher_mcp_c PRIVATE -fvisibility=default)
        message(STATUS "Using default visibility for symbol export")
    else()
        target_compile_options(gopher_mcp_c PRIVATE -fvisibility=hidden)
        message(STATUS "Using hidden visibility (may not work with FFI)")
    endif()
    
    target_compile_definitions(gopher_mcp_c PRIVATE MCP_BUILD_LIBRARY MCP_BUILD_SHARED)
    
    # Add explicit export for C functions
    target_compile_definitions(gopher_mcp_c PRIVATE MCP_API_EXPORT)
    
    # Ensure all mcp_* functions are exported by adding export attributes
    target_compile_definitions(gopher_mcp_c PRIVATE MCP_EXPORT_SYMBOLS)

    # Generate export map
    set(EXPORT_MAP "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.map")
    file(WRITE ${EXPORT_MAP} "{\n  global:\n")
    file(APPEND ${EXPORT_MAP} "    mcp_*;\n")  # Export all mcp_* symbols
    file(APPEND ${EXPORT_MAP} "  local:\n    *;\n};\n")
    
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(gopher_mcp_c PROPERTIES
            LINK_FLAGS "-Wl,--version-script=${EXPORT_MAP}"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # macOS uses different linker flags
        # Generate comprehensive symbols list for macOS
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "# Export all MCP API symbols\n")
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "mcp_*\n")
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "_mcp_*\n")
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "MCP_*\n")
        
        # Set proper linker flags for macOS
        set_target_properties(gopher_mcp_c PROPERTIES
            LINK_FLAGS "-Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols"
        )
        
        # Alternative approach: Use flat namespace and export all symbols for debugging
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set_target_properties(gopher_mcp_c PROPERTIES
                LINK_FLAGS "-Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols -Wl,-flat_namespace"
            )
        endif()
        
        # Ensure proper symbol visibility on macOS (only if export is enabled)
        if(EXPORT_ALL_SYMBOLS)
            target_compile_options(gopher_mcp_c PRIVATE -fvisibility=default)
        endif()
    endif()
endif()

# Include directories
target_include_directories(gopher_mcp_c
    PUBLIC
        $<BUILD_INTERFACE:${GOPHER_MCP_ROOT_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${GOPHER_MCP_ROOT_DIR}/src
        ${CMAKE_BINARY_DIR}/_deps/fmt-src/include
)

# Find OpenSSL for JWT signature verification
find_package(OpenSSL REQUIRED)

# Find CURL for JWKS fetching
find_package(CURL REQUIRED)

# Link with shared libraries 
# The shared library already contains all needed dependencies
# When gopher-mcp is built as static only, link to static version
if(TARGET gopher-mcp AND NOT TARGET gopher-mcp-shared)
    # gopher-mcp exists but no shared version - it's an alias to static
    target_link_libraries(gopher_mcp_c
        PRIVATE
            gopher-mcp-static
            gopher-mcp-event-static
            gopher-mcp-logging-static
            OpenSSL::SSL
            OpenSSL::Crypto
            CURL::libcurl
    )
else()
    # Normal case - shared libraries exist
    target_link_libraries(gopher_mcp_c
        PRIVATE
            gopher-mcp
            gopher-mcp-event
            gopher-mcp-logging
            OpenSSL::SSL
            OpenSSL::Crypto
            CURL::libcurl
    )
endif()

# Add nghttp2 linking if needed
if(NGHTTP2_FOUND)
    if(NGHTTP2_LIBRARY_DIRS)
        target_link_directories(gopher_mcp_c PRIVATE ${NGHTTP2_LIBRARY_DIRS})
    endif()
    if(NGHTTP2_LIBRARIES)
        target_link_libraries(gopher_mcp_c PRIVATE ${NGHTTP2_LIBRARIES})
    endif()
endif()

# Compile features
target_compile_features(gopher_mcp_c PRIVATE cxx_std_17)

# ============================================================================
# Static library option
# ============================================================================

option(BUILD_C_API_STATIC "Build static C API library" ON)

if(BUILD_C_API_STATIC)
    add_library(gopher_mcp_c_static STATIC ${MCP_C_API_SOURCES})
    
    set_target_properties(gopher_mcp_c_static PROPERTIES
        OUTPUT_NAME gopher_mcp_c
        POSITION_INDEPENDENT_CODE ON
    )
    
    target_include_directories(gopher_mcp_c_static
        PUBLIC
            $<BUILD_INTERFACE:${GOPHER_MCP_ROOT_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${GOPHER_MCP_ROOT_DIR}/src
            ${CMAKE_BINARY_DIR}/_deps/fmt-src/include
    )
    
    target_link_libraries(gopher_mcp_c_static
        PRIVATE
            gopher-mcp-static
            gopher-mcp-event-static
            gopher-mcp-logging-static
            OpenSSL::SSL
            OpenSSL::Crypto
            CURL::libcurl
    )
    
    target_compile_features(gopher_mcp_c_static PRIVATE cxx_std_17)
    # Add compile definitions for tests
    target_compile_definitions(gopher_mcp_c_static PRIVATE 
        $<$<CONFIG:Debug>:MCP_RAII_DEBUG_MODE>
        MCP_RAII_DISABLE_ATEXIT  # Disable atexit handler for tests
    )
endif()

# ============================================================================
# Installation
# ============================================================================
# Installation is handled by the main CMakeLists.txt to ensure proper
# export configuration and unified installation of all components

# ============================================================================
# pkg-config support
# ============================================================================
# pkg-config file generation is handled by the main CMakeLists.txt

# ============================================================================
# Language binding support files
# ============================================================================

# Install binding examples and build files (if enabled)
if(BUILD_BINDINGS_EXAMPLES AND GOPHER_MCP_INSTALL AND NOT GOPHER_MCP_IS_SUBMODULE)
    install(
        DIRECTORY ${GOPHER_MCP_ROOT_DIR}/bindings/
        DESTINATION share/gopher-mcp/bindings
        PATTERN "build" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
        PATTERN "node_modules" EXCLUDE
        PATTERN ".git" EXCLUDE
        PATTERN "*.o" EXCLUDE
        PATTERN "*.so" EXCLUDE
        PATTERN "*.dylib" EXCLUDE
    )
endif()

# ============================================================================
# Testing
# ============================================================================

# Tests are handled at the top level CMakeLists.txt

# ============================================================================
# Documentation
# ============================================================================

if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
        set(DOXYGEN_EXTRACT_ALL YES)
        set(DOXYGEN_EXTRACT_PRIVATE NO)
        set(DOXYGEN_EXTRACT_STATIC YES)
        set(DOXYGEN_GENERATE_HTML YES)
        set(DOXYGEN_GENERATE_MAN YES)
        
        doxygen_add_docs(
            gopher_mcp_c_docs
            ${GOPHER_MCP_ROOT_DIR}/include/mcp/c_api/mcp_c_api.h
            ${GOPHER_MCP_ROOT_DIR}/include/mcp/c_api/mcp_c_types.h
            COMMENT "Generating Gopher MCP C API documentation"
        )
        
        install(
            DIRECTORY ${DOXYGEN_OUTPUT_DIRECTORY}/
            DESTINATION share/doc/gopher_mcp_c
        )
    endif()
endif()

# ============================================================================
# Print configuration summary
# ============================================================================

message(STATUS "Gopher MCP C API Configuration:")
message(STATUS "  Shared library: ${BUILD_SHARED_LIBS}")
message(STATUS "  Static library: ${BUILD_C_API_STATIC}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Language bindings:")
message(STATUS "    Python: ${GOPHER_MCP_ROOT_DIR}/bindings/python")
message(STATUS "    TypeScript: ${GOPHER_MCP_ROOT_DIR}/bindings/typescript")
message(STATUS "    Go: ${GOPHER_MCP_ROOT_DIR}/bindings/go")
message(STATUS "    Rust: ${GOPHER_MCP_ROOT_DIR}/bindings/rust")

# ============================================================================
# FFI Support Configuration
# ============================================================================
message(STATUS "  FFI Support:")
message(STATUS "    Symbol Export: ${EXPORT_ALL_SYMBOLS}")
if(EXPORT_ALL_SYMBOLS)
    message(STATUS "    ✓ Library will be built with symbols exported for FFI usage")
    message(STATUS "    ✓ TypeScript SDK should be able to load the shared library")
else()
    message(STATUS "    ✗ Library will be built with hidden symbols (may not work with FFI)")
    message(STATUS "    ✗ TypeScript SDK may fail to load the shared library")
endif()
