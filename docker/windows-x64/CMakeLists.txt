cmake_minimum_required(VERSION 3.16)
project(gopher-mcp-auth VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard to C++11 for maximum compatibility
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# For Windows, we'll compile with stubs for OpenSSL/CURL functions
# and use Windows native APIs where possible
add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601)

# Auth library source files - use real implementation with Windows adaptations
set(AUTH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp_auth_implementation.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp_auth_crypto_optimized.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp_auth_network_optimized.cc
    /docker/windows-x64/windows_compat.cc
)

# Create the shared library
add_library(gopher_mcp_auth SHARED ${AUTH_SOURCES})

# Set library properties
set_target_properties(gopher_mcp_auth PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    PREFIX ""  # Remove lib prefix for Windows
)

# Include directories
target_include_directories(gopher_mcp_auth
    PUBLIC
        $<BUILD_INTERFACE:/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CURL_INCLUDE_DIR}
        ${OPENSSL_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(gopher_mcp_auth
    PRIVATE
        ${OPENSSL_SSL_LIBRARY}
        ${OPENSSL_CRYPTO_LIBRARY}
        ${CURL_LIBRARY}
        ws2_32   # Windows sockets
        crypt32  # Windows crypto
        wldap32  # LDAP for CURL
        winmm    # Windows multimedia
)

# Install rules
install(TARGETS gopher_mcp_auth
    EXPORT gopher_mcp_auth-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Copy required DLLs to bin directory
install(FILES 
    ${CURL_ROOT}/bin/libcurl-4.dll
    ${CURL_ROOT}/bin/libssl-3-x64.dll
    ${CURL_ROOT}/bin/libcrypto-3-x64.dll
    ${CURL_ROOT}/bin/zlib1.dll
    ${CURL_ROOT}/bin/libssh2-1.dll
    DESTINATION bin
    OPTIONAL
)