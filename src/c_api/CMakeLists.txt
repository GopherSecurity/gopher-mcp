# CMake configuration for MCP C API bindings
cmake_minimum_required(VERSION 3.14)

# ============================================================================
# C API Library
# ============================================================================

# Collect C API source files
set(MCP_C_API_SOURCES
    mcp_c_api_core.cc
    mcp_c_api_connection.cc
    mcp_c_api_client.cc
    mcp_c_api_server.cc
    mcp_c_api_json.cc
    mcp_c_api_utils.cc
)

# Create shared library for C bindings
add_library(mcp_c SHARED ${MCP_C_API_SOURCES})

# Set library properties
set_target_properties(mcp_c PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Export only C API symbols
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Windows: Use .def file or __declspec(dllexport)
    set_target_properties(mcp_c PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
    target_compile_definitions(mcp_c PRIVATE MCP_C_API_EXPORTS)
else()
    # Unix/Linux/macOS: Use visibility attributes
    target_compile_options(mcp_c PRIVATE -fvisibility=hidden)
    
    # Generate export map
    set(EXPORT_MAP "${CMAKE_CURRENT_BINARY_DIR}/mcp_c.map")
    file(WRITE ${EXPORT_MAP} "{\n  global:\n")
    file(APPEND ${EXPORT_MAP} "    mcp_*;\n")  # Export all mcp_* symbols
    file(APPEND ${EXPORT_MAP} "  local:\n    *;\n};\n")
    
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(mcp_c PROPERTIES
            LINK_FLAGS "-Wl,--version-script=${EXPORT_MAP}"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # macOS uses different linker flags
        set_target_properties(mcp_c PROPERTIES
            LINK_FLAGS "-Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/mcp_c.symbols"
        )
        # Generate symbols list for macOS
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mcp_c.symbols" "_mcp_*\n")
    endif()
endif()

# Include directories
target_include_directories(mcp_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# Link with the main MCP C++ library
target_link_libraries(mcp_c
    PRIVATE
        mcp_cpp
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${ZLIB_LIBRARIES}
)

# Compile features
target_compile_features(mcp_c PRIVATE cxx_std_17)

# ============================================================================
# Static library option
# ============================================================================

option(BUILD_C_API_STATIC "Build static C API library" ON)

if(BUILD_C_API_STATIC)
    add_library(mcp_c_static STATIC ${MCP_C_API_SOURCES})
    
    set_target_properties(mcp_c_static PROPERTIES
        OUTPUT_NAME mcp_c
        POSITION_INDEPENDENT_CODE ON
    )
    
    target_include_directories(mcp_c_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    
    target_link_libraries(mcp_c_static
        PRIVATE
            mcp_cpp_static
            ${LIBEVENT_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${ZLIB_LIBRARIES}
    )
    
    target_compile_features(mcp_c_static PRIVATE cxx_std_17)
endif()

# ============================================================================
# Installation
# ============================================================================

# Install libraries
install(TARGETS mcp_c
    EXPORT MCPCTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

if(BUILD_C_API_STATIC)
    install(TARGETS mcp_c_static
        EXPORT MCPCTargets
        ARCHIVE DESTINATION lib
    )
endif()

# Install C API headers
install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/mcp/c_api/
    DESTINATION include/mcp/c_api
    FILES_MATCHING PATTERN "*.h"
    PATTERN "mcp_c_bridge.h" EXCLUDE  # Don't install internal header
)

# ============================================================================
# Package configuration
# ============================================================================

# Create package config file
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/MCPCConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/MCPCConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MCPCConfig.cmake"
    INSTALL_DESTINATION lib/cmake/mcp_c
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/MCPCConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/MCPCConfigVersion.cmake"
    DESTINATION lib/cmake/mcp_c
)

install(
    EXPORT MCPCTargets
    FILE MCPCTargets.cmake
    NAMESPACE MCP::
    DESTINATION lib/cmake/mcp_c
)

# ============================================================================
# pkg-config support
# ============================================================================

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/mcp_c.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mcp_c.pc"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/mcp_c.pc"
    DESTINATION lib/pkgconfig
)

# ============================================================================
# Language binding support files
# ============================================================================

# Install binding examples and build files
install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/
    DESTINATION share/mcp/bindings
    PATTERN "build" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "__pycache__" EXCLUDE
)

# ============================================================================
# Testing
# ============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

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
            mcp_c_docs
            ${CMAKE_SOURCE_DIR}/include/mcp/c_api/mcp_c_api.h
            ${CMAKE_SOURCE_DIR}/include/mcp/c_api/mcp_c_types.h
            COMMENT "Generating C API documentation"
        )
        
        install(
            DIRECTORY ${DOXYGEN_OUTPUT_DIRECTORY}/
            DESTINATION share/doc/mcp_c
        )
    endif()
endif()

# ============================================================================
# Print configuration summary
# ============================================================================

message(STATUS "MCP C API Configuration:")
message(STATUS "  Shared library: ${BUILD_SHARED_LIBS}")
message(STATUS "  Static library: ${BUILD_C_API_STATIC}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Language bindings:")
message(STATUS "    Python: ${CMAKE_SOURCE_DIR}/bindings/python")
message(STATUS "    TypeScript: ${CMAKE_SOURCE_DIR}/bindings/typescript")
message(STATUS "    Go: ${CMAKE_SOURCE_DIR}/bindings/go")
message(STATUS "    Rust: ${CMAKE_SOURCE_DIR}/bindings/rust")