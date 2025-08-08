# Advanced MCP Echo Examples
cmake_minimum_required(VERSION 3.10)

# Add executables
add_executable(stdio_echo_server_advanced 
    stdio_echo_server_advanced.cc)

add_executable(stdio_echo_client_advanced 
    stdio_echo_client_advanced.cc)

# Link libraries for server
target_link_libraries(stdio_echo_server_advanced
    mcp-sdk
    mcp-event
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Link libraries for client
target_link_libraries(stdio_echo_client_advanced
    mcp-sdk
    mcp-event
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Set C++ standard
set_target_properties(stdio_echo_server_advanced PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(stdio_echo_client_advanced PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Add include directories
target_include_directories(stdio_echo_server_advanced PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/stdio_echo
)

target_include_directories(stdio_echo_client_advanced PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/stdio_echo
)

# Installation rules
install(TARGETS stdio_echo_server_advanced stdio_echo_client_advanced
    RUNTIME DESTINATION bin
)

# Add test script
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_advanced_echo.sh
    ${CMAKE_CURRENT_BINARY_DIR}/test_advanced_echo.sh
    COPYONLY
)